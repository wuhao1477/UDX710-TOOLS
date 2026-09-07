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
      "{\"data\":{\"battery\":100,\"voltage\":4350,"
      "\"current\":null,\"charging\":1,\"oemname\":\"SZ\"}}";
  char oem[16];
  int value;

  assert(goform_json_string(json, "oemname", oem, sizeof(oem)) == 0);
  assert(strcmp(oem, "SZ") == 0);
  assert(goform_json_int(json, "battery", &value) == 0 && value == 100);
  assert(goform_json_int(json, "voltage", &value) == 0 && value == 4350);
  assert(goform_json_int(json, "current", &value) != 0);
}

int main(void) {
  test_device_info_fields();
  return 0;
}
