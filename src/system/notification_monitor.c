#define _POSIX_C_SOURCE 200809L

#include "notification.h"

#include "database.h"
#include "device_profile.h"
#include "netif.h"
#include "sysinfo.h"
#include "traffic.h"
#include "wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTIFICATION_MAX_CLIENTS 64
#define NOTIFICATION_SIGNATURE_SIZE 2048

static int g_snapshot_ready;
static char g_interface_signature[NOTIFICATION_SIGNATURE_SIZE];
static char g_ip_signature[NOTIFICATION_SIGNATURE_SIZE];
static int g_rj45_present;
static int g_data_online;
static char g_client_macs[NOTIFICATION_MAX_CLIENTS][18];
static int g_client_count;
static int g_clients_ready;
static char g_network_type[32];
static int g_network_ready;
static double g_signal_value;
static int g_signal_ready;
static double g_traffic_value;
static int g_traffic_ready;

static int rule_enabled(NotificationEventType type, NotificationRule *rule) {
  return notification_get_rule(type, rule) == 0 && rule->enabled;
}

static void emit_message(NotificationEventType type, const char *title,
                         const char *message) {
  NotificationEvent event = {0};
  event.type = type;
  snprintf(event.title, sizeof(event.title), "%s", title);
  snprintf(event.message, sizeof(event.message), "%s", message);
  event.timestamp = time(NULL);
  notification_emit(&event);
}

static int append_signature(char *out, size_t out_size,
                            const NetInterface *interfaces, int count,
                            int addresses) {
  size_t used = 0;
  for (int i = 0; i < count; i++) {
    int written;
    if (addresses) {
      written = snprintf(out + used, out_size - used, "%s|%s|%s;",
                         interfaces[i].name, interfaces[i].inet_addr,
                         interfaces[i].inet6_addr);
    } else {
      written = snprintf(out + used, out_size - used, "%s|%d;",
                         interfaces[i].name, interfaces[i].is_up);
    }
    if (written < 0 || (size_t)written >= out_size - used) return -1;
    used += (size_t)written;
  }
  return 0;
}

static int is_rj45_interface(const char *name) {
  return name && (strncmp(name, "eth", 3) == 0 ||
                  strncmp(name, "en", 2) == 0 ||
                  strncmp(name, "lan", 3) == 0);
}

static void update_connectivity(void) {
  NetInterface interfaces[MAX_NET_INTERFACES];
  char interface_signature[NOTIFICATION_SIGNATURE_SIZE] = {0};
  char ip_signature[NOTIFICATION_SIGNATURE_SIZE] = {0};
  int count = netif_get_list(interfaces, MAX_NET_INTERFACES);
  int rj45 = 0;
  int data_online = 0;
  const DeviceProfile *profile = device_profile_get();
  if (count <= 0) return;
  if (append_signature(interface_signature, sizeof(interface_signature),
                       interfaces, count, 0) != 0 ||
      append_signature(ip_signature, sizeof(ip_signature), interfaces, count,
                       1) != 0) {
    return;
  }
  for (int i = 0; i < count; i++) {
    if (is_rj45_interface(interfaces[i].name) && interfaces[i].is_up) {
      rj45 = 1;
    }
    if (profile->data_iface[0] &&
        strcmp(profile->data_iface, interfaces[i].name) == 0 &&
        interfaces[i].is_up) {
      data_online = 1;
    }
  }

  if (g_snapshot_ready) {
    if (notification_value_changed(g_interface_signature,
                                   interface_signature)) {
      emit_message(NOTIFICATION_EVENT_NETWORK_INTERFACE_CHANGED,
                   "网络接口变化", "网络接口状态发生变化");
    }
    if (notification_value_changed(g_ip_signature, ip_signature)) {
      emit_message(NOTIFICATION_EVENT_IP_CHANGED, "IP地址变化",
                   "IPv4或IPv6地址发生变化");
    }
    if (g_rj45_present != rj45) {
      emit_message(NOTIFICATION_EVENT_RJ45_CHANGED, "RJ45连接变化",
                   rj45 ? "RJ45接口已接入" : "RJ45接口已断开");
    }
    if (g_data_online != data_online) {
      emit_message(NOTIFICATION_EVENT_DEVICE_STATUS, "设备网络状态变化",
                   data_online ? "数据网络已连接" : "数据网络已断开");
    }
  }
  snprintf(g_interface_signature, sizeof(g_interface_signature), "%s",
           interface_signature);
  snprintf(g_ip_signature, sizeof(g_ip_signature), "%s", ip_signature);
  g_rj45_present = rj45;
  g_data_online = data_online;
  g_snapshot_ready = 1;
}

static int collect_client_macs(char macs[][18], int max_count) {
  WifiClient clients[NOTIFICATION_MAX_CLIENTS];
  int count = wifi_get_clients(clients, NOTIFICATION_MAX_CLIENTS);
  if (count < 0) return -1;
  if (count > max_count) count = max_count;
  for (int i = 0; i < count; i++) {
    snprintf(macs[i], 18, "%s", clients[i].mac);
  }
  return count;
}

static int mac_exists(const char macs[][18], int count, const char *mac) {
  for (int i = 0; i < count; i++) {
    if (strcmp(macs[i], mac) == 0) return 1;
  }
  return 0;
}

static void update_clients(void) {
  char current[NOTIFICATION_MAX_CLIENTS][18];
  int count = collect_client_macs(current, NOTIFICATION_MAX_CLIENTS);
  if (count < 0) return;
  if (g_clients_ready &&
      notification_mac_set_changed(g_client_macs, g_client_count, current,
                                   count)) {
    int added = 0;
    int removed = 0;
    for (int i = 0; i < count; i++) {
      if (!mac_exists(g_client_macs, g_client_count, current[i])) added = 1;
    }
    for (int i = 0; i < g_client_count; i++) {
      if (!mac_exists(current, count, g_client_macs[i])) removed = 1;
    }
    emit_message(NOTIFICATION_EVENT_DEVICE_CHANGED, "接入设备变化",
                 added && !removed ? "有设备接入" :
                 removed && !added ? "有设备断开" : "接入设备列表发生变化");
  }
  memcpy(g_client_macs, current, sizeof(current));
  g_client_count = count;
  g_clients_ready = 1;
}

static int parse_signal(const char *value, double *percent, int *dbm) {
  if (!value || !percent || !dbm) return -1;
  return sscanf(value, "%lf%%, %d dBm", percent, dbm) == 2 ? 0 : -1;
}

static void update_signal(void) {
  NotificationRule rule;
  char value[64];
  double percent;
  int dbm;
  if (!rule_enabled(NOTIFICATION_EVENT_SIGNAL_LOW, &rule) ||
      get_signal_strength(value, sizeof(value)) != 0 ||
      parse_signal(value, &percent, &dbm) != 0) {
    return;
  }
  double current = strcmp(rule.threshold_unit, "dbm") == 0 ? dbm : percent;
  if (g_signal_ready &&
      notification_threshold_crossed(g_signal_value, current, rule.threshold,
                                     0)) {
    char message[128];
    snprintf(message, sizeof(message), "当前信号 %.0f%s，低于 %.0f%s", current,
             strcmp(rule.threshold_unit, "dbm") == 0 ? " dBm" : "%",
             rule.threshold,
             strcmp(rule.threshold_unit, "dbm") == 0 ? " dBm" : "%");
    emit_message(NOTIFICATION_EVENT_SIGNAL_LOW, "网络信号过低", message);
  }
  g_signal_value = current;
  g_signal_ready = 1;
}

static void update_traffic(void) {
  NotificationRule rule;
  long long total;
  double current;
  if (!rule_enabled(NOTIFICATION_EVENT_TRAFFIC_THRESHOLD, &rule) ||
      traffic_get_total_bytes(&total) != 0) {
    return;
  }
  if (strcmp(rule.threshold_unit, "percent") == 0) {
    long long limit = config_get_ll("traffic_much", 0);
    if (limit <= 0) return;
    current = (double)total * 100.0 / (double)limit;
  } else {
    current = (double)total;
  }
  if (g_traffic_ready &&
      notification_threshold_crossed(g_traffic_value, current,
                                     rule.threshold, 1)) {
    char message[128];
    snprintf(message, sizeof(message), "已使用流量达到 %.0f%s，超过 %.0f%s",
             current, strcmp(rule.threshold_unit, "percent") == 0 ? "%" : " B",
             rule.threshold,
             strcmp(rule.threshold_unit, "percent") == 0 ? "%" : " B");
    emit_message(NOTIFICATION_EVENT_TRAFFIC_THRESHOLD, "流量达到阈值", message);
  }
  g_traffic_value = current;
  g_traffic_ready = 1;
}

static void update_network_type(void) {
  NotificationRule rule;
  char type[32] = {0};
  char band[32] = {0};
  if (!rule_enabled(NOTIFICATION_EVENT_NETWORK_TYPE_CHANGED, &rule) ||
      get_network_type_and_band(type, sizeof(type), band, sizeof(band)) != 0 ||
      type[0] == '\0' || strcmp(type, "N/A") == 0) {
    return;
  }
  if (g_network_ready && notification_value_changed(g_network_type, type)) {
    char message[128];
    snprintf(message, sizeof(message), "网络类型已切换为 %s%s%s%s", type,
             band[0] ? "（" : "", band[0] ? band : "", band[0] ? "）" : "");
    emit_message(NOTIFICATION_EVENT_NETWORK_TYPE_CHANGED, "4G/5G网络变化",
                 message);
  }
  snprintf(g_network_type, sizeof(g_network_type), "%s", type);
  g_network_ready = 1;
}

void notification_maintenance(void) {
  NotificationRule rule;
  int connectivity = rule_enabled(NOTIFICATION_EVENT_NETWORK_INTERFACE_CHANGED,
                                  &rule) ||
                     rule_enabled(NOTIFICATION_EVENT_IP_CHANGED, &rule) ||
                     rule_enabled(NOTIFICATION_EVENT_RJ45_CHANGED, &rule) ||
                     rule_enabled(NOTIFICATION_EVENT_DEVICE_STATUS, &rule);
  if (connectivity) update_connectivity();
  if (rule_enabled(NOTIFICATION_EVENT_DEVICE_CHANGED, &rule)) update_clients();
  update_signal();
  update_traffic();
  update_network_type();
}
