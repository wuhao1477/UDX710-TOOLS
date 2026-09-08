#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TELEMETRY_URL_SIZE 512
#define TELEMETRY_TOKEN_SIZE 256
#define TELEMETRY_ERROR_SIZE 256

typedef struct {
  int enabled;
  int interval_sec;
  char url[TELEMETRY_URL_SIZE];
  char group_token[TELEMETRY_TOKEN_SIZE];
  int token_present;
} TelemetryConfig;

typedef struct {
  int running;
  time_t last_success;
  time_t last_failure;
  unsigned long long sent_count;
  unsigned long long failed_count;
  char last_error[TELEMETRY_ERROR_SIZE];
} TelemetryStatus;

int telemetry_redact_text(const char *input, char *output, size_t output_size);
int telemetry_build_record(char *output, size_t output_size,
                           const char *device_id, const char *boot_id,
                           unsigned long long sequence, const char *type,
                           const char *source, const char *payload_json);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_H */
