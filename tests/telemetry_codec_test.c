#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "telemetry.h"

static void test_redacts_secrets(void) {
  char out[512];

  assert(telemetry_redact_text(
             "password=wifi token=abc state=ready", out, sizeof(out)) == 0);
  assert(strstr(out, "wifi") == NULL);
  assert(strstr(out, "abc") == NULL);
  assert(strstr(out, "state=ready") != NULL);
}

static void test_removes_sms_body(void) {
  char out[512];

  assert(telemetry_redact_text(
             "event=sms_received body=secret text sender=10086", out,
             sizeof(out)) == 0);
  assert(strstr(out, "secret text") == NULL);
  assert(strstr(out, "sender=10086") != NULL);
}

static void test_builds_record(void) {
  char out[1024];

  assert(telemetry_build_record(out, sizeof(out), "SN-1", "boot-1", 7,
                                "snapshot", "system",
                                "{\"cpu_usage\":12.5}") == 0);
  assert(strstr(out, "\"device_id\":\"SN-1\"") != NULL);
  assert(strstr(out, "\"sequence\":7") != NULL);
  assert(strstr(out, "cpu_usage") != NULL);
}

static void test_rejects_small_buffer(void) {
  char out[16];

  assert(telemetry_build_record(out, sizeof(out), "SN-1", "boot-1", 1,
                                "event", "system", "{}") != 0);
}

int main(void) {
  test_redacts_secrets();
  test_removes_sms_body();
  test_builds_record();
  test_rejects_small_buffer();
  puts("telemetry codec tests passed");
  return 0;
}
