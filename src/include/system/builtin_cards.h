#ifndef BUILTIN_CARDS_H
#define BUILTIN_CARDS_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

void handle_builtin_cards(struct mg_connection *c, struct mg_http_message *hm);
void handle_builtin_card_real_name(struct mg_connection *c,
                                   struct mg_http_message *hm);
void handle_builtin_card_switch(struct mg_connection *c,
                                struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif
