/**
 * @file exec_utils.h
 * @brief Command execution utilities (Go: system/exec.go)
 */

#ifndef EXEC_UTILS_H
#define EXEC_UTILS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* APP_DEMO_PATH 已移除，改用 ofono.h 中的 D-Bus 接口 */

/**
 * @brief 执行命令并获取输出
 * @param output 输出缓冲区
 * @param size 缓冲区大小
 * @param cmd 命令
 * @param ... 参数列表 (以 NULL 结尾)
 * @return 0 成功, -1 失败
 */
int run_command(char *output, size_t size, const char *cmd, ...);

/**
 * @brief 使用参数数组执行命令，不经过shell
 */
int run_command_argv(char *output, size_t size, char *const argv[]);

/**
 * @brief 带超时执行命令
 * @param timeout_sec 超时秒数
 * @param output 输出缓冲区
 * @param size 缓冲区大小
 * @param cmd 命令
 * @param ... 参数列表 (以 NULL 结尾)
 * @return 0 成功, -1 失败
 */
int run_command_timeout(int timeout_sec, char *output, size_t size, const char *cmd, ...);

/**
 * @brief 设备重启
 */
void device_reboot(void);

/**
 * @brief 设备关机
 */
void device_poweroff(void);

/**
 * @brief 清除系统缓存
 * @return 0 成功, -1 失败
 */
int clear_cache(void);

#ifdef __cplusplus
}
#endif

#endif /* EXEC_UTILS_H */
