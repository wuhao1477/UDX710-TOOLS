#include "builtin_card_rules.h"

#include "strict_json.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const char *operator_id;
  const char *name;
  int priority;
} BuiltinCardOperator;

static const BuiltinCardOperator OPERATORS[] = {
    {"46000", "中国移动", 7},
    {"46001", "中国联通", 11},
    {"46003", "中国电信", 9},
    {"46011", "中国电信", 9},
};

int builtin_card_priority_for_operator(const char *operator_id) {
  if (!operator_id) {
    return -1;
  }
  for (size_t i = 0; i < sizeof(OPERATORS) / sizeof(OPERATORS[0]); i++) {
    if (strcmp(OPERATORS[i].operator_id, operator_id) == 0) {
      return OPERATORS[i].priority;
    }
  }
  return -1;
}

const char *builtin_card_operator_name(const char *operator_id) {
  if (!operator_id) {
    return "未知运营商";
  }
  for (size_t i = 0; i < sizeof(OPERATORS) / sizeof(OPERATORS[0]); i++) {
    if (strcmp(OPERATORS[i].operator_id, operator_id) == 0) {
      return OPERATORS[i].name;
    }
  }
  return "未知运营商";
}

int builtin_card_profile_supported(const char *oemname, const char *devtype,
                                   const char *devsubtype) {
  if (!oemname || !devtype || !devsubtype || strcmp(oemname, "SZ") != 0 ||
      strcmp(devsubtype, "SRB876") != 0) {
    return 0;
  }
  return strcmp(devtype, "541") == 0 || strcmp(devtype, "542") == 0;
}

int builtin_card_card_is_switchable(const char *iccid, int real_state) {
  return iccid && iccid[0] != '\0' && real_state == 2;
}

int builtin_card_parse_real_name_status(const char *json, int *status) {
  StrictJsonValue root;
  StrictJsonValue code_value;
  StrictJsonValue success_value;
  StrictJsonValue data_value;
  StrictJsonValue status_value;
  int code;
  int success;

  if (!json || !status) {
    return -1;
  }
  /* The service contract is: {code: 0, success: true, data: { ... }}.
   * Do not accept a status copied from an unrelated nested object or a
   * response with an empty/string data field. */
  if (strict_json_parse(json, &root) != 0 ||
      strict_json_object_get(root, "code", &code_value) != 0 ||
      strict_json_get_int(code_value, 0, 0, &code) != 0 || code != 0 ||
      strict_json_object_get(root, "success", &success_value) != 0 ||
      strict_json_get_bool(success_value, &success) != 0 || !success ||
      strict_json_object_get(root, "data", &data_value) != 0 ||
      data_value.type != STRICT_JSON_OBJECT ||
      strict_json_object_get(data_value, "realNameStatus", &status_value) !=
          0 ||
      strict_json_get_int(status_value, 0, 2, status) != 0) {
    return -1;
  }
  return 0;
}

int builtin_card_build_priority_query(int priority, char *out, size_t out_size) {
  int written;

  if (!out || out_size == 0 ||
      (priority != 7 && priority != 9 && priority != 11)) {
    return -1;
  }
  written = snprintf(
      out, out_size,
      "goformId=setDeviceConfig&configOption=setPriorityMnc&priorityMnc=%d&"
      "configValue=%d&save=1",
      priority, priority);
  return written >= 0 && (size_t)written < out_size ? 0 : -1;
}
