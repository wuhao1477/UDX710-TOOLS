#ifndef SMS_PARSER_H
#define SMS_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int sms_select_incoming_text(const char *signal_message,
                             const char *text_property,
                             const char *content_property,
                             const char *message_property, char *output,
                             size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* SMS_PARSER_H */
