#define _POSIX_C_SOURCE 200809L

#include "telemetry.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int append_text(char *output, size_t output_size, size_t *used,
                       const char *text, size_t length) {
  if (!output || !used || *used > output_size || length > output_size - *used - 1) {
    return -1;
  }
  memcpy(output + *used, text, length);
  *used += length;
  output[*used] = '\0';
  return 0;
}

static int append_format(char *output, size_t output_size, size_t *used,
                         const char *format, ...) {
  va_list args;
  int needed;

  va_start(args, format);
  needed = vsnprintf(output + *used, output_size - *used, format, args);
  va_end(args);
  if (needed < 0 || (size_t)needed >= output_size - *used) return -1;
  *used += (size_t)needed;
  return 0;
}

static int append_json_string(char *output, size_t output_size, size_t *used,
                              const char *value) {
  const unsigned char *cursor = (const unsigned char *)(value ? value : "");

  if (append_text(output, output_size, used, "\"", 1) != 0) return -1;
  while (*cursor != '\0') {
    char escaped[7];
    size_t length = 1;

    switch (*cursor) {
      case '\\':
        escaped[0] = '\\';
        escaped[1] = '\\';
        length = 2;
        break;
      case '"':
        escaped[0] = '\\';
        escaped[1] = '"';
        length = 2;
        break;
      case '\n':
        escaped[0] = '\\';
        escaped[1] = 'n';
        length = 2;
        break;
      case '\r':
        escaped[0] = '\\';
        escaped[1] = 'r';
        length = 2;
        break;
      case '\t':
        escaped[0] = '\\';
        escaped[1] = 't';
        length = 2;
        break;
      default:
        if (*cursor < 0x20) {
          snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
          length = 6;
        } else {
          escaped[0] = (char)*cursor;
        }
        break;
    }
    if (append_text(output, output_size, used, escaped, length) != 0) return -1;
    cursor++;
  }
  return append_text(output, output_size, used, "\"", 1);
}

static int contains_ci(const char *value, size_t value_length,
                       const char *needle) {
  size_t needle_length = strlen(needle);
  size_t i;

  if (needle_length == 0 || value_length < needle_length) return 0;
  for (i = 0; i + needle_length <= value_length; i++) {
    if (strncasecmp(value + i, needle, needle_length) == 0) return 1;
  }
  return 0;
}

static int is_key_char(unsigned char value) {
  return isalnum(value) || value == '_' || value == '-';
}

static int sensitive_key(const char *key, size_t key_length) {
  static const char *const names[] = {
      "password", "passwd", "pwd",       "token",      "secret",
      "authorization", "cookie", "api_key", "access_key", "private_key",
      "body",
  };
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    if (contains_ci(key, key_length, names[i])) return 1;
  }
  return 0;
}

static int find_assignment(const char *input, size_t input_length, size_t from,
                           size_t *key_start, size_t *key_end,
                           size_t *separator) {
  size_t i;

  for (i = from; i < input_length; i++) {
    size_t end;
    size_t cursor;

    if (!isalpha((unsigned char)input[i]) && input[i] != '_') continue;
    end = i + 1;
    while (end < input_length && is_key_char((unsigned char)input[end])) end++;
    cursor = end;
    while (cursor < input_length &&
           isspace((unsigned char)input[cursor])) {
      cursor++;
    }
    if (cursor < input_length && (input[cursor] == '=' || input[cursor] == ':')) {
      *key_start = i;
      *key_end = end;
      *separator = cursor;
      return 0;
    }
  }
  return -1;
}

static size_t skip_value(const char *input, size_t input_length,
                         size_t value_start, size_t next_assignment) {
  size_t cursor = value_start;

  while (cursor < input_length && isspace((unsigned char)input[cursor])) cursor++;
  if (cursor < input_length && input[cursor] == '"') {
    cursor++;
    while (cursor < input_length) {
      if (input[cursor] == '\\') {
        cursor += cursor + 1 < input_length ? 2 : 1;
      } else if (input[cursor++] == '"') {
        break;
      }
    }
    return cursor;
  }
  if (next_assignment != input_length) return next_assignment;
  while (cursor < input_length && input[cursor] != '\n' && input[cursor] != '\r') {
    cursor++;
  }
  return cursor;
}

static int redact_auth_scheme(char *output, size_t output_size) {
  size_t length = strlen(output);
  size_t i;

  for (i = 0; i + 7 < length; i++) {
    size_t prefix_length = 0;
    if (strncasecmp(output + i, "Bearer ", 7) == 0) {
      prefix_length = 7;
    } else if (i + 6 < length && strncasecmp(output + i, "Basic ", 6) == 0) {
      prefix_length = 6;
    }
    if (prefix_length == 0) continue;

    size_t value_start = i + prefix_length;
    size_t value_end = value_start;
    size_t replacement_length = strlen("[REDACTED]");
    while (value_end < length && !isspace((unsigned char)output[value_end])) {
      value_end++;
    }
    if (value_end - value_start < replacement_length) replacement_length = 3;
    if (length - (value_end - value_start) + replacement_length + 1 > output_size) {
      return -1;
    }
    memmove(output + value_start + replacement_length, output + value_end,
            length - value_end + 1);
    memcpy(output + value_start,
           replacement_length == 3 ? "***" : "[REDACTED]", replacement_length);
    length = strlen(output);
    i = value_start + replacement_length;
  }
  return 0;
}

int telemetry_redact_text(const char *input, char *output, size_t output_size) {
  size_t input_length;
  size_t input_cursor = 0;
  size_t output_used = 0;

  if (!input || !output || output_size == 0) return -1;
  input_length = strlen(input);
  output[0] = '\0';
  while (input_cursor < input_length) {
    size_t key_start;
    size_t key_end;
    size_t separator;
    size_t next_key_start;
    size_t value_start;
    size_t value_end;

    if (find_assignment(input, input_length, input_cursor, &key_start, &key_end,
                        &separator) != 0) {
      if (append_text(output, output_size, &output_used, input + input_cursor,
                      input_length - input_cursor) != 0) {
        return -1;
      }
      break;
    }
    if (append_text(output, output_size, &output_used, input + input_cursor,
                    separator + 1 - input_cursor) != 0) {
      return -1;
    }
    value_start = separator + 1;
    next_key_start = input_length;
    size_t ignored_key_end;
    size_t ignored_separator;
    if (find_assignment(input, input_length, value_start, &next_key_start,
                        &ignored_key_end, &ignored_separator) != 0) {
      next_key_start = input_length;
    }
    value_end = skip_value(input, input_length, value_start, next_key_start);
    if (sensitive_key(input + key_start, key_end - key_start)) {
      if (append_text(output, output_size, &output_used, "[REDACTED]", 10) != 0) {
        return -1;
      }
      input_cursor = value_end;
    } else {
      if (append_text(output, output_size, &output_used, input + value_start,
                      value_end - value_start) != 0) {
        return -1;
      }
      input_cursor = value_end;
    }
  }
  return redact_auth_scheme(output, output_size);
}

int telemetry_build_record(char *output, size_t output_size,
                           const char *device_id, const char *boot_id,
                           unsigned long long sequence, const char *type,
                           const char *source, const char *payload_json) {
  char observed_at[32];
  struct tm utc;
  time_t now = time(NULL);
  size_t used = 0;

  if (!output || output_size == 0 || !device_id || !boot_id || !type ||
      !source || !payload_json || now == (time_t)-1 || gmtime_r(&now, &utc) == NULL ||
      strftime(observed_at, sizeof(observed_at), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
    return -1;
  }
  output[0] = '\0';
  if (append_text(output, output_size, &used,
                  "{\"schema_version\":1,\"device_id\":",
                  strlen("{\"schema_version\":1,\"device_id\":")) != 0 ||
      append_json_string(output, output_size, &used, device_id) != 0 ||
      append_text(output, output_size, &used, ",\"boot_id\":",
                  strlen(",\"boot_id\":")) != 0 ||
      append_json_string(output, output_size, &used, boot_id) != 0 ||
      append_format(output, output_size, &used, ",\"sequence\":%llu,\"observed_at\":",
                    sequence) != 0 ||
      append_json_string(output, output_size, &used, observed_at) != 0 ||
      append_text(output, output_size, &used, ",\"type\":",
                  strlen(",\"type\":")) != 0 ||
      append_json_string(output, output_size, &used, type) != 0 ||
      append_text(output, output_size, &used, ",\"source\":",
                  strlen(",\"source\":")) != 0 ||
      append_json_string(output, output_size, &used, source) != 0 ||
      append_text(output, output_size, &used, ",\"payload\":",
                  strlen(",\"payload\":")) != 0 ||
      append_text(output, output_size, &used, payload_json, strlen(payload_json)) != 0 ||
      append_text(output, output_size, &used, "}\n", 2) != 0) {
    output[0] = '\0';
    return -1;
  }
  return 0;
}
