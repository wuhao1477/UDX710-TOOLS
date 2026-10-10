/**
 * @file health.h
 * @brief Read-only, real-time device health API.
 */

#ifndef HEALTH_H
#define HEALTH_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GET /api/health - collect a non-persistent health snapshot */
void handle_health(struct mg_connection *c, struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif /* HEALTH_H */
