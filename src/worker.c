/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include <pthread.h>

#include <event2/event.h>

#include "db.h"
#include "logger.h"
#include "worker.h"
#include "misc.h"

#define THREADPOOL_NTHREADS 4

struct evthr_pool;

static struct evthr_pool *worker_threadpool;


/* ----------------- Thread handling borrowed from libevhtp ----------------- */

#ifndef TAILQ_FOREACH_SAFE
#define TAILQ_FOREACH_SAFE(var, head, field, tvar)        \
    for ((var) = TAILQ_FIRST((head));                     \
         (var) && ((tvar) = TAILQ_NEXT((var), field), 1); \
         (var) = (tvar))
#endif

#define _evthr_read(thr, cmd, sock) \
    (recv(sock, cmd, sizeof(struct evthr_cmd), 0) == sizeof(struct evthr_cmd)) ? 1 : 0

#define EVTHR_SHARED_PIPE 1

enum evthr_res {
    EVTHR_RES_OK = 0,
    EVTHR_RES_BACKLOG,
    EVTHR_RES_RETRY,
    EVTHR_RES_NOCB,
    EVTHR_RES_FATAL
};

struct evthr;

typedef void (*evthr_cb)(struct evthr *thr, void *cmd_arg, void *shared);
typedef void (*evthr_init_cb)(struct evthr *thr, void *shared);
typedef void (*evthr_exit_cb)(struct evthr *thr, void *shared);

struct evthr_cmd {
    uint8_t  stop;
    void *args;
    evthr_cb cb;
} __attribute__((packed));

struct evthr_pool {
#ifdef EVTHR_SHARED_PIPE
    int rdr;
    int wdr;
#endif
    int nthreads;
    TAILQ_HEAD(evthr_pool_slist, evthr) threads;
};

struct evthr {
    int             rdr;
    int             wdr;
    char            err;
    struct event *event;
    struct event_base *evbase;
    pthread_mutex_t lock;
    pthread_t     *thr;
    evthr_init_cb   init_cb;
    evthr_exit_cb   exit_cb;
    void          *arg;
    void          *aux;
#ifdef EVTHR_SHARED_PIPE
    int            pool_rdr;
    struct event *shared_pool_ev;
#endif
    TAILQ_ENTRY(evthr) next;
};


static void
_evthr_read_cmd(evutil_socket_t sock, short which, void *args)
{
    struct evthr *thread;
    struct evthr_cmd cmd;
    int stopped;

    if (!(thread = (struct evthr *)args)) {
        return;
    }

    stopped = 0;

    if (_evthr_read(thread, &cmd, sock) == 1) {
        stopped = cmd.stop;

        if (cmd.cb != NULL) {
            (cmd.cb)(thread, cmd.args, thread->arg);
        }
    }

    if (stopped == 1) {
        event_base_loopbreak(thread->evbase);
    }

    return;
}

static void *
_evthr_loop(void *args)
{
    struct evthr *thread;

    if (!(thread = (struct evthr *)args)) {
        return NULL;
    }

    if (thread == NULL || thread->thr == NULL) {
        pthread_exit(NULL);
    }

    thread->evbase = event_base_new();
    thread->event  = event_new(thread->evbase, thread->rdr,
                               EV_READ | EV_PERSIST, _evthr_read_cmd, args);

    event_add(thread->event, NULL);

#ifdef EVTHR_SHARED_PIPE
    if (thread->pool_rdr > 0) {
        thread->shared_pool_ev = event_new(thread->evbase, thread->pool_rdr,
                                           EV_READ | EV_PERSIST, _evthr_read_cmd, args);
        event_add(thread->shared_pool_ev, NULL);
    }
#endif

    pthread_mutex_lock(&thread->lock);
    if (thread->init_cb != NULL) {
        (thread->init_cb)(thread, thread->arg);
    }

    pthread_mutex_unlock(&thread->lock);

    CHECK_ERR(L_MAIN, thread->err);

    event_base_loop(thread->evbase, 0);

    pthread_mutex_lock(&thread->lock);
    if (thread->exit_cb != NULL) {
        (thread->exit_cb)(thread, thread->arg);
    }

    pthread_mutex_unlock(&thread->lock);

    pthread_exit(NULL);
}

static enum evthr_res
evthr_defer(struct evthr *thread, evthr_cb cb, void *arg)
{
    struct evthr_cmd cmd = {
        .cb   = cb,
        .args = arg,
        .stop = 0
    };

    if (send(thread->wdr, &cmd, sizeof(cmd), 0) <= 0) {
        return EVTHR_RES_RETRY;
    }

    return EVTHR_RES_OK;
}

static enum evthr_res
evthr_stop(struct evthr *thread)
{
    struct evthr_cmd cmd = {
        .cb   = NULL,
        .args = NULL,
        .stop = 1
    };

    if (send(thread->wdr, &cmd, sizeof(struct evthr_cmd), 0) < 0) {
        return EVTHR_RES_RETRY;
    }

    pthread_join(*thread->thr, NULL);
    return EVTHR_RES_OK;
}

static struct event_base *
evthr_get_base(struct evthr * thr)
{
    return thr ? thr->evbase : NULL;
}

static void
evthr_free(struct evthr *thread)
{
    if (thread == NULL) {
        return;
    }

    if (thread->rdr > 0) {
        close(thread->rdr);
    }

    if (thread->wdr > 0) {
        close(thread->wdr);
    }

    if (thread->thr) {
        free(thread->thr);
    }

    if (thread->event) {
        event_free(thread->event);
    }

#ifdef EVTHR_SHARED_PIPE
    if (thread->shared_pool_ev) {
        event_free(thread->shared_pool_ev);
    }
#endif

    if (thread->evbase) {
        event_base_free(thread->evbase);
    }

    free(thread);
}

static struct evthr *
evthr_wexit_new(evthr_init_cb init_cb, evthr_exit_cb exit_cb, void *args)
{
    struct evthr *thread;
    int fds[2];

    if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == -1) {
        return NULL;
    }

    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    if (!(thread = calloc(1, sizeof(struct evthr)))) {
        return NULL;
    }

    thread->thr     = malloc(sizeof(pthread_t));
    thread->arg     = args;
    thread->rdr     = fds[0];
    thread->wdr     = fds[1];

    thread->init_cb = init_cb;
    thread->exit_cb = exit_cb;

    if (pthread_mutex_init(&thread->lock, NULL)) {
        evthr_free(thread);
        return NULL;
    }

    return thread;
}

static int
evthr_start(struct evthr *thread)
{
    if (thread == NULL || thread->thr == NULL) {
        return -1;
    }

    if (pthread_create(thread->thr, NULL, _evthr_loop, (void *)thread)) {
        return -1;
    }

    return 0;
}

static void
evthr_pool_free(struct evthr_pool *pool)
{
    struct evthr *thread;
    struct evthr *save;

    if (pool == NULL) {
        return;
    }

    TAILQ_FOREACH_SAFE(thread, &pool->threads, next, save) {
        TAILQ_REMOVE(&pool->threads, thread, next);

        evthr_free(thread);
    }

    free(pool);
}

static enum evthr_res
evthr_pool_stop(struct evthr_pool *pool)
{
    struct evthr *thr;
    struct evthr *save;

    if (pool == NULL) {
        return EVTHR_RES_FATAL;
    }

    TAILQ_FOREACH_SAFE(thr, &pool->threads, next, save) {
        evthr_stop(thr);
    }

    return EVTHR_RES_OK;
}

static inline int
get_backlog_(struct evthr *thread)
{
    int backlog = 0;

    ioctl(thread->rdr, FIONREAD, &backlog);

    return (int)(backlog / sizeof(struct evthr_cmd));
}

static enum evthr_res
evthr_pool_defer(struct evthr_pool *pool, evthr_cb cb, void *arg)
{
#ifdef EVTHR_SHARED_PIPE
    struct evthr_cmd cmd = {
        .cb   = cb,
        .args = arg,
        .stop = 0
    };

    if (send(pool->wdr, &cmd, sizeof(cmd), 0) == -1) {
        return EVTHR_RES_RETRY;
    }

    return EVTHR_RES_OK;
#endif
    struct evthr *thread      = NULL;
    struct evthr *min_thread  = NULL;
    int min_backlog = 0;

    if (pool == NULL) {
        return EVTHR_RES_FATAL;
    }

    if (cb == NULL) {
        return EVTHR_RES_NOCB;
    }

    TAILQ_FOREACH(thread, &pool->threads, next) {
        int backlog = get_backlog_(thread);

        if (backlog == 0) {
            min_thread = thread;
            break;
        }

        if (min_thread == NULL || backlog < min_backlog) {
            min_thread  = thread;
            min_backlog = backlog;
        }
    }

    return evthr_defer(min_thread, cb, arg);
}

static struct evthr_pool *
evthr_pool_wexit_new(int nthreads, evthr_init_cb init_cb, evthr_exit_cb exit_cb, void *shared)
{
    struct evthr_pool *pool;
    int            i;

#ifdef EVTHR_SHARED_PIPE
    int            fds[2];
#endif

    if (nthreads == 0) {
        return NULL;
    }

    if (!(pool = calloc(1, sizeof(struct evthr_pool)))) {
        return NULL;
    }

    pool->nthreads = nthreads;
    TAILQ_INIT(&pool->threads);

#ifdef EVTHR_SHARED_PIPE
    if (evutil_socketpair(AF_UNIX, SOCK_DGRAM, 0, fds) == -1) {
        return NULL;
    }

    evutil_make_socket_nonblocking(fds[0]);
    evutil_make_socket_nonblocking(fds[1]);

    pool->rdr = fds[0];
    pool->wdr = fds[1];
#endif

    for (i = 0; i < nthreads; i++) {
        struct evthr * thread;

        if (!(thread = evthr_wexit_new(init_cb, exit_cb, shared))) {
            evthr_pool_free(pool);
            return NULL;
        }

#ifdef EVTHR_SHARED_PIPE
        thread->pool_rdr = fds[0];
#endif

        TAILQ_INSERT_TAIL(&pool->threads, thread, next);
    }

    return pool;
}

static int
evthr_pool_start(struct evthr_pool *pool)
{
    struct evthr *evthr = NULL;

    if (pool == NULL) {
        return -1;
    }

    TAILQ_FOREACH(evthr, &pool->threads, next) {
        if (evthr_start(evthr) < 0) {
            return -1;
        }

        usleep(5000);
    }

    return 0;
}


/* ----------------------------- CALLBACK EXECUTION ------------------------- */
/*                                 Worker threads                             */

struct worker_arg
{
  void (*cb)(void *);
  void *cb_arg;
  int delay;
  struct event *timer;
};


static void
execute_cb(int fd, short what, void *arg)
{
  struct worker_arg *cmdarg = arg;

  cmdarg->cb(cmdarg->cb_arg);

  event_free(cmdarg->timer);
  free(cmdarg->cb_arg);
  free(cmdarg);
}

static void
execute(struct evthr *thr, void *arg, void *shared)
{
  struct worker_arg *cmdarg = arg;
  struct timeval tv = { cmdarg->delay, 0 };
  struct event_base *evbase;

  if (cmdarg->delay)
    {
      evbase = evthr_get_base(thr);
      cmdarg->timer = evtimer_new(evbase, execute_cb, cmdarg);
      evtimer_add(cmdarg->timer, &tv);
      return;
    }

  cmdarg->cb(cmdarg->cb_arg);
  free(cmdarg->cb_arg);
  free(cmdarg);
}

static void
init_cb(struct evthr *thr, void *shared)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Error: DB init failed (worker thread)\n");
      thr->err = EIO;
      return;
    }

  thread_setname(pthread_self(), "worker");
}

static void
exit_cb(struct evthr *thr, void *shared)
{
  db_perthread_deinit();
}


/* ---------------------------- Our worker API  --------------------------- */

void
worker_execute(void (*cb)(void *), void *cb_arg, size_t arg_size, int delay)
{
  struct worker_arg *cmdarg;
  void *argcpy;

  cmdarg = calloc(1, sizeof(struct worker_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not allocate worker_arg\n");
      return;
    }

  if (arg_size > 0)
    {
      argcpy = malloc(arg_size);
      if (!argcpy)
	{
	  DPRINTF(E_LOG, L_MAIN, "Out of memory\n");
	  free(cmdarg);
	  return;
	}

      memcpy(argcpy, cb_arg, arg_size);
    }
  else
    argcpy = NULL;

  cmdarg->cb = cb;
  cmdarg->cb_arg = argcpy;
  cmdarg->delay = delay;

  evthr_pool_defer(worker_threadpool, execute, cmdarg);
}

int
worker_init(void)
{
  int ret;

  worker_threadpool = evthr_pool_wexit_new(THREADPOOL_NTHREADS, init_cb, exit_cb, NULL);
  if (!worker_threadpool)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not create worker thread pool\n");
      goto error;
    }

  ret = evthr_pool_start(worker_threadpool);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not spawn worker threads\n");
      goto error;
    }

  return 0;
  
 error:
  worker_deinit();
  return -1;
}

void
worker_deinit(void)
{
  evthr_pool_stop(worker_threadpool);
  evthr_pool_free(worker_threadpool);
}
