#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include "goform_client.h"
#include "mongoose.h"

typedef struct {
  int completed;
  int result;
  char response[32];
} AsyncResult;

static int test_worker(void *arg, char *response, size_t response_size) {
  (void)arg;
  usleep(10000);
  assert(response_size > strlen("worker-ok"));
  strcpy(response, "worker-ok");
  return 0;
}

static int cancel_worker(void *arg, char *response, size_t response_size) {
  (void)arg;
  usleep(50000);
  if (response_size > 0) response[0] = '\0';
  return 0;
}

static void test_completion(struct mg_connection *c, int result,
                            const char *response, void *arg) {
  AsyncResult *state = (AsyncResult *)arg;

  (void)c;
  state->result = result;
  snprintf(state->response, sizeof(state->response), "%s", response);
  state->completed++;
}

static void noop_completion(struct mg_connection *c, int result,
                            const char *response, void *arg) {
  (void)c;
  (void)result;
  (void)response;
  (void)arg;
}

static void event_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_WAKEUP) {
    goform_async_handle_wakeup(c, (const struct mg_str *)ev_data);
  }
}

static void poll_until(struct mg_mgr *mgr, AsyncResult *state, int expected) {
  for (int i = 0; i < 100 && state->completed < expected; i++) {
    mg_mgr_poll(mgr, 20);
  }
}

int main(void) {
  struct mg_mgr mgr;
  struct mg_connection *connection;
  AsyncResult state = {0};

  mg_mgr_init(&mgr);
  connection = mg_listen(&mgr, "tcp://127.0.0.1:0", event_handler, NULL);
  assert(connection != NULL);
  assert(goform_async_init(&mgr) == 0);

  assert(goform_async_submit(connection, test_worker, &state, NULL,
                              test_completion) == 0);
  assert(goform_async_submit(connection, test_worker, NULL, NULL,
                              noop_completion) != 0);
  poll_until(&mgr, &state, 1);
  assert(state.completed == 1);
  assert(state.result == 0);
  assert(strcmp(state.response, "worker-ok") == 0);

  assert(goform_async_submit(connection, cancel_worker, &state, NULL,
                              test_completion) == 0);
  goform_async_cancel(connection);
  poll_until(&mgr, &state, 2);
  assert(state.completed == 1);

  goform_async_shutdown();
  mg_mgr_free(&mgr);
  return 0;
}
