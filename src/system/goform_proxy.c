#include "goform_proxy.h"

#include "goform_client.h"
#include "http_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GOFORM_PROXY_RESPONSE_SIZE 32768

static int proxy_get(const char *operation, char *response, size_t size) {
  if (strcmp(operation, "getDevInfo") == 0)
    return goform_get_device_info(response, size);
  if (strcmp(operation, "getAllDeviceInfo") == 0)
    return goform_get_all_device_info(response, size);
  if (strcmp(operation, "getSimInfo") == 0)
    return goform_get_sim_info(response, size);
  if (strcmp(operation, "getDevicePkgInfo") == 0)
    return goform_get_package_info(response, size);
  return -1;
}

void handle_goform_proxy(struct mg_connection *c, struct mg_http_message *hm) {
  char operation[32] = {0};
  char *operation_value;
  char *ssid = NULL;
  char *password = NULL;
  char response[GOFORM_PROXY_RESPONSE_SIZE];
  int priority = 0;
  double number;
  int result;

  HTTP_CHECK_POST(c, hm);
  operation_value = mg_json_get_str(hm->body, "$.operation");
  if (!operation_value || strlen(operation_value) >= sizeof(operation)) {
    free(operation_value);
    HTTP_ERROR(c, 400, "operation 参数无效");
    return;
  }
  snprintf(operation, sizeof(operation), "%s", operation_value);
  free(operation_value);
  if (!goform_proxy_operation_allowed(operation)) {
    HTTP_ERROR(c, 400, "goform 操作未允许");
    return;
  }

  result = -1;
  if (operation[0] == 'g') {
    result = proxy_get(operation, response, sizeof(response));
  } else if (strcmp(operation, "setapinfo") == 0) {
    ssid = mg_json_get_str(hm->body, "$.ssid");
    password = mg_json_get_str(hm->body, "$.password");
    if (ssid && password) {
      result = goform_set_wifi_info(ssid, password, response, sizeof(response));
    }
  } else if (strcmp(operation, "setPriorityMnc") == 0 &&
             mg_json_get_num(hm->body, "$.priorityMnc", &number)) {
    priority = (int)number;
    result = goform_set_priority_mnc(priority, response, sizeof(response));
  }
  free(ssid);
  free(password);

  if (result != 0) {
    HTTP_ERROR(c, 502, "goform 请求失败");
    return;
  }
  HTTP_JSON(c, 200, response);
}
