/**
 * @file power_key.h
 * @brief 电源键监听模块
 * 
 * 监听电源键事件:
 * - 长按: 关机 (全部红灯闪烁3秒后关机)
 * - 双击: 切换LED灯光状态
 */

#ifndef POWER_KEY_H
#define POWER_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化电源键监听
 * @return 成功返回0，失败返回-1
 */
int power_key_init(void);

/**
 * @brief 关闭电源键监听
 */
void power_key_deinit(void);

/**
 * @brief 获取LED灯光是否开启
 * @return 1=开启, 0=关闭
 */
int power_key_get_led_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_KEY_H */

