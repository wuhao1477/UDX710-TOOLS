/**
 * @file led.h
 * @brief LED 灯光控制头文件
 * 
 * 基于 fyapp 逆向分析实现的 LED 控制模块
 * 支持: LTE灯、5G NR灯、电池灯、WiFi灯
 */

#ifndef LED_H
#define LED_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED 模式枚举 */
typedef enum {
    LED_MODE_OFF = 0,        /* 关闭 */
    LED_MODE_ON = 1,         /* 常亮 */
    LED_MODE_FLASH_SLOW = 2, /* 慢闪 (1秒周期: 亮500ms, 灭500ms) */
    LED_MODE_FLASH_FAST = 3  /* 快闪 (0.5秒周期) */
} LedMode;

/* LED 状态枚举 - 对应 fyapp 的状态定义 */
typedef enum {
    LED_STATE_IDLE = 0,      /* 空闲 */
    LED_STATE_STARTUP,       /* 启动中 */
    LED_STATE_DIALING,       /* 拨号中 */
    LED_STATE_CONNECTED,     /* 已连接 */
    LED_STATE_OFFLINE,       /* 离线 */
    LED_STATE_ERROR          /* 错误 */
} LedState;

/* LED 节点 ID */
typedef enum {
    LED_LTE_RED = 0,
    LED_LTE_GREEN,
    LED_LTE_BLUE,
    LED_NR_RED,
    LED_NR_GREEN,
    LED_NR_BLUE,
    LED_VBAT_RED,
    LED_VBAT_GREEN,
    LED_WIFI_RED,
    LED_WIFI_GREEN,
    LED_COUNT
} LedId;

/**
 * @brief 初始化 LED 模块
 */
void led_init(void);

/**
 * @brief 关闭 LED 模块
 */
void led_deinit(void);


/**
 * @brief 设置单个 LED 模式
 * @param id LED ID
 * @param mode LED 模式
 */
void led_set_mode(LedId id, LedMode mode);

/**
 * @brief 设置 LED 亮度
 * @param id LED ID
 * @param brightness 亮度值 (0-255)
 */
void led_set_brightness(LedId id, int brightness);

/**
 * @brief 更新网络状态灯
 * @param is_5g 是否为5G网络
 * @param state 网络状态
 */
void led_update_network(int is_5g, LedState state);

/**
 * @brief 更新电池状态灯
 * @param capacity 电池电量百分比 (0-100)
 * @param is_charging 是否正在充电
 */
void led_update_battery(int capacity, int is_charging);

/**
 * @brief 更新 WiFi 状态灯
 * @param is_on WiFi 是否开启
 * @param has_clients 是否有客户端连接
 */
void led_update_wifi(int is_on, int has_clients);

/**
 * @brief 关闭所有 LED
 */
void led_all_off(void);

/**
 * @brief 刷新 LED 状态
 * 
 * 重新读取电池和网络状态并更新 LED
 * 用于双击电源键开启 LED 时强制刷新
 */
void led_refresh(void);

/* HTTP API 处理函数 */
void handle_led_status(struct mg_connection *c, struct mg_http_message *hm);
void handle_led_control(struct mg_connection *c, struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif /* LED_H */

