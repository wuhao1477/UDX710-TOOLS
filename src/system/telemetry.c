#define _POSIX_C_SOURCE 200809L

#include "telemetry.h"

#include "database.h"
#include "exec_utils.h"
#include "sysinfo.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define TELEMETRY_DEFAULT_INTERVAL 60
#define TELEMETRY_MIN_INTERVAL 10
#define TELEMETRY_MAX_INTERVAL 3600
#define TELEMETRY_FRAGMENT_SIZE (1024U * 1024U)
#define TELEMETRY_DEVICE_ID_SIZE 128
#define TELEMETRY_BOOT_ID_SIZE 64

typedef struct {
  char *data;
  size_t length;
  size_t capacity;
} TelemetryBuffer;

static TelemetryConfig g_config;
static TelemetryStatus g_status;
static TelemetryBuffer g_pending;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ready = PTHREAD_COND_INITIALIZER;
static pthread_t g_worker;
static int g_initialized;
static int g_stop;
static int g_flush_requested;
static int g_sender_active;
static unsigned long long g_sequence;
static char g_device_id[TELEMETRY_DEVICE_ID_SIZE];
static char g_boot_id[TELEMETRY_BOOT_ID_SIZE];

static void copy_string(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0) return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static int config_get_text(const char *key, char *value, size_t value_size) {
  if (config_get(key, value, value_size) != 0) {
    value[0] = '\0';
    return -1;
  }
  db_unescape_string(value);
  return 0;
}

static int config_set_text(const char *key, const char *value) {
  char escaped[TELEMETRY_TOKEN_SIZE * 2 + TELEMETRY_URL_SIZE * 2];

  db_escape_string(value ? value : "", escaped, sizeof(escaped));
  return config_set(key, escaped);
}

static int url_valid(const char *url) {
  if (!url || url[0] == '\0') return 0;
  return strncmp(url, "http://", 7) == 0 ||
         strncmp(url, "https://", 8) == 0;
}

static int read_device_identity(void) {
  FILE *file;
  size_t length;

  memset(g_device_id, 0, sizeof(g_device_id));
  memset(g_boot_id, 0, sizeof(g_boot_id));
  if (get_serial(g_device_id, sizeof(g_device_id)) != 0 ||
      g_device_id[0] == '\0' || strcmp(g_device_id, "N/A") == 0) {
    snprintf(g_device_id, sizeof(g_device_id), "");
  }

  file = fopen("/proc/sys/kernel/random/boot_id", "r");
  if (file) {
    if (fgets(g_boot_id, sizeof(g_boot_id), file)) {
      length = strlen(g_boot_id);
      while (length > 0 && (g_boot_id[length - 1] == '\n' ||
                            g_boot_id[length - 1] == '\r')) {
        g_boot_id[--length] = '\0';
      }
    }
    fclose(file);
  }
  if (g_boot_id[0] == '\0') snprintf(g_boot_id, sizeof(g_boot_id), "unknown");
  return g_device_id[0] == '\0' ? -1 : 0;
}

static int config_valid(const TelemetryConfig *config) {
  if (!config || config->interval_sec < TELEMETRY_MIN_INTERVAL ||
      config->interval_sec > TELEMETRY_MAX_INTERVAL ||
      (config->url[0] != '\0' && !url_valid(config->url))) {
    return 0;
  }
  if (config->enabled &&
      (config->url[0] == '\0' || config->group_token[0] == '\0' ||
       g_device_id[0] == '\0')) {
    return 0;
  }
  return 1;
}

static void load_config(void) {
  char value[TELEMETRY_URL_SIZE];

  memset(&g_config, 0, sizeof(g_config));
  g_config.interval_sec = config_get_int("telemetry_interval_sec",
                                         TELEMETRY_DEFAULT_INTERVAL);
  if (g_config.interval_sec < TELEMETRY_MIN_INTERVAL ||
      g_config.interval_sec > TELEMETRY_MAX_INTERVAL) {
    g_config.interval_sec = TELEMETRY_DEFAULT_INTERVAL;
  }
  g_config.enabled = config_get_int("telemetry_enabled", 0) ? 1 : 0;
  if (config_get_text("telemetry_url", value, sizeof(value)) == 0) {
    copy_string(g_config.url, sizeof(g_config.url), value);
  }
  if (config_get_text("telemetry_group_token", value, sizeof(value)) == 0) {
    copy_string(g_config.group_token, sizeof(g_config.group_token), value);
    g_config.token_present = g_config.group_token[0] != '\0';
  }
  if (!config_valid(&g_config)) g_config.enabled = 0;
}

static int buffer_append(TelemetryBuffer *buffer, const char *data, size_t length) {
  size_t required;
  size_t capacity;
  char *grown;

  if (!buffer || !data || length == 0) return 0;
  required = buffer->length + length + 1;
  if (required <= buffer->capacity) {
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 0;
  }
  capacity = buffer->capacity ? buffer->capacity : TELEMETRY_FRAGMENT_SIZE;
  while (capacity < required) capacity *= 2;
  grown = (char *)realloc(buffer->data, capacity);
  if (!grown) return -1;
  buffer->data = grown;
  buffer->capacity = capacity;
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 0;
}

static char *json_quote(const char *value) {
  const unsigned char *cursor = (const unsigned char *)(value ? value : "");
  size_t required = 3;
  char *output;
  size_t used = 0;

  while (*cursor) {
    required += (*cursor == '\\' || *cursor == '"')
                    ? 2
                    : (*cursor < 0x20 ? 6 : 1);
    cursor++;
  }
  output = (char *)malloc(required + 1);
  if (!output) return NULL;
  output[used++] = '"';
  cursor = (const unsigned char *)(value ? value : "");
  while (*cursor) {
    if (*cursor == '\\' || *cursor == '"') output[used++] = '\\';
    if (*cursor == '\n') {
      output[used++] = '\\';
      output[used++] = 'n';
    } else if (*cursor == '\r') {
      output[used++] = '\\';
      output[used++] = 'r';
    } else if (*cursor == '\t') {
      output[used++] = '\\';
      output[used++] = 't';
    } else if (*cursor < 0x20) {
      used += (size_t)snprintf(output + used, required + 1 - used, "\\u%04x",
                               *cursor);
    } else {
      output[used++] = (char)*cursor;
    }
    cursor++;
  }
  output[used++] = '"';
  output[used] = '\0';
  return output;
}

static int append_message_record(TelemetryBuffer *batch, const char *type,
                                 const char *source, const char *message) {
  char *redacted = NULL;
  char *quoted = NULL;
  char *payload = NULL;
  char *record = NULL;
  size_t message_length = strlen(message ? message : "");
  size_t payload_size = message_length * 2 + 64;
  size_t record_size = payload_size * 2 + 512;
  int result = -1;

  redacted = (char *)malloc(message_length * 2 + 64);
  payload = (char *)malloc(payload_size);
  record = (char *)malloc(record_size);
  if (!redacted || !payload || !record ||
      telemetry_redact_text(message ? message : "", redacted,
                            message_length * 2 + 64) != 0) {
    goto cleanup;
  }
  quoted = json_quote(redacted);
  if (!quoted || snprintf(payload, payload_size, "{\"message\":%s}", quoted) < 0 ||
      telemetry_build_record(record, record_size, g_device_id, g_boot_id,
                             g_sequence++, type, source, payload) != 0 ||
      buffer_append(batch, record, strlen(record)) != 0) {
    goto cleanup;
  }
  result = 0;

cleanup:
  free(redacted);
  free(quoted);
  free(payload);
  free(record);
  return result;
}

static int write_all(int fd, const char *data, size_t length) {
  while (length > 0) {
    ssize_t written = write(fd, data, length);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return -1;
    data += written;
    length -= (size_t)written;
  }
  return 0;
}

static int send_gzip_fragment(const TelemetryConfig *config, const char *data,
                              size_t length) {
  int gzip_input[2] = {-1, -1};
  int gzip_output[2] = {-1, -1};
  int response_pipe[2] = {-1, -1};
  pid_t gzip_pid = -1;
  pid_t curl_pid = -1;
  int gzip_status = 0;
  int curl_status = 0;
  char auth[TELEMETRY_TOKEN_SIZE + 32];
  char device_header[TELEMETRY_DEVICE_ID_SIZE + 32];
  char response[512];
  char *curl_argv[24];
  int argc = 0;
  int result = -1;

  if (!config || !data || length == 0 || pipe(gzip_input) != 0 ||
      pipe(gzip_output) != 0 || pipe(response_pipe) != 0) {
    goto cleanup;
  }
  snprintf(auth, sizeof(auth), "Authorization: Bearer %s", config->group_token);
  snprintf(device_header, sizeof(device_header), "X-Device-ID: %s", g_device_id);
  curl_argv[argc++] = (char *)"curl";
  curl_argv[argc++] = (char *)"-sS";
  curl_argv[argc++] = (char *)"--max-time";
  curl_argv[argc++] = (char *)"30";
  curl_argv[argc++] = (char *)"-f";
  curl_argv[argc++] = (char *)"-X";
  curl_argv[argc++] = (char *)"POST";
  curl_argv[argc++] = (char *)config->url;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = auth;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = device_header;
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = (char *)"Content-Type: application/x-ndjson";
  curl_argv[argc++] = (char *)"-H";
  curl_argv[argc++] = (char *)"Content-Encoding: gzip";
  curl_argv[argc++] = (char *)"--data-binary";
  curl_argv[argc++] = (char *)"@-";
  curl_argv[argc] = NULL;

  gzip_pid = fork();
  if (gzip_pid == 0) {
    dup2(gzip_input[0], STDIN_FILENO);
    dup2(gzip_output[1], STDOUT_FILENO);
    close(gzip_input[0]);
    close(gzip_input[1]);
    close(gzip_output[0]);
    close(gzip_output[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
    execlp("gzip", "gzip", "-c", (char *)NULL);
    _exit(127);
  }
  if (gzip_pid < 0) goto cleanup;

  curl_pid = fork();
  if (curl_pid == 0) {
    dup2(gzip_output[0], STDIN_FILENO);
    dup2(response_pipe[1], STDOUT_FILENO);
    dup2(response_pipe[1], STDERR_FILENO);
    close(gzip_input[0]);
    close(gzip_input[1]);
    close(gzip_output[0]);
    close(gzip_output[1]);
    close(response_pipe[0]);
    close(response_pipe[1]);
    execvp(curl_argv[0], curl_argv);
    _exit(127);
  }
  if (curl_pid < 0) goto cleanup;

  close(gzip_input[0]);
  gzip_input[0] = -1;
  close(gzip_output[0]);
  gzip_output[0] = -1;
  close(gzip_output[1]);
  gzip_output[1] = -1;
  close(response_pipe[1]);
  response_pipe[1] = -1;
  if (write_all(gzip_input[1], data, length) != 0) goto cleanup;
  close(gzip_input[1]);
  gzip_input[1] = -1;
  response[0] = '\0';
  read(response_pipe[0], response, sizeof(response) - 1);
  close(response_pipe[0]);
  response_pipe[0] = -1;
  waitpid(gzip_pid, &gzip_status, 0);
  waitpid(curl_pid, &curl_status, 0);
  result = WIFEXITED(gzip_status) && WEXITSTATUS(gzip_status) == 0 &&
           WIFEXITED(curl_status) && WEXITSTATUS(curl_status) == 0
               ? 0
               : -1;

cleanup:
  if (gzip_input[0] >= 0) close(gzip_input[0]);
  if (gzip_input[1] >= 0) close(gzip_input[1]);
  if (gzip_output[0] >= 0) close(gzip_output[0]);
  if (gzip_output[1] >= 0) close(gzip_output[1]);
  if (response_pipe[0] >= 0) close(response_pipe[0]);
  if (response_pipe[1] >= 0) close(response_pipe[1]);
  if (gzip_pid > 0) waitpid(gzip_pid, &gzip_status, 0);
  if (curl_pid > 0) waitpid(curl_pid, &curl_status, 0);
  return result;
}

static void update_result(int success, const char *error) {
  pthread_mutex_lock(&g_lock);
  if (success) {
    g_status.last_success = time(NULL);
    g_status.sent_count++;
    g_status.last_error[0] = '\0';
  } else {
    g_status.last_failure = time(NULL);
    g_status.failed_count++;
    copy_string(g_status.last_error, sizeof(g_status.last_error),
                error ? error : "上传失败");
  }
  pthread_mutex_unlock(&g_lock);
}

static TelemetryBuffer take_pending(void) {
  TelemetryBuffer pending;
  pthread_mutex_lock(&g_lock);
  pending = g_pending;
  memset(&g_pending, 0, sizeof(g_pending));
  pthread_mutex_unlock(&g_lock);
  return pending;
}

static void send_pending(const TelemetryConfig *config) {
  TelemetryBuffer pending = take_pending();
  if (pending.length > 0) {
    int success;
    pthread_mutex_lock(&g_lock);
    g_sender_active = 1;
    pthread_mutex_unlock(&g_lock);
    success = send_gzip_fragment(config, pending.data, pending.length) == 0;
    pthread_mutex_lock(&g_lock);
    g_sender_active = 0;
    pthread_mutex_unlock(&g_lock);
    update_result(success, success ? NULL : "日志分片上传失败");
  }
  free(pending.data);
}

static void collect_minimal_snapshot(const TelemetryConfig *config) {
  char payload[256];
  char record[1024];
  TelemetryBuffer batch = {0};
  double uptime = get_uptime();

  snprintf(payload, sizeof(payload), "{\"uptime\":%.3f}", uptime);
  if (telemetry_build_record(record, sizeof(record), g_device_id, g_boot_id,
                             g_sequence++, "snapshot", "system", payload) == 0 &&
      buffer_append(&batch, record, strlen(record)) == 0) {
    TelemetryBuffer pending = take_pending();
    buffer_append(&batch, pending.data, pending.length);
    free(pending.data);
    pthread_mutex_lock(&g_lock);
    g_sender_active = 1;
    pthread_mutex_unlock(&g_lock);
    update_result(send_gzip_fragment(config, batch.data, batch.length) == 0,
                  "快照上传失败");
    pthread_mutex_lock(&g_lock);
    g_sender_active = 0;
    pthread_mutex_unlock(&g_lock);
  }
  free(batch.data);
}

static void *telemetry_worker(void *arg) {
  (void)arg;
  pthread_mutex_lock(&g_lock);
  g_status.running = 1;
  while (!g_stop) {
    TelemetryConfig config;
    struct timespec deadline;

    while (!g_stop && (!g_config.enabled || !config_valid(&g_config))) {
      pthread_cond_wait(&g_ready, &g_lock);
    }
    if (g_stop) break;
    config = g_config;
    pthread_mutex_unlock(&g_lock);

    collect_minimal_snapshot(&config);
    send_pending(&config);

    pthread_mutex_lock(&g_lock);
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += config.interval_sec;
    g_flush_requested = 0;
    while (!g_stop && !g_flush_requested && g_config.enabled) {
      if (pthread_cond_timedwait(&g_ready, &g_lock, &deadline) == ETIMEDOUT) break;
    }
  }
  g_status.running = 0;
  pthread_mutex_unlock(&g_lock);
  return NULL;
}

int telemetry_init(const char *db_path) {
  if (g_initialized) return 0;
  if (db_init(db_path) != 0 || read_device_identity() != 0) return -1;
  pthread_mutex_lock(&g_lock);
  load_config();
  g_stop = 0;
  g_initialized = 1;
  pthread_mutex_unlock(&g_lock);
  if (pthread_create(&g_worker, NULL, telemetry_worker, NULL) != 0) {
    pthread_mutex_lock(&g_lock);
    g_initialized = 0;
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  pthread_cond_signal(&g_ready);
  return 0;
}

void telemetry_deinit(void) {
  if (!g_initialized) return;
  pthread_mutex_lock(&g_lock);
  g_stop = 1;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
  pthread_join(g_worker, NULL);
  pthread_mutex_lock(&g_lock);
  free(g_pending.data);
  memset(&g_pending, 0, sizeof(g_pending));
  g_initialized = 0;
  pthread_mutex_unlock(&g_lock);
}

int telemetry_get_config(TelemetryConfig *config) {
  if (!config) return -1;
  pthread_mutex_lock(&g_lock);
  *config = g_config;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_save_config(const TelemetryConfig *config, int clear_token) {
  TelemetryConfig next;

  if (!config) return -1;
  pthread_mutex_lock(&g_lock);
  next = *config;
  if (!config->token_present) next.token_present = g_config.token_present;
  if (!config->token_present) {
    copy_string(next.group_token, sizeof(next.group_token), g_config.group_token);
  }
  if (clear_token) {
    next.group_token[0] = '\0';
    next.token_present = 0;
  }
  if (!config_valid(&next) || config_set_int("telemetry_enabled", next.enabled) != 0 ||
      config_set_int("telemetry_interval_sec", next.interval_sec) != 0 ||
      config_set_text("telemetry_url", next.url) != 0 ||
      config_set_text("telemetry_group_token", next.group_token) != 0) {
    pthread_mutex_unlock(&g_lock);
    return -1;
  }
  g_config = next;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_get_status(TelemetryStatus *status) {
  if (!status) return -1;
  pthread_mutex_lock(&g_lock);
  *status = g_status;
  pthread_mutex_unlock(&g_lock);
  return 0;
}

int telemetry_test(const TelemetryConfig *config) {
  char auth[TELEMETRY_TOKEN_SIZE + 32];
  char device_header[TELEMETRY_DEVICE_ID_SIZE + 32];
  char response[32] = {0};
  char *argv[24];
  int argc = 0;
  int result;

  if (!config || !url_valid(config->url) || config->group_token[0] == '\0' ||
      g_device_id[0] == '\0') {
    return -1;
  }
  snprintf(auth, sizeof(auth), "Authorization: Bearer %s", config->group_token);
  snprintf(device_header, sizeof(device_header), "X-Device-ID: %s", g_device_id);
  argv[argc++] = (char *)"curl";
  argv[argc++] = (char *)"-sS";
  argv[argc++] = (char *)"-o";
  argv[argc++] = (char *)"/dev/null";
  argv[argc++] = (char *)"-w";
  argv[argc++] = (char *)"%{http_code}";
  argv[argc++] = (char *)"-I";
  argv[argc++] = (char *)"--max-time";
  argv[argc++] = (char *)"10";
  argv[argc++] = (char *)config->url;
  argv[argc++] = (char *)"-H";
  argv[argc++] = auth;
  argv[argc++] = (char *)"-H";
  argv[argc++] = device_header;
  argv[argc] = NULL;
  pthread_mutex_lock(&g_lock);
  g_sender_active = 1;
  pthread_mutex_unlock(&g_lock);
  result = run_command_argv(response, sizeof(response), argv);
  pthread_mutex_lock(&g_lock);
  g_sender_active = 0;
  pthread_mutex_unlock(&g_lock);
  return result == 0 && strcmp(response, "204") == 0 ? 0 : -1;
}

void telemetry_capture_line(const char *source, const char *line) {
  if (!source || !line) return;
  pthread_mutex_lock(&g_lock);
  if (g_initialized && g_config.enabled && !g_sender_active) {
    append_message_record(&g_pending, "log", source, line);
    if (g_pending.length >= TELEMETRY_FRAGMENT_SIZE) {
      g_flush_requested = 1;
      pthread_cond_signal(&g_ready);
    }
  }
  pthread_mutex_unlock(&g_lock);
}

void telemetry_capture_at(const char *command, const char *response, int success) {
  char message[4096];

  snprintf(message, sizeof(message), "command=%s result=%s response=%s",
           command ? command : "", success ? "ok" : "error",
           response ? response : "");
  telemetry_capture_line("at", message);
  pthread_mutex_lock(&g_lock);
  g_flush_requested = 1;
  pthread_cond_signal(&g_ready);
  pthread_mutex_unlock(&g_lock);
}

void telemetry_capture_command(const char *command, const char *output,
                               int success) {
  char message[4096];

  snprintf(message, sizeof(message), "command=%s result=%s output=%s",
           command ? command : "", success ? "ok" : "error",
           output ? output : "");
  telemetry_capture_line("command", message);
}
