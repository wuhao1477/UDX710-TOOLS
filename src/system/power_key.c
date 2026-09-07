/**
 * @file power_key.c
 * @brief 电源键监听实现
 * 
 * 监听 /dev/input/event0 的电源键事件 (key code = 74)
 * - 长按 2 秒: 全部红灯闪烁 3 秒后关机
 * - 双击 (500ms 内): 切换 LED 灯光状态
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <linux/input.h>
#include <glib.h>
#include "power_key.h"
#include "led.h"

#define INPUT_DEVICE    "/dev/input/event0"
#define POWER_KEY_CODE  116     /* KEY_POWER - 从 fyapp 逆向确认 */
#define LONG_PRESS_MS   2000    /* 长按阈值 2 秒 */
#define DOUBLE_CLICK_MS 500     /* 双击间隔阈值 500ms */
#define SHUTDOWN_DELAY  3000    /* 关机前红灯闪烁时间 3 秒 */

static int input_fd = -1;
static pthread_t monitor_thread;
static volatile int running = 0;
static int led_enabled = 1;  /* LED 灯光是否开启 */
static volatile int key_pressed = 0;  /* 按键是否按下 */
static guint long_press_timer_id = 0;  /* 长按检测定时器 */

/* 时间戳转毫秒 */
static long long timeval_to_ms(struct timeval *tv) {
    return (long long)tv->tv_sec * 1000 + tv->tv_usec / 1000;
}

/* 关机定时器回调 - 在主循环中执行 */
static gboolean shutdown_timer_callback(gpointer data) {
    (void)data;
    printf("[PowerKey] 红灯闪烁完成，执行关机命令\n");
    system("poweroff");
    return FALSE;  /* 只执行一次 */
}

/* 启动红灯闪烁和延迟关机 */
static void start_shutdown_sequence(void) {
    printf("[PowerKey] 长按检测，启动关机序列...\n");
    
    /* 全部红灯快闪 */
    led_set_mode(LED_LTE_RED, LED_MODE_FLASH_FAST);
    led_set_mode(LED_NR_RED, LED_MODE_FLASH_FAST);
    led_set_mode(LED_VBAT_RED, LED_MODE_FLASH_FAST);
    led_set_mode(LED_WIFI_RED, LED_MODE_FLASH_FAST);
    
    /* 关闭其他灯 */
    led_set_mode(LED_LTE_GREEN, LED_MODE_OFF);
    led_set_mode(LED_LTE_BLUE, LED_MODE_OFF);
    led_set_mode(LED_NR_GREEN, LED_MODE_OFF);
    led_set_mode(LED_NR_BLUE, LED_MODE_OFF);
    led_set_mode(LED_VBAT_GREEN, LED_MODE_OFF);
    led_set_mode(LED_WIFI_GREEN, LED_MODE_OFF);
    
    printf("[PowerKey] 红灯闪烁 3 秒后关机...\n");
    
    /* 使用 GLib 定时器延迟关机，让主循环继续运行以驱动 LED 闪烁 */
    /* 3秒闪烁 + 1秒等待 = 4秒后关机 */
    g_timeout_add(4000, shutdown_timer_callback, NULL);
}

/* 长按检测定时器回调 - 按下 2 秒后触发 */
static gboolean long_press_timer_callback(gpointer data) {
    (void)data;
    long_press_timer_id = 0;
    
    if (key_pressed) {
        printf("[PowerKey] 长按 2 秒确认，触发关机\n");
        start_shutdown_sequence();
    }
    return FALSE;  /* 只执行一次 */
}


/* 切换 LED 灯光状态 */
static void toggle_led(void) {
    led_enabled = !led_enabled;
    
    if (led_enabled) {
        printf("[PowerKey] 双击检测，开启 LED 监听灯光\n");
        /* 刷新 LED 状态 - 重新读取电池和网络状态 */
        led_refresh();
    } else {
        printf("[PowerKey] 双击检测，关闭所有 LED\n");
        led_all_off();
    }
}

/* 电源键监听线程 */
static void *power_key_thread(void *arg) {
    (void)arg;
    struct input_event ev;
    long long press_time = 0;      /* 按下时间 */
    long long last_release = 0;    /* 上次释放时间 */
    int click_count = 0;           /* 点击计数 */
    
    printf("[PowerKey] 监听线程启动\n");
    
    while (running) {
        ssize_t n = read(input_fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) {
            if (errno == EINTR) continue;
            printf("[PowerKey] read 失败: n=%zd, errno=%d (%s)\n", n, errno, strerror(errno));
            break;
        }
        
        /* 打印所有事件用于调试 */
        printf("[PowerKey] 事件: type=%d, code=%d, value=%d\n", 
               ev.type, ev.code, ev.value);
        
        /* 只处理按键事件 */
        if (ev.type != EV_KEY) {
            continue;
        }
        
        printf("[PowerKey] 按键事件: code=%d, value=%d\n", ev.code, ev.value);
        
        /* 检查是否是电源键 (code=74 或其他) */
        if (ev.code != POWER_KEY_CODE) {
            continue;
        }
        
        long long now = timeval_to_ms(&ev.time);
        
        if (ev.value == 1) {
            /* 按下 */
            printf("[PowerKey] 电源键按下\n");
            press_time = now;
            key_pressed = 1;
            
            /* 检查双击 - 距离上次释放在 500ms 内 */
            if (last_release > 0 && (now - last_release) < DOUBLE_CLICK_MS) {
                click_count++;
                printf("[PowerKey] 双击计数: %d, 间隔: %lld ms\n", click_count, now - last_release);
                
                /* 立即触发双击 */
                if (click_count >= 2) {
                    printf("[PowerKey] 触发双击切换 LED\n");
                    toggle_led();
                    click_count = 0;
                    last_release = 0;  /* 重置，避免连续触发 */
                    /* 不启动长按定时器 */
                    continue;
                }
            } else {
                click_count = 1;
            }
            
            /* 启动长按检测定时器 - 2秒后检查是否仍按住 */
            if (long_press_timer_id > 0) {
                g_source_remove(long_press_timer_id);
            }
            long_press_timer_id = g_timeout_add(LONG_PRESS_MS, long_press_timer_callback, NULL);
            
        } else if (ev.value == 0) {
            /* 释放 */
            key_pressed = 0;
            long long hold_time = now - press_time;
            printf("[PowerKey] 电源键释放, 按住时间: %lld ms\n", hold_time);
            
            /* 取消长按定时器 */
            if (long_press_timer_id > 0) {
                g_source_remove(long_press_timer_id);
                long_press_timer_id = 0;
            }
            
            last_release = now;
        }
    }
    
    printf("[PowerKey] 监听线程退出\n");
    return NULL;
}

int power_key_init(void) {
    if (running) return 0;
    
    printf("[PowerKey] 初始化开始...\n");
    
    /* 尝试打开多个 input 设备 */
    const char *devices[] = {
        "/dev/input/event0",
        "/dev/input/event1",
        "/dev/input/event2",
        "/dev/input/event3",
        NULL
    };
    
    const char *used_device = NULL;
    for (int i = 0; devices[i] != NULL; i++) {
        input_fd = open(devices[i], O_RDONLY);
        if (input_fd >= 0) {
            used_device = devices[i];
            printf("[PowerKey] 成功打开设备: %s (fd=%d)\n", devices[i], input_fd);
            break;
        } else {
            printf("[PowerKey] 无法打开 %s: %s\n", devices[i], strerror(errno));
        }
    }
    
    if (input_fd < 0) {
        printf("[PowerKey] 所有 input 设备都无法打开\n");
        return -1;
    }
    
    running = 1;
    
    /* 创建监听线程 */
    if (pthread_create(&monitor_thread, NULL, power_key_thread, NULL) != 0) {
        printf("[PowerKey] 创建线程失败: %s\n", strerror(errno));
        close(input_fd);
        input_fd = -1;
        running = 0;
        return -1;
    }
    
    printf("[PowerKey] 电源键监听已启动 (key=%d, device=%s)\n", 
           POWER_KEY_CODE, used_device);
    return 0;
}

void power_key_deinit(void) {
    if (!running) return;
    
    running = 0;
    
    if (input_fd >= 0) {
        close(input_fd);
        input_fd = -1;
    }
    
    pthread_join(monitor_thread, NULL);
    printf("[PowerKey] 电源键监听已关闭\n");
}

int power_key_get_led_enabled(void) {
    return led_enabled;
}

