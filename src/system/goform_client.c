#define _POSIX_C_SOURCE 200809L

#include "goform_client.h"

#include "builtin_card_rules.h"
#include "exec_utils.h"
#include "strict_json.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BUILTIN_CARD_GOFORM_BASE
#define BUILTIN_CARD_GOFORM_BASE "http://127.0.0.1:8443"
#endif

#ifndef BUILTIN_CARD_REAL_NAME_URL
#define BUILTIN_CARD_REAL_NAME_URL \
  "https://fy168w.xinhaicapital.com/fysys/UI/getRealNameICCID.ajax"
#endif

#ifndef BUILTIN_CARD_CURL_BIN
#define BUILTIN_CARD_CURL_BIN "/usr/bin/curl"
#endif

static unsigned long request_once = 1;

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

  if (!url || !response || response_size == 0 ||
      build_headers(once, sizeof(once), timestamp, sizeof(timestamp)) != 0) {
    return -1;
  }
  return run_command(response, response_size, BUILTIN_CARD_CURL_BIN, "-sS",
                     "--fail", "--connect-timeout", "3", "--max-time", "8", "-X",
                     "GET", "-H", "Host: 127.0.0.1:8443", "-H", once,
                     "-H", timestamp, "-H", "content-type: application/json",
                     url, NULL);
}

static int curl_post_real_name(const char *operator_id, const char *device_id,
                               char *response, size_t response_size) {
  if (!operator_id || !device_id || !response || response_size == 0) {
    return -1;
  }
  return run_command(
      response, response_size, BUILTIN_CARD_CURL_BIN, "-sS", "--fail",
      "--connect-timeout",
      "5", "--max-time", "8", "-X", "POST", BUILTIN_CARD_REAL_NAME_URL,
      "--data-urlencode", operator_id, "--data-urlencode", device_id,
      "--data-urlencode", "locationStr=", "--data-urlencode",
      "keySimCardID=", "--data-urlencode", "cardType=", NULL);
}

static int goform_get_cmd(const char *cmd, char *response, size_t response_size) {
  char url[256];

  if (!cmd || !response || response_size == 0 ||
      (strcmp(cmd, "getDevInfo") != 0 &&
       strcmp(cmd, "getAllDeviceInfo") != 0 &&
       strcmp(cmd, "getSimInfo") != 0 &&
       strcmp(cmd, "getDevicePkgInfo") != 0) ||
      snprintf(url, sizeof(url), BUILTIN_CARD_GOFORM_BASE
               "/goform/goform_get_cmd_process?cmd=%s", cmd) >=
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

int goform_set_priority_mnc(int priority, char *response, size_t response_size) {
  char query[192];
  char url[512];

  if (builtin_card_build_priority_query(priority, query, sizeof(query)) != 0 ||
      !response || response_size == 0 ||
      snprintf(url, sizeof(url), BUILTIN_CARD_GOFORM_BASE
               "/goform/goform_set_cmd_process?%s&admin=admin&pwd=admin",
               query) >= (int)sizeof(url)) {
    return -1;
  }
  return curl_get(url, response, response_size);
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

static int json_get_field(const char *json, const char *key,
                          StrictJsonValue *value) {
  StrictJsonValue root;
  StrictJsonValue data;

  if (!json || !key || !value || strict_json_parse(json, &root) != 0) {
    return -1;
  }
  if (strict_json_object_get(root, key, value) == 0) return 0;
  if (strict_json_object_get(root, "data", &data) != 0 ||
      data.type != STRICT_JSON_OBJECT ||
      strict_json_object_get(data, key, value) != 0) {
    return -1;
  }
  return 0;
}

int goform_json_string(const char *json, const char *key, char *out,
                       size_t out_size) {
  StrictJsonValue value;

  return json_get_field(json, key, &value) == 0
             ? strict_json_copy_string(value, out, out_size)
             : -1;
}

int goform_json_int(const char *json, const char *key, int *out) {
  StrictJsonValue value;

  return out && json_get_field(json, key, &value) == 0
             ? strict_json_get_int_or_string(value, INT_MIN, INT_MAX, out)
             : -1;
}
