#ifndef GOFORM_PROXY_H
#define GOFORM_PROXY_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

void handle_goform_proxy(struct mg_connection *c, struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif
