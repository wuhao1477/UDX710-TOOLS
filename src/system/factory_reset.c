/**
 * @file factory_reset.c
 * @brief 恢复出厂设置实现
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mongoose.h"
#include "factory_reset.h"
#include "exec_utils.h"
#include "traffic.h"
#include "http_utils.h"
#include "device_profile.h"

#define VNSTAT_DB "/var/lib/vnstat/vnstat.db"
#define MAIN_DB "/home/root/6677/6677.db"

/* GET /api/factory-reset - 恢复出厂设置 */
void handle_factory_reset(struct mg_connection *c, struct mg_http_message *hm) {
    HTTP_CHECK_POST(c, hm);

    char output[256];

    /* 1. 删除流量统计数据库并重新初始化 */
    run_command(output, sizeof(output), "rm", "-f", VNSTAT_DB, NULL);
    init_traffic();

    /* 2. 删除主数据库 */
    run_command(output, sizeof(output), "rm", "-f", MAIN_DB, NULL);

    /* 3. 删除当前设备实际使用的 WiFi 配置文件 */
    const DeviceProfile *profile = device_profile_get();
    if (profile->wifi_config[0]) {
        run_command(output, sizeof(output), "rm", "-f", profile->wifi_config,
                    NULL);
    } else {
        run_command(output, sizeof(output), "rm", "-f",
                    "/mnt/data/hostapd_2g.conf", NULL);
        run_command(output, sizeof(output), "rm", "-f",
                    "/mnt/data/hostapd_5g.conf", NULL);
    }

    /* 先返回响应 */
    HTTP_OK(c, "{\"success\":true,\"msg\":\"Factory reset complete, rebooting...\"}");

    /* 延迟1秒后重启 */
    sleep(1);
    run_command(output, sizeof(output), "/sbin/reboot", NULL);
}
