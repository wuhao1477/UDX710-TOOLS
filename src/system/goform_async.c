#define _POSIX_C_SOURCE 200809L

#include "goform_client.h"

#include "mongoose.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  GOFORM_JOB_QUEUED = 0,
  GOFORM_JOB_RUNNING,
  GOFORM_JOB_COMPLETED
} GoformJobState;

typedef struct GoformAsyncJob {
  struct GoformAsyncJob *next;
  unsigned long connection_id;
  GoformAsyncWorker worker;
  GoformAsyncCompletion completion;
  void *arg;
  void (*free_arg)(void *arg);
  GoformJobState state;
  int canceled;
  int result;
  char response[GOFORM_ASYNC_RESPONSE_SIZE];
} GoformAsyncJob;

static struct {
  struct mg_mgr *mgr;
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  GoformAsyncJob *jobs;
  int initialized;
  int stopping;
} g_async = {0};

static void free_noop(void *arg) { (void) arg; }

static void dispose_job(GoformAsyncJob *job) {
  if (!job) return;
  if (job->free_arg) job->free_arg(job->arg);
  free(job);
}

static GoformAsyncJob *find_job_locked(GoformAsyncJob *needle,
                                       unsigned long connection_id) {
  GoformAsyncJob *job;

  for (job = g_async.jobs; job; job = job->next) {
    if ((needle && job == needle) ||
        (!needle && job->connection_id == connection_id)) {
      return job;
    }
  }
  return NULL;
}

static void unlink_job_locked(GoformAsyncJob *needle) {
  GoformAsyncJob **cursor;

  for (cursor = &g_async.jobs; *cursor; cursor = &(*cursor)->next) {
    if (*cursor == needle) {
      *cursor = needle->next;
      needle->next = NULL;
      return;
    }
  }
}

static GoformAsyncJob *take_queued_job_locked(void) {
  GoformAsyncJob *job;

  for (job = g_async.jobs; job; job = job->next) {
    if (job->state == GOFORM_JOB_QUEUED) {
      job->state = GOFORM_JOB_RUNNING;
      return job;
    }
  }
  return NULL;
}

static void *goform_worker_thread(void *unused) {
  (void) unused;

  for (;;) {
    GoformAsyncJob *job;

    pthread_mutex_lock(&g_async.mutex);
    while (!g_async.stopping &&
           (job = take_queued_job_locked()) == NULL) {
      pthread_cond_wait(&g_async.condition, &g_async.mutex);
    }
    if (g_async.stopping) {
      GoformAsyncJob *queued = g_async.jobs;
      GoformAsyncJob *next;

      /* No new work is accepted during shutdown.  Drop queued work while
       * allowing the currently running curl command to finish. */
      while (queued) {
        next = queued->next;
        if (queued->state == GOFORM_JOB_QUEUED) {
          unlink_job_locked(queued);
          queued->canceled = 1;
          dispose_job(queued);
        }
        queued = next;
      }
      pthread_mutex_unlock(&g_async.mutex);
      break;
    }
    if (job->canceled) {
      unlink_job_locked(job);
      pthread_mutex_unlock(&g_async.mutex);
      dispose_job(job);
      continue;
    }
    pthread_mutex_unlock(&g_async.mutex);

    job->response[0] = '\0';
    job->result = job->worker(job->arg, job->response,
                              sizeof(job->response));

    pthread_mutex_lock(&g_async.mutex);
    if (g_async.stopping || job->canceled) {
      unlink_job_locked(job);
      pthread_mutex_unlock(&g_async.mutex);
      dispose_job(job);
      continue;
    }
    job->state = GOFORM_JOB_COMPLETED;

    /* Only the event-loop thread may touch an mg_connection.  The wakeup
     * payload is an opaque token; the main thread validates it against the
     * job list before dereferencing it.  Keep the mutex while publishing the
     * wakeup so a close callback cannot free the completed job concurrently. */
    if (!mg_wakeup(g_async.mgr, job->connection_id, &job, sizeof(job))) {
      unlink_job_locked(job);
      pthread_mutex_unlock(&g_async.mutex);
      dispose_job(job);
    } else {
      pthread_mutex_unlock(&g_async.mutex);
    }
  }
  return NULL;
}

int goform_async_init(struct mg_mgr *mgr) {
  int result;

  if (!mgr || g_async.initialized || !mg_wakeup_init(mgr)) return -1;
  memset(&g_async, 0, sizeof(g_async));
  g_async.mgr = mgr;
  pthread_mutex_init(&g_async.mutex, NULL);
  pthread_cond_init(&g_async.condition, NULL);
  result = pthread_create(&g_async.thread, NULL, goform_worker_thread, NULL);
  if (result != 0) {
    pthread_cond_destroy(&g_async.condition);
    pthread_mutex_destroy(&g_async.mutex);
    memset(&g_async, 0, sizeof(g_async));
    return -1;
  }
  g_async.initialized = 1;
  return 0;
}

int goform_async_submit(struct mg_connection *c, GoformAsyncWorker worker,
                        void *arg, void (*free_arg)(void *arg),
                        GoformAsyncCompletion completion) {
  GoformAsyncJob *job;

  if (!free_arg) free_arg = free_noop;
  if (!c || !worker || !completion || !g_async.initialized) {
    free_arg(arg);
    return -1;
  }
  job = (GoformAsyncJob *) calloc(1, sizeof(*job));
  if (!job) {
    free_arg(arg);
    return -1;
  }
  job->connection_id = c->id;
  job->worker = worker;
  job->completion = completion;
  job->arg = arg;
  job->free_arg = free_arg;
  job->state = GOFORM_JOB_QUEUED;

  pthread_mutex_lock(&g_async.mutex);
  if (g_async.stopping) {
    pthread_mutex_unlock(&g_async.mutex);
    dispose_job(job);
    return -1;
  }
  if (find_job_locked(NULL, c->id) != NULL) {
    pthread_mutex_unlock(&g_async.mutex);
    dispose_job(job);
    return -1;
  }
  job->next = g_async.jobs;
  g_async.jobs = job;
  c->is_full = 1;
  pthread_cond_signal(&g_async.condition);
  pthread_mutex_unlock(&g_async.mutex);
  return 0;
}

void goform_async_cancel(struct mg_connection *c) {
  GoformAsyncJob *job;

  if (!c || !g_async.initialized) return;
  pthread_mutex_lock(&g_async.mutex);
  job = find_job_locked(NULL, c->id);
  if (job) {
    job->canceled = 1;
    if (job->state == GOFORM_JOB_QUEUED ||
        job->state == GOFORM_JOB_COMPLETED) {
      unlink_job_locked(job);
      pthread_mutex_unlock(&g_async.mutex);
      dispose_job(job);
      return;
    }
  }
  pthread_mutex_unlock(&g_async.mutex);
}

void goform_async_handle_wakeup(struct mg_connection *c,
                                const struct mg_str *data) {
  GoformAsyncJob *token;
  GoformAsyncJob *job;

  if (!c || !data || data->len != sizeof(token) || !g_async.initialized) {
    return;
  }
  memcpy(&token, data->buf, sizeof(token));
  pthread_mutex_lock(&g_async.mutex);
  job = find_job_locked(token, 0);
  if (!job || job->connection_id != c->id ||
      job->state != GOFORM_JOB_COMPLETED || job->canceled) {
    pthread_mutex_unlock(&g_async.mutex);
    return;
  }
  unlink_job_locked(job);
  pthread_mutex_unlock(&g_async.mutex);

  job->completion(c, job->result, job->response, job->arg);
  c->is_full = 0;
  dispose_job(job);
}

void goform_async_shutdown(void) {
  GoformAsyncJob *job;
  GoformAsyncJob *next;

  if (!g_async.initialized) return;
  pthread_mutex_lock(&g_async.mutex);
  g_async.stopping = 1;
  pthread_cond_signal(&g_async.condition);
  pthread_mutex_unlock(&g_async.mutex);
  pthread_join(g_async.thread, NULL);

  pthread_mutex_lock(&g_async.mutex);
  /* The worker has stopped, so no completion callback can race this cleanup. */
  job = g_async.jobs;
  g_async.jobs = NULL;
  pthread_mutex_unlock(&g_async.mutex);
  while (job) {
    next = job->next;
    dispose_job(job);
    job = next;
  }
  pthread_cond_destroy(&g_async.condition);
  pthread_mutex_destroy(&g_async.mutex);
  memset(&g_async, 0, sizeof(g_async));
}
