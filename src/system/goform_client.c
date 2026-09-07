#define _POSIX_C_SOURCE 200809L

#include "goform_client.h"

#include "builtin_card_rules.h"
#include "exec_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GOFORM_BASE "http://127.0.0.1:8443"
#define REAL_NAME_URL "http://fy168w.xinhaicapital.com/fysys/UI/getRealNameICCID.ajax"
#define CURL_BIN "/usr/bin/curl"

static unsigned long request_once = 1;

static int copy_value(char *out, size_t out_size, const char *start,
                      const char *end) {
  size_t length;

  if (!out || out_size == 0 || !start || !end || end < start) {
    return -1;
  }
  length = (size_t)(end - start);
  if (length >= out_size) {
    return -1;
  }
  memcpy(out, start, length);
  out[length] = '\0';
  return 0;
}

static const char *skip_space(const char *value) {
  while (value && isspace((unsigned char)*value)) {
    value++;
  }
  return value;
}

static int is_digits(const char *value) {
  if (!value || !*value) {
    return 0;
  }
  for (const char *p = value; *p; p++) {
    if (!isdigit((unsigned char)*p)) {
      return 0;
    }
  }
  return 1;
}

static int url_encode(const char *value, char *out, size_t out_size) {
  static const char hex[] = "0123456789ABCDEF";
  size_t used = 0;

  if (!value || !out || out_size == 0) return -1;
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    int safe = isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
    size_t need = safe ? 1 : 3;
    if (used + need >= out_size) return -1;
    if (safe) {
      out[used++] = (char)*p;
    } else {
      out[used++] = '%';
      out[used++] = hex[*p >> 4];
      out[used++] = hex[*p & 0x0F];
    }
  }
  out[used] = '\0';
  return 0;
}

static int build_headers(char *once, size_t once_size, char *timestamp,
                         size_t timestamp_size) {
  time_t now = time(NULL);
  int once_written;
  int timestamp_written;

  once_written = snprintf(once, once_size, "once: %lu", request_once++);
  timestamp_written = snprintf(timestamp, timestamp_size, "timestamp: %lld",
                               (long long)now);
  return once_written >= 0 && timestamp_written >= 0 &&
                 (size_t)once_written < once_size &&
                 (size_t)timestamp_written < timestamp_size
             ? 0
             : -1;
}

static int curl_get(const char *url, char *response, size_t response_size) {
  char once[48];
  char timestamp[64];
  if (build_headers(once, sizeof(once), timestamp, sizeof(timestamp)) != 0) {
    return -1;
  }
  return run_command(response, response_size, CURL_BIN, "-sS", "--max-time",
                     "5", "-X", "GET", "-H", "Host: 127.0.0.1:8443",
                     "-H", once, "-H", timestamp, "-H",
                     "content-type: application/json", url, NULL);
}

static int curl_post_real_name(const char *operator_id, const char *device_id,
                               char *response, size_t response_size) {
  if (!operator_id || !device_id || !response || response_size == 0) {
    return -1;
  }
  return run_command(
      response, response_size, CURL_BIN, "-sS", "--max-time", "8", "-X",
      "POST", REAL_NAME_URL, "--data-urlencode", operator_id,
      "--data-urlencode", device_id, "--data-urlencode", "locationStr=",
      "--data-urlencode", "keySimCardID=", "--data-urlencode", "cardType=",
      NULL);
}

static int goform_get_cmd(const char *cmd, char *response, size_t response_size) {
  char url[256];
  if (!cmd || !response || response_size == 0 ||
      (strcmp(cmd, "getDevInfo") != 0 &&
       strcmp(cmd, "getAllDeviceInfo") != 0 &&
       strcmp(cmd, "getSimInfo") != 0 &&
       strcmp(cmd, "getDevicePkgInfo") != 0)) {
    return -1;
  }
  if (snprintf(url, sizeof(url),
               GOFORM_BASE "/goform/goform_get_cmd_process?cmd=%s", cmd) >=
      (int)sizeof(url)) {
    return -1;
  }
  return curl_get(url, response, response_size);
}

int goform_get_device_info(char *response, size_t response_size) {
  return goform_get_cmd("getDevInfo", response, response_size);
}

int goform_get_all_device_info(char *response, size_t response_size) {
  return goform_get_cmd("getAllDeviceInfo", response, response_size);
}

int goform_get_sim_info(char *response, size_t response_size) {
  return goform_get_cmd("getSimInfo", response, response_size);
}

int goform_get_package_info(char *response, size_t response_size) {
  return goform_get_cmd("getDevicePkgInfo", response, response_size);
}

int goform_build_wifi_query(const char *ssid, const char *password, char *query,
                            size_t query_size) {
  char encoded_ssid[256];
  char encoded_password[384];
  int written;

  if (!ssid || !password || !*ssid || !*password || !query || query_size == 0 ||
      url_encode(ssid, encoded_ssid, sizeof(encoded_ssid)) != 0 ||
      url_encode(password, encoded_password, sizeof(encoded_password)) != 0) {
    return -1;
  }
  written = snprintf(query, query_size,
                     "goformId=setapinfo&ap_ssid=%s&ap_ssidpwd=%s&"
                     "admin=admin&pwd=admin",
                     encoded_ssid, encoded_password);
  return written >= 0 && (size_t)written < query_size ? 0 : -1;
}

int goform_set_wifi_info(const char *ssid, const char *password, char *response,
                         size_t response_size) {
  char query[768];
  char url[1024];

  if (goform_build_wifi_query(ssid, password, query, sizeof(query)) != 0 ||
      !response || response_size == 0 ||
      snprintf(url, sizeof(url), GOFORM_BASE
               "/goform/goform_set_cmd_process?%s", query) >=
          (int)sizeof(url)) {
    return -1;
  }
  return curl_get(url, response, response_size);
}

int goform_set_priority_mnc(int priority, char *response, size_t response_size) {
  char query[192];
  char url[512];

  if (builtin_card_build_priority_query(priority, query, sizeof(query)) != 0 ||
      !response || response_size == 0 ||
      snprintf(url, sizeof(url), GOFORM_BASE
               "/goform/goform_set_cmd_process?%s&admin=admin&pwd=admin",
               query) >= (int)sizeof(url)) {
    return -1;
  }
  return curl_get(url, response, response_size);
}

int goform_proxy_operation_allowed(const char *operation) {
  static const char *const allowed[] = {
      "getDevInfo",       "getAllDeviceInfo", "getSimInfo",
      "getDevicePkgInfo", "setapinfo",        "setPriorityMnc",
      NULL};
  if (!operation) return 0;
  for (int i = 0; allowed[i]; i++) {
    if (strcmp(operation, allowed[i]) == 0) return 1;
  }
  return 0;
}

int goform_check_real_name(const char *operator_id, const char *device_id,
                           char *response, size_t response_size) {
  char operator_param[64];
  char device_param[128];

  if (!is_digits(operator_id) || !is_digits(device_id) ||
      snprintf(operator_param, sizeof(operator_param), "operatorId=%s",
               operator_id) >= (int)sizeof(operator_param) ||
      snprintf(device_param, sizeof(device_param), "devid=%s", device_id) >=
          (int)sizeof(device_param)) {
    return -1;
  }
  return curl_post_real_name(operator_param, device_param, response,
                             response_size);
}

static const char *find_json_value(const char *json, const char *key) {
  char needle[96];
  const char *cursor;
  int written;

  if (!json || !key) {
    return NULL;
  }
  written = snprintf(needle, sizeof(needle), "\"%s\"", key);
  if (written < 0 || (size_t)written >= sizeof(needle)) {
    return NULL;
  }
  cursor = strstr(json, needle);
  if (!cursor) {
    return NULL;
  }
  cursor = strchr(cursor + strlen(needle), ':');
  return cursor ? skip_space(cursor + 1) : NULL;
}

int goform_json_string(const char *json, const char *key, char *out,
                       size_t out_size) {
  const char *value = find_json_value(json, key);
  const char *end;

  if (!value || *value != '"') {
    return -1;
  }
  value++;
  end = strchr(value, '"');
  return end ? copy_value(out, out_size, value, end) : -1;
}

int goform_json_int(const char *json, const char *key, int *out) {
  const char *value = find_json_value(json, key);
  char *end;
  long parsed;

  if (!value || !out) {
    return -1;
  }
  parsed = strtol(value, &end, 10);
  if (end == value) {
    return -1;
  }
  *out = (int)parsed;
  return 0;
}
