#include "builtin_card_rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

int builtin_card_parse_real_name_status(const char *json, int *status) {
  const char *key;
  const char *cursor;
  char *end;
  long value;

  if (!json || !status) {
    return -1;
  }
  key = strstr(json, "\"realNameStatus\"");
  if (!key) {
    return -1;
  }
  cursor = strchr(key + strlen("\"realNameStatus\""), ':');
  if (!cursor) {
    return -1;
  }
  cursor++;
  while (isspace((unsigned char)*cursor)) {
    cursor++;
  }
  if (*cursor == '\"') {
    cursor++;
  }
  value = strtol(cursor, &end, 10);
  if (end == cursor) {
    return -1;
  }
  *status = (int)value;
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
