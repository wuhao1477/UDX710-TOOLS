/**
 * @file factory_reset.h
 * @brief 恢复出厂设置头文件
 */

#ifndef FACTORY_RESET_H
#define FACTORY_RESET_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

void handle_factory_reset(struct mg_connection *c, struct mg_http_message *hm);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_RESET_H */

