#define _POSIX_C_SOURCE 200809L

#include "notification.h"

#include <stdio.h>
#include <string.h>

static const char *const EVENT_IDS[NOTIFICATION_EVENT_COUNT] = {
    "sms_received", "login",          "rj45_changed",
    "network_interface_changed",      "ip_changed",
    "device_changed",                 "operator_changed",
    "signal_low",                     "traffic_threshold",
    "network_type_changed",            "device_status"};

const char *notification_event_id(NotificationEventType type) {
  if (type < 0 || type >= NOTIFICATION_EVENT_COUNT) {
    return "unknown";
  }
  return EVENT_IDS[type];
}

int notification_event_from_id(const char *id, NotificationEventType *type) {
  if (!id || !type) {
    return -1;
  }
  for (int i = 0; i < NOTIFICATION_EVENT_COUNT; i++) {
    if (strcmp(id, EVENT_IDS[i]) == 0) {
      *type = (NotificationEventType)i;
      return 0;
    }
  }
  return -1;
}

int notification_rules_defaults(NotificationRule *rules, size_t count) {
  if (!rules || count < NOTIFICATION_EVENT_COUNT) {
    return -1;
  }
  memset(rules, 0, sizeof(*rules) * count);
  for (int i = 0; i < NOTIFICATION_EVENT_COUNT; i++) {
    rules[i].cooldown_sec = 300;
    snprintf(rules[i].threshold_unit, sizeof(rules[i].threshold_unit),
             "state");
  }
  rules[NOTIFICATION_EVENT_SMS_RECEIVED].enabled = 1;
  rules[NOTIFICATION_EVENT_SIGNAL_LOW].threshold = 20;
  snprintf(rules[NOTIFICATION_EVENT_SIGNAL_LOW].threshold_unit,
           sizeof(rules[NOTIFICATION_EVENT_SIGNAL_LOW].threshold_unit),
           "percent");
  rules[NOTIFICATION_EVENT_TRAFFIC_THRESHOLD].threshold = 80;
  snprintf(rules[NOTIFICATION_EVENT_TRAFFIC_THRESHOLD].threshold_unit,
           sizeof(rules[NOTIFICATION_EVENT_TRAFFIC_THRESHOLD].threshold_unit),
           "percent");
  return NOTIFICATION_EVENT_COUNT;
}

int notification_threshold_crossed(double previous, double current,
                                   double threshold, int rising) {
  if (rising) {
    return previous < threshold && current >= threshold;
  }
  return previous >= threshold && current < threshold;
}

int notification_cooldown_elapsed(time_t now, time_t last, int cooldown_sec) {
  if (last <= 0 || cooldown_sec <= 0 || now < last) {
    return 1;
  }
  return difftime(now, last) >= cooldown_sec;
}

int notification_value_changed(const char *previous, const char *current) {
  return strcmp(previous ? previous : "", current ? current : "") != 0;
}

static int mac_exists(const char macs[][18], size_t count, const char *mac) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(macs[i], mac) == 0) return 1;
  }
  return 0;
}

int notification_mac_set_changed(const char previous[][18], size_t previous_count,
                                 const char current[][18], size_t current_count) {
  if (!previous || !current || previous_count != current_count) return 1;
  for (size_t i = 0; i < previous_count; i++) {
    if (!mac_exists(current, current_count, previous[i])) return 1;
  }
  return 0;
}

static int append_text(char *output, size_t output_size, size_t *used,
                       const char *value, size_t value_len) {
  if (!output || !used || !value || *used > output_size - 1 ||
      value_len > output_size - 1 - *used) {
    return -1;
  }
  memcpy(output + *used, value, value_len);
  *used += value_len;
  output[*used] = '\0';
  return 0;
}

static const char *template_value(const char *token,
                                  const NotificationEvent *event,
                                  char *time_text, size_t time_size) {
  if (strcmp(token, "event") == 0) {
    return notification_event_id(event->type);
  }
  if (strcmp(token, "title") == 0) {
    return event->title;
  }
  if (strcmp(token, "message") == 0) {
    return event->message;
  }
  if (strcmp(token, "sender") == 0) {
    return event->sender;
  }
  if (strcmp(token, "content") == 0) {
    return event->content;
  }
  if (strcmp(token, "time") == 0) {
    struct tm local_time;
    if (localtime_r(&event->timestamp, &local_time) == NULL ||
        strftime(time_text, time_size, "%Y-%m-%d %H:%M:%S", &local_time) ==
            0) {
      time_text[0] = '\0';
    }
    return time_text;
  }
  return NULL;
}

int notification_expand_template(const char *template_text,
                                 const NotificationEvent *event, char *output,
                                 size_t output_size) {
  size_t used = 0;
  const char *cursor;
  char time_text[32];

  if (!template_text || !event || !output || output_size == 0) {
    return -1;
  }
  output[0] = '\0';
  cursor = template_text;
  while (*cursor) {
    const char *start = strstr(cursor, "#{");
    if (!start) {
      return append_text(output, output_size, &used, cursor,
                         strlen(cursor));
    }
    if (append_text(output, output_size, &used, cursor,
                    (size_t)(start - cursor)) != 0) {
      return -1;
    }
    const char *end = strchr(start + 2, '}');
    if (!end || end - start - 2 >= 32) {
      return append_text(output, output_size, &used, start,
                         strlen(start));
    }
    char token[32];
    size_t token_len = (size_t)(end - start - 2);
    memcpy(token, start + 2, token_len);
    token[token_len] = '\0';
    const char *value = template_value(token, event, time_text,
                                       sizeof(time_text));
    if (!value) {
      if (append_text(output, output_size, &used, start,
                      (size_t)(end - start + 1)) != 0) {
        return -1;
      }
    } else if (append_text(output, output_size, &used, value,
                           strlen(value)) != 0) {
      return -1;
    }
    cursor = end + 1;
  }
  return 0;
}
