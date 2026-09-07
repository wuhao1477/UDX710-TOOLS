/**
 * @file traffic.h
 * @brief 流量控制头文件
 */

#ifndef TRAFFIC_H
#define TRAFFIC_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

void init_traffic(void);
int traffic_get_total_bytes(long long *total);
void handle_get_traffic_total(struct mg_connection *c, struct mg_http_message *hm);
void handle_get_traffic_config(struct mg_connection *c, struct mg_http_message *hm);
void handle_set_traffic_limit(struct mg_connection *c, struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif /* TRAFFIC_H */
