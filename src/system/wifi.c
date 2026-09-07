/**
 * @file wifi.c
 * @brief WiFi控制模块 - 简化实现，基于hostapd进程管理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "wifi.h"
#include "wifi_clients.h"
#include "exec_utils.h"
#include "device_profile.h"
#include "goform_client.h"

/* 配置文件路径 */
#define HOSTAPD_2G_CONF "/mnt/data/hostapd_2g.conf"
#define HOSTAPD_5G_CONF "/mnt/data/hostapd_5g.conf"
#define HOSTAPD_2G_PID  "/mnt/data/hw2pid"
#define HOSTAPD_5G_PID  "/mnt/data/hw5pid"

/* WiFi驱动 */
#define WIFI_DRIVER_PATH "/lib/modules/4.14.98/extra/aic8800D80_fdrv.ko"
#define WIFI_DRIVER_NAME "aic8800D80_fdrv"

/* 数据库路径 */
#define WIFI_DB_PATH "6677.db"

/* 默认配置模板 */
static const char *DEFAULT_5G_CONF = 
"interface=wlan0\n"
"ctrl_interface=/mnt/data\n"
"ssid=WiFi_5G\n"
"hw_mode=any\n"
"channel=0\n"
"chanlist=36-48 149-165\n"
"country_code=CN\n"
"auth_algs=1\n"
"driver=nl80211\n"
"wme_enabled=1\n"
"wpa=2\n"
"wpa_passphrase=12345678\n"
"ieee80211n=1\n"
"ieee80211ac=1\n"
"ieee80211ax=1\n"
"vht_oper_chwidth=1\n"
"he_oper_chwidth=1\n"
"he_basic_mcs_nss_set=65530\n"
"he_twt_required=0\n"
"he_su_beamformee=1\n"
"vht_capab=[SHORT-GI-40][VHT40+][SHORT-GI-80][MAX-A-MPDU-LEN-EXP7]\n"
"ht_capab=[SHORT-GI-20][SHORT-GI-40][HT40+]\n"
"rsn_pairwise=CCMP\n"
"beacon_int=100\n"
"acs_num_scans=5\n"
"max_num_sta=32\n";

static const char *DEFAULT_2G_CONF = 
"interface=wlan0\n"
"ctrl_interface=/mnt/data\n"
"ssid=WiFi_2G\n"
"hw_mode=g\n"
"channel=6\n"
"chanlist=1-13\n"
"country_code=CN\n"
"auth_algs=1\n"
"driver=nl80211\n"
"wme_enabled=1\n"
"wpa=2\n"
"wpa_passphrase=12345678\n"
"ieee80211n=1\n"
"ht_capab=[SHORT-GI-20][SHORT-GI-40][HT40+]\n"
"rsn_pairwise=CCMP\n"
"beacon_int=100\n"
"acs_num_scans=5\n"
"max_num_sta=32\n";

/* 缓存当前频段 */
static char g_current_band[16] = {0};

/* ==================== 前向声明 ==================== */
static int wifi_acl_db_init(void);
static int wifi_load_acl_from_db(void);
static int wifi_read_config_param(const char *conf_file, const char *param_name,
                                  char *value, size_t value_size);
static const char *wifi_get_conf_file(const char *band);

static const char *wifi_iface(void) {
    const DeviceProfile *profile = device_profile_get();
    return profile->wifi_iface[0] ? profile->wifi_iface : "wlan0";
}

/* ==================== 数据库操作 ==================== */

/**
 * @brief 执行SQL命令
 */
static int wifi_db_execute(const char *sql) {
    char output[512];
    char cmd[1024];
    
    snprintf(cmd, sizeof(cmd), "sqlite3 '%s' \"%s\"", WIFI_DB_PATH, sql);
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
        printf("[WiFi] SQL执行失败: %s\n", sql);
        return -1;
    }
    return 0;
}

/**
 * @brief 初始化WiFi配置表
 */
static int wifi_db_init(void) {
    return wifi_db_execute(
        "CREATE TABLE IF NOT EXISTS wifi_config ("
        "id INTEGER PRIMARY KEY,"
        "enabled INTEGER DEFAULT 1,"
        "band TEXT DEFAULT '5G'"
        ");"
    );
}

/**
 * @brief 从数据库加载WiFi配置
 * @param enabled 输出：是否启用
 * @param band 输出：频段
 * @param band_size band缓冲区大小
 * @return 0=成功, -1=失败或无记录
 */
static int wifi_db_load_config(int *enabled, char *band, size_t band_size) {
    char output[128];
    char cmd[256];
    
    snprintf(cmd, sizeof(cmd), 
        "sqlite3 -separator '|' '%s' \"SELECT enabled, band FROM wifi_config WHERE id=1;\"",
        WIFI_DB_PATH);
    
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
        return -1;
    }
    
    if (strlen(output) == 0) {
        return -1;  /* 无记录 */
    }
    
    /* 解析: enabled|band */
    char *sep = strchr(output, '|');
    if (!sep) return -1;
    
    *sep = '\0';
    if (enabled) *enabled = atoi(output);
    if (band && band_size > 0) {
        char *nl = strchr(sep + 1, '\n');
        if (nl) *nl = '\0';
        strncpy(band, sep + 1, band_size - 1);
        band[band_size - 1] = '\0';
    }
    
    return 0;
}

/**
 * @brief 保存WiFi配置到数据库
 */
static int wifi_db_save_config(int enabled, const char *band) {
    char sql[256];
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO wifi_config (id, enabled, band) VALUES (1, %d, '%s');",
        enabled, band ? band : "5G");
    return wifi_db_execute(sql);
}

/**
 * @brief 检查文件是否存在
 */
static int file_exists(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0);
}

/**
 * @brief 检查驱动是否已加载
 */
static int wifi_driver_loaded(void) {
    char output[512];
    if (run_command(output, sizeof(output), "lsmod", NULL) == 0) {
        if (strstr(output, WIFI_DRIVER_NAME)) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 加载WiFi驱动
 */
static int wifi_load_driver(void) {
    char output[512];
    
    if (wifi_driver_loaded()) {
        printf("[WiFi] 驱动已加载\n");
        return 0;
    }
    
    printf("[WiFi] 加载驱动: %s\n", WIFI_DRIVER_PATH);
    if (run_command(output, sizeof(output), "insmod", WIFI_DRIVER_PATH, NULL) != 0) {
        printf("[WiFi] 驱动加载失败\n");
        return -1;
    }
    
    usleep(500000);  /* 等待500ms */
    printf("[WiFi] 驱动加载成功\n");
    return 0;
}

/**
 * @brief 检查hostapd是否运行
 * @return 1=运行中, 0=未运行
 */
static int wifi_is_running(void) {
    FILE *fp;
    char pid_str[32] = {0};
    char proc_path[64];
    
    /* 检查 5G PID 文件 */
    fp = fopen(HOSTAPD_5G_PID, "r");
    if (fp) {
        if (fgets(pid_str, sizeof(pid_str), fp)) {
            char *nl = strchr(pid_str, '\n');
            if (nl) *nl = '\0';
            if (strlen(pid_str) > 0) {
                snprintf(proc_path, sizeof(proc_path), "/proc/%s", pid_str);
                fclose(fp);
                if (access(proc_path, F_OK) == 0) {
                    return 1;
                }
            } else {
                fclose(fp);
            }
        } else {
            fclose(fp);
        }
    }
    
    /* 检查 2.4G PID 文件 */
    memset(pid_str, 0, sizeof(pid_str));
    fp = fopen(HOSTAPD_2G_PID, "r");
    if (fp) {
        if (fgets(pid_str, sizeof(pid_str), fp)) {
            char *nl = strchr(pid_str, '\n');
            if (nl) *nl = '\0';
            if (strlen(pid_str) > 0) {
                snprintf(proc_path, sizeof(proc_path), "/proc/%s", pid_str);
                fclose(fp);
                if (access(proc_path, F_OK) == 0) {
                    return 1;
                }
            } else {
                fclose(fp);
            }
        } else {
            fclose(fp);
        }
    }

    char output[1024];
    if (run_command(output, sizeof(output), "sh", "-c",
                    "ps 2>/dev/null | grep hostapd | grep -v grep", NULL) == 0 &&
        strstr(output, "hostapd")) {
        return 1;
    }
    return 0;
}

/**
 * @brief 获取当前运行的频段
 * @return "2.4G", "5G", 或 NULL(未运行)
 */
static const char* wifi_get_active_band(void) {
    char output[1024];
    const DeviceProfile *profile = device_profile_get();
    
    if (!wifi_is_running()) {
        return NULL;
    }

    if (run_command(output, sizeof(output), "sh", "-c", 
        "ps aux 2>/dev/null | grep hostapd | grep -v grep", NULL) == 0) {
        if (strstr(output, "hostapd_2g.conf")) {
            strncpy(g_current_band, "2.4G", sizeof(g_current_band) - 1);
            return g_current_band;
        } else if (strstr(output, "hostapd_5g.conf")) {
            strncpy(g_current_band, "5G", sizeof(g_current_band) - 1);
            return g_current_band;
        }
        if (profile->wifi_config[0] && strstr(output, profile->wifi_config)) {
            char hw_mode[16];
            if (wifi_read_config_param(profile->wifi_config, "hw_mode",
                                       hw_mode, sizeof(hw_mode)) == 0 &&
                strcmp(hw_mode, "g") == 0) {
                strncpy(g_current_band, "2.4G", sizeof(g_current_band) - 1);
            } else {
                strncpy(g_current_band, "5G", sizeof(g_current_band) - 1);
            }
            return g_current_band;
        }
    }

    strncpy(g_current_band, "5G", sizeof(g_current_band) - 1);
    return g_current_band;
}

/**
 * @brief 确保配置文件存在，不存在则创建默认配置
 */
static int wifi_ensure_config_exists(const char *band) {
    const char *conf_file = wifi_get_conf_file(band);
    const char *default_conf;
    FILE *fp;
    const char *rest;
    
    if (strcmp(band, "2.4G") == 0 || strcmp(band, "2.4g") == 0) {
        default_conf = DEFAULT_2G_CONF;
    } else {
        default_conf = DEFAULT_5G_CONF;
    }
    
    if (file_exists(conf_file)) {
        return 0;
    }
    
    printf("[WiFi] 创建默认配置文件: %s\n", conf_file);
    fp = fopen(conf_file, "w");
    if (!fp) {
        printf("[WiFi] 无法创建配置文件\n");
        return -1;
    }
    
    rest = strchr(default_conf, '\n');
    fprintf(fp, "interface=%s\n%s", wifi_iface(), rest ? rest + 1 : "");
    fclose(fp);
    return 0;
}

/**
 * @brief 从配置文件读取参数
 */
static int wifi_read_config_param(const char *conf_file, const char *param_name, 
                                   char *value, size_t value_size) {
    FILE *fp;
    char line[256];
    size_t param_len;
    
    if (!conf_file || !param_name || !value || value_size == 0) return -1;
    
    fp = fopen(conf_file, "r");
    if (!fp) return -1;
    
    param_len = strlen(param_name);
    value[0] = '\0';
    
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        if (strncmp(line, param_name, param_len) == 0 && line[param_len] == '=') {
            char *val_start = line + param_len + 1;
            char *newline = strchr(val_start, '\n');
            if (newline) *newline = '\0';
            char *cr = strchr(val_start, '\r');
            if (cr) *cr = '\0';
            
            strncpy(value, val_start, value_size - 1);
            value[value_size - 1] = '\0';
            fclose(fp);
            return 0;
        }
    }
    
    fclose(fp);
    return -1;
}

/**
 * @brief 修改配置文件参数
 */
static int wifi_write_config_param(const char *conf_file, const char *param_name, 
                                    const char *value) {
    char output[512];
    char cmd[512];
    
    if (!conf_file || !param_name || !value) return -1;
    
    /* 使用sed修改参数 */
    snprintf(cmd, sizeof(cmd), "sed -i 's/^%s=.*/%s=%s/' %s", 
             param_name, param_name, value, conf_file);
    run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
    
    /* 如果参数不存在，则添加 */
    snprintf(cmd, sizeof(cmd), "grep -q '^%s=' %s || echo '%s=%s' >> %s",
             param_name, conf_file, param_name, value, conf_file);
    run_command(output, sizeof(output), "sh", "-c", cmd, NULL);
    
    return 0;
}

/**
 * @brief 获取当前使用的配置文件路径
 */
static const char* wifi_get_conf_file(const char *band) {
    const DeviceProfile *profile = device_profile_get();
    if (profile->wifi_config[0]) {
        return profile->wifi_config;
    }
    if (strcmp(band, "2.4G") == 0 || strcmp(band, "2.4g") == 0) {
        return HOSTAPD_2G_CONF;
    }
    return HOSTAPD_5G_CONF;
}

/**
 * @brief 获取PID文件路径
 */
static const char* wifi_get_pid_file(const char *band) {
    if (strcmp(band, "2.4G") == 0 || strcmp(band, "2.4g") == 0) {
        return HOSTAPD_2G_PID;
    }
    return HOSTAPD_5G_PID;
}

/**
 * @brief 启动hostapd
 */
static int wifi_start_hostapd(const char *band) {
    char output[512];
    char cmd[512];
    const char *conf_file = wifi_get_conf_file(band);
    const char *pid_file = wifi_get_pid_file(band);
    
    printf("[WiFi] 启动hostapd, 频段: %s\n", band);
    
    /* 确保配置文件存在 */
    if (wifi_ensure_config_exists(band) != 0) {
        return -1;
    }
    
    /* 关闭接口 */
    run_command(output, sizeof(output), "ifconfig", wifi_iface(), "down", NULL);
    usleep(100000);
    
    /* 启动hostapd - 使用 system() 因为 -B 后台模式会立即返回 */
    snprintf(cmd, sizeof(cmd), 
        "hostapd -s -B %s -i %s -P %s -dd &", 
        conf_file, wifi_iface(), pid_file);
    printf("[WiFi] 执行: %s\n", cmd);
    system(cmd);
    
    usleep(1000000);  /* 等待1秒让hostapd启动 */
    
    /* 检查是否启动成功 */
    if (!wifi_is_running()) {
        printf("[WiFi] hostapd启动失败\n");
        return -1;
    }
    
    /* 添加到tether网桥 */
    run_command(output, sizeof(output), "brctl", "addif", "tether", wifi_iface(), NULL);
    
    /* 从数据库加载黑白名单 */
    wifi_load_acl_from_db();
    
    printf("[WiFi] hostapd启动成功\n");
    return 0;
}

/**
 * @brief 停止hostapd
 */
static int wifi_stop_hostapd(void) {
    char output[256];
    
    printf("[WiFi] 停止hostapd\n");
    run_command(output, sizeof(output), "killall", "hostapd", NULL);
    usleep(200000);
    return 0;
}

/**
 * @brief 重启WiFi（保持当前频段）
 */
static int wifi_restart(void) {
    const char *band = wifi_get_active_band();
    
    if (!band) {
        printf("[WiFi] WiFi未运行，无法重启\n");
        return -1;
    }
    
    wifi_stop_hostapd();
    return wifi_start_hostapd(band);
}

/* ==================== 公开API ==================== */

int wifi_init(void) {
    int db_enabled = 1;
    char db_band[16] = "5G";
    int load_ret;
    
    printf("[WiFi] 初始化WiFi模块\n");
    
    /* 延迟等待系统网桥就绪（connman/dnsmasq需要时间初始化） */
    printf("[WiFi] 等待系统网桥就绪...\n");
    sleep(10);
    
    /* 初始化数据库表 */
    if (wifi_db_init() != 0) {
        printf("[WiFi] 数据库表初始化失败\n");
    }
    
    /* 初始化ACL数据库表 */
    wifi_acl_db_init();
    
    /* 加载驱动 */
    wifi_load_driver();
    
    /* 确保两个配置文件都存在 */
    wifi_ensure_config_exists("5G");
    wifi_ensure_config_exists("2.4G");
    
    /* 从数据库加载配置 */
    load_ret = wifi_db_load_config(&db_enabled, db_band, sizeof(db_band));
    printf("[WiFi] 数据库加载结果: ret=%d, enabled=%d, band=%s\n", load_ret, db_enabled, db_band);
    
    if (load_ret != 0) {
        /* 无记录，使用默认值：启用5G */
        printf("[WiFi] 无数据库配置，使用默认值: enabled=1, band=5G\n");
        db_enabled = 1;
        strcpy(db_band, "5G");
        wifi_db_save_config(db_enabled, db_band);
    }
    
    /* 根据配置启动WiFi */
    if (db_enabled && !wifi_is_running()) {
        printf("[WiFi] 根据数据库配置启动WiFi, 频段: %s\n", db_band);
        int start_ret = wifi_start_hostapd(db_band);
        printf("[WiFi] hostapd启动结果: %d\n", start_ret);
    } else if (db_enabled) {
        printf("[WiFi] 检测到系统已有 hostapd，保留当前 WiFi 实例\n");
    } else {
        printf("[WiFi] WiFi配置为关闭状态(enabled=0)，不启动\n");
    }
    
    return 0;
}

int wifi_get_status(WifiConfig *config) {
    char value[128];
    const char *band;
    const char *conf_file;
    char goform_response[16384];
    char goform_ssid[64] = {0};
    char goform_password[64] = {0};
    int goform_enabled = -1;
    int goform_hidden = -1;
    
    if (!config) return -1;
    
    memset(config, 0, sizeof(WifiConfig));

    if (goform_get_device_info(goform_response, sizeof(goform_response)) == 0) {
        goform_json_string(goform_response, "wifissid", goform_ssid,
                           sizeof(goform_ssid));
        goform_json_string(goform_response, "wifipwd", goform_password,
                           sizeof(goform_password));
        goform_json_int(goform_response, "wifistate", &goform_enabled);
        goform_json_int(goform_response, "ssidhide", &goform_hidden);
    }
    
    /* 检查是否运行 */
    config->enabled = wifi_is_running();
    
    /* 获取当前频段 */
    band = wifi_get_active_band();
    if (band) {
        strncpy(config->band, band, sizeof(config->band) - 1);
        conf_file = wifi_get_conf_file(band);
    } else {
        /* 未运行时默认读取5G配置 */
        strncpy(config->band, "5G", sizeof(config->band) - 1);
        conf_file = wifi_get_conf_file("5G");
    }
    
    /* 读取SSID */
    if (wifi_read_config_param(conf_file, "ssid", value, sizeof(value)) == 0) {
        strncpy(config->ssid, value, sizeof(config->ssid) - 1);
    }
    
    /* 读取密码 */
    if (wifi_read_config_param(conf_file, "wpa_passphrase", value, sizeof(value)) == 0) {
        strncpy(config->password, value, sizeof(config->password) - 1);
    }
    
    /* 读取信道 */
    if (wifi_read_config_param(conf_file, "channel", value, sizeof(value)) == 0) {
        config->channel = atoi(value);
    }
    
    /* 读取最大连接数 */
    if (wifi_read_config_param(conf_file, "max_num_sta", value, sizeof(value)) == 0) {
        config->max_clients = atoi(value);
    } else {
        config->max_clients = 32;
    }
    
    /* 加密方式固定WPA2 */
    strncpy(config->encryption, "WPA2", sizeof(config->encryption) - 1);

    if (goform_ssid[0]) {
        strncpy(config->ssid, goform_ssid, sizeof(config->ssid) - 1);
    }
    if (goform_password[0]) {
        strncpy(config->password, goform_password, sizeof(config->password) - 1);
    }
    if (goform_enabled >= 0) {
        config->enabled = goform_enabled != 0;
    }
    if (goform_hidden >= 0) {
        config->hidden = goform_hidden != 0;
    }
    
    return 0;
}

int wifi_enable(const char *band) {
    if (!band) band = "5G";
    
    printf("[WiFi] 启用WiFi, 频段: %s\n", band);
    
    /* 确保驱动已加载 */
    if (wifi_load_driver() != 0) {
        return -1;
    }
    
    /* 如果已运行，先停止 */
    if (wifi_is_running()) {
        wifi_stop_hostapd();
    }
    
    int ret = wifi_start_hostapd(band);
    
    /* 保存配置到数据库 */
    if (ret == 0) {
        wifi_db_save_config(1, band);
    }
    
    return ret;
}

int wifi_disable(void) {
    printf("[WiFi] 禁用WiFi\n");
    
    /* 获取当前频段用于保存 */
    const char *current_band = wifi_get_active_band();
    const char *save_band = current_band ? current_band : "5G";
    
    if (!wifi_is_running()) {
        printf("[WiFi] WiFi已经是关闭状态\n");
        /* 仍然保存关闭状态到数据库 */
        wifi_db_save_config(0, save_band);
        return 0;
    }
    
    int ret = wifi_stop_hostapd();
    
    /* 保存配置到数据库 */
    wifi_db_save_config(0, save_band);
    
    return ret;
}

int wifi_set_ap_info(const char *ssid, const char *password) {
    WifiConfig current;
    char response[8192];
    const char *target_ssid = ssid && *ssid ? ssid : NULL;
    const char *target_password = password && *password ? password : NULL;
    int result;

    if ((target_password && strlen(target_password) < 8) ||
        wifi_get_status(&current) != 0) {
        return -1;
    }
    if (!target_ssid) target_ssid = current.ssid;
    if (!target_password) target_password = current.password;
    if (!target_ssid[0] || !target_password[0] ||
        goform_set_wifi_info(target_ssid, target_password, response,
                             sizeof(response)) != 0 ||
        goform_json_int(response, "result", &result) != 0 || result != 0) {
        return -1;
    }
    return 0;
}

int wifi_set_ssid(const char *ssid) {
    if (!ssid || strlen(ssid) == 0) return -1;
    return wifi_set_ap_info(ssid, NULL);
}

int wifi_set_password(const char *password) {
    if (!password || strlen(password) < 8) return -1;
    return wifi_set_ap_info(NULL, password);
}

int wifi_set_band(const char *band) {
    if (!band) return -1;
    
    printf("[WiFi] 切换频段: %s\n", band);
    
    /* 确保驱动已加载 */
    if (wifi_load_driver() != 0) {
        return -1;
    }
    
    /* 停止当前hostapd */
    if (wifi_is_running()) {
        wifi_stop_hostapd();
    }
    
    /* 启动新频段 */
    int ret = wifi_start_hostapd(band);
    
    /* 保存配置到数据库（保持enabled状态为1，因为切换频段意味着WiFi是开启的） */
    if (ret == 0) {
        wifi_db_save_config(1, band);
    }
    
    return ret;
}

int wifi_set_max_clients(int max_clients) {
    const char *band;
    const char *conf_file;
    char value[16];
    
    if (max_clients < 1 || max_clients > 128) {
        printf("[WiFi] 最大连接数范围: 1-128\n");
        return -1;
    }
    
    printf("[WiFi] 设置最大连接数: %d\n", max_clients);
    
    band = wifi_get_active_band();
    if (!band) band = "5G";
    conf_file = wifi_get_conf_file(band);
    
    snprintf(value, sizeof(value), "%d", max_clients);
    wifi_write_config_param(conf_file, "max_num_sta", value);
    
    if (wifi_is_running()) {
        return wifi_restart();
    }
    
    return 0;
}

int wifi_set_config(const WifiConfig *config) {
    const char *conf_file;
    char value[16];
    int need_restart = 0;
    
    if (!config) return -1;
    
    printf("[WiFi] 设置完整配置\n");
    
    conf_file = wifi_get_conf_file(config->band);
    
    /* 确保配置文件存在 */
    wifi_ensure_config_exists(config->band);
    
    /* 更新SSID */
    if (strlen(config->ssid) > 0) {
        wifi_write_config_param(conf_file, "ssid", config->ssid);
        need_restart = 1;
    }
    
    /* 更新密码 */
    if (strlen(config->password) >= 8) {
        wifi_write_config_param(conf_file, "wpa_passphrase", config->password);
        need_restart = 1;
    }
    
    /* 更新最大连接数 */
    if (config->max_clients > 0) {
        snprintf(value, sizeof(value), "%d", config->max_clients);
        wifi_write_config_param(conf_file, "max_num_sta", value);
        need_restart = 1;
    }
    
    /* 处理启用/禁用 */
    if (config->enabled) {
        /* 检查是否需要切换频段 */
        const char *current_band = wifi_get_active_band();
        if (!wifi_is_running() || !current_band || strcmp(current_band, config->band) != 0) {
            return wifi_enable(config->band);
        } else if (need_restart) {
            return wifi_restart();
        }
    } else {
        return wifi_disable();
    }
    
    return 0;
}

/* 保留兼容性API */
int wifi_set_channel(int channel) {
    (void)channel;
    printf("[WiFi] 信道设置已禁用（使用自动信道）\n");
    return 0;
}

int wifi_set_hidden(int hidden) {
    (void)hidden;
    printf("[WiFi] 隐藏SSID设置已禁用\n");
    return 0;
}

int wifi_reload(void) {
    return wifi_restart();
}


/* ==================== ACL数据库操作 ==================== */

/**
 * @brief 初始化ACL数据库表
 */
static int wifi_acl_db_init(void) {
    wifi_db_execute(
        "CREATE TABLE IF NOT EXISTS wifi_blacklist ("
        "mac TEXT PRIMARY KEY,"
        "created_at INTEGER DEFAULT (strftime('%s','now'))"
        ");"
    );
    wifi_db_execute(
        "CREATE TABLE IF NOT EXISTS wifi_whitelist ("
        "mac TEXT PRIMARY KEY,"
        "created_at INTEGER DEFAULT (strftime('%s','now'))"
        ");"
    );
    return 0;
}

/**
 * @brief 从数据库加载ACL到hostapd
 */
static int wifi_load_acl_from_db(void) {
    static char output[2048];  /* 静态缓冲区 */
    char cmd[256];
    char *line, *saveptr;
    
    printf("[WiFi] 从数据库加载ACL...\n");
    
    /* 加载黑名单 */
    snprintf(cmd, sizeof(cmd), 
        "sqlite3 '%s' \"SELECT mac FROM wifi_blacklist;\"", WIFI_DB_PATH);
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) == 0) {
        line = strtok_r(output, "\n", &saveptr);
        while (line) {
            if (strlen(line) >= 17) {
                char acl_cmd[128];
                snprintf(acl_cmd, sizeof(acl_cmd),
                    "hostapd_cli -p /mnt/data -i %s deny_acl ADD_MAC %s",
                    wifi_iface(), line);
                system(acl_cmd);
                printf("[WiFi] 加载黑名单: %s\n", line);
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }
    
    /* 加载白名单 */
    snprintf(cmd, sizeof(cmd), 
        "sqlite3 '%s' \"SELECT mac FROM wifi_whitelist;\"", WIFI_DB_PATH);
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) == 0) {
        line = strtok_r(output, "\n", &saveptr);
        while (line) {
            if (strlen(line) >= 17) {
                char acl_cmd[128];
                snprintf(acl_cmd, sizeof(acl_cmd),
                    "hostapd_cli -p /mnt/data -i %s accept_acl ADD_MAC %s",
                    wifi_iface(), line);
                system(acl_cmd);
                printf("[WiFi] 加载白名单: %s\n", line);
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }
    
    return 0;
}

/* ==================== 客户端管理 ==================== */

static WifiClient *find_client_by_mac(WifiClient *clients, int count,
                                      const char *mac) {
    for (int i = 0; i < count; i++) {
        if (strcmp(clients[i].mac, mac) == 0) {
            return &clients[i];
        }
    }
    return NULL;
}

static WifiClient *ensure_client(WifiClient *clients, int *count, int max_count,
                                 const char *mac) {
    WifiClient *client = find_client_by_mac(clients, *count, mac);
    if (client || *count >= max_count) {
        return client;
    }
    client = &clients[*count];
    memset(client, 0, sizeof(*client));
    snprintf(client->mac, sizeof(client->mac), "%s", mac);
    snprintf(client->interface, sizeof(client->interface), "tether");
    snprintf(client->access_type, sizeof(client->access_type), "bridge");
    (*count)++;
    return client;
}

static void merge_client(WifiClient *target, const WifiClient *source) {
    if (!target || !source) {
        return;
    }
    if (source->ipv4[0]) {
        snprintf(target->ipv4, sizeof(target->ipv4), "%s", source->ipv4);
    }
    if (source->ipv6[0]) {
        snprintf(target->ipv6, sizeof(target->ipv6), "%s", source->ipv6);
    }
    if (source->interface[0] && strcmp(source->interface, "tether") != 0) {
        snprintf(target->interface, sizeof(target->interface), "%s",
                 source->interface);
        snprintf(target->access_type, sizeof(target->access_type), "%s",
                 source->access_type);
    }
}

static void collect_arp_clients(WifiClient *clients, int max_count, int *count) {
    static char output[8192];
    char *line;
    char *saveptr = NULL;

    if (run_command(output, sizeof(output), "cat", "/proc/net/arp", NULL) !=
        0) {
        return;
    }
    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        WifiClient parsed;
        if (wifi_client_parse_arp_line(line, &parsed) == 0) {
            WifiClient *target = ensure_client(clients, count, max_count,
                                                parsed.mac);
            merge_client(target, &parsed);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

static void collect_neighbor_clients(WifiClient *clients, int max_count,
                                     int *count) {
    static char output[8192];
    char *line;
    char *saveptr = NULL;

    if (run_command(output, sizeof(output), "ip", "neigh", "show", "dev",
                    "tether", NULL) != 0) {
        return;
    }
    line = strtok_r(output, "\n", &saveptr);
    while (line) {
        WifiClient parsed;
        if (wifi_client_parse_neigh_line(line, &parsed) == 0) {
            WifiClient *target = ensure_client(clients, count, max_count,
                                                parsed.mac);
            merge_client(target, &parsed);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

typedef struct {
    int port;
    char interface[32];
} BridgePort;

static void collect_bridge_clients(WifiClient *clients, int max_count,
                                   int *count) {
    static char stp_output[8192];
    static char fdb_output[8192];
    BridgePort ports[16];
    int port_count = 0;
    char *line;
    char *saveptr = NULL;

    if (run_command(stp_output, sizeof(stp_output), "brctl", "showstp",
                    "tether", NULL) == 0) {
        line = strtok_r(stp_output, "\n", &saveptr);
        while (line && port_count < 16) {
            int port;
            if (wifi_client_parse_bridge_port(line, ports[port_count].interface,
                                              sizeof(ports[port_count].interface),
                                              &port) == 0) {
                ports[port_count].port = port;
                port_count++;
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
    }

    if (run_command(fdb_output, sizeof(fdb_output), "brctl", "showmacs",
                    "tether", NULL) != 0) {
        return;
    }
    saveptr = NULL;
    line = strtok_r(fdb_output, "\n", &saveptr);
    while (line) {
        char mac[18];
        int port;
        int local;
        if (wifi_client_parse_fdb_line(line, mac, sizeof(mac), &port, &local) ==
                0 &&
            !local) {
            WifiClient *target = ensure_client(clients, count, max_count, mac);
            for (int i = 0; target && i < port_count; i++) {
                if (ports[i].port == port) {
                    snprintf(target->interface, sizeof(target->interface), "%s",
                             ports[i].interface);
                    snprintf(target->access_type, sizeof(target->access_type),
                             "%s", wifi_client_access_type(ports[i].interface));
                    break;
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

int wifi_get_clients(WifiClient *clients, int max_count) {
    static char output[8192];  /* 静态缓冲区，避免栈溢出 */
    char cmd[128];
    int count = 0;
    
    if (!clients || max_count <= 0) return -1;
    
    /* 执行 hostapd_cli all_sta */
    snprintf(cmd, sizeof(cmd), 
        "hostapd_cli -p /mnt/data -i %s all_sta", wifi_iface());
    
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
        printf("[WiFi] 获取客户端列表失败\n");
        return -1;
    }
    
    /* 解析输出 - 每个客户端以MAC地址开头 */
    char *p = output;
    char *line_end;
    WifiClient *current = NULL;
    
    while (*p && count < max_count) {
        line_end = strchr(p, '\n');
        if (line_end) *line_end = '\0';
        
        /* 检测MAC地址行 (xx:xx:xx:xx:xx:xx) */
        if (strlen(p) == 17 && p[2] == ':' && p[5] == ':') {
            if (current) count++;  /* 保存上一个客户端 */
            if (count >= max_count) break;
            
            current = &clients[count];
            memset(current, 0, sizeof(WifiClient));
            strncpy(current->mac, p, 17);
            current->mac[17] = '\0';
            snprintf(current->interface, sizeof(current->interface), "%s",
                     wifi_iface());
            snprintf(current->access_type, sizeof(current->access_type), "wifi");
        }
        /* 解析属性 */
        else if (current) {
            if (strncmp(p, "rx_rate_info=", 13) == 0) {
                /* rx_rate_info=1730 vhtmcs 9 vhtnss 2 - 取第一个数字(kbps) */
                current->rx_bytes = strtoul(p + 13, NULL, 10);
            }
            else if (strncmp(p, "tx_rate_info=", 13) == 0) {
                /* tx_rate_info=7800 vhtmcs 9 vhtnss 2 - 取第一个数字(kbps) */
                current->tx_bytes = strtoul(p + 13, NULL, 10);
            }
            else if (strncmp(p, "signal=", 7) == 0) {
                current->signal = atoi(p + 7);
            }
            else if (strncmp(p, "connected_time=", 15) == 0) {
                current->connected_time = atoi(p + 15);
            }
        }
        
        if (line_end) {
            p = line_end + 1;
        } else {
            break;
        }
    }
    
    /* 保存最后一个客户端 */
    if (current && count < max_count) count++;

    /* hostapd 在部分固件上不维护 station 表，补充网桥邻居和 FDB。 */
    collect_arp_clients(clients, max_count, &count);
    collect_neighbor_clients(clients, max_count, &count);
    collect_bridge_clients(clients, max_count, &count);
    
    printf("[WiFi] 获取到 %d 个客户端\n", count);
    return count;
}

/* ==================== 黑名单管理 ==================== */

int wifi_blacklist_add(const char *mac) {
    char cmd[128];
    char sql[128];
    
    if (!mac || strlen(mac) < 17) return -1;
    
    /* 添加到hostapd黑名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s deny_acl ADD_MAC %s",
        wifi_iface(), mac);
    system(cmd);
    
    /* 踢出该客户端 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s deauthenticate %s",
        wifi_iface(), mac);
    system(cmd);
    
    /* 保存到数据库 */
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO wifi_blacklist (mac) VALUES ('%s');", mac);
    wifi_db_execute(sql);
    
    printf("[WiFi] 添加黑名单: %s\n", mac);
    return 0;
}

int wifi_blacklist_del(const char *mac) {
    char cmd[128];
    char sql[128];
    
    if (!mac || strlen(mac) < 17) return -1;
    
    /* 从hostapd移除 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s deny_acl DEL_MAC %s",
        wifi_iface(), mac);
    system(cmd);
    
    /* 从数据库删除 */
    snprintf(sql, sizeof(sql),
        "DELETE FROM wifi_blacklist WHERE mac='%s';", mac);
    wifi_db_execute(sql);
    
    printf("[WiFi] 移除黑名单: %s\n", mac);
    return 0;
}

int wifi_blacklist_clear(void) {
    char cmd[128];
    
    /* 清空hostapd黑名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s deny_acl CLEAR", wifi_iface());
    system(cmd);
    
    /* 清空数据库 */
    wifi_db_execute("DELETE FROM wifi_blacklist;");
    
    printf("[WiFi] 清空黑名单\n");
    return 0;
}

int wifi_blacklist_list(char macs[][18], int max_count) {
    static char output[2048];  /* 静态缓冲区 */
    char cmd[128];
    int count = 0;
    
    if (!macs || max_count <= 0) return -1;
    
    /* 从hostapd获取黑名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s deny_acl SHOW", wifi_iface());
    
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
        return -1;
    }
    
    /* 解析输出 - 每行格式: xx:xx:xx:xx:xx:xx VLAN_ID=0 */
    char *line = strtok(output, "\n");
    while (line && count < max_count) {
        if (strlen(line) >= 17 && line[2] == ':') {
            strncpy(macs[count], line, 17);
            macs[count][17] = '\0';
            count++;
        }
        line = strtok(NULL, "\n");
    }
    
    return count;
}

/* ==================== 白名单管理 ==================== */

int wifi_whitelist_add(const char *mac) {
    char cmd[128];
    char sql[128];
    
    if (!mac || strlen(mac) < 17) return -1;
    
    /* 添加到hostapd白名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s accept_acl ADD_MAC %s",
        wifi_iface(), mac);
    system(cmd);
    
    /* 保存到数据库 */
    snprintf(sql, sizeof(sql),
        "INSERT OR REPLACE INTO wifi_whitelist (mac) VALUES ('%s');", mac);
    wifi_db_execute(sql);
    
    printf("[WiFi] 添加白名单: %s\n", mac);
    return 0;
}

int wifi_whitelist_del(const char *mac) {
    char cmd[128];
    char sql[128];
    
    if (!mac || strlen(mac) < 17) return -1;
    
    /* 从hostapd移除 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s accept_acl DEL_MAC %s",
        wifi_iface(), mac);
    system(cmd);
    
    /* 从数据库删除 */
    snprintf(sql, sizeof(sql),
        "DELETE FROM wifi_whitelist WHERE mac='%s';", mac);
    wifi_db_execute(sql);
    
    printf("[WiFi] 移除白名单: %s\n", mac);
    return 0;
}

int wifi_whitelist_clear(void) {
    char cmd[128];
    
    /* 清空hostapd白名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s accept_acl CLEAR", wifi_iface());
    system(cmd);
    
    /* 清空数据库 */
    wifi_db_execute("DELETE FROM wifi_whitelist;");
    
    printf("[WiFi] 清空白名单\n");
    return 0;
}

int wifi_whitelist_list(char macs[][18], int max_count) {
    static char output[2048];  /* 静态缓冲区 */
    char cmd[128];
    int count = 0;
    
    if (!macs || max_count <= 0) return -1;
    
    /* 从hostapd获取白名单 */
    snprintf(cmd, sizeof(cmd),
        "hostapd_cli -p /mnt/data -i %s accept_acl SHOW", wifi_iface());
    
    if (run_command(output, sizeof(output), "sh", "-c", cmd, NULL) != 0) {
        return -1;
    }
    
    /* 解析输出 */
    char *line = strtok(output, "\n");
    while (line && count < max_count) {
        if (strlen(line) >= 17 && line[2] == ':') {
            strncpy(macs[count], line, 17);
            macs[count][17] = '\0';
            count++;
        }
        line = strtok(NULL, "\n");
    }
    
    return count;
}
