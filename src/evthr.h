#ifndef __EVTHR_H__
#define __EVTHR_H__

enum evthr_res {
    EVTHR_RES_OK = 0,
    EVTHR_RES_BACKLOG,
    EVTHR_RES_RETRY,
    EVTHR_RES_NOCB,
    EVTHR_RES_FATAL
};

struct evthr_pool;
struct evthr;

typedef void (*evthr_cb)(struct evthr *thr, void *cmd_arg, void *shared);
typedef void (*evthr_init_cb)(struct evthr *thr, void *shared);
typedef void (*evthr_exit_cb)(struct evthr *thr, void *shared);

struct event_base *
evthr_get_base(struct evthr *thr);

void
evthr_set_aux(struct evthr *thr, void *aux);

void *
evthr_get_aux(struct evthr *thr);

enum evthr_res
evthr_pool_defer(struct evthr_pool *pool, evthr_cb cb, void *arg);

struct evthr_pool *
evthr_pool_wexit_new(int nthreads, evthr_init_cb init_cb, evthr_exit_cb exit_cb, void *shared);

void
evthr_pool_free(struct evthr_pool *pool);

enum evthr_res
evthr_pool_stop(struct evthr_pool *pool);

int
evthr_pool_start(struct evthr_pool *pool);

#endif /* !__EVTHR_H__ */
