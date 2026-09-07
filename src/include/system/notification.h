#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stddef.h>
#include <time.h>

#define NOTIFICATION_MAX_RULES 16

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NOTIFICATION_EVENT_SMS_RECEIVED = 0,
  NOTIFICATION_EVENT_LOGIN,
  NOTIFICATION_EVENT_RJ45_CHANGED,
  NOTIFICATION_EVENT_NETWORK_INTERFACE_CHANGED,
  NOTIFICATION_EVENT_IP_CHANGED,
  NOTIFICATION_EVENT_DEVICE_CHANGED,
  NOTIFICATION_EVENT_OPERATOR_CHANGED,
  NOTIFICATION_EVENT_SIGNAL_LOW,
  NOTIFICATION_EVENT_TRAFFIC_THRESHOLD,
  NOTIFICATION_EVENT_NETWORK_TYPE_CHANGED,
  NOTIFICATION_EVENT_DEVICE_STATUS,
  NOTIFICATION_EVENT_COUNT
} NotificationEventType;

typedef struct {
  int id;
  NotificationEventType type;
  int enabled;
  double threshold;
  char threshold_unit[16];
  int cooldown_sec;
} NotificationRule;

typedef struct {
  NotificationEventType type;
  char title[128];
  char message[1024];
  char sender[64];
  char content[1024];
  time_t timestamp;
} NotificationEvent;

typedef struct {
  int enabled;
  char platform[32];
  char url[512];
  char body[2048];
  char headers[512];
} NotificationWebhookConfig;

typedef struct {
  int id;
  char event_id[32];
  char title[128];
  char sender[64];
  char request[2048];
  char response[1024];
  int result;
  time_t created_at;
} NotificationLog;

const char *notification_event_id(NotificationEventType type);
int notification_event_from_id(const char *id, NotificationEventType *type);
int notification_rule_validate(const NotificationRule *rule);
int notification_threshold_crossed(double previous, double current,
                                   double threshold, int rising);
int notification_cooldown_elapsed(time_t now, time_t last, int cooldown_sec);
int notification_value_changed(const char *previous, const char *current);
int notification_mac_set_changed(const char previous[][18], size_t previous_count,
                                 const char current[][18], size_t current_count);
int notification_expand_template(const char *template_text,
                                 const NotificationEvent *event, char *output,
                                 size_t output_size);

int notification_init(const char *db_path);
void notification_deinit(void);
int notification_emit(const NotificationEvent *event);
int notification_get_webhook_config(NotificationWebhookConfig *config);
int notification_save_webhook_config(
    const NotificationWebhookConfig *config);
int notification_test_webhook(void);
int notification_get_logs(char *json_output, size_t size, int max_count);
int notification_get_rules(NotificationRule *rules, size_t capacity);
int notification_add_rule(NotificationRule *rule);
int notification_update_rule(const NotificationRule *rule);
int notification_delete_rule(int id);
void notification_maintenance(void);

#ifdef __cplusplus
}
#endif

#endif /* NOTIFICATION_H */
