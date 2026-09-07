#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "exec_utils.h"
#include "notification.h"

static void test_event_id_mapping(void) {
  NotificationEventType type;

  assert(strcmp(notification_event_id(NOTIFICATION_EVENT_SMS_RECEIVED),
                "sms_received") == 0);
  assert(notification_event_from_id("login", &type) == 0);
  assert(type == NOTIFICATION_EVENT_LOGIN);
  assert(notification_event_from_id("not-supported", &type) != 0);
}

static void test_rule_validation(void) {
  NotificationRule rule = {
      .id = 0,
      .type = NOTIFICATION_EVENT_SIGNAL_LOW,
      .enabled = 1,
      .threshold = 20,
      .threshold_unit = "percent",
      .cooldown_sec = 300,
  };

  assert(notification_rule_validate(&rule) == 0);
  snprintf(rule.threshold_unit, sizeof(rule.threshold_unit), "state");
  assert(notification_rule_validate(&rule) != 0);
  rule.type = NOTIFICATION_EVENT_LOGIN;
  assert(notification_rule_validate(&rule) == 0);
}

static void test_template_expansion(void) {
  NotificationEvent event = {
      .type = NOTIFICATION_EVENT_SMS_RECEIVED,
      .title = "新短信",
      .message = "设备收到短信",
      .sender = "13800000000",
      .content = "测试内容",
      .timestamp = 1,
  };
  char output[512];

  assert(notification_expand_template(
             "#{event}|#{title}|#{message}|#{sender}|#{content}|#{time}",
             &event, output, sizeof(output)) == 0);
  assert(strstr(output, "sms_received") != NULL);
  assert(strstr(output, "新短信") != NULL);
  assert(strstr(output, "13800000000") != NULL);
  assert(strstr(output, "测试内容") != NULL);
}

static void test_threshold_and_cooldown(void) {
  assert(notification_threshold_crossed(21, 19, 20, 0) == 1);
  assert(notification_threshold_crossed(19, 21, 20, 0) == 0);
  assert(notification_threshold_crossed(79, 80, 80, 1) == 1);
  assert(notification_threshold_crossed(80, 81, 80, 1) == 0);
  assert(notification_cooldown_elapsed(1000, 700, 300) == 1);
  assert(notification_cooldown_elapsed(1000, 701, 300) == 0);
}

static void test_exec_argv_without_shell(void) {
  char output[32];
  char *argv[] = {"printf", "%s", "safe-value", NULL};

  assert(run_command_argv(output, sizeof(output), argv) == 0);
  assert(strcmp(output, "safe-value") == 0);
}

static void test_snapshot_comparison(void) {
  const char before[][18] = {"00:11:22:33:44:55"};
  const char same[][18] = {"00:11:22:33:44:55"};
  const char after[][18] = {"00:11:22:33:44:66"};

  assert(notification_value_changed("wlan1", "wlan1") == 0);
  assert(notification_value_changed("wlan1", "wlan0") == 1);
  assert(notification_mac_set_changed(before, 1, same, 1) == 0);
  assert(notification_mac_set_changed(before, 1, after, 1) == 1);
}

int main(void) {
  test_event_id_mapping();
  test_rule_validation();
  test_template_expansion();
  test_threshold_and_cooldown();
  test_exec_argv_without_shell();
  test_snapshot_comparison();
  puts("notification tests passed");
  return 0;
}
