#ifndef STRICT_JSON_H
#define STRICT_JSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  STRICT_JSON_INVALID = 0,
  STRICT_JSON_NULL,
  STRICT_JSON_BOOL,
  STRICT_JSON_NUMBER,
  STRICT_JSON_STRING,
  STRICT_JSON_OBJECT,
  STRICT_JSON_ARRAY
} StrictJsonType;

typedef struct {
  const char *buf;
  size_t len;
  StrictJsonType type;
} StrictJsonValue;

int strict_json_parse(const char *json, StrictJsonValue *value);
int strict_json_object_get(StrictJsonValue object, const char *key,
                           StrictJsonValue *value);
int strict_json_get_int(StrictJsonValue value, int minimum, int maximum,
                        int *out);
int strict_json_get_int_or_string(StrictJsonValue value, int minimum,
                                  int maximum, int *out);
int strict_json_get_bool(StrictJsonValue value, int *out);
int strict_json_copy_string(StrictJsonValue value, char *out,
                            size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
