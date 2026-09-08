#define _POSIX_C_SOURCE 200809L

#include "telemetry.h"

#include "database.h"
#include "apn.h"
#include "charge.h"
#include "device_profile.h"
#include "exec_utils.h"
#include "ipv6_proxy.h"
#include "json_builder.h"
#include "netif.h"
#include "notification.h"
#include "ofono.h"
#include "rathole.h"
#include "sysinfo.h"
#include "traffic.h"
#include "usb_mode.h"
#include "wifi.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TELEMETRY_DEFAULT_INTERVAL 60
#define TELEMETRY_MIN_INTERVAL 10
#define TELEMETRY_MAX_INTERVAL 3600
#define TELEMETRY_FRAGMENT_SIZE (1024U * 1024U)
#define TELEMETRY_DEVICE_ID_SIZE 128
#define TELEMETRY_BOOT_ID_SIZE 64

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} TelemetryBuffer;

static TelemetryConfig g_config;
static TelemetryStatus g_status;
static TelemetryBuffer g_pending;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_initialized;
static int g_stop;
static int g_flush_requested;
static int g_sender_active;
static unsigned long long g_sequence;
static char g_device_id[TELEMETRY_DEVICE_ID_SIZE];
static char g_boot_id[TELEMETRY_BOOT_ID_SIZE];
static int g_output_pipe[2] = {-1, -1};
static int g_saved_stdout = -1;
static int g_saved_stderr = -1;
static int g_output_stop;
static int g_output_running;
static pthread_t g_output_thread;

static void copy_string(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static void *output_reader(void *arg) {
  char *line = NULL;
  size_t length = 0;
  size_t capacity = 0;
  char chunk[1024];
  (void)arg;

  while (!g_output_stop) {
    ssize_t count = read(g_output_pipe[0], chunk, sizeof(chunk));
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    for (ssize_t i = 0; i < count; i++) {
      if (length + 1 >= capacity) {
        size_t next_capacity = capacity ? capacity * 2 : 2048;
        char *next = (char *)realloc(line, next_capacity);
        if (!next) {
          length = 0;
          continue;
        }
        line = next;
        capacity = next_capacity;
      }
      if (chunk[i] == '\n') {
        line[length] = '\0';
        telemetry_capture_line("service", line);
        length = 0;
      } else if (chunk[i] != '\r') {
        line[length++] = chunk[i];
      }
    }
  }
  if (length > 0 && line) {
    line[length] = '\0';
    telemetry_capture_line("service", line);
  }
  free(line);
  return NULL;
}

static int config_get_text(const char *key, char *value, size_t value_size) {
  if (config_get(key, value, value_size) != 0) {
    value[0] = '\0';
    return -1;
  }
  db_unescape_string(value);
  return 0;
}

static int config_set_text(const char *key, const char *value) {
  char escaped[TELEMETRY_TOKEN_SIZE * 2 + TELEMETRY_URL_SIZE * 2];

  db_escape_string(value ? value : "", escaped, sizeof(escaped));
  return config_set(key, escaped);
}

static int url_valid(const char *url) {
  if (!url || url[0] == '\0') return 0;
  return strncmp(url, "http://", 7) == 0 ||
         strncmp(url, "https://", 8) == 0;
}

static int read_device_identity(void) {
  FILE *file;
  size_t length;

  memset(g_device_id, 0, sizeof(g_device_id));
  memset(g_boot_id, 0, sizeof(g_boot_id));
  if (get_serial(g_device_id, sizeof(g_device_id)) != 0 ||
      g_device_id[0] == '\0' || strcmp(g_device_id, "N/A") == 0) {
    snprintf(g_device_id, sizeof(g_device_id), "");
  }

  file = fopen("/proc/sys/kernel/random/boot_id", "r");
  if (file) {
    if (fgets(g_boot_id, sizeof(g_boot_id), file)) {
      length = strlen(g_boot_id);
      while (length > 0 && (g_boot_id[length - 1] == '\n' ||
                            g_boot_id[length - 1] == '\r')) {
        g_boot_id[--length] = '\0';
      }
    }
    fclose(file);
  }
  if (g_boot_id[0] == '\0') snprintf(g_boot_id, sizeof(g_boot_id), "unknown");
  return g_device_id[0] == '\0' ? -1 : 0;
}

static int config_valid(const TelemetryConfig *config) {
  if (!config || config->interval_sec < TELEMETRY_MIN_INTERVAL ||
      config->interval_sec > TELEMETRY_MAX_INTERVAL ||
      (config->url[0] != '\0' && !url_valid(config->url))) {
    return 0;
  }
  if (config->enabled &&
      (config->url[0] == '\0' || config->group_token[0] == '\0' ||
       g_device_id[0] == '\0')) {
    return 0;
  }
  return 1;
}

static void load_config(void) {
  char value[TELEMETRY_URL_SIZE];

  memset(&g_config, 0, sizeof(g_config));
  g_config.interval_sec = config_get_int("telemetry_interval_sec",
                                         TELEMETRY_DEFAULT_INTERVAL);
  if (g_config.interval_sec < TELEMETRY_MIN_INTERVAL ||
      g_config.interval_sec > TELEMETRY_MAX_INTERVAL) {
    g_config.interval_sec = TELEMETRY_DEFAULT_INTERVAL;
  }
  g_config.enabled = config_get_int("telemetry_enabled", 0) ? 1 : 0;
  if (config_get_text("telemetry_url", value, sizeof(value)) == 0) {
    copy_string(g_config.url, sizeof(g_config.url), value);
  }
  if (config_get_text("telemetry_group_token", value, sizeof(value)) == 0) {
    copy_string(g_config.group_token, sizeof(g_config.group_token), value);
    g_config.token_present = g_config.group_token[0] != '\0';
  }
  if (!config_valid(&g_config)) g_config.enabled = 0;
}

static int buffer_append(TelemetryBuffer *buffer, const char *data, size_t length) {
  size_t required;
  size_t capacity;
  char *grown;

  if (!buffer || !data || length == 0) return 0;
  required = buffer->length + length + 1;
  if (required <= buffer->capacity) {
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 0;
  }
  capacity = buffer->capacity ? buffer->capacity : TELEMETRY_FRAGMENT_SIZE;
  while (capacity < required) capacity *= 2;
  grown = (char *)realloc(buffer->data, capacity);
  if (!grown) return -1;
  buffer->data = grown;
  buffer->capacity = capacity;
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 0;
}

static char *json_quote(const char *value) {
  const unsigned char *cursor = (const unsigned char *)(value ? value : "");
  size_t required = 3;
  char *output;
  size_t used = 0;

  while (*cursor) {
    required += (*cursor == '\\' || *cursor == '"')
                    ? 2
                    : (*cursor < 0x20 ? 6 : 1);
    cursor++;
  }
  output = (char *)malloc(required + 1);
  if (!output) return NULL;
  output[used++] = '"';
  cursor = (const unsigned char *)(value ? value : "");
  while (*cursor) {
    if (*cursor == '\\' || *cursor == '"') output[used++] = '\\';
    if (*cursor == '\n') {
      output[used++] = '\\';
      output[used++] = 'n';
    } else if (*cursor == '\r') {
      output[used++] = '\\';
      output[used++] = 'r';
    } else if (*cursor == '\t') {
      output[used++] = '\\';
      output[used++] = 't';
    } else if (*cursor < 0x20) {
      used += (size_t)snprintf(output + used, required + 1 - used, "\\u%04x",
                               *cursor);
    } else {
      output[used++] = (char)*cursor;
    }
    cursor++;
  }
  output[used++] = '"';
  output[used] = '\0';
  return output;
}

static int append_message_record(TelemetryBuffer *batch, const char *type,
                                 const char *source, const char *message) {
  char *redacted = NULL;
  char *quoted = NULL;
  char *payload = NULL;
  char *record = NULL;
  size_t message_length = strlen(message ? message : "");
  size_t payload_size = message_length * 2 + 64;
  size_t record_size = payload_size * 2 + 512;
  int result = -1;

  redacted = (char *)malloc(message_length * 2 + 64);
  payload = (char *)malloc(payload_size);
  record = (char *)malloc(record_size);
  if (!redacted || !payload || !record ||
      telemetry_redact_text(message ? message : "", redacted,
                            message_length * 2 + 64) != 0) {
    goto cleanup;
  }
  quoted = json_quote(redacted);
  if (!quoted || snprintf(payload, payload_size, "{\"message\":%s}", quoted) < 0 ||
      telemetry_build_record(record, record_size, g_device_id, g_boot_id,
                             g_sequence++, type, source, payload) != 0 ||
      buffer_append(batch, record, strlen(record)) != 0) {
    goto cleanup;
  }
  result = 0;

cleanup:
  free(redacted);
  free(quoted);
  free(payload);
  free(record);
  return result;
}

static int write_all(int fd, const char *data, size_t length) {
  while (length > 0) {
    ssize_t written = write(fd, data, length);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return -1;
    data += written;
    length -= (size_t)written;
  }
  return 0;
}

static int send_gzip_fragment(const TelemetryConfig *config, const char *data,
                              size_t length) {
  int gzip_input[2] = {-1, -1};
  int gzip_output[2] = {-1, -1};
  int response_pipe[2] = {-1, -1};
  pid_t gzip_pid = -1;
  pid_t curl_pid = -1;
  int gzip_status = 0;
  int curl_status = 0;
  char auth[TELEMETRY_TOKEN_SIZE + 32];
  char device_header[TELEMETRY_DEVICE_ID_SIZE + 32];
  char response[512];
  char *curl_argv[24];
  int argc = 0;
  int result = -1;

  if (!config || !data || length == 0 || pipe(gzip_input) != 0 ||
      pipe(gzip_output) != 0 || pipe(response_pipe) != 0) {
    goto cleanup;
  }
  snprintf(auth, sizeof(auth), "Authorization: Bearer %s", config->group_token);
  snprintf(device_header, sizeof(device_header), "X-Device-ID: %s", g_device_id);
  curl_argv[argc++] = (char *)"curl";
  curl_argv[argc++] = (char *)"-sS";
  curl_argv[argc++] = (char *)"--max-time";
  curl_argv[argc++] = (char *)"30";
  curl_argv[argc++] = (char *)"-f";
  curl_argv[argc++] = (char *)"-X";
  curl_argv[argc++] = (char *)"POST";
  curl_argv[argc++] = (char *)config->url;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = auth;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = device_header;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = (char *)"Content-Type: application/x-ndjson";
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = (char *)"Content-Encoding: gzip";
  curl_argv[argc++] = (char *)"--data-binary";
  curl_argv[argc++] = (char *)"@-";
  curl_argv[argc] = NULL;

  gzip_pid = fork();
  if (gzip_pid == 0) {
    dup2(gzip_input[0], STDIN_FILENO);
    dup2(gzip_output[1], STDOUT_FILENO);
    close(gzip_input[0]);
    close(gzip_input[1]);
    close(gzip_output[0]);
    close(gzip_output[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
    execlp("gzip", "gzip", "-c", (char *)NULL);
    _exit(127);
  }
  if (gzip_pid < 0) goto cleanup;

  curl_pid = fork();
  if (curl_pid == 0) {
    dup2(gzip_output[0], STDIN_FILENO);
    dup2(response_pipe[1], STDOUT_FILENO);
    dup2(response_pipe[1], STDERR_FILENO);
    close(gzip_input[0]);
    close(gzip_input[1]);
    close(gzip_output[0]);
    close(gzip_output[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
    execvp(curl_argv[0], curl_argv);
    _exit(127);
  }
  if (curl_pid < 0) goto cleanup;

  close(gzip_input[0]);
  gzip_input[0] = -1;
  close(gzip_output[0]);
  gzip_output[0] = -1;
  close(gzip_output[1]);
  gzip_output[1] = -1;
  close(response_pipe[1]);
  response_pipe[1] = -1;
  if (write_all(gzip_input[1], data, length) != 0) goto cleanup;
  close(gzip_input[1]);
  gzip_input[1] = -1;
  response[0] = '\0';
  read(response_pipe[0], response, sizeof(response) - 1);
  close(response_pipe[0]);
  response_pipe[0] = -1;
  waitpid(gzip_pid, &gzip_status, 0);
  waitpid(curl_pid, &curl_status, 0);
  result = WIFEXITED(gzip_status) && WEXITSTATUS(gzip_status) == 0 &&
           WIFEXITED(curl_status) && WEXITSTATUS(curl_status) == 0
               ? 0
               : -1;

cleanup:
  if (gzip_input[0] >= 0) close(gzip_input[0]);
  if (gzip_input[1] >= 0) close(gzip_input[1]);
  if (gzip_output[0] >= 0) close(gzip_output[0]);
  if (gzip_output[1] >= 0) close(gzip_output[1]);
  if (response_pipe[0] >= 0) close(response_pipe[0]);
  if (response_pipe[1] >= 0) close(response_pipe[1]);
  if (gzip_pid > 0) waitpid(gzip_pid, &gzip_status, 0);
  if (curl_pid > 0) waitpid(curl_pid, &curl_status, 0);
  return result;
}

static void update_result(int success, const char *error) {
  pthread_mutex_lock(&g_lock);
  if (success) {
    g_status.last_success = time(NULL);
    g_status.sent_count++;
    g_status.last_error[0] = '\0';
  } else {
    g_status.last_failure = time(NULL);
    g_status.failed_count++;
    copy_string(g_status.last_error, sizeof(g_status.last_error),
                error ? error : "上传失败");
  }
  pthread_mutex_unlock(&g_lock);
}

static TelemetryBuffer take_pending(void) {
  TelemetryBuffer pending;
  pthread_mutex_lock(&g_lock);
  pending = g_pending;
  memset(&g_pending, 0, sizeof(g_pending));
  pthread_mutex_unlock(&g_lock);
  return pending;
}

static void send_pending(const TelemetryConfig *config) {
  TelemetryBuffer pending = take_pending();
  if (pending.length > 0) {
    int success;
    pthread_mutex_lock(&g_lock);
    g_sender_active = 1;
    pthread_mutex_unlock(&g_lock);
    success = send_gzip_fragment(config, pending.data, pending.length) == 0;
    pthread_mutex_lock(&g_lock);
    g_sender_active = 0;
    pthread_mutex_unlock(&g_lock);
    update_result(success, success ? NULL : "日志分片上传失败");
  }
  free(pending.data);
}

static void add_system_snapshot(JsonBuilder *json, const SystemInfo *info) {
  json_key_obj_open(json, "system");
  json_add_str(json, "hostname", info->hostname);
  json_add_str(json, "sysname", info->sysname);
  json_add_str(json, "release", info->release);
  json_add_str(json, "version", info->version);
  json_add_str(json, "machine", info->machine);
  json_add_ulong(json, "total_ram", info->total_ram);
  json_add_ulong(json, "free_ram", info->free_ram);
  json_add_ulong(json, "cached_ram", info->cached_ram);
  json_add_double(json, "cpu_usage", info->cpu_usage);
  json_add_double(json, "uptime", info->uptime);
  json_add_str(json, "bridge_status", info->bridge_status);
  json_add_str(json, "sim_slot", info->sim_slot);
  json_add_str(json, "signal_strength", info->signal_strength);
  json_add_double(json, "thermal_temp", info->thermal_temp);
  json_add_str(json, "power_status", info->power_status);
  json_add_str(json, "power_source", info->power_source);
  json_add_bool(json, "battery_supported", info->battery_supported);
  json_add_str(json, "battery_health", info->battery_health);
  json_add_int(json, "battery_capacity", (int)info->battery_capacity);
  json_add_str(json, "ssid", info->ssid);
  json_add_str(json, "select_network_mode", info->select_network_mode);
  json_add_int(json, "is_activated", info->is_activated);
  json_add_str(json, "serial", info->serial);
  json_add_str(json, "network_mode", info->network_mode);
  json_add_bool(json, "airplane_mode", info->airplane_mode);
  json_add_str(json, "imei", info->imei);
  json_add_str(json, "iccid", info->iccid);
  json_add_str(json, "imsi", info->imsi);
  json_add_str(json, "carrier", info->carrier);
  json_add_str(json, "network_type", info->network_type);
  json_add_str(json, "network_band", info->network_band);
  json_add_int(json, "qci", info->qci);
  json_add_int(json, "downlink_rate", info->downlink_rate);
  json_add_int(json, "uplink_rate", info->uplink_rate);
  json_obj_close(json);
}

static void add_interfaces_snapshot(JsonBuilder *json, NetInterface *interfaces,
                                    int interface_count) {
  json_arr_open(json, "interfaces");
  for (int i = 0; i < interface_count; i++) {
    json_arr_obj_open(json);
    json_add_str(json, "name", interfaces[i].name);
    json_add_str(json, "hwaddr", interfaces[i].hwaddr);
    json_add_str(json, "inet_addr", interfaces[i].inet_addr);
    json_add_str(json, "inet6_addr", interfaces[i].inet6_addr);
    json_add_str(json, "mask", interfaces[i].mask);
    json_add_bool(json, "is_up", interfaces[i].is_up);
    json_add_bool(json, "monitoring", interfaces[i].monitoring);
    if (interfaces[i].monitoring) {
      NetifStats stats;
      if (netif_get_stats(interfaces[i].name, &stats) == 0) {
        json_key_obj_open(json, "rx");
        json_add_long(json, "bytes_per_second", stats.rx.bytespersecond);
        json_add_long(json, "packets_per_second", stats.rx.packetspersecond);
        json_add_long(json, "bytes", stats.rx.bytes);
        json_add_long(json, "packets", stats.rx.packets);
        json_add_long(json, "total_bytes", stats.rx.totalbytes);
        json_add_long(json, "total_packets", stats.rx.totalpackets);
        json_obj_close(json);
        json_key_obj_open(json, "tx");
        json_add_long(json, "bytes_per_second", stats.tx.bytespersecond);
        json_add_long(json, "packets_per_second", stats.tx.packetspersecond);
        json_add_long(json, "bytes", stats.tx.bytes);
        json_add_long(json, "packets", stats.tx.packets);
        json_add_long(json, "total_bytes", stats.tx.totalbytes);
        json_add_long(json, "total_packets", stats.tx.totalpackets);
        json_obj_close(json);
      }
    }
    json_obj_close(json);
  }
  json_arr_close(json);
}

static void add_clients_snapshot(JsonBuilder *json, WifiClient *clients,
                                 int client_count) {
  json_arr_open(json, "clients");
  for (int i = 0; i < client_count; i++) {
    json_arr_obj_open(json);
    json_add_str(json, "mac", clients[i].mac);
    json_add_str(json, "ipv4", clients[i].ipv4);
    json_add_str(json, "ipv6", clients[i].ipv6);
    json_add_str(json, "interface", clients[i].interface);
    json_add_str(json, "access_type", clients[i].access_type);
    json_add_ulong(json, "rx_bytes", clients[i].rx_bytes);
    json_add_ulong(json, "tx_bytes", clients[i].tx_bytes);
    json_add_int(json, "signal", clients[i].signal);
    json_add_int(json, "connected_time", clients[i].connected_time);
    json_obj_close(json);
  }
  json_arr_close(json);
}

static void collect_minimal_snapshot(const TelemetryConfig *config) {
  SystemInfo info;
  DeviceProfile profile;
  NetInterface interfaces[MAX_NET_INTERFACES];
  WifiClient clients[64];
  WifiConfig wifi;
  ApnConfig apn;
  RatholeConfig rathole;
  RatholeStatus rathole_status;
  IPv6ProxyConfig ipv6_config;
  IPv6ProxyStatus ipv6_status;
  NotificationWebhookConfig notification;
  UsbAdbStatus adb;
  JsonBuilder *json = NULL;
  char *payload = NULL;
  char *record = NULL;
  TelemetryBuffer batch = {0};
  TelemetryBuffer pending;
  long long traffic_total = 0;
  int data_active = 0;
  int roaming_allowed = 0;
  int is_roaming = 0;
  int battery_capacity = 0;
  int charging = 0;
  int interface_count;
  int client_count;
  int usb_mode;
  int record_size;

  memset(&info, 0, sizeof(info));
  memset(&profile, 0, sizeof(profile));
  memset(&wifi, 0, sizeof(wifi));
  memset(&apn, 0, sizeof(apn));
  memset(&rathole, 0, sizeof(rathole));
  memset(&rathole_status, 0, sizeof(rathole_status));
  memset(&ipv6_config, 0, sizeof(ipv6_config));
  memset(&ipv6_status, 0, sizeof(ipv6_status));
  memset(&notification, 0, sizeof(notification));
  memset(&adb, 0, sizeof(adb));
  device_profile_refresh();
  profile = *device_profile_get();
  get_system_info(&info);
  interface_count = netif_get_list(interfaces, MAX_NET_INTERFACES);
  if (interface_count < 0) interface_count = 0;
  client_count = wifi_get_clients(clients, 64);
  if (client_count < 0) client_count = 0;
  wifi_get_status(&wifi);
  apn_get_config(&apn);
  rathole_get_config(&rathole);
  rathole_get_status(&rathole_status);
  ipv6_proxy_get_config(&ipv6_config);
  ipv6_proxy_get_status(&ipv6_status);
  notification_get_webhook_config(&notification);
  ofono_get_data_status(&data_active);
  ofono_get_roaming_status(&roaming_allowed, &is_roaming);
  charge_get_battery_status(&battery_capacity, &charging);
  usb_adb_get_status(&adb);
  usb_mode = usb_mode_get_current_hardware();
  traffic_get_total_bytes(&traffic_total);

  json = json_new();
  if (!json) goto cleanup;
  json_obj_open(json);
  add_system_snapshot(json, &info);
  json_key_obj_open(json, "device_profile");
  json_add_str(json, "model", profile.model);
  json_add_bool(json, "mains_powered", profile.mains_powered);
  json_add_bool(json, "battery_supported", profile.battery_supported);
  json_add_bool(json, "rj45_usable", profile.rj45_usable);
  json_add_bool(json, "typec_present", profile.typec_present);
  json_add_bool(json, "usb_rndis_available", profile.usb_rndis_available);
  json_add_str(json, "wifi_iface", profile.wifi_iface);
  json_add_str(json, "data_iface", profile.data_iface);
  json_add_str(json, "rj45_iface", profile.rj45_iface);
  json_obj_close(json);
  add_interfaces_snapshot(json, interfaces, interface_count);
  add_clients_snapshot(json, clients, client_count);
  json_key_obj_open(json, "traffic");
  json_add_long(json, "total_bytes", traffic_total);
  json_obj_close(json);
  json_key_obj_open(json, "wifi");
  json_add_bool(json, "enabled", wifi.enabled);
  json_add_str(json, "band", wifi.band);
  json_add_str(json, "ssid", wifi.ssid);
  json_add_int(json, "channel", wifi.channel);
  json_add_str(json, "encryption", wifi.encryption);
  json_add_bool(json, "hidden", wifi.hidden);
  json_add_int(json, "max_clients", wifi.max_clients);
  json_obj_close(json);
  json_key_obj_open(json, "network");
  json_add_bool(json, "data_active", data_active);
  json_add_bool(json, "roaming_allowed", roaming_allowed);
  json_add_bool(json, "is_roaming", is_roaming);
  json_obj_close(json);
  json_key_obj_open(json, "power");
  json_add_int(json, "battery_capacity", battery_capacity);
  json_add_bool(json, "charging", charging);
  json_obj_close(json);
  json_key_obj_open(json, "usb");
  json_add_int(json, "mode", usb_mode);
  json_add_int(json, "adb_daemon_running", adb.daemon_running);
  json_add_int(json, "adb_usb_enabled", adb.usb_enabled);
  json_add_int(json, "adb_wireless_enabled", adb.wireless_enabled);
  json_add_int(json, "adb_wireless_port", adb.wireless_port);
  json_obj_close(json);
  json_key_obj_open(json, "apn");
  json_add_int(json, "mode", apn.mode);
  json_add_int(json, "template_id", apn.template_id);
  json_add_int(json, "auto_start", apn.auto_start);
  json_obj_close(json);
  json_key_obj_open(json, "rathole");
  json_add_str(json, "server_addr", rathole.server_addr);
  json_add_int(json, "auto_start", rathole.auto_start);
  json_add_int(json, "enabled", rathole.enabled);
  json_add_bool(json, "running", rathole_status.running);
  json_add_int(json, "service_count", rathole_status.service_count);
  json_obj_close(json);
  json_key_obj_open(json, "ipv6_proxy");
  json_add_int(json, "enabled", ipv6_config.enabled);
  json_add_int(json, "auto_start", ipv6_config.auto_start);
  json_add_int(json, "send_enabled", ipv6_config.send_enabled);
  json_add_int(json, "send_interval", ipv6_config.send_interval);
  json_add_bool(json, "running", ipv6_status.running);
  json_add_int(json, "rule_count", ipv6_status.rule_count);
  json_add_str(json, "ipv6_addr", ipv6_status.ipv6_addr);
  json_obj_close(json);
  json_key_obj_open(json, "notification");
  json_add_bool(json, "enabled", notification.enabled);
  json_add_str(json, "platform", notification.platform);
  json_obj_close(json);
  json_obj_close(json);
  payload = json_finish(json);
  json = NULL;
  if (!payload) goto cleanup;
  record_size = (int)(strlen(payload) * 2 + 1024);
  record = (char *)malloc((size_t)record_size);
  if (!record || telemetry_build_record(record, (size_t)record_size, g_device_id,
                                        g_boot_id, g_sequence++, "snapshot",
                                        "device", payload) != 0 ||
      buffer_append(&batch, record, strlen(record)) != 0) {
    goto cleanup;
  }
  pending = take_pending();
  buffer_append(&batch, pending.data, pending.length);
  free(pending.data);
  pthread_mutex_lock(&g_lock);
  g_sender_active = 1;
  pthread_mutex_unlock(&g_lock);
  update_result(send_gzip_fragment(config, batch.data, batch.length) == 0,
                "快照上传失败");
  pthread_mutex_lock(&g_lock);
  g_sender_active = 0;
  pthread_mutex_unlock(&g_lock);

cleanup:
  if (json) json_free(json);
  free(payload);
  free(record);
  free(batch.data);
}

static void *telemetry_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_lock);
  g_status.running = 1;
  while (!g_stop) {
    TelemetryConfig config;
    struct timespec deadline;

    while (!g_stop && (!g_config.enabled || !config_valid(&g_config))) {
      pthread_cond_wait(&g_ready, &g_lock);
    }
    if (g_stop) break;
    config = g_config;
    pthread_mutex_unlock(&g_lock);

    collect_minimal_snapshot(&config);
    send_pending(&config);

    pthread_mutex_lock(&g_lock);
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += config.interval_sec;
    g_flush_requested = 0;
    while (!g_stop && !g_flush_requested && g_config.enabled) {
      if (pthread_cond_timedwait(&g_ready, &g_lock, &deadline) == ETIMEDOUT) break;
    }
  }
  g_status.running = 0;
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

int telemetry_init(const char *db_path) {
  if (g_initialized) return 0;
  if (db_init(db_path) != 0 || read_device_identity() != 0) return -1;
  load_config();
  pthread_mutex_lock(&g_lock);
  g_stop = 0;
  g_initialized = 1;
  pthread_mutex_unlock(&g_lock);
  if (pthread_create(&g_worker, NULL, telemetry_worker, NULL) != 0) {
    pthread_mutex_lock(&g_lock);
    g_initialized = 0;
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  pthread_cond_signal(&g_ready);
  return 0;
}

int telemetry_start_output_capture(void) {
  int thread_started = 0;

  if (g_output_running) return 0;
  if (pipe(g_output_pipe) != 0) return -1;
  g_output_stop = 0;
  g_saved_stdout = dup(STDOUT_FILENO);
  g_saved_stderr = dup(STDERR_FILENO);
  if (g_saved_stdout < 0 || g_saved_stderr < 0 ||
      pthread_create(&g_output_thread, NULL, output_reader, NULL) != 0) {
    if (g_saved_stdout >= 0) close(g_saved_stdout);
    if (g_saved_stderr >= 0) close(g_saved_stderr);
    if (g_output_pipe[0] >= 0) close(g_output_pipe[0]);
    if (g_output_pipe[1] >= 0) close(g_output_pipe[1]);
    g_output_pipe[0] = g_output_pipe[1] = -1;
    return -1;
  }
  thread_started = 1;
  if (dup2(g_output_pipe[1], STDOUT_FILENO) < 0 ||
      dup2(g_output_pipe[1], STDERR_FILENO) < 0) {
    g_output_stop = 1;
    close(g_output_pipe[1]);
    g_output_pipe[1] = -1;
    if (thread_started) pthread_join(g_output_thread, NULL);
    if (g_saved_stdout >= 0) dup2(g_saved_stdout, STDOUT_FILENO);
    if (g_saved_stderr >= 0) dup2(g_saved_stderr, STDERR_FILENO);
    if (g_saved_stdout >= 0) close(g_saved_stdout);
    if (g_saved_stderr >= 0) close(g_saved_stderr);
    close(g_output_pipe[0]);
    g_output_pipe[0] = -1;
    g_saved_stdout = g_saved_stderr = -1;
    return -1;
  }
  close(g_output_pipe[1]);
  g_output_pipe[1] = -1;
  g_output_running = 1;
  return 0;
}

void telemetry_stop_output_capture(void) {
  if (!g_output_running) return;
  g_output_stop = 1;
  if (g_saved_stdout >= 0) dup2(g_saved_stdout, STDOUT_FILENO);
  if (g_saved_stderr >= 0) dup2(g_saved_stderr, STDERR_FILENO);
  if (g_saved_stdout >= 0) close(g_saved_stdout);
  if (g_saved_stderr >= 0) close(g_saved_stderr);
  g_saved_stdout = g_saved_stderr = -1;
  pthread_join(g_output_thread, NULL);
  close(g_output_pipe[0]);
  g_output_pipe[0] = -1;
  g_output_running = 0;
}

void telemetry_deinit(void) {
  if (!g_initialized) return;
  pthread_mutex_lock(&g_lock);
  g_stop = 1;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
  pthread_join(g_worker, NULL);
  pthread_mutex_lock(&g_lock);
  free(g_pending.data);
  memset(&g_pending, 0, sizeof(g_pending));
  g_initialized = 0;
  pthread_mutex_unlock(&g_lock);
}

int telemetry_get_config(TelemetryConfig *config) {
  if (!config) return -1;
  pthread_mutex_lock(&g_lock);
  *config = g_config;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_save_config(const TelemetryConfig *config, int clear_token) {
  TelemetryConfig current;
  TelemetryConfig next;

  if (!config) return -1;
  pthread_mutex_lock(&g_lock);
  current = g_config;
  pthread_mutex_unlock(&g_lock);
  next = *config;
  if (!config->token_present) next.token_present = current.token_present;
  if (!config->token_present) {
    copy_string(next.group_token, sizeof(next.group_token), current.group_token);
  }
  if (clear_token) {
    next.group_token[0] = '\0';
    next.token_present = 0;
  }
  if (!config_valid(&next) || config_set_int("telemetry_enabled", next.enabled) != 0 ||
      config_set_int("telemetry_interval_sec", next.interval_sec) != 0 ||
      config_set_text("telemetry_url", next.url) != 0 ||
      config_set_text("telemetry_group_token", next.group_token) != 0) {
    return -1;
  }
  pthread_mutex_lock(&g_lock);
  g_config = next;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_get_status(TelemetryStatus *status) {
  if (!status) return -1;
  pthread_mutex_lock(&g_lock);
  *status = g_status;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_test(const TelemetryConfig *config) {
  char auth[TELEMETRY_TOKEN_SIZE + 32];
  char device_header[TELEMETRY_DEVICE_ID_SIZE + 32];
  char response[32] = {0};
  char *argv[24];
  int argc = 0;
  int result;

  if (!config || !url_valid(config->url) || config->group_token[0] == '\0' ||
      g_device_id[0] == '\0') {
    return -1;
  }
  snprintf(auth, sizeof(auth), "Authorization: Bearer %s", config->group_token);
  snprintf(device_header, sizeof(device_header), "X-Device-ID: %s", g_device_id);
  argv[argc++] = (char *)"curl";
  argv[argc++] = (char *)"-sS";
  argv[argc++] = (char *)"-o";
  argv[argc++] = (char *)"/dev/null";
  argv[argc++] = (char *)"-w";
  argv[argc++] = (char *)"%{http_code}";
  argv[argc++] = (char *)"-I";
  argv[argc++] = (char *)"--max-time";
  argv[argc++] = (char *)"10";
  argv[argc++] = (char *)config->url;
  argv[argc++] = (char *)"-H";
  argv[argc++] = auth;
  argv[argc++] = (char *)"-H";
  argv[argc++] = device_header;
  argv[argc] = NULL;
  pthread_mutex_lock(&g_lock);
  g_sender_active = 1;
  pthread_mutex_unlock(&g_lock);
  result = run_command_argv(response, sizeof(response), argv);
  pthread_mutex_lock(&g_lock);
  g_sender_active = 0;
  pthread_mutex_unlock(&g_lock);
  return result == 0 && strcmp(response, "204") == 0 ? 0 : -1;
}

void telemetry_capture_line(const char *source, const char *line) {
  if (!source || !line) return;
  pthread_mutex_lock(&g_lock);
  if (g_initialized && g_config.enabled && !g_sender_active) {
    append_message_record(&g_pending, "log", source, line);
    if (g_pending.length >= TELEMETRY_FRAGMENT_SIZE) {
      g_flush_requested = 1;
      pthread_cond_signal(&g_ready);
    }
  }
  pthread_mutex_unlock(&g_lock);
}

void telemetry_capture_at(const char *command, const char *response, int success) {
  char message[4096];

  snprintf(message, sizeof(message), "command=%s result=%s response=%s",
           command ? command : "", success ? "ok" : "error",
           response ? response : "");
  telemetry_capture_line("at", message);
  pthread_mutex_lock(&g_lock);
  g_flush_requested = 1;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
}

void telemetry_capture_command(const char *command, const char *output,
                               int success) {
  char message[4096];

  snprintf(message, sizeof(message), "command=%s result=%s output=%s",
           command ? command : "", success ? "ok" : "error",
           output ? output : "");
  telemetry_capture_line("command", message);
}
