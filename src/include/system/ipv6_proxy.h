/**
 * @file ipv6_proxy.h
 * @brief IPv6端口转发模块 - 内置6tunnel，支持定时发送IPv6地址
 * 
 * 功能：
 * - IPv6端口转发（内置6tunnel代码）
 * - 定时发送IPv6地址到Webhook
 * - 配置持久化到SQLite数据库
 * - 自启动支持
 */

#ifndef IPV6_PROXY_H
#define IPV6_PROXY_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 最大规则数量 */
#define IPV6_PROXY_MAX_RULES 10

/* PID文件路径模板 */
#define IPV6_PROXY_PID_DIR "/tmp/ipv6_proxy"

/*============================================================================
 * 数据结构
 *============================================================================*/

/**
 * IPv6代理配置
 */
typedef struct {
    int enabled;            /* 服务是否启用 */
    int auto_start;         /* 开机自启动 */
    int send_enabled;       /* 启用定时发送 */
    int send_interval;      /* 发送间隔（分钟） */
    char webhook_url[512];  /* Webhook URL */
    char webhook_body[2048];/* 请求Body模板 */
    char webhook_headers[512]; /* 自定义Headers */
} IPv6ProxyConfig;

/**
 * IPv6转发规则
 */
typedef struct {
    int id;                 /* 规则ID */
    int local_port;         /* 本地端口（设备内部端口） */
    int ipv6_port;          /* IPv6监听端口 */
    int enabled;            /* 是否启用 */
    time_t created_at;      /* 创建时间 */
} IPv6ProxyRule;

/**
 * 服务状态
 */
typedef struct {
    int running;            /* 服务是否运行 */
    int rule_count;         /* 规则总数 */
    int active_count;       /* 活跃规则数 */
    char ipv6_addr[64];     /* 当前IPv6地址 */
} IPv6ProxyStatus;

/*============================================================================
 * 初始化和清理
 *============================================================================*/

/**
 * 初始化IPv6代理模块
 * @param db_path 数据库路径
 * @return 0成功，-1失败
 */
int ipv6_proxy_init(const char *db_path);

/**
 * 清理IPv6代理模块
 */
void ipv6_proxy_deinit(void);

/*============================================================================
 * 配置管理
 *============================================================================*/

/**
 * 获取配置
 * @param config 输出配置结构
 * @return 0成功，-1失败
 */
int ipv6_proxy_get_config(IPv6ProxyConfig *config);

/**
 * 保存配置
 * @param config 配置结构
 * @return 0成功，-1失败
 */
int ipv6_proxy_set_config(const IPv6ProxyConfig *config);

/*============================================================================
 * 规则管理
 *============================================================================*/

/**
 * 获取规则列表
 * @param rules 输出规则数组
 * @param max_count 最大数量
 * @return 实际规则数量，-1失败
 */
int ipv6_proxy_rule_list(IPv6ProxyRule *rules, int max_count);

/**
 * 添加规则
 * @param local_port 本地端口
 * @param ipv6_port IPv6端口
 * @return 新规则ID，-1失败
 */
int ipv6_proxy_rule_add(int local_port, int ipv6_port);

/**
 * 更新规则
 * @param id 规则ID
 * @param local_port 本地端口
 * @param ipv6_port IPv6端口
 * @param enabled 是否启用
 * @return 0成功，-1失败
 */
int ipv6_proxy_rule_update(int id, int local_port, int ipv6_port, int enabled);

/**
 * 删除规则
 * @param id 规则ID
 * @return 0成功，-1失败
 */
int ipv6_proxy_rule_delete(int id);

/*============================================================================
 * 服务控制
 *============================================================================*/

/**
 * 启动服务
 * @return 0成功，-1失败
 */
int ipv6_proxy_start(void);

/**
 * 停止服务
 * @return 0成功，-1失败
 */
int ipv6_proxy_stop(void);

/**
 * 重启服务
 * @return 0成功，-1失败
 */
int ipv6_proxy_restart(void);

/**
 * 获取服务状态
 * @param status 输出状态结构
 * @return 运行状态：1运行中，0未运行
 */
int ipv6_proxy_get_status(IPv6ProxyStatus *status);

/**
 * Allow a native IPv6 listener through the device firewall.
 * The management service owns port 6677 directly and does not use a proxy
 * child, but it still needs the same firewall handling as proxy rules.
 */
void ipv6_proxy_allow_port(int port);

/*============================================================================
 * IPv6地址
 *============================================================================*/

/**
 * 获取当前IPv6地址
 * @param addr 输出地址缓冲区
 * @param size 缓冲区大小
 * @return 0成功，-1失败
 */
int ipv6_proxy_get_ipv6_addr(char *addr, size_t size);

/*============================================================================
 * Webhook发送
 *============================================================================*/

/**
 * 立即发送IPv6地址
 * @return 0成功，-1失败
 */
int ipv6_proxy_send_now(void);

/**
 * 测试发送（异步）
 * @return 0成功，-1失败
 */
int ipv6_proxy_test_send(void);

/*============================================================================
 * 发送日志
 *============================================================================*/

/**
 * 获取发送日志列表
 * @param json_output 输出JSON字符串
 * @param size 缓冲区大小
 * @param max_count 最大记录数（建议100）
 * @return 0成功，-1失败
 */
int ipv6_proxy_get_send_logs(char *json_output, size_t size, int max_count);

#ifdef __cplusplus
}
#endif

#endif /* IPV6_PROXY_H */
