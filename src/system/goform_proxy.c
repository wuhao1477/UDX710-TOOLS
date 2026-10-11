#include "goform_proxy.h"

#include "builtin_cards.h"
#include "goform_client.h"
#include "http_utils.h"

#include <stdlib.h>
#include <string.h>

#define GOFORM_PROXY_RESPONSE_SIZE 32768

static int proxy_get(const char *operation, char *response, size_t size) {
  if (strcmp(operation, "getDevInfo") == 0) {
    return goform_get_device_info(response, size);
  }
  if (strcmp(operation, "getAllDeviceInfo") == 0) {
    return goform_get_all_device_info(response, size);
  }
  if (strcmp(operation, "getSimInfo") == 0) {
    return goform_get_sim_info(response, size);
  }
  if (strcmp(operation, "getDevicePkgInfo") == 0) {
    return goform_get_package_info(response, size);
  }
  return -1;
}

static int proxy_get_operation_allowed(const char *operation) {
  return strcmp(operation, "getDevInfo") == 0 ||
         strcmp(operation, "getAllDeviceInfo") == 0 ||
         strcmp(operation, "getSimInfo") == 0 ||
         strcmp(operation, "getDevicePkgInfo") == 0;
}

void handle_goform_proxy(struct mg_connection *c, struct mg_http_message *hm) {
  char *operation;
  char *operator_id = NULL;
  char response[GOFORM_PROXY_RESPONSE_SIZE];
  int priority;
  int result;

  HTTP_CHECK_POST(c, hm);
  operation = mg_json_get_str(hm->body, "$.operation");
  if (!operation) {
    HTTP_ERROR(c, 400, "operation 参数无效");
    return;
  }

  if (proxy_get_operation_allowed(operation)) {
    result = proxy_get(operation, response, sizeof(response));
    free(operation);
    if (result != 0) {
      HTTP_ERROR(c, 502, "goform 查询失败");
      return;
    }
    HTTP_JSON(c, 200, response);
    return;
  }

  /* The write operation is intentionally bound to operatorId.  A raw
   * priorityMnc is never accepted, so this endpoint cannot bypass the
   * real-name check in builtin_card_switch_operator(). */
  if (strcmp(operation, "setPriorityMnc") != 0) {
    free(operation);
    HTTP_ERROR(c, 400, "goform 操作未允许");
    return;
  }
  operator_id = mg_json_get_str(hm->body, "$.operatorId");
  free(operation);
  if (!operator_id) {
    HTTP_ERROR(c, 400, "setPriorityMnc 必须提供 operatorId");
    return;
  }
  result = builtin_card_switch_operator(operator_id, response, sizeof(response),
                                        &priority);
  free(operator_id);
  if (result == -2) {
    HTTP_ERROR(c, 409, "当前设备不支持内置卡运营商切换");
  } else if (result == -3) {
    HTTP_ERROR(c, 400, "内置卡不存在或运营商参数无效");
  } else if (result == -4) {
    HTTP_ERROR(c, 502, "实名状态查询失败");
  } else if (result == -5) {
    HTTP_ERROR(c, 403, "该内置卡尚未实名，禁止切换");
  } else if (result != 0) {
    HTTP_ERROR(c, 502, "设备运营商切换失败");
  } else {
    HTTP_JSON(c, 200, response);
  }
}
