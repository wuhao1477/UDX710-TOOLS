/**
 * @file wifi.h
 * @brief WiFi控制模块 - 基于hostapd进程管理
 */

#ifndef WIFI_H
#define WIFI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi配置结构体 */
typedef struct {
    int enabled;           /* WiFi是否启用 (hostapd是否运行) */
    char band[16];         /* 频段: "2.4G" 或 "5G" */
    char ssid[64];         /* WiFi名称 */
    char password[64];     /* WiFi密码 */
    int channel;           /* 信道 */
    char encryption[16];   /* 加密方式: "WPA2" */
    int hidden;            /* 保留字段 */
    int max_clients;       /* 最大连接数 */
} WifiConfig;

/**
 * @brief 初始化WiFi模块（加载驱动，确保配置文件存在）
 * @return 0 成功, -1 失败
 */
int wifi_init(void);

/**
 * @brief 获取WiFi状态
 * @param config 输出WiFi配置
 * @return 0 成功, -1 失败
 */
int wifi_get_status(WifiConfig *config);

/**
 * @brief 启用WiFi
 * @param band 频段: "2.4G" 或 "5G"，NULL默认5G
 * @return 0 成功, -1 失败
 */
int wifi_enable(const char *band);

/**
 * @brief 禁用WiFi
 * @return 0 成功, -1 失败
 */
int wifi_disable(void);

/**
 * @brief 设置SSID（修改后自动重启生效）
 * @param ssid 新的SSID
 * @return 0 成功, -1 失败
 */
int wifi_set_ssid(const char *ssid);

/**
 * @brief 设置密码（修改后自动重启生效）
 * @param password 新的密码（至少8位）
 * @return 0 成功, -1 失败
 */
int wifi_set_password(const char *password);
int wifi_set_ap_info(const char *ssid, const char *password);

/**
 * @brief 切换WiFi频段
 * @param band 频段: "2.4G" 或 "5G"
 * @return 0 成功, -1 失败
 */
int wifi_set_band(const char *band);

/**
 * @brief 设置最大连接数
 * @param max_clients 最大连接数 (1-128)
 * @return 0 成功, -1 失败
 */
int wifi_set_max_clients(int max_clients);

/**
 * @brief 设置完整WiFi配置
 * @param config WiFi配置
 * @return 0 成功, -1 失败
 */
int wifi_set_config(const WifiConfig *config);

/* 兼容性API（保留但功能禁用） */
int wifi_set_channel(int channel);
int wifi_set_hidden(int hidden);
int wifi_reload(void);

/* ==================== 客户端管理 ==================== */

/* WiFi客户端信息结构体 */
typedef struct {
    char mac[18];           /* MAC地址 xx:xx:xx:xx:xx:xx */
    char ipv4[40];          /* IPv4地址 */
    char ipv6[80];          /* IPv6地址 */
    char interface[32];     /* 实际桥接接口 */
    char access_type[16];   /* wifi/usb/rj45/bridge/unknown */
    unsigned long rx_bytes; /* 接收字节数（客户端上传） */
    unsigned long tx_bytes; /* 发送字节数（客户端下载） */
    int signal;             /* 信号强度 (dBm, 负值) */
    int connected_time;     /* 连接时长(秒) */
} WifiClient;

/**
 * @brief 获取已连接的WiFi客户端列表
 * @param clients 输出客户端数组
 * @param max_count 数组最大容量
 * @return 实际客户端数量, -1失败
 */
int wifi_get_clients(WifiClient *clients, int max_count);

/* ==================== 黑名单管理 ==================== */

/**
 * @brief 添加MAC到黑名单（同时踢出该客户端）
 * @param mac MAC地址
 * @return 0成功, -1失败
 */
int wifi_blacklist_add(const char *mac);

/**
 * @brief 从黑名单移除MAC
 * @param mac MAC地址
 * @return 0成功, -1失败
 */
int wifi_blacklist_del(const char *mac);

/**
 * @brief 清空黑名单
 * @return 0成功, -1失败
 */
int wifi_blacklist_clear(void);

/**
 * @brief 获取黑名单列表
 * @param macs 输出MAC地址数组 (每个18字节)
 * @param max_count 数组最大容量
 * @return 实际数量, -1失败
 */
int wifi_blacklist_list(char macs[][18], int max_count);

/* ==================== 白名单管理 ==================== */

/**
 * @brief 添加MAC到白名单
 * @param mac MAC地址
 * @return 0成功, -1失败
 */
int wifi_whitelist_add(const char *mac);

/**
 * @brief 从白名单移除MAC
 * @param mac MAC地址
 * @return 0成功, -1失败
 */
int wifi_whitelist_del(const char *mac);

/**
 * @brief 清空白名单
 * @return 0成功, -1失败
 */
int wifi_whitelist_clear(void);

/**
 * @brief 获取白名单列表
 * @param macs 输出MAC地址数组 (每个18字节)
 * @param max_count 数组最大容量
 * @return 实际数量, -1失败
 */
int wifi_whitelist_list(char macs[][18], int max_count);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H */
