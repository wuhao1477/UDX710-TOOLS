/**
 * @file main.c
 * @brief 服务器主程序入口 (对应 Go: main.go)
 */

#include "http_server.h"
#include "device_profile.h"
#include "led.h"
#include "netif.h"
#include "ofono.h"
#include "power_key.h"
#include "telemetry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  const char *port = "6677";

  telemetry_start_output_capture();

  /* 解析命令行参数 */
  if (argc > 1) {
    port = argv[1];
  }

  printf("=== ofono-server (C version) ===\n");

  device_profile_init();
  const DeviceProfile *profile = device_profile_get();
  printf("设备能力: wifi=%s data=%s rj45=%s typec=%s battery=%s\n",
         profile->wifi_iface[0] ? profile->wifi_iface : "none",
         profile->data_iface[0] ? profile->data_iface : "none",
         profile->rj45_usable ? "usable" : "unavailable",
         profile->typec_present ? "present" : "absent",
         profile->battery_supported ? "present" : "external-power");

  /* 同步系统时间 */
  system("ntpdate ntp.aliyun.com > /dev/null 2>&1 &");

  /* 初始化 ofono D-Bus 连接 */
  if (!ofono_init()) {
    fprintf(stderr, "警告: ofono D-Bus 连接失败，部分功能可能不可用\n");
  }

  led_init();
  if (power_key_init() != 0) {
    fprintf(stderr, "警告: 电源键监听初始化失败\n");
  }

  /* 初始化网络接口监听（自动恢复之前启用的监听） */
  init_netif();

  /* 启动数据连接监听（无论当前状态） */
  printf("启动数据连接监听...\n");
  ofono_start_data_monitor();

  /* 启动 HTTP 服务器 */
  if (http_server_start(port) != 0) {
    fprintf(stderr, "服务器启动失败\n");
    power_key_deinit();
    led_deinit();
    ofono_stop_data_monitor();
    ofono_deinit();
    return 1;
  }

  /* 运行事件循环 */
  http_server_run();

  /* 清理 */
  http_server_stop();
  telemetry_stop_output_capture();
  power_key_deinit();
  led_deinit();
  ofono_stop_data_monitor();
  ofono_deinit();

  return 0;
}
