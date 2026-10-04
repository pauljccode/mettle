/* Exercise Mettle's notifier lifecycle with its bundled libeio version.
 * A barrier forces a worker to finish between eio_poll and ev_async_start.
 */
#include <ev.h>
#include <eio.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct ev_loop *loop;

static ev_timer deadline;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static int keep_active, notifications, released, injected, completed;
#define EV_LOOP_FLAGS (EVFLAG_NOENV | EVBACKEND_SELECT | EVFLAG_FORKCHECK)
static int controlled_poll(void);
static void work(eio_req *req) {
  if ((intptr_t)req->data != 2) return;
  pthread_mutex_lock(&mutex);
  while (!released) pthread_cond_wait(&condition, &mutex);
  pthread_mutex_unlock(&mutex);
}
static int finished(eio_req *req) {
  (void)req;
  ++completed;
  return 0;
}
static int controlled_poll(void) {
  int result = eio_poll();
  if (!injected && completed == 1) {
    injected = 1;
    pthread_mutex_lock(&mutex);
    released = 1;
    pthread_cond_broadcast(&condition);
    while (notifications < 2) pthread_cond_wait(&condition, &mutex);
    pthread_mutex_unlock(&mutex);
  }
  return result;
}

#define eio_poll controlled_poll
#include "notifier-callbacks.h"

#undef eio_poll
static void want_poll(void) {
  eio_want_poll();
  pthread_mutex_lock(&mutex);
  ++notifications;
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&mutex);
}
static void stop(EV_P_ ev_timer *w, int events) {
  (void)w; (void)events;
  ev_break(EV_A_ EVBREAK_ALL);
}
int main(int argc, char **argv) {
  if (argc != 2 || (strcmp(argv[1], "baseline") && strcmp(argv[1], "candidate"))) return 2;
  alarm(10);
  keep_active = !strcmp(argv[1], "candidate");
  loop = ev_default_loop(EV_LOOP_FLAGS);
  if (!loop) return 2;
  ev_idle_init(&eio_idle_watcher, eio_idle_cb);
  ev_async_init(&eio_async_watcher, eio_async_cb);
  ev_async_start(loop, &eio_async_watcher);
  if (eio_init(want_poll, eio_done_poll) != 0) return 2;
  if (!eio_custom(work, 0, finished, (void *)(intptr_t)1)) return 2;
  if (!eio_custom(work, 0, finished, (void *)(intptr_t)2)) return 2;
  ev_timer_init(&deadline, stop, 1.0, 0);
  ev_timer_start(loop, &deadline);
  ev_run(loop, 0);
  unsigned int pending = eio_npending();
  printf("mode=%s notifications=%d completed=%d pending=%u\n",
    keep_active ? "keep-active" : "stop-start", notifications, completed, pending);
  int ok = notifications == 2 && completed == (keep_active ? 2 : 1)
    && pending == (keep_active ? 0 : 1);
  eio_poll(); /* Drain the intentionally stranded completion before teardown. */
  ev_async_stop(loop, &eio_async_watcher);
  ev_loop_destroy(loop);
  alarm(0);
  return ok ? 0 : 1;
}
