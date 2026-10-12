#ifndef GOFORM_CLIENT_H
#define GOFORM_CLIENT_H

#include <stddef.h>

struct mg_connection;
struct mg_mgr;
struct mg_str;

#define GOFORM_ASYNC_RESPONSE_SIZE 32768

#ifdef __cplusplus
extern "C" {
#endif

int goform_get_device_info(char *response, size_t response_size);
int goform_get_all_device_info(char *response, size_t response_size);
int goform_get_sim_info(char *response, size_t response_size);
int goform_get_package_info(char *response, size_t response_size);
int goform_set_priority_mnc(int priority, char *response, size_t response_size);
int goform_check_real_name(const char *operator_id, const char *device_id,
                           char *response, size_t response_size);

int goform_json_string(const char *json, const char *key, char *out,
                       size_t out_size);
int goform_json_int(const char *json, const char *key, int *out);

/*
 * Network/device requests must not run in the Mongoose event-loop thread.
 * The worker callback is executed on a dedicated background thread.  Its
 * response buffer is owned by the async layer and is valid only while the
 * completion callback is running on the event-loop thread.
 */
typedef int (*GoformAsyncWorker)(void *arg, char *response,
                                 size_t response_size);
typedef void (*GoformAsyncCompletion)(struct mg_connection *c, int result,
                                      const char *response, void *arg);

int goform_async_init(struct mg_mgr *mgr);
void goform_async_shutdown(void);
int goform_async_submit(struct mg_connection *c, GoformAsyncWorker worker,
                        void *arg, void (*free_arg)(void *arg),
                        GoformAsyncCompletion completion);
void goform_async_cancel(struct mg_connection *c);
void goform_async_handle_wakeup(struct mg_connection *c,
                                const struct mg_str *data);

#ifdef __cplusplus
}
#endif

#endif
