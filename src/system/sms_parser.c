#include "sms_parser.h"

#include <stdio.h>
#include <string.h>

static int copy_candidate(const char *value, char *output, size_t output_size) {
  if (!value || !value[0]) return 0;
  if (value[0] == '/') return 0;
  snprintf(output, output_size, "%s", value);
  return output[0] != '\0';
}

int sms_select_incoming_text(const char *signal_message,
                             const char *text_property,
                             const char *content_property,
                             const char *message_property, char *output,
                             size_t output_size) {
  const char *candidates[] = {signal_message, text_property, content_property,
                              message_property};
  if (!output || output_size == 0) return -1;
  output[0] = '\0';
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (copy_candidate(candidates[i], output, output_size)) return 0;
  }
  return -1;
}
