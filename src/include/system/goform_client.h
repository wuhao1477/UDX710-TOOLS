#ifndef GOFORM_CLIENT_H
#define GOFORM_CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int goform_get_device_info(char *response, size_t response_size);
int goform_get_all_device_info(char *response, size_t response_size);
int goform_get_sim_info(char *response, size_t response_size);
int goform_get_package_info(char *response, size_t response_size);
int goform_build_wifi_query(const char *ssid, const char *password, char *query,
                            size_t query_size);
int goform_set_wifi_info(const char *ssid, const char *password, char *response,
                         size_t response_size);
int goform_check_real_name(const char *operator_id, const char *device_id,
                           char *response, size_t response_size);

int goform_json_string(const char *json, const char *key, char *out,
                       size_t out_size);
int goform_json_int(const char *json, const char *key, int *out);

#ifdef __cplusplus
}
#endif

#endif
