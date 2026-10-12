#define _POSIX_C_SOURCE 200809L

#include "strict_json.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define STRICT_JSON_MAX_DEPTH 32

typedef struct {
  const char *cursor;
  const char *end;
} JsonParser;

static void skip_space(JsonParser *parser) {
  while (parser->cursor < parser->end &&
         (*parser->cursor == ' ' || *parser->cursor == '\t' ||
          *parser->cursor == '\r' || *parser->cursor == '\n')) {
    parser->cursor++;
  }
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int parse_string(JsonParser *parser) {
  if (parser->cursor >= parser->end || *parser->cursor != '"') return -1;
  parser->cursor++;
  while (parser->cursor < parser->end) {
    unsigned char ch = (unsigned char)*parser->cursor++;
    if (ch == '"') return 0;
    if (ch < 0x20) return -1;
    if (ch != '\\') continue;
    if (parser->cursor >= parser->end) return -1;
    ch = (unsigned char)*parser->cursor++;
    if (ch == '"' || ch == '\\' || ch == '/' || ch == 'b' || ch == 'f' ||
        ch == 'n' || ch == 'r' || ch == 't') {
      continue;
    }
    if (ch != 'u' || parser->end - parser->cursor < 4 ||
        hex_value(parser->cursor[0]) < 0 || hex_value(parser->cursor[1]) < 0 ||
        hex_value(parser->cursor[2]) < 0 || hex_value(parser->cursor[3]) < 0) {
      return -1;
    }
    parser->cursor += 4;
  }
  return -1;
}

static int parse_number(JsonParser *parser) {
  const char *cursor = parser->cursor;

  if (cursor < parser->end && *cursor == '-') cursor++;
  if (cursor >= parser->end) return -1;
  if (*cursor == '0') {
    cursor++;
    if (cursor < parser->end && *cursor >= '0' && *cursor <= '9') return -1;
  } else {
    if (*cursor < '1' || *cursor > '9') return -1;
    while (cursor < parser->end && *cursor >= '0' && *cursor <= '9') cursor++;
  }
  if (cursor < parser->end && *cursor == '.') {
    cursor++;
    if (cursor >= parser->end || *cursor < '0' || *cursor > '9') return -1;
    while (cursor < parser->end && *cursor >= '0' && *cursor <= '9') cursor++;
  }
  if (cursor < parser->end && (*cursor == 'e' || *cursor == 'E')) {
    cursor++;
    if (cursor < parser->end && (*cursor == '+' || *cursor == '-')) cursor++;
    if (cursor >= parser->end || *cursor < '0' || *cursor > '9') return -1;
    while (cursor < parser->end && *cursor >= '0' && *cursor <= '9') cursor++;
  }
  parser->cursor = cursor;
  return 0;
}

static int parse_value(JsonParser *parser, StrictJsonValue *value,
                       unsigned depth);

static int parse_object(JsonParser *parser, unsigned depth) {
  if (depth > STRICT_JSON_MAX_DEPTH || parser->cursor >= parser->end ||
      *parser->cursor != '{') {
    return -1;
  }
  parser->cursor++;
  skip_space(parser);
  if (parser->cursor < parser->end && *parser->cursor == '}') {
    parser->cursor++;
    return 0;
  }
  for (;;) {
    StrictJsonValue ignored;
    if (parse_string(parser) != 0) return -1;
    skip_space(parser);
    if (parser->cursor >= parser->end || *parser->cursor++ != ':') return -1;
    if (parse_value(parser, &ignored, depth + 1) != 0) return -1;
    skip_space(parser);
    if (parser->cursor >= parser->end) return -1;
    if (*parser->cursor == '}') {
      parser->cursor++;
      return 0;
    }
    if (*parser->cursor++ != ',') return -1;
    skip_space(parser);
  }
}

static int parse_array(JsonParser *parser, unsigned depth) {
  if (depth > STRICT_JSON_MAX_DEPTH || parser->cursor >= parser->end ||
      *parser->cursor != '[') {
    return -1;
  }
  parser->cursor++;
  skip_space(parser);
  if (parser->cursor < parser->end && *parser->cursor == ']') {
    parser->cursor++;
    return 0;
  }
  for (;;) {
    StrictJsonValue ignored;
    if (parse_value(parser, &ignored, depth + 1) != 0) return -1;
    skip_space(parser);
    if (parser->cursor >= parser->end) return -1;
    if (*parser->cursor == ']') {
      parser->cursor++;
      return 0;
    }
    if (*parser->cursor++ != ',') return -1;
    skip_space(parser);
  }
}

static int parse_value(JsonParser *parser, StrictJsonValue *value,
                       unsigned depth) {
  const char *start;

  skip_space(parser);
  if (parser->cursor >= parser->end || depth > STRICT_JSON_MAX_DEPTH) return -1;
  start = parser->cursor;
  switch (*parser->cursor) {
    case '"':
      if (parse_string(parser) != 0) return -1;
      value->type = STRICT_JSON_STRING;
      break;
    case '{':
      if (parse_object(parser, depth) != 0) return -1;
      value->type = STRICT_JSON_OBJECT;
      break;
    case '[':
      if (parse_array(parser, depth) != 0) return -1;
      value->type = STRICT_JSON_ARRAY;
      break;
    case 't':
      if (parser->end - parser->cursor < 4 ||
          memcmp(parser->cursor, "true", 4) != 0) return -1;
      parser->cursor += 4;
      value->type = STRICT_JSON_BOOL;
      break;
    case 'f':
      if (parser->end - parser->cursor < 5 ||
          memcmp(parser->cursor, "false", 5) != 0) return -1;
      parser->cursor += 5;
      value->type = STRICT_JSON_BOOL;
      break;
    case 'n':
      if (parser->end - parser->cursor < 4 ||
          memcmp(parser->cursor, "null", 4) != 0) return -1;
      parser->cursor += 4;
      value->type = STRICT_JSON_NULL;
      break;
    default:
      if (parse_number(parser) != 0) return -1;
      value->type = STRICT_JSON_NUMBER;
      break;
  }
  value->buf = start;
  value->len = (size_t)(parser->cursor - start);
  return 0;
}

int strict_json_parse(const char *json, StrictJsonValue *value) {
  JsonParser parser;

  if (!json || !value) return -1;
  parser.cursor = json;
  parser.end = json + strlen(json);
  if (parse_value(&parser, value, 0) != 0) return -1;
  skip_space(&parser);
  return parser.cursor == parser.end ? 0 : -1;
}

static int key_matches(StrictJsonValue key, const char *wanted) {
  size_t length;

  if (!wanted || key.type != STRICT_JSON_STRING || key.len < 2) return 0;
  length = strlen(wanted);
  return key.len == length + 2 && key.buf[0] == '"' &&
         key.buf[key.len - 1] == '"' &&
         memcmp(key.buf + 1, wanted, length) == 0;
}

int strict_json_object_get(StrictJsonValue object, const char *key,
                           StrictJsonValue *value) {
  JsonParser parser;
  int found = 0;

  if (object.type != STRICT_JSON_OBJECT || !object.buf || !key || !value ||
      object.len < 2) {
    return -1;
  }
  parser.cursor = object.buf;
  parser.end = object.buf + object.len;
  if (parser.cursor >= parser.end || *parser.cursor++ != '{') return -1;
  skip_space(&parser);
  if (parser.cursor < parser.end && *parser.cursor == '}') return -1;
  for (;;) {
    StrictJsonValue name;
    StrictJsonValue candidate;

    if (parse_value(&parser, &name, 0) != 0 ||
        name.type != STRICT_JSON_STRING) {
      return -1;
    }
    skip_space(&parser);
    if (parser.cursor >= parser.end || *parser.cursor++ != ':') return -1;
    if (parse_value(&parser, &candidate, 0) != 0) return -1;
    if (key_matches(name, key)) {
      if (found) return -1;
      *value = candidate;
      found = 1;
    }
    skip_space(&parser);
    if (parser.cursor >= parser.end) return -1;
    if (*parser.cursor == '}') {
      parser.cursor++;
      return found ? 0 : -1;
    }
    if (*parser.cursor++ != ',') return -1;
    skip_space(&parser);
  }
}

int strict_json_get_int(StrictJsonValue value, int minimum, int maximum,
                        int *out) {
  char number[64];
  char *end;
  long parsed;

  if (!out || value.type != STRICT_JSON_NUMBER || value.len == 0 ||
      value.len >= sizeof(number) || minimum > maximum) {
    return -1;
  }
  for (size_t i = 0; i < value.len; i++) {
    if ((value.buf[i] < '0' || value.buf[i] > '9') &&
        !(i == 0 && value.buf[i] == '-')) {
      return -1;
    }
  }
  memcpy(number, value.buf, value.len);
  number[value.len] = '\0';
  errno = 0;
  parsed = strtol(number, &end, 10);
  if (errno == ERANGE || end == number || *end != '\0' || parsed < minimum ||
      parsed > maximum) {
    return -1;
  }
  *out = (int)parsed;
  return 0;
}

int strict_json_get_int_or_string(StrictJsonValue value, int minimum,
                                  int maximum, int *out) {
  char number[64];
  StrictJsonValue number_value = value;

  if (value.type == STRICT_JSON_STRING) {
    if (value.len < 2 || value.len - 2 >= sizeof(number)) return -1;
    memcpy(number, value.buf + 1, value.len - 2);
    number[value.len - 2] = '\0';
    number_value.buf = number;
    number_value.len = value.len - 2;
    number_value.type = STRICT_JSON_NUMBER;
  }
  return strict_json_get_int(number_value, minimum, maximum, out);
}

int strict_json_get_bool(StrictJsonValue value, int *out) {
  if (!out || value.type != STRICT_JSON_BOOL) return -1;
  if (value.len == 4 && memcmp(value.buf, "true", 4) == 0) {
    *out = 1;
    return 0;
  }
  if (value.len == 5 && memcmp(value.buf, "false", 5) == 0) {
    *out = 0;
    return 0;
  }
  return -1;
}

int strict_json_copy_string(StrictJsonValue value, char *out,
                            size_t out_size) {
  size_t written = 0;

  if (!out || out_size == 0 || value.type != STRICT_JSON_STRING ||
      value.len < 2 || value.buf[0] != '"' ||
      value.buf[value.len - 1] != '"') {
    return -1;
  }
  for (size_t i = 1; i + 1 < value.len; i++) {
    unsigned char ch = (unsigned char)value.buf[i];
    if (ch == '\\') {
      if (++i + 1 >= value.len) return -1;
      ch = (unsigned char)value.buf[i];
      switch (ch) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'b': ch = '\b'; break;
        case 'f': ch = '\f'; break;
        case 'n': ch = '\n'; break;
        case 'r': ch = '\r'; break;
        case 't': ch = '\t'; break;
        case 'u':
          /* Device identity fields are ASCII.  Rejecting non-ASCII escapes
           * avoids silently corrupting UTF-16 code points in this small API. */
          return -1;
        default:
          return -1;
      }
    }
    if (written + 1 >= out_size) return -1;
    out[written++] = (char)ch;
  }
  out[written] = '\0';
  return 0;
}
