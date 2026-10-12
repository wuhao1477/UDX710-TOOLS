#define _POSIX_C_SOURCE 200809L

#include "builtin_cards.h"

#include "builtin_card_rules.h"
#include "goform_client.h"
#include "http_utils.h"
#include "json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICCID_CONFIG "/mnt/data/fy/config/iccidcfg"
#define GOFORM_RESPONSE_SIZE GOFORM_ASYNC_RESPONSE_SIZE
#define MAX_BUILTIN_CARDS 8

typedef struct {
  char slot[24];
  char iccid[32];
  char operator_id[16];
  int real_state;
} BuiltinCard;

typedef struct {
  char oemname[16];
  char devtype[16];
  char devsubtype[32];
  char device_id[64];
  int current_priority;
} DeviceIdentity;

static int copy_field(char *destination, size_t destination_size,
                      const char *source) {
  size_t length;

  if (!destination || destination_size == 0 || !source) {
    return -1;
  }
  length = strlen(source);
  if (length >= destination_size) {
    return -1;
  }
  memcpy(destination, source, length + 1);
  return 0;
}

static int parse_config_line(char *line, char *key, size_t key_size,
                             char *value, size_t value_size) {
  char *first;
  char *last;
  size_t length;

  if (!line || !key || !value || key_size == 0 || value_size == 0) {
    return -1;
  }
  first = strchr(line, '|');
  if (!first) {
    return -1;
  }
  length = (size_t)(first - line);
  if (length == 0 || length >= key_size) {
    return -1;
  }
  memcpy(key, line, length);
  key[length] = '\0';
  last = strrchr(first + 1, '|');
  if (!last) {
    return -1;
  }
  last++;
  last[strcspn(last, "\r\n")] = '\0';
  if (strlen(last) >= value_size) {
    return -1;
  }
  snprintf(value, value_size, "%s", last);
  return 0;
}

static BuiltinCard *find_card(BuiltinCard *cards, size_t count,
                              const char *slot) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(cards[i].slot, slot) == 0) {
      return &cards[i];
    }
  }
  return NULL;
}

static int read_cards(BuiltinCard *cards, size_t limit, size_t *count_out) {
  FILE *file;
  char line[256];
  size_t count = 0;

  if (!cards || limit == 0 || !count_out) {
    return -1;
  }
  file = fopen(ICCID_CONFIG, "r");
  if (!file) {
    return -1;
  }
  while (count < limit && fgets(line, sizeof(line), file)) {
    char key[96];
    char value[96];
    const char *prefix = "cfg_iccid_";
    BuiltinCard *card;

    if (parse_config_line(line, key, sizeof(key), value, sizeof(value)) != 0 ||
        strncmp(key, prefix, strlen(prefix)) != 0 || value[0] == '\0') {
      continue;
    }
    card = find_card(cards, count, key + strlen(prefix));
    if (!card) {
      card = &cards[count++];
      memset(card, 0, sizeof(*card));
      if (copy_field(card->slot, sizeof(card->slot), key + strlen(prefix)) !=
          0) {
        count--;
        continue;
      }
    }
    if (copy_field(card->iccid, sizeof(card->iccid), value) != 0) {
      continue;
    }
  }
  fclose(file);

  file = fopen(ICCID_CONFIG, "r");
  if (!file) {
    return -1;
  }
  while (fgets(line, sizeof(line), file)) {
    char key[96];
    char value[96];
    const char *suffix;
    BuiltinCard *card;

    if (parse_config_line(line, key, sizeof(key), value, sizeof(value)) != 0) {
      continue;
    }
    if (strncmp(key, "cfg_isp_", 8) == 0) {
      suffix = key + 8;
      card = find_card(cards, count, suffix);
      if (card && copy_field(card->operator_id, sizeof(card->operator_id),
                             value) == 0) {
      }
    } else if (strncmp(key, "cfg_realstate_", 14) == 0) {
      suffix = key + 14;
      card = find_card(cards, count, suffix);
      if (card) {
        card->real_state = atoi(value);
      }
    }
  }
  fclose(file);
  *count_out = count;
  return 0;
}

static int read_identity(DeviceIdentity *identity) {
  char response[GOFORM_RESPONSE_SIZE];

  if (!identity || goform_get_device_info(response, sizeof(response)) != 0 ||
      goform_json_string(response, "oemname", identity->oemname,
                         sizeof(identity->oemname)) != 0 ||
      goform_json_string(response, "devtype", identity->devtype,
                         sizeof(identity->devtype)) != 0 ||
      goform_json_string(response, "devsubtype", identity->devsubtype,
                         sizeof(identity->devsubtype)) != 0 ||
      goform_json_string(response, "device", identity->device_id,
                         sizeof(identity->device_id)) != 0) {
    return -1;
  }
  identity->current_priority = 0;
  goform_json_int(response, "usemnc", &identity->current_priority);
  return 0;
}

static int ensure_supported(DeviceIdentity *identity) {
  return read_identity(identity) == 0 &&
         builtin_card_profile_supported(identity->oemname, identity->devtype,
                                        identity->devsubtype);
}

static void mask_iccid(const char *iccid, char *out, size_t out_size) {
  size_t length;

  if (!iccid || !out || out_size == 0) {
    return;
  }
  length = strlen(iccid);
  if (length < 8) {
    snprintf(out, out_size, "已配置");
    return;
  }
  snprintf(out, out_size, "%.*s****%s", 6, iccid, iccid + length - 4);
}

static void respond_unsupported(struct mg_connection *c) {
  HTTP_JSON(c, 409,
            "{\"Code\":1,\"Error\":\"当前设备不是已确认的 SZ50 设备\","
            "\"Data\":null}");
}

static void add_card_json(JsonBuilder *json, const BuiltinCard *card) {
  char masked[32];

  mask_iccid(card->iccid, masked, sizeof(masked));
  json_arr_obj_open(json);
  json_add_str(json, "slot", card->slot);
  json_add_str(json, "operatorId", card->operator_id);
  json_add_str(json, "operatorName",
               builtin_card_operator_name(card->operator_id));
  json_add_int(json, "realState", card->real_state);
  json_add_str(json, "iccid", masked);
  json_obj_close(json);
}

static int get_operator_from_body(struct mg_http_message *hm, char *operator_id,
                                  size_t operator_size) {
  char *value = mg_json_get_str(hm->body, "$.operatorId");
  int ok = value && strlen(value) < operator_size;

  if (ok) {
    snprintf(operator_id, operator_size, "%s", value);
  }
  free(value);
  return ok ? 0 : -1;
}

static int find_requested_card(const char *operator_id, BuiltinCard *cards,
                               size_t count, int require_ready) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(cards[i].operator_id, operator_id) == 0 &&
        cards[i].iccid[0] != '\0' &&
        (!require_ready ||
         builtin_card_card_is_switchable(cards[i].iccid,
                                          cards[i].real_state))) {
      return (int)i;
    }
  }
  return -1;
}

static int find_requested_card_any(const char *operator_id,
                                   BuiltinCard *cards, size_t count) {
  return find_requested_card(operator_id, cards, count, 0);
}

static int check_real_name(const DeviceIdentity *identity,
                           const char *operator_id, int *status) {
  char response[GOFORM_RESPONSE_SIZE];

  if (!identity || !operator_id || !status ||
      goform_check_real_name(operator_id, identity->device_id, response,
                             sizeof(response)) != 0 ||
      builtin_card_parse_real_name_status(response, status) != 0) {
    return -1;
  }
  return 0;
}

static int response_result_is_success(const char *response) {
  int result;

  if (!response || goform_json_int(response, "result", &result) != 0) {
    return -1;
  }
  return result == 0 ? 0 : -1;
}

/*
 * Return values for the shared switch operation:
 *   0  success
 *  -2  unsupported device profile
 *  -3  invalid or unavailable card
 *  -4  real-name service failure
 *  -5  card is not real-name verified
 *  -6  device goform rejected or could not be reached
 */
int builtin_card_switch_operator(const char *operator_id, char *response,
                                 size_t response_size, int *priority_out) {
  BuiltinCard cards[MAX_BUILTIN_CARDS];
  DeviceIdentity identity;
  size_t count = 0;
  int status;
  int priority;

  if (!operator_id || !response || response_size == 0 || !priority_out) {
    return -3;
  }
  if (!ensure_supported(&identity)) {
    return -2;
  }
  if (read_cards(cards, MAX_BUILTIN_CARDS, &count) != 0 ||
      find_requested_card(operator_id, cards, count, 1) < 0) {
    return -3;
  }
  priority = builtin_card_priority_for_operator(operator_id);
  if (priority < 0) {
    return -3;
  }
  if (check_real_name(&identity, operator_id, &status) != 0) {
    return -4;
  }
  if (status != 2) {
    return -5;
  }
  if (goform_set_priority_mnc(priority, response, response_size) != 0 ||
      response_result_is_success(response) != 0) {
    return -6;
  }
  *priority_out = priority;
  return 0;
}

typedef struct {
  char operator_id[16];
} BuiltinCardAsyncArg;

static int copy_async_response(char *response, size_t response_size,
                               char *owned_response) {
  size_t length;

  if (!response || response_size == 0 || !owned_response) {
    free(owned_response);
    return -1;
  }
  length = strlen(owned_response);
  if (length >= response_size) {
    free(owned_response);
    return -1;
  }
  memcpy(response, owned_response, length + 1);
  free(owned_response);
  return 0;
}

static int build_cards_response(const DeviceIdentity *identity,
                                BuiltinCard *cards, size_t count,
                                char *response, size_t response_size) {
  JsonBuilder *json;
  char *owned_response;

  if (!identity || !cards || !response || response_size == 0) return -1;
  json = json_new();
  if (!json) return -1;
  json_obj_open(json);
  json_add_int(json, "Code", 0);
  json_add_str(json, "Error", "");
  json_key_obj_open(json, "Data");
  json_add_bool(json, "supported", 1);
  json_add_int(json, "currentPriority", identity->current_priority);
  json_arr_open(json, "cards");
  for (size_t i = 0; i < count; i++) add_card_json(json, &cards[i]);
  json_arr_close(json);
  json_obj_close(json);
  json_obj_close(json);
  owned_response = json_finish(json);
  return copy_async_response(response, response_size, owned_response);
}

static int builtin_cards_list_worker(void *arg, char *response,
                                     size_t response_size) {
  BuiltinCard cards[MAX_BUILTIN_CARDS];
  DeviceIdentity identity;
  size_t count = 0;

  (void) arg;
  if (!ensure_supported(&identity)) return -2;
  if (read_cards(cards, MAX_BUILTIN_CARDS, &count) != 0) return -3;
  return build_cards_response(&identity, cards, count, response, response_size);
}

static void complete_builtin_cards_list(struct mg_connection *c, int result,
                                        const char *response, void *arg) {
  (void) arg;
  if (result == -2) {
    respond_unsupported(c);
  } else if (result == -3) {
    HTTP_ERROR(c, 503, "无法读取内置卡配置");
  } else if (result != 0) {
    HTTP_ERROR(c, 500, "响应内存分配失败");
  } else {
    HTTP_JSON(c, 200, response);
  }
}

static int builtin_card_real_name_worker(void *arg, char *response,
                                         size_t response_size) {
  BuiltinCardAsyncArg *task = (BuiltinCardAsyncArg *) arg;
  BuiltinCard cards[MAX_BUILTIN_CARDS];
  DeviceIdentity identity;
  size_t count = 0;
  int status;
  JsonBuilder *json;
  char *owned_response;

  if (!task || !ensure_supported(&identity)) {
    return -2;
  }
  if (read_cards(cards, MAX_BUILTIN_CARDS, &count) != 0 ||
      builtin_card_priority_for_operator(task->operator_id) < 0 ||
      find_requested_card_any(task->operator_id, cards, count) < 0) {
    return -3;
  }
  if (check_real_name(&identity, task->operator_id, &status) != 0) return -4;
  json = json_new();
  if (!json) return -1;
  json_obj_open(json);
  json_add_int(json, "Code", status == 2 ? 0 : 1);
  json_add_str(json, "Error", status == 2 ? "" : "该内置卡尚未实名");
  json_key_obj_open(json, "Data");
  json_add_str(json, "operatorId", task->operator_id);
  json_add_int(json, "realNameStatus", status);
  json_add_bool(json, "verified", status == 2);
  json_obj_close(json);
  json_obj_close(json);
  owned_response = json_finish(json);
  return copy_async_response(response, response_size, owned_response);
}

static void complete_builtin_card_real_name(struct mg_connection *c,
                                            int result, const char *response,
                                            void *arg) {
  (void) arg;
  if (result == -2) {
    respond_unsupported(c);
  } else if (result == -3) {
    HTTP_ERROR(c, 400, "内置卡不存在或运营商参数无效");
  } else if (result == -4) {
    HTTP_ERROR(c, 502, "实名状态查询失败");
  } else if (result != 0) {
    HTTP_ERROR(c, 500, "响应内存分配失败");
  } else {
    HTTP_JSON(c, 200, response);
  }
}

static int builtin_card_switch_worker(void *arg, char *response,
                                      size_t response_size) {
  BuiltinCardAsyncArg *task = (BuiltinCardAsyncArg *) arg;
  int priority;

  return task ? builtin_card_switch_operator(task->operator_id, response,
                                             response_size, &priority)
              : -3;
}

static void complete_builtin_card_switch(struct mg_connection *c, int result,
                                         const char *response, void *arg) {
  BuiltinCardAsyncArg *task = (BuiltinCardAsyncArg *) arg;
  int priority;
  JsonBuilder *json;

  if (result == -2) {
    respond_unsupported(c);
    return;
  }
  if (result == -3) {
    HTTP_ERROR(c, 400, "内置卡不存在、未就绪或运营商参数无效");
    return;
  }
  if (result == -4) {
    HTTP_ERROR(c, 502, "实名状态查询失败");
    return;
  }
  if (result == -5) {
    HTTP_ERROR(c, 403, "该内置卡尚未实名，禁止切换");
    return;
  }
  if (result != 0 || !task ||
      (priority = builtin_card_priority_for_operator(task->operator_id)) < 0) {
    HTTP_ERROR(c, 502, "设备运营商切换失败");
    return;
  }
  json = json_new();
  if (!json) {
    HTTP_ERROR(c, 500, "响应内存分配失败");
    return;
  }
  json_obj_open(json);
  json_add_int(json, "Code", 0);
  json_add_str(json, "Error", "");
  json_key_obj_open(json, "Data");
  json_add_str(json, "operatorId", task->operator_id);
  json_add_str(json, "operatorName",
               builtin_card_operator_name(task->operator_id));
  json_add_int(json, "priorityMnc", priority);
  json_add_int(json, "result", 0);
  json_add_str(json, "status", "requested");
  json_obj_close(json);
  json_obj_close(json);
  HTTP_OK_FREE(c, json_finish(json));
  (void) response;
}

void handle_builtin_cards(struct mg_connection *c, struct mg_http_message *hm) {
  HTTP_CHECK_GET(c, hm);
  if (goform_async_submit(c, builtin_cards_list_worker, NULL, NULL,
                          complete_builtin_cards_list) != 0) {
    HTTP_ERROR(c, 503, "内置卡服务暂不可用");
  }
}

void handle_builtin_card_real_name(struct mg_connection *c,
                                   struct mg_http_message *hm) {
  char operator_id[16];
  BuiltinCardAsyncArg *task;

  HTTP_CHECK_POST(c, hm);
  if (get_operator_from_body(hm, operator_id, sizeof(operator_id)) != 0) {
    HTTP_ERROR(c, 400, "内置卡不存在或运营商参数无效");
    return;
  }
  task = (BuiltinCardAsyncArg *)calloc(1, sizeof(*task));
  if (!task) {
    HTTP_ERROR(c, 500, "响应内存分配失败");
    return;
  }
  snprintf(task->operator_id, sizeof(task->operator_id), "%s", operator_id);
  if (goform_async_submit(c, builtin_card_real_name_worker, task, free,
                          complete_builtin_card_real_name) != 0) {
    HTTP_ERROR(c, 503, "实名服务暂不可用");
  }
}

void handle_builtin_card_switch(struct mg_connection *c,
                                struct mg_http_message *hm) {
  char operator_id[16];
  BuiltinCardAsyncArg *task;

  HTTP_CHECK_POST(c, hm);
  if (get_operator_from_body(hm, operator_id, sizeof(operator_id)) != 0) {
    HTTP_ERROR(c, 400, "运营商参数无效");
    return;
  }
  task = (BuiltinCardAsyncArg *)calloc(1, sizeof(*task));
  if (!task) {
    HTTP_ERROR(c, 500, "响应内存分配失败");
    return;
  }
  snprintf(task->operator_id, sizeof(task->operator_id), "%s", operator_id);
  if (goform_async_submit(c, builtin_card_switch_worker, task, free,
                          complete_builtin_card_switch) != 0) {
    HTTP_ERROR(c, 503, "内置卡服务暂不可用");
  }
}
