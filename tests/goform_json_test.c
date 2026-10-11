#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "goform_client.h"

int run_command(char *output, size_t size, const char *cmd, ...) {
  (void)output;
  (void)size;
  (void)cmd;
  return -1;
}

static void test_device_info_fields(void) {
  const char *json =
      "{\"data\":{\"oemname\":\"SZ\",\"devtype\":\"542\","
      "\"devsubtype\":\"SRB876\",\"device\":\"123456\","
      "\"usemnc\":11}}";
  char oem[16];
  int priority;

  assert(goform_json_string(json, "oemname", oem, sizeof(oem)) == 0);
  assert(strcmp(oem, "SZ") == 0);
  assert(goform_json_int(json, "usemnc", &priority) == 0);
  assert(priority == 11);
}

static void test_json_parser_rejects_missing_values(void) {
  char value[16];
  int number;

  assert(goform_json_string("{\"oemname\":null}", "oemname", value,
                           sizeof(value)) != 0);
  assert(goform_json_int("{\"usemnc\":null}", "usemnc", &number) != 0);
}

int main(void) {
  test_device_info_fields();
  test_json_parser_rejects_missing_values();
  return 0;
}
