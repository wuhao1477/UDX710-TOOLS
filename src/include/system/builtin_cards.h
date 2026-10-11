#ifndef BUILTIN_CARDS_H
#define BUILTIN_CARDS_H

#include "mongoose.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void handle_builtin_cards(struct mg_connection *c, struct mg_http_message *hm);
void handle_builtin_card_real_name(struct mg_connection *c,
                                   struct mg_http_message *hm);
void handle_builtin_card_switch(struct mg_connection *c,
                                struct mg_http_message *hm);
int builtin_card_switch_operator(const char *operator_id, char *response,
                                 size_t response_size, int *priority_out);

#ifdef __cplusplus
}
#endif

#endif
