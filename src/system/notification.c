#define _POSIX_C_SOURCE 200809L

#include "notification.h"

#include "database.h"
#include "exec_utils.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define NOTIFICATION_QUEUE_SIZE 8
#define NOTIFICATION_LOG_SIZE 30
#define CURL_BIN "/usr/bin/curl"

typedef struct {
  NotificationEvent event;
  int force;
} NotificationJob;

static NotificationWebhookConfig g_webhook;
static NotificationRule g_rules[NOTIFICATION_MAX_RULES];
static time_t g_last_sent[NOTIFICATION_MAX_RULES];
static int g_rule_count;
static NotificationLog g_logs[NOTIFICATION_LOG_SIZE];
static int g_log_count;
static int g_log_id;
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_queue_ready = PTHREAD_COND_INITIALIZER;
static NotificationJob g_queue[NOTIFICATION_QUEUE_SIZE];
static size_t g_queue_head;
static size_t g_queue_tail;
static size_t g_queue_count;
static pthread_t g_worker;
static int g_worker_running;
static int g_worker_stop;

static void copy_string(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static void add_log(const NotificationEvent *event, const char *request,
                    const char *response, int result) {
  pthread_mutex_lock(&g_log_lock);
  int index = g_log_count % NOTIFICATION_LOG_SIZE;
  NotificationLog *log = &g_logs[index];
  memset(log, 0, sizeof(*log));
  log->id = ++g_log_id;
  copy_string(log->event_id, sizeof(log->event_id),
              notification_event_id(event ? event->type : NOTIFICATION_EVENT_COUNT));
  copy_string(log->title, sizeof(log->title), event ? event->title : "");
  copy_string(log->sender, sizeof(log->sender), event ? event->sender : "");
  copy_string(log->request, sizeof(log->request), request);
  copy_string(log->response, sizeof(log->response), response);
  log->result = result;
  log->created_at = time(NULL);
  if (g_log_count < NOTIFICATION_LOG_SIZE) g_log_count++;
  pthread_mutex_unlock(&g_log_lock);
}

static int enqueue_job(const NotificationEvent *event, int force) {
  int ret = -1;
  pthread_mutex_lock(&g_state_lock);
  if (!g_worker_running || g_queue_count >= NOTIFICATION_QUEUE_SIZE) {
    pthread_mutex_unlock(&g_state_lock);
    return -1;
  }
  g_queue[g_queue_tail].event = *event;
  g_queue[g_queue_tail].force = force;
  g_queue_tail = (g_queue_tail + 1) % NOTIFICATION_QUEUE_SIZE;
  g_queue_count++;
  ret = 0;
  pthread_cond_signal(&g_queue_ready);
  pthread_mutex_unlock(&g_state_lock);
  return ret;
}

static void trim_header(char *line) {
  char *start = line;
  char *end;
  while (*start == ' ' || *start == '\t' || *start == '\r') start++;
  if (start != line) memmove(line, start, strlen(start) + 1);
  end = line + strlen(line);
  while (end > line && (end[-1] == ' ' || end[-1] == '\t' ||
                        end[-1] == '\r')) {
    *--end = '\0';
  }
}

static int send_webhook(const NotificationJob *job) {
  NotificationWebhookConfig config;
  char body[4096];
  char headers[sizeof(config.headers)];
  char response[1024];
  char *argv[32];
  int argc = 0;
  int has_content_type = 0;
  char *save = NULL;
  char *line;

  pthread_mutex_lock(&g_state_lock);
  config = g_webhook;
  pthread_mutex_unlock(&g_state_lock);
  if (!job->force && (!config.enabled || config.url[0] == '\0')) return -1;
  if ((strncmp(config.url, "http://", 7) != 0 &&
       strncmp(config.url, "https://", 8) != 0) ||
      notification_expand_template(config.body, &job->event, body,
                                   sizeof(body)) != 0) {
    add_log(&job->event, "", "Webhook URL或模板无效", 0);
    return -1;
  }

  argv[argc++] = (char *)CURL_BIN;
  argv[argc++] = (char *)"-sS";
  argv[argc++] = (char *)"--max-time";
  argv[argc++] = (char *)"10";
  argv[argc++] = (char *)"-f";
  argv[argc++] = (char *)"-X";
  argv[argc++] = (char *)"POST";
  argv[argc++] = config.url;
  copy_string(headers, sizeof(headers), config.headers);
  line = strtok_r(headers, "\n", &save);
  while (line && argc < 28) {
    trim_header(line);
    if (*line && strchr(line, ':')) {
      argv[argc++] = (char *)"-H";
      argv[argc++] = line;
      if (strncasecmp(line, "content-type:", 13) == 0) {
        has_content_type = 1;
      }
    }
    line = strtok_r(NULL, "\n", &save);
  }
  if (!has_content_type && argc < 30) {
    argv[argc++] = (char *)"-H";
    argv[argc++] = (char *)"Content-Type: application/json";
  }
  argv[argc++] = (char *)"--data-raw";
  argv[argc++] = body;
  argv[argc] = NULL;

  response[0] = '\0';
  int result = run_command_argv(response, sizeof(response), argv) == 0;
  add_log(&job->event, body, response, result);
  return result ? 0 : -1;
}

static void *notification_worker(void *arg) {
  (void)arg;
  while (1) {
    NotificationJob job;
    pthread_mutex_lock(&g_state_lock);
    while (g_queue_count == 0 && !g_worker_stop) {
      pthread_cond_wait(&g_queue_ready, &g_state_lock);
    }
    if (g_worker_stop) {
      g_queue_count = 0;
      pthread_mutex_unlock(&g_state_lock);
      return NULL;
    }
    job = g_queue[g_queue_head];
    g_queue_head = (g_queue_head + 1) % NOTIFICATION_QUEUE_SIZE;
    g_queue_count--;
    pthread_mutex_unlock(&g_state_lock);
    send_webhook(&job);
  }
}

static void load_webhook(void) {
  char output[4096];
  char *fields[5] = {0};
  char *cursor;
  char *start;
  int count = 0;
  memset(&g_webhook, 0, sizeof(g_webhook));
  copy_string(g_webhook.platform, sizeof(g_webhook.platform), "pushplus");
  if (db_query_rows("SELECT enabled, platform, url, body, headers FROM webhook_config WHERE id = 1;",
                    "|", output, sizeof(output)) != 0 || output[0] == '\0') {
    return;
  }
  start = output;
  for (cursor = output; *cursor && count < 5; cursor++) {
    if (*cursor == '|') {
      *cursor = '\0';
      fields[count++] = start;
      start = cursor + 1;
    }
  }
  if (count < 5) fields[count++] = start;
  if (count < 5) return;
  g_webhook.enabled = atoi(fields[0]);
  copy_string(g_webhook.platform, sizeof(g_webhook.platform), fields[1]);
  copy_string(g_webhook.url, sizeof(g_webhook.url), fields[2]);
  copy_string(g_webhook.body, sizeof(g_webhook.body), fields[3]);
  copy_string(g_webhook.headers, sizeof(g_webhook.headers), fields[4]);
  db_unescape_string(g_webhook.url);
  db_unescape_string(g_webhook.body);
  db_unescape_string(g_webhook.headers);
}

static void migrate_legacy_rules(void) {
  if (config_get_int("notification_entries_migrated", 0)) return;
  db_execute_safe(
      "INSERT OR IGNORE INTO notification_entries "
      "(event_type, enabled, threshold, threshold_unit, cooldown_sec) "
      "SELECT event_type, enabled, threshold, threshold_unit, cooldown_sec "
      "FROM notification_rules WHERE enabled = 1;");
  config_set_int("notification_entries_migrated", 1);
}

static void load_rules(void) {
  char output[8192];
  g_rule_count = 0;
  memset(g_rules, 0, sizeof(g_rules));
  if (db_query_rows(
          "SELECT id, event_type, enabled, threshold, threshold_unit, "
          "cooldown_sec FROM notification_entries ORDER BY id;",
          "|", output, sizeof(output)) != 0 || output[0] == '\0') {
    return;
  }
  char *save = NULL;
  char *row = strtok_r(output, "\n", &save);
  while (row && g_rule_count < NOTIFICATION_MAX_RULES) {
    char *fields[6] = {0};
    char *field_save = NULL;
    char *field = strtok_r(row, "|", &field_save);
    int count = 0;
    while (field && count < 6) {
      fields[count++] = field;
      field = strtok_r(NULL, "|", &field_save);
    }
    NotificationRule rule = {0};
    if (count == 6 && fields[1] &&
        notification_event_from_id(fields[1], &rule.type) == 0) {
      rule.id = atoi(fields[0]);
      rule.enabled = atoi(fields[2]) ? 1 : 0;
      rule.threshold = atof(fields[3]);
      copy_string(rule.threshold_unit, sizeof(rule.threshold_unit), fields[4]);
      rule.cooldown_sec = atoi(fields[5]);
      if (notification_rule_validate(&rule) == 0) {
        g_rules[g_rule_count++] = rule;
      }
    }
    row = strtok_r(NULL, "\n", &save);
  }
}

int notification_init(const char *db_path) {
  if (g_worker_running) return 0;
  if (db_init(db_path) != 0) return -1;
  migrate_legacy_rules();
  pthread_mutex_lock(&g_state_lock);
  load_webhook();
  load_rules();
  memset(g_last_sent, 0, sizeof(g_last_sent));
  g_queue_head = g_queue_tail = g_queue_count = 0;
  g_worker_stop = 0;
  g_worker_running = 1;
  pthread_mutex_unlock(&g_state_lock);
  if (pthread_create(&g_worker, NULL, notification_worker, NULL) != 0) {
    pthread_mutex_lock(&g_state_lock);
    g_worker_running = 0;
    pthread_mutex_unlock(&g_state_lock);
    return -1;
  }
  return 0;
}

void notification_deinit(void) {
  if (!g_worker_running) return;
  pthread_mutex_lock(&g_state_lock);
  g_worker_stop = 1;
  pthread_cond_signal(&g_queue_ready);
  pthread_mutex_unlock(&g_state_lock);
  pthread_join(g_worker, NULL);
  pthread_mutex_lock(&g_state_lock);
  g_worker_running = 0;
  pthread_mutex_unlock(&g_state_lock);
}

int notification_emit(const NotificationEvent *event) {
  int rule_index = -1;
  time_t now;
  if (!event || event->type < 0 || event->type >= NOTIFICATION_EVENT_COUNT)
    return -1;
  now = time(NULL);
  pthread_mutex_lock(&g_state_lock);
  for (int i = 0; i < g_rule_count; i++) {
    if (g_rules[i].type == event->type && g_rules[i].enabled &&
        notification_cooldown_elapsed(now, g_last_sent[i],
                                      g_rules[i].cooldown_sec)) {
      rule_index = i;
      break;
    }
  }
  if (rule_index < 0 || g_webhook.url[0] == '\0' || !g_webhook.enabled) {
    pthread_mutex_unlock(&g_state_lock);
    return 0;
  }
  pthread_mutex_unlock(&g_state_lock);
  if (enqueue_job(event, 0) != 0) return -1;
  pthread_mutex_lock(&g_state_lock);
  g_last_sent[rule_index] = now;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

int notification_get_webhook_config(NotificationWebhookConfig *config) {
  if (!config) return -1;
  pthread_mutex_lock(&g_state_lock);
  *config = g_webhook;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

int notification_save_webhook_config(
    const NotificationWebhookConfig *config) {
  char platform[128], url[1024], body[4096], headers[1024], sql[8192];
  if (!config || config->enabled < 0 || config->enabled > 1 ||
      (config->url[0] && strncmp(config->url, "http://", 7) != 0 &&
       strncmp(config->url, "https://", 8) != 0)) {
    return -1;
  }
  db_escape_string(config->platform, platform, sizeof(platform));
  db_escape_string(config->url, url, sizeof(url));
  db_escape_string(config->body, body, sizeof(body));
  db_escape_string(config->headers, headers, sizeof(headers));
  if (snprintf(sql, sizeof(sql),
               "INSERT OR REPLACE INTO webhook_config "
               "(id, enabled, platform, url, body, headers) VALUES "
               "(1, %d, '%s', '%s', '%s', '%s');",
               config->enabled, platform, url, body, headers) >=
      (int)sizeof(sql) || db_execute_safe(sql) != 0) {
    return -1;
  }
  pthread_mutex_lock(&g_state_lock);
  g_webhook = *config;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

static int find_rule_index_locked(int id) {
  for (int i = 0; i < g_rule_count; i++) {
    if (g_rules[i].id == id) return i;
  }
  return -1;
}

static int find_event_index_locked(NotificationEventType type) {
  for (int i = 0; i < g_rule_count; i++) {
    if (g_rules[i].type == type) return i;
  }
  return -1;
}

int notification_get_rules(NotificationRule *rules, size_t capacity) {
  if (!rules) return -1;
  pthread_mutex_lock(&g_state_lock);
  if (capacity < (size_t)g_rule_count) {
    pthread_mutex_unlock(&g_state_lock);
    return -1;
  }
  memcpy(rules, g_rules, sizeof(NotificationRule) * (size_t)g_rule_count);
  int count = g_rule_count;
  pthread_mutex_unlock(&g_state_lock);
  return count;
}

int notification_add_rule(NotificationRule *rule) {
  char sql[512];
  int id;
  if (!rule || notification_rule_validate(rule) != 0 ||
      rule->enabled != 1) {
    return -1;
  }
  pthread_mutex_lock(&g_state_lock);
  int duplicate = find_event_index_locked(rule->type) >= 0;
  int full = g_rule_count >= NOTIFICATION_MAX_RULES;
  pthread_mutex_unlock(&g_state_lock);
  if (duplicate || full ||
      snprintf(sql, sizeof(sql),
               "INSERT INTO notification_entries "
               "(event_type, enabled, threshold, threshold_unit, cooldown_sec) "
               "VALUES ('%s', 1, %.3f, '%s', %d);",
               notification_event_id(rule->type), rule->threshold,
               rule->threshold_unit, rule->cooldown_sec) >= (int)sizeof(sql) ||
      db_execute_safe(sql) != 0) {
    return -1;
  }
  char id_sql[256];
  snprintf(id_sql, sizeof(id_sql),
           "SELECT id FROM notification_entries WHERE event_type='%s';",
           notification_event_id(rule->type));
  id = db_query_int(id_sql, 0);
  if (id <= 0) return -1;
  rule->id = id;
  pthread_mutex_lock(&g_state_lock);
  if (g_rule_count >= NOTIFICATION_MAX_RULES) {
    pthread_mutex_unlock(&g_state_lock);
    return -1;
  }
  g_rules[g_rule_count] = *rule;
  g_last_sent[g_rule_count] = 0;
  g_rule_count++;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

int notification_update_rule(const NotificationRule *rule) {
  char sql[512];
  if (!rule || rule->id <= 0 || notification_rule_validate(rule) != 0 ||
      rule->enabled != 1) {
    return -1;
  }
  pthread_mutex_lock(&g_state_lock);
  int index = find_rule_index_locked(rule->id);
  int duplicate = find_event_index_locked(rule->type);
  if (duplicate >= 0 && duplicate != index) index = -1;
  pthread_mutex_unlock(&g_state_lock);
  if (index < 0 ||
      snprintf(sql, sizeof(sql),
               "UPDATE notification_entries SET event_type='%s', enabled=1, "
               "threshold=%.3f, threshold_unit='%s', cooldown_sec=%d "
               "WHERE id=%d;",
               notification_event_id(rule->type), rule->threshold,
               rule->threshold_unit, rule->cooldown_sec, rule->id) >=
          (int)sizeof(sql) ||
      db_execute_safe(sql) != 0) {
    return -1;
  }
  pthread_mutex_lock(&g_state_lock);
  g_rules[index] = *rule;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

int notification_delete_rule(int id) {
  char sql[128];
  pthread_mutex_lock(&g_state_lock);
  int index = find_rule_index_locked(id);
  pthread_mutex_unlock(&g_state_lock);
  if (index < 0 || snprintf(sql, sizeof(sql),
                             "DELETE FROM notification_entries WHERE id=%d;",
                             id) >= (int)sizeof(sql) ||
      db_execute_safe(sql) != 0) {
    return -1;
  }
  pthread_mutex_lock(&g_state_lock);
  for (int i = index; i + 1 < g_rule_count; i++) {
    g_rules[i] = g_rules[i + 1];
    g_last_sent[i] = g_last_sent[i + 1];
  }
  g_rule_count--;
  memset(&g_rules[g_rule_count], 0, sizeof(g_rules[g_rule_count]));
  g_last_sent[g_rule_count] = 0;
  pthread_mutex_unlock(&g_state_lock);
  return 0;
}

int notification_test_webhook(void) {
  NotificationJob job = {0};
  pthread_mutex_lock(&g_state_lock);
  if (g_webhook.url[0] == '\0' || !g_worker_running ||
      g_queue_count >= NOTIFICATION_QUEUE_SIZE) {
    pthread_mutex_unlock(&g_state_lock);
    return -1;
  }
  pthread_mutex_unlock(&g_state_lock);
  job.force = 1;
  job.event.type = NOTIFICATION_EVENT_SMS_RECEIVED;
  copy_string(job.event.title, sizeof(job.event.title), "通知测试");
  copy_string(job.event.message, sizeof(job.event.message), "通知管理测试消息");
  copy_string(job.event.sender, sizeof(job.event.sender), "system");
  copy_string(job.event.content, sizeof(job.event.content), "这是一条测试通知");
  job.event.timestamp = time(NULL);
  return enqueue_job(&job.event, 1);
}

static void json_escape(char *out, size_t out_size, const char *value) {
  size_t used = 0;
  if (!out || out_size == 0) return;
  for (const unsigned char *p = (const unsigned char *)(value ? value : "");
       *p && used + 2 < out_size; p++) {
    if (*p == '"' || *p == '\\') {
      out[used++] = '\\';
    } else if (*p == '\n') {
      out[used++] = '\\';
      out[used++] = 'n';
      continue;
    } else if (*p == '\r') {
      out[used++] = '\\';
      out[used++] = 'r';
      continue;
    }
    out[used++] = (char)*p;
  }
  out[used] = '\0';
}

int notification_get_logs(char *json_output, size_t size, int max_count) {
  size_t used = 0;
  int count;
  if (!json_output || size < 3) return -1;
  if (max_count <= 0 || max_count > NOTIFICATION_LOG_SIZE)
    max_count = NOTIFICATION_LOG_SIZE;
  json_output[used++] = '[';
  pthread_mutex_lock(&g_log_lock);
  count = g_log_count < max_count ? g_log_count : max_count;
  for (int i = 0; i < count; i++) {
    int index = (g_log_count - 1 - i) % NOTIFICATION_LOG_SIZE;
    char event_id[64], title[256], sender[128], request[4096], response[2048];
    int written;
    if (index < 0) index += NOTIFICATION_LOG_SIZE;
    json_escape(event_id, sizeof(event_id), g_logs[index].event_id);
    json_escape(title, sizeof(title), g_logs[index].title);
    json_escape(sender, sizeof(sender), g_logs[index].sender);
    json_escape(request, sizeof(request), g_logs[index].request);
    json_escape(response, sizeof(response), g_logs[index].response);
    written = snprintf(json_output + used, size - used,
                       "%s{\"id\":%d,\"event\":\"%s\","
                       "\"title\":\"%s\",\"sender\":\"%s\","
                       "\"request\":\"%s\","
                       "\"response\":\"%s\",\"result\":%d,"
                       "\"created_at\":%ld}",
                       i ? "," : "", g_logs[index].id, event_id, title,
                       sender, request, response, g_logs[index].result,
                       (long)g_logs[index].created_at);
    if (written < 0 || (size_t)written >= size - used) {
      pthread_mutex_unlock(&g_log_lock);
      return -1;
    }
    used += (size_t)written;
  }
  pthread_mutex_unlock(&g_log_lock);
  if (used + 2 > size) return -1;
  json_output[used++] = ']';
  json_output[used] = '\0';
  return 0;
}
