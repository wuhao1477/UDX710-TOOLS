#include <assert.h>
#include <string.h>

#include "sms_parser.h"

static void test_signal_text_has_priority(void) {
  char output[64];

  assert(sms_select_incoming_text("短信正文", "属性正文", "", "", output,
                                 sizeof(output)) == 0);
  assert(strcmp(output, "短信正文") == 0);
}

static void test_property_text_fills_empty_signal(void) {
  char output[64];

  assert(sms_select_incoming_text("", "属性正文", "", "", output,
                                 sizeof(output)) == 0);
  assert(strcmp(output, "属性正文") == 0);
  assert(sms_select_incoming_text("/ril_0/message_1", "", "Content正文", "",
                                 output, sizeof(output)) == 0);
  assert(strcmp(output, "Content正文") == 0);
}

static void test_empty_text_is_reported(void) {
  char output[16];

  assert(sms_select_incoming_text("", "", "", "", output, sizeof(output)) !=
         0);
  assert(output[0] == '\0');
}

int main(void) {
  test_signal_text_has_priority();
  test_property_text_fills_empty_signal();
  test_empty_text_is_reported();
  return 0;
}
