/**
 * @file phone_case.c
 * @brief 手机壳模式模块实现
 *
 * 功能：
 * - 后台网络状态监控
 * - 断网自动恢复（数据漫游、PDP激活、防火墙清理）
 * - 开机自启动
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "phone_case.h"
#include "database.h"
#include "exec_utils.h"
#include "device_profile.h"
#include "ofono.h"

#define PHONE_CASE_KEY "phone_case_enabled"
#define CARRIER_PATH_PREFIX "/sys/class/net/"
#define CARRIER_PATH_SUFFIX "/carrier"

/* 全局状态 */
static volatile int g_running = 0;      /* 监控线程是否运行 */
static pthread_t g_monitor_thread;       /* 监控线程 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * 检查指定网络接口是否连接
 * @param ifname 接口名称
 * @return 1=已连接, 0=未连接, -1=接口不存在
 */
static int check_interface_carrier(const char *ifname) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s%s", CARRIER_PATH_PREFIX, ifname, CARRIER_PATH_SUFFIX);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        return -1;  /* 接口不存在 */
    }

    int carrier = 0;
    if (fscanf(fp, "%d", &carrier) != 1) {
        carrier = 0;
    }
    fclose(fp);

    return carrier;
}

/**
 * 检查网络是否已连接
 * 与 shell 脚本一致，只检查 eth0 的 carrier
 * @return 1=已连接, 0=未连接
 */
static int check_network_connected(void) {
    const DeviceProfile *profile = device_profile_get();
    if (profile->data_iface[0] &&
        check_interface_carrier(profile->data_iface) == 1) {
        return 1;
    }
    if (profile->rj45_iface[0] &&
        check_interface_carrier(profile->rj45_iface) == 1) {
        return 1;
    }
    return 0;
}

/**
 * 执行网络恢复操作
 */
static void execute_network_recovery(void) {
    char output[256];
    char modem_path[64];

    printf("[PhoneCase] 执行网络恢复操作...\n");

    /* 启用数据漫游 */
    ofono_get_current_modem_path(modem_path, sizeof(modem_path));
    run_command(output, sizeof(output), "dbus-send",
        "--system", "--print-reply", "--dest=org.ofono",
        modem_path, "org.ofono.ConnectionManager.SetProperty",
        "string:RoamingAllowed", "variant:boolean:true", NULL);

    /* 设置自动连接 */
    run_command(output, sizeof(output), "connmanctl", "setautoconnect", "on", NULL);

    /* 激活 PDP 上下文 */
    run_command(output, sizeof(output), "connmanctl", "ActivatePdp", "1", NULL);

    /* 清除 IPv4 防火墙规则 */
    run_command(output, sizeof(output), "sh", "-c",
        "iptables -F; iptables -X; iptables -Z", NULL);

    /* 清除 IPv6 防火墙规则 */
    run_command(output, sizeof(output), "sh", "-c",
        "ip6tables -F; ip6tables -X; ip6tables -Z", NULL);

    printf("[PhoneCase] 网络恢复操作完成\n");
}

/**
 * 监控线程函数
 */
static void* phone_case_monitor_thread(void *arg) {
    (void)arg;  /* 未使用参数 */

    int recovery_count = 0;
    const int max_recovery_per_hour = 20;  /* 每小时最大恢复次数，防止无限循环 */
    time_t hour_start = time(NULL);

    printf("[PhoneCase] 监控线程已启动\n");

    while (g_running) {
        /* 重置每小时计数器 */
        time_t now = time(NULL);
        if (now - hour_start >= 3600) {
            recovery_count = 0;
            hour_start = now;
        }

        /* 检查网络状态 */
        if (!check_network_connected()) {
            /* 检查是否超过最大恢复次数 */
            if (recovery_count < max_recovery_per_hour) {
                execute_network_recovery();
                recovery_count++;
                printf("[PhoneCase] 本小时已执行 %d/%d 次恢复\n",
                       recovery_count, max_recovery_per_hour);
                sleep(10);  /* 恢复后等待10秒 */
            } else {
                printf("[PhoneCase] 已达到最大恢复次数，暂停恢复\n");
                sleep(60);  /* 等待1分钟 */
            }
        } else {
            sleep(5);  /* 网络正常，每5秒检查一次 */
        }
    }

    printf("[PhoneCase] 监控线程已退出\n");
    return NULL;
}

/**
 * 初始化手机壳模式模块
 */
void phone_case_init(void) {
    printf("[PhoneCase] 模块初始化\n");

    /* 检查是否需要自动启动 */
    int enabled = config_get_int(PHONE_CASE_KEY, 0);
    if (enabled) {
        printf("[PhoneCase] 自启动已启用，启动监控线程\n");
        phone_case_start();
    }
}

/**
 * 清理手机壳模式模块
 */
void phone_case_deinit(void) {
    phone_case_stop();
    pthread_mutex_destroy(&g_lock);
    printf("[PhoneCase] 模块已清理\n");
}

/**
 * 启动监控
 * @return 0=成功, -1=失败
 */
int phone_case_start(void) {
    pthread_mutex_lock(&g_lock);

    if (g_running) {
        pthread_mutex_unlock(&g_lock);
        printf("[PhoneCase] 监控已在运行中\n");
        return 0;
    }

    g_running = 1;

    if (pthread_create(&g_monitor_thread, NULL, phone_case_monitor_thread, NULL) != 0) {
        g_running = 0;
        pthread_mutex_unlock(&g_lock);
        printf("[PhoneCase] 创建监控线程失败: %s\n", strerror(errno));
        return -1;
    }

    pthread_mutex_unlock(&g_lock);

    /* 保存启用状态 */
    config_set_int(PHONE_CASE_KEY, 1);

    printf("[PhoneCase] 监控已启动\n");
    return 0;
}

/**
 * 停止监控
 * @return 0=成功
 */
int phone_case_stop(void) {
    pthread_mutex_lock(&g_lock);

    if (!g_running) {
        pthread_mutex_unlock(&g_lock);
        printf("[PhoneCase] 监控未运行\n");
        return 0;
    }

    g_running = 0;

    pthread_mutex_unlock(&g_lock);

    /* 等待线程退出 */
    pthread_join(g_monitor_thread, NULL);

    /* 保存禁用状态 */
    config_set_int(PHONE_CASE_KEY, 0);

    printf("[PhoneCase] 监控已停止\n");
    return 0;
}

/**
 * 获取当前状态
 * @return 1=运行中, 0=已停止
 */
int phone_case_get_status(void) {
    return g_running;
}

/**
 * 设置开关状态
 * @param enabled 1=开启, 0=关闭
 * @return 0=成功, -1=失败
 */
int phone_case_set_enabled(int enabled) {
    if (enabled) {
        return phone_case_start();
    } else {
        return phone_case_stop();
    }
}

/*============================================================================
 * HTTP API 处理函数
 *============================================================================*/

#include "mongoose.h"
#include "http_utils.h"
#include "json_builder.h"

/**
 * GET/POST /api/phone-case - 获取/设置手机壳模式状态
 */
void handle_phone_case(struct mg_connection *c, struct mg_http_message *hm) {
    if (hm->method.len == 3 && memcmp(hm->method.buf, "GET", 3) == 0) {
        /* GET - 获取当前状态 */
        JsonBuilder *j = json_new();
        json_obj_open(j);
        json_add_bool(j, "enabled", phone_case_get_status());
        json_obj_close(j);
        HTTP_OK_FREE(c, json_finish(j));
    }
    else if (hm->method.len == 4 && memcmp(hm->method.buf, "POST", 4) == 0) {
        /* POST - 设置状态 */
        int enabled = 0;
        int val = 0;
        if (mg_json_get_bool(hm->body, "$.enabled", &val)) {
            enabled = val;
        }

        if (phone_case_set_enabled(enabled) == 0) {
            JsonBuilder *j = json_new();
            json_obj_open(j);
            json_add_str(j, "status", "ok");
            json_add_bool(j, "enabled", phone_case_get_status());
            json_obj_close(j);
            HTTP_OK_FREE(c, json_finish(j));
        } else {
            HTTP_ERROR(c, 500, "操作失败");
        }
    }
    else {
        HTTP_ERROR(c, 405, "Method not allowed");
    }
}
