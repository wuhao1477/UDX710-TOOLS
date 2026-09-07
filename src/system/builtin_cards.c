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
#define GOFORM_RESPONSE_SIZE 32768

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

static const char *operator_name(const char *operator_id) {
  if (!operator_id) {
    return "未知运营商";
  }
  if (strcmp(operator_id, "46000") == 0) {
    return "中国移动";
  }
  if (strcmp(operator_id, "46001") == 0) {
    return "中国联通";
  }
  if (strcmp(operator_id, "46003") == 0 ||
      strcmp(operator_id, "46011") == 0) {
    return "中国电信";
  }
  return "未知运营商";
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
      if (count >= limit) {
        break;
      }
      card = &cards[count++];
      memset(card, 0, sizeof(*card));
      snprintf(card->slot, sizeof(card->slot), "%s", key + strlen(prefix));
    }
    snprintf(card->iccid, sizeof(card->iccid), "%s", value);
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
      if (card) {
        snprintf(card->operator_id, sizeof(card->operator_id), "%s", value);
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
  if (length < 8 || out_size < 16) {
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
  json_add_str(json, "operatorName", operator_name(card->operator_id));
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
                               size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(cards[i].operator_id, operator_id) == 0 &&
        cards[i].iccid[0] != '\0') {
      return (int)i;
    }
  }
  return -1;
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

void handle_builtin_cards(struct mg_connection *c, struct mg_http_message *hm) {
  BuiltinCard cards[8];
  DeviceIdentity identity;
  size_t count = 0;
  JsonBuilder *json;

  HTTP_CHECK_GET(c, hm);
  if (!ensure_supported(&identity)) {
    respond_unsupported(c);
    return;
  }
  if (read_cards(cards, 8, &count) != 0) {
    HTTP_ERROR(c, 503, "无法读取内置卡配置");
    return;
  }
  json = json_new();
  json_obj_open(json);
  json_add_int(json, "Code", 0);
  json_add_str(json, "Error", "");
  json_key_obj_open(json, "Data");
  json_add_bool(json, "supported", 1);
  json_add_int(json, "currentPriority", identity.current_priority);
  json_arr_open(json, "cards");
  for (size_t i = 0; i < count; i++) {
    add_card_json(json, &cards[i]);
  }
  json_arr_close(json);
  json_obj_close(json);
  json_obj_close(json);
  HTTP_OK_FREE(c, json_finish(json));
}

void handle_builtin_card_real_name(struct mg_connection *c,
                                   struct mg_http_message *hm) {
  BuiltinCard cards[8];
  DeviceIdentity identity;
  char operator_id[16];
  size_t count = 0;
  int status;
  JsonBuilder *json;

  HTTP_CHECK_POST(c, hm);
  if (!ensure_supported(&identity)) {
    respond_unsupported(c);
    return;
  }
  if (get_operator_from_body(hm, operator_id, sizeof(operator_id)) != 0 ||
      read_cards(cards, 8, &count) != 0 ||
      find_requested_card(operator_id, cards, count) < 0) {
    HTTP_ERROR(c, 400, "内置卡不存在或运营商参数无效");
    return;
  }
  if (check_real_name(&identity, operator_id, &status) != 0) {
    HTTP_ERROR(c, 502, "实名状态查询失败");
    return;
  }
  json = json_new();
  json_obj_open(json);
  json_add_int(json, "Code", status == 2 ? 0 : 1);
  json_add_str(json, "Error", status == 2 ? "" : "该内置卡尚未实名");
  json_key_obj_open(json, "Data");
  json_add_str(json, "operatorId", operator_id);
  json_add_int(json, "realNameStatus", status);
  json_add_bool(json, "verified", status == 2);
  json_obj_close(json);
  json_obj_close(json);
  HTTP_OK_FREE(c, json_finish(json));
}

void handle_builtin_card_switch(struct mg_connection *c,
                                struct mg_http_message *hm) {
  BuiltinCard cards[8];
  DeviceIdentity identity;
  char operator_id[16];
  char priority_query[192];
  size_t count = 0;
  int status;
  int priority;
  JsonBuilder *json;

  HTTP_CHECK_POST(c, hm);
  if (!ensure_supported(&identity)) {
    respond_unsupported(c);
    return;
  }
  if (get_operator_from_body(hm, operator_id, sizeof(operator_id)) != 0 ||
      read_cards(cards, 8, &count) != 0 ||
      find_requested_card(operator_id, cards, count) < 0) {
    HTTP_ERROR(c, 400, "内置卡不存在或运营商参数无效");
    return;
  }
  priority = builtin_card_priority_for_operator(operator_id);
  if (priority < 0) {
    HTTP_ERROR(c, 400, "当前运营商没有已确认的 SZ50 映射");
    return;
  }
  if (check_real_name(&identity, operator_id, &status) != 0) {
    HTTP_ERROR(c, 502, "实名状态查询失败");
    return;
  }
  if (status != 2) {
    HTTP_ERROR(c, 403, "该内置卡尚未实名，禁止切换");
    return;
  }
  if (builtin_card_build_priority_query(priority, priority_query,
                                        sizeof(priority_query)) != 0) {
    HTTP_ERROR(c, 502, "goform 运营商切换失败");
    return;
  }
  json = json_new();
  json_obj_open(json);
  json_add_int(json, "Code", 0);
  json_add_str(json, "Error", "");
  json_key_obj_open(json, "Data");
  json_add_str(json, "operatorId", operator_id);
  json_add_str(json, "operatorName", operator_name(operator_id));
  json_add_int(json, "priorityMnc", priority);
  json_add_str(json, "status", "approved");
  json_add_str(json, "goformQuery", priority_query);
  json_add_str(json, "message", "实名校验通过，可调用设备原生 goform 切换");
  json_obj_close(json);
  json_obj_close(json);
  HTTP_OK_FREE(c, json_finish(json));
}
