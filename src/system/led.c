/**
 * @file led.c
 * @brief LED 灯光控制实现
 * 
 * 基于 fyapp 逆向分析实现的 LED 控制模块
 * 使用 charge.c 的回调监听电池状态变化
 * 使用 ofono D-Bus 接口获取网络状态
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <glib.h>
#include "led.h"
#include "ofono.h"
#include "sysinfo.h"
#include "charge.h"
#include "device_profile.h"

/* 网络状态检测定时器 */
static guint network_timer_id = 0;

/* LED sysfs 路径定义 */
static char led_paths[LED_COUNT][256];

/* LED trigger 路径定义 - 用于禁用硬件触发器 */
static char led_trigger_paths[LED_COUNT][256];

static const int led_color_for_id[LED_COUNT] = {
    0, 1, 2, 0, 1, 2, 0, 1, 0, 1
};

static const char *led_names[LED_COUNT] = {
    "lte_red", "lte_green", "lte_blue",
    "nr_red", "nr_green", "nr_blue",
    "vbat_red", "vbat_green",
    "wifi_red", "wifi_green"
};

/* LED 节点状态 */
typedef struct {
    LedMode mode;
    int brightness;
    int current_state;
} LedNode;

static LedNode led_nodes[LED_COUNT];
static pthread_mutex_t led_mutex = PTHREAD_MUTEX_INITIALIZER;
static guint flash_timer_id = 0;
static int flash_tick = 0;
static int led_initialized = 0;

/* 本地状态 */
typedef struct {
    int battery_capacity;
    int battery_charging;
    int network_connected;
    int is_5g;
    int wifi_hostapd_running;  /* hostapd 进程状态: 1=运行, 0=未运行, -1=未知 */
} LocalStatus;

/* 初始值设为 -1，确保首次一定触发更新 */
static LocalStatus local_status = {-1, -1, -1, -1, -1};

/* 网络未知状态LED标志 - 用于 led_refresh() 重置 */
static int unknown_led_set = 0;

/* hostapd PID 文件路径 */
/* 前向声明 */
static int wifi_hostapd_running(void);

static void configure_led_paths(void) {
    const DeviceProfile *profile = device_profile_get();
    const char *bases[] = {profile->led_red, profile->led_green,
                           profile->led_blue};

    for (int i = 0; i < LED_COUNT; i++) {
        const char *base = bases[led_color_for_id[i]];
        if (!base || base[0] == '\0') {
            led_paths[i][0] = '\0';
            led_trigger_paths[i][0] = '\0';
            continue;
        }
        snprintf(led_paths[i], sizeof(led_paths[i]), "%s/brightness", base);
        snprintf(led_trigger_paths[i], sizeof(led_trigger_paths[i]), "%s/trigger",
                 base);
    }
}






/*============================================================================
 * LED 基础操作
 *============================================================================*/

/* 禁用所有 LED 硬件触发器，改为软件控制 */
/* 使用 system() 命令，与 fyapp 保持一致 */
static void led_disable_triggers(void) {
    for (int i = 0; i < LED_COUNT; i++) {
        if (led_trigger_paths[i][0] == '\0') {
            continue;
        }
        FILE *file = fopen(led_trigger_paths[i], "w");
        if (file) {
            fputs("none", file);
            fclose(file);
        }
    }
    printf("[LED] 已按设备能力禁用 LED 硬件触发器\n");
}

/* 写入 LED 亮度到 sysfs */
static int led_write_brightness(LedId id, int value) {
    if (id >= LED_COUNT) return -1;
    if (led_paths[id][0] == '\0') return -1;
    
    FILE *f = fopen(led_paths[id], "w");
    if (!f) return -1;
    fprintf(f, "%d", value > 0 ? 1 : 0);
    fclose(f);
    return 0;
}

/* 闪烁定时器回调 - 500ms 间隔 */
/* 添加定期强制刷新机制，与 fyapp 行为一致 */
static gboolean led_flash_callback(gpointer data) {
    (void)data;
    static int force_refresh_counter = 0;
    
    pthread_mutex_lock(&led_mutex);
    flash_tick++;
    force_refresh_counter++;
    
    /* 每 6 个 tick (3秒) 强制刷新一次，防止系统其他进程覆盖 LED 状态 */
    int force_refresh = (force_refresh_counter % 6) == 0;
    
    for (int i = 0; i < LED_COUNT; i++) {
        LedNode *node = &led_nodes[i];
        int should_on = 0;
        
        switch (node->mode) {
            case LED_MODE_OFF:
                should_on = 0;
                break;
            case LED_MODE_ON:
                should_on = 1;
                break;
            case LED_MODE_FLASH_SLOW:
                /* 慢闪: 每2个tick切换 = 1秒周期 (亮500ms, 灭500ms) */
                should_on = (flash_tick % 2) == 0;
                break;
            case LED_MODE_FLASH_FAST:
                /* 快闪: 每个tick切换 = 500ms周期 (亮250ms, 灭250ms) */
                should_on = (flash_tick % 2) == 0;
                break;
        }
        
        /* 状态变化或强制刷新时写入 brightness */
        if (force_refresh || node->current_state != should_on) {
            node->current_state = should_on;
            led_write_brightness(i, should_on ? node->brightness : 0);
        }
    }
    
    pthread_mutex_unlock(&led_mutex);
    return TRUE;
}

/*============================================================================
 * 电池状态回调 (由 charge.c 调用)
 *============================================================================*/

/* 电池状态变化回调 - 由 charge.c 的 uevent 监听触发 */
static void on_battery_change(int capacity, int charging) {
    if (!device_profile_get()->battery_supported) {
        return;
    }
    if (local_status.battery_capacity == -1 ||
        capacity != local_status.battery_capacity || 
        charging != local_status.battery_charging) {
        local_status.battery_capacity = capacity;
        local_status.battery_charging = charging;
        led_update_battery(capacity, charging);
        printf("[LED] 电池状态: %d%%, %s\n", capacity, charging ? "充电中" : "未充电");
    }
}


/*============================================================================
 * 网络状态读取与监听 (使用 ofono D-Bus)
 *============================================================================*/

/* 读取网络状态 - 使用 ofono D-Bus 接口 */
static void read_network_status(int *connected, int *is_5g) {
    *connected = -1;  /* -1 表示未知/获取失败 */
    *is_5g = 0;       /* 默认 4G */
    
    /* 获取当前卡槽 */
    char slot[16], ril_path[32];
    if (get_current_slot(slot, ril_path) != 0 || strcmp(ril_path, "unknown") == 0) {
        return;
    }
    
    /* 获取信号强度判断是否连接 */
    int strength = 0, dbm = 0;
    if (ofono_network_get_signal_strength(ril_path, &strength, &dbm, OFONO_TIMEOUT_MS) == 0) {
        *connected = (strength > 0) ? 1 : 0;
    }
    
    /* 获取网络模式判断是否5G */
    char mode_buf[64] = {0};
    if (ofono_network_get_mode_sync(ril_path, mode_buf, sizeof(mode_buf), OFONO_TIMEOUT_MS) == 0) {
        /* 检查是否为5G模式 */
        if (strstr(mode_buf, "nr") || strstr(mode_buf, "5g") || strstr(mode_buf, "NR")) {
            *is_5g = 1;
        }
    }
}

/* 网络状态未知时，4G和5G红灯同时闪烁 */
static void led_network_unknown(void) {
    /* 4G (LTE) 红灯闪烁 */
    led_set_mode(LED_LTE_RED, LED_MODE_FLASH_SLOW);
    led_set_mode(LED_LTE_GREEN, LED_MODE_OFF);
    led_set_mode(LED_LTE_BLUE, LED_MODE_OFF);
    
    /* 5G (NR) 红灯闪烁 */
    led_set_mode(LED_NR_RED, LED_MODE_FLASH_SLOW);
    led_set_mode(LED_NR_GREEN, LED_MODE_OFF);
    led_set_mode(LED_NR_BLUE, LED_MODE_OFF);
}

/* 处理网络状态变化 */
static void handle_network_change(void) {
    int connected, is_5g;
    read_network_status(&connected, &is_5g);
    
    /* 如果获取失败 (connected == -1)，4G和5G红灯同时闪烁 */
    if (connected == -1) {
        if (!unknown_led_set) {
            unknown_led_set = 1;
            local_status.network_connected = -1;
            local_status.is_5g = -1;
            led_network_unknown();
            printf("[LED] 网络状态未知，4G和5G红灯同时闪烁\n");
        }
        return;
    }
    
    /* 网络状态已知，重置未知标志 */
    unknown_led_set = 0;
    
    if (connected != local_status.network_connected || 
        is_5g != local_status.is_5g) {
        local_status.network_connected = connected;
        local_status.is_5g = is_5g;
        LedState state = connected ? LED_STATE_CONNECTED : LED_STATE_OFFLINE;
        led_update_network(is_5g, state);
        printf("[LED] 网络状态变化: %s, %s\n", 
               connected ? "已连接" : "离线", is_5g ? "5G" : "4G");
    }
}

/* 处理 WiFi 状态变化 */
static void handle_wifi_change(void) {
    int running = wifi_hostapd_running();
    
    if (running != local_status.wifi_hostapd_running) {
        local_status.wifi_hostapd_running = running;
        led_update_wifi(1, 0);  /* is_on=1，内部检测 hostapd */
        printf("[LED] WiFi hostapd 状态变化: %s\n", running ? "运行中" : "未运行");
    }
}

/* 网络状态定时检测回调 - 每2秒检测一次 */
static gboolean on_network_timer(gpointer data) {
    (void)data;
    handle_network_change();
    handle_wifi_change();  /* 同时检测 WiFi 状态 */
    return TRUE;
}

/* 启动网络监听 */
static void start_network_monitor(void) {
    if (network_timer_id > 0) return;
    
    /* 使用定时器轮询 ofono D-Bus 接口 */
    network_timer_id = g_timeout_add(2000, on_network_timer, NULL);
    
    if (network_timer_id > 0) {
        printf("[LED] 网络状态监听已启动 (ofono D-Bus, 2秒轮询)\n");
    }
}

/* 停止网络监听 */
static void stop_network_monitor(void) {
    if (network_timer_id > 0) {
        g_source_remove(network_timer_id);
        network_timer_id = 0;
    }
}

/*============================================================================
 * LED 模块初始化与关闭
 *============================================================================*/

void led_init(void) {
    if (led_initialized) return;

    configure_led_paths();
    
    /* 禁用硬件触发器，确保 LED 由软件控制 */
    led_disable_triggers();
    
    pthread_mutex_lock(&led_mutex);
    
    /* 初始化所有 LED 节点 */
    for (int i = 0; i < LED_COUNT; i++) {
        led_nodes[i].mode = LED_MODE_OFF;
        led_nodes[i].brightness = 1;
        led_nodes[i].current_state = 0;
        led_write_brightness(i, 0);
    }
    
    /* 启动闪烁定时器 (500ms 间隔，慢闪1秒周期) */
    flash_timer_id = g_timeout_add(500, led_flash_callback, NULL);
    
    led_initialized = 1;
    pthread_mutex_unlock(&led_mutex);
    
    /* 注册电池状态回调 (由 charge.c 的 uevent 监听触发) */
    charge_register_callback(on_battery_change);
    
    /* 启动网络监听 (ofono D-Bus 轮询) */
    start_network_monitor();
    
    /* 读取初始网络状态 */
    handle_network_change();
    
    /* 读取初始 WiFi 状态 */
    handle_wifi_change();
    
    printf("[LED] LED 模块初始化完成\n");
}

void led_deinit(void) {
    if (!led_initialized) return;
    
    /* 停止监听 */
    charge_register_callback(NULL);  /* 取消电池回调 */
    stop_network_monitor();
    
    pthread_mutex_lock(&led_mutex);
    
    if (flash_timer_id > 0) {
        g_source_remove(flash_timer_id);
        flash_timer_id = 0;
    }
    
    /* 关闭所有 LED */
    for (int i = 0; i < LED_COUNT; i++) {
        led_write_brightness(i, 0);
    }
    
    led_initialized = 0;
    pthread_mutex_unlock(&led_mutex);
    
    printf("[LED] LED 模块已关闭\n");
}


/*============================================================================
 * LED 控制 API
 *============================================================================*/

void led_set_mode(LedId id, LedMode mode) {
    if (id >= LED_COUNT) return;
    
    pthread_mutex_lock(&led_mutex);
    led_nodes[id].mode = mode;
    
    if (mode == LED_MODE_OFF) {
        led_nodes[id].current_state = 0;
        led_write_brightness(id, 0);
    } else if (mode == LED_MODE_ON) {
        led_nodes[id].current_state = 1;
        led_write_brightness(id, led_nodes[id].brightness);
    }
    pthread_mutex_unlock(&led_mutex);
}

void led_set_brightness(LedId id, int brightness) {
    if (id >= LED_COUNT) return;
    
    pthread_mutex_lock(&led_mutex);
    led_nodes[id].brightness = brightness > 0 ? brightness : 1;
    pthread_mutex_unlock(&led_mutex);
}

void led_update_network(int is_5g, LedState state) {
    LedId red_id = is_5g ? LED_NR_RED : LED_LTE_RED;
    LedId green_id = is_5g ? LED_NR_GREEN : LED_LTE_GREEN;
    LedId blue_id = is_5g ? LED_NR_BLUE : LED_LTE_BLUE;
    
    /* 先关闭另一组网络灯 */
    LedId other_red = is_5g ? LED_LTE_RED : LED_NR_RED;
    LedId other_green = is_5g ? LED_LTE_GREEN : LED_NR_GREEN;
    LedId other_blue = is_5g ? LED_LTE_BLUE : LED_NR_BLUE;
    led_set_mode(other_red, LED_MODE_OFF);
    led_set_mode(other_green, LED_MODE_OFF);
    led_set_mode(other_blue, LED_MODE_OFF);
    
    /* 简化为两种状态: 已连接=绿灯亮, 其他=红灯闪 */
    if (state == LED_STATE_CONNECTED) {
        /* 已连接: 绿灯常亮 */
        led_set_mode(red_id, LED_MODE_OFF);
        led_set_mode(green_id, LED_MODE_ON);
        led_set_mode(blue_id, LED_MODE_OFF);
    } else {
        /* 未连接/异常: 红灯闪烁 */
        led_set_mode(red_id, LED_MODE_FLASH_SLOW);
        led_set_mode(green_id, LED_MODE_OFF);
        led_set_mode(blue_id, LED_MODE_OFF);
    }
}

void led_update_battery(int capacity, int is_charging) {
    if (!device_profile_get()->battery_supported) {
        return;
    }
    if (is_charging) {
        /* 充电状态 */
        if (capacity < 30) {
            /* 电量<30%: 红灯闪烁 */
            led_set_mode(LED_VBAT_RED, LED_MODE_FLASH_SLOW);
            led_set_mode(LED_VBAT_GREEN, LED_MODE_OFF);
        } else if (capacity >= 100) {
            /* 电量100%(满电): 绿灯常亮 */
            led_set_mode(LED_VBAT_RED, LED_MODE_OFF);
            led_set_mode(LED_VBAT_GREEN, LED_MODE_ON);
        } else {
            /* 电量30%~99%: 绿灯闪烁 */
            led_set_mode(LED_VBAT_RED, LED_MODE_OFF);
            led_set_mode(LED_VBAT_GREEN, LED_MODE_FLASH_SLOW);
        }
    } else {
        /* 未充电状态 */
        if (capacity < 30) {
            /* 电量<30%: 红灯常亮 */
            led_set_mode(LED_VBAT_RED, LED_MODE_ON);
            led_set_mode(LED_VBAT_GREEN, LED_MODE_OFF);
        } else {
            /* 电量>=30%: 绿灯常亮 */
            led_set_mode(LED_VBAT_RED, LED_MODE_OFF);
            led_set_mode(LED_VBAT_GREEN, LED_MODE_ON);
        }
    }
}

/**
 * @brief 检测 hostapd 进程是否运行
 * @return 1=运行中, 0=未运行
 */
static int wifi_hostapd_running(void) {
    const DeviceProfile *profile = device_profile_get();
    FILE *pipe = popen("ps 2>/dev/null", "r");
    char line[512];
    int running = 0;

    if (!pipe) {
        return 0;
    }
    while (fgets(line, sizeof(line), pipe)) {
        if (strstr(line, "hostapd") &&
            (profile->wifi_iface[0] == '\0' ||
             strstr(line, profile->wifi_iface))) {
            running = 1;
            break;
        }
    }
    pclose(pipe);
    return running;
}

void led_update_wifi(int is_on, int has_clients) {
    (void)has_clients;  /* 不再使用 has_clients 参数 */
    
    if (!is_on) {
        /* WiFi 关闭 - 灯灭 */
        led_set_mode(LED_WIFI_RED, LED_MODE_OFF);
        led_set_mode(LED_WIFI_GREEN, LED_MODE_OFF);
    } else {
        /* WiFi 开启 - 根据 hostapd 进程状态决定 */
        int running = wifi_hostapd_running();
        led_set_mode(LED_WIFI_RED, LED_MODE_OFF);
        if (running) {
            /* hostapd 运行中 - 绿灯常亮 */
            led_set_mode(LED_WIFI_GREEN, LED_MODE_ON);
        } else {
            /* hostapd 未运行 - 绿灯闪烁 */
            led_set_mode(LED_WIFI_GREEN, LED_MODE_FLASH_SLOW);
        }
    }
}

void led_all_off(void) {
    for (int i = 0; i < LED_COUNT; i++) {
        led_set_mode(i, LED_MODE_OFF);
    }
}

void led_refresh(void) {
    if (!led_initialized) return;
    
    /* 重置本地状态，强制触发更新 */
    local_status.battery_capacity = -1;
    local_status.battery_charging = -1;
    local_status.network_connected = -1;
    local_status.is_5g = -1;
    local_status.wifi_hostapd_running = -1;
    unknown_led_set = 0;  /* 重置网络未知标志，确保刷新时重新设置LED */
    
    /* 重新读取电池状态并更新 LED */
    if (device_profile_get()->battery_supported) {
        int capacity, charging;
        charge_get_battery_status(&capacity, &charging);
        local_status.battery_capacity = capacity;
        local_status.battery_charging = charging;
        led_update_battery(capacity, charging);
    }
    
    /* 触发网络状态更新 */
    handle_network_change();
    
    /* 更新 WiFi LED 状态 */
    int wifi_running = wifi_hostapd_running();
    local_status.wifi_hostapd_running = wifi_running;
    led_update_wifi(1, 0);  /* is_on=1 让函数内部检测 hostapd 状态 */
    printf("[LED] WiFi hostapd: %s\n", wifi_running ? "运行中" : "未运行");
    
    printf("[LED] LED 状态已刷新\n");
}


/*============================================================================
 * HTTP API
 *============================================================================*/

void handle_led_status(struct mg_connection *c, struct mg_http_message *hm) {
    if (hm->method.len == 7 && memcmp(hm->method.buf, "OPTIONS", 7) == 0) {
        mg_http_reply(c, 200, "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type\r\n", "");
        return;
    }
    
    char json[1024];
    int offset = 0;
    
    offset += snprintf(json + offset, sizeof(json) - offset,
        "{\"Code\":0,\"Error\":\"\",\"Data\":{\"leds\":[");
    
    pthread_mutex_lock(&led_mutex);
    for (int i = 0; i < LED_COUNT; i++) {
        if (i > 0) offset += snprintf(json + offset, sizeof(json) - offset, ",");
        offset += snprintf(json + offset, sizeof(json) - offset,
            "{\"id\":%d,\"name\":\"%s\",\"mode\":%d,\"state\":%d}",
            i, led_names[i], led_nodes[i].mode, led_nodes[i].current_state);
    }
    pthread_mutex_unlock(&led_mutex);
    
    offset += snprintf(json + offset, sizeof(json) - offset,
        "],\"battery\":{\"capacity\":%d,\"charging\":%s},"
        "\"network\":{\"connected\":%s,\"is_5g\":%s}}}",
        local_status.battery_capacity,
        local_status.battery_charging ? "true" : "false",
        local_status.network_connected ? "true" : "false",
        local_status.is_5g ? "true" : "false");
    
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "%s", json);
}

void handle_led_control(struct mg_connection *c, struct mg_http_message *hm) {
    if (hm->method.len == 7 && memcmp(hm->method.buf, "OPTIONS", 7) == 0) {
        mg_http_reply(c, 200, "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type\r\n", "");
        return;
    }
    
    char *action = strstr(hm->body.buf, "\"action\"");
    if (action && strstr(action, "all_off")) {
        led_all_off();
        mg_http_reply(c, 200,
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n",
            "{\"Code\":0,\"Error\":\"\",\"Data\":\"所有LED已关闭\"}");
        return;
    }
    
    int id = -1, mode = -1;
    char *p = strstr(hm->body.buf, "\"id\"");
    if (p) { p = strchr(p, ':'); if (p) id = atoi(p + 1); }
    p = strstr(hm->body.buf, "\"mode\"");
    if (p) { p = strchr(p, ':'); if (p) mode = atoi(p + 1); }
    
    if (id < 0 || id >= LED_COUNT || mode < 0 || mode > 3) {
        mg_http_reply(c, 200,
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n",
            "{\"Code\":1,\"Error\":\"无效的参数\",\"Data\":null}");
        return;
    }
    
    led_set_mode(id, mode);
    
    mg_http_reply(c, 200,
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n",
        "{\"Code\":0,\"Error\":\"\",\"Data\":\"LED已设置\"}");
}
