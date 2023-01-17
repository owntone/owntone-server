/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <syscall.h> // get thread ID

#include <event2/event.h>

#include <regex.h>
#include <zlib.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "worker.h"
#include "httpd.h"
#include "httpd_internal.h"
#include "transcode.h"
#ifdef LASTFM
# include "lastfm.h"
#endif
#ifdef HAVE_LIBWEBSOCKETS
# include "websocket.h"
#endif

#define STREAM_CHUNK_SIZE (64 * 1024)
#define ERR_PAGE "<html>\n<head>\n" \
  "<title>%d %s</title>\n" \
  "</head>\n<body>\n" \
  "<h1>%s</h1>\n" \
  "</body>\n</html>\n"

#define HTTPD_STREAM_SAMPLE_RATE 44100
#define HTTPD_STREAM_BPS         16
#define HTTPD_STREAM_CHANNELS    2

extern struct httpd_module httpd_dacp;
extern struct httpd_module httpd_daap;
extern struct httpd_module httpd_jsonapi;
extern struct httpd_module httpd_artworkapi;
extern struct httpd_module httpd_streaming;
extern struct httpd_module httpd_oauth;
extern struct httpd_module httpd_rsp;

// Must be in sync with enum httpd_modules
static struct httpd_module *httpd_modules[] = {
    &httpd_dacp,
    &httpd_daap,
    &httpd_jsonapi,
    &httpd_artworkapi,
    &httpd_streaming,
    &httpd_oauth,
    &httpd_rsp,
    NULL
};


struct content_type_map {
  char *ext;
  char *ctype;
};

struct stream_ctx {
  struct httpd_request *hreq;
  uint8_t *buf;
  struct evbuffer *evbuf;
  struct event *ev;
  int id;
  int fd;
  off_t size;
  off_t stream_size;
  off_t offset;
  off_t start_offset;
  off_t end_offset;
  int marked;
  struct transcode_ctx *xcode;
};

static const struct content_type_map ext2ctype[] =
  {
    { ".html", "text/html; charset=utf-8" },
    { ".xml",  "text/xml; charset=utf-8" },
    { ".css",  "text/css; charset=utf-8" },
    { ".txt",  "text/plain; charset=utf-8" },
    { ".js",   "application/javascript; charset=utf-8" },
    { ".gif",  "image/gif" },
    { ".ico",  "image/x-ico" },
    { ".png",  "image/png" },
    { NULL, NULL }
  };

static char webroot_directory[PATH_MAX];

static const char *httpd_allow_origin;
static int httpd_port;


#define THREADPOOL_NTHREADS 4

struct evthr_pool;

static struct evthr_pool *httpd_threadpool;


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
    httpd_server *server;
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


/* -------------------------------- HELPERS --------------------------------- */

static int
path_is_legal(const char *path)
{
  return strncmp(webroot_directory, path, strlen(webroot_directory));
}

/* Callback from the worker thread (async operation as it may block) */
static void
playcount_inc_cb(void *arg)
{
  int *id = arg;

  db_file_inc_playcount(*id);
}

#ifdef LASTFM
/* Callback from the worker thread (async operation as it may block) */
static void
scrobble_cb(void *arg)
{
  int *id = arg;

  lastfm_scrobble(*id);
}
#endif


/* --------------------------- MODULES INTERFACE ---------------------------- */

static void
modules_handlers_unset(struct httpd_uri_map *uri_map)
{
  struct httpd_uri_map *uri;

  for (uri = uri_map; uri->preg; uri++)
    {
      regfree(uri->preg); // Frees allocation by regcomp
      free(uri->preg); // Frees our own calloc
    }
}

static int
modules_handlers_set(struct httpd_uri_map *uri_map)
{
  struct httpd_uri_map *uri;
  char buf[64];
  int ret;

  for (uri = uri_map; uri->handler; uri++)
    {
      uri->preg = calloc(1, sizeof(regex_t));
      if (!uri->preg)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Error setting URI handler, out of memory");
	  goto error;
	}

      ret = regcomp(uri->preg, uri->regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
	{
	  regerror(ret, uri->preg, buf, sizeof(buf));
	  DPRINTF(E_LOG, L_HTTPD, "Error setting URI handler, regexp error: %s\n", buf);
	  goto error;
	}
    }

  return 0;

 error:
  modules_handlers_unset(uri_map);
  return -1;
}

static int
modules_init(void)
{
  struct httpd_module **ptr;
  struct httpd_module *m;

  for (ptr = httpd_modules; *ptr; ptr++)
    {
      m = *ptr;
      m->initialized = (!m->init || m->init() == 0);
      if (!m->initialized)
	{
	  DPRINTF(E_FATAL, L_HTTPD, "%s init failed\n", m->name);
	  return -1;
	}

      if (modules_handlers_set(m->handlers) != 0)
	{
	  DPRINTF(E_FATAL, L_HTTPD, "%s handler configuration failed\n", m->name);
	  return -1;
	}
    }

  return 0;
}

static void
modules_deinit(void)
{
  struct httpd_module **ptr;
  struct httpd_module *m;

  for (ptr = httpd_modules; *ptr; ptr++)
    {
      m = *ptr;
      if (m->initialized && m->deinit)
	m->deinit();

      modules_handlers_unset(m->handlers);
    }
}

static struct httpd_module *
modules_search(const char *path)
{
  struct httpd_module **ptr;
  struct httpd_module *m;
  const char **test;
  bool is_found = false;

  for (ptr = httpd_modules; *ptr; ptr++)
    {
      m = *ptr;
      if (!m->subpaths || !m->request)
	continue;

      for (test = m->subpaths; *test && !is_found; test++)
	is_found = (strncmp(path, *test, strlen(*test)) == 0);

      for (test = m->fullpaths; *test && !is_found; test++)
	is_found = (strcmp(path, *test) == 0);

      if (is_found)
	return m;
    }

  return NULL;
}


/* --------------------------- REQUEST HELPERS ------------------------------ */

static void
cors_headers_add(struct httpd_request *hreq, const char *allow_origin)
{
  if (allow_origin)
    httpd_header_add(hreq->out_headers, "Access-Control-Allow-Origin", httpd_allow_origin);

  httpd_header_add(hreq->out_headers, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  httpd_header_add(hreq->out_headers, "Access-Control-Allow-Headers", "authorization");
}

static int
handle_cors_preflight(struct httpd_request *hreq, const char *allow_origin)
{
  bool is_cors_preflight;

  is_cors_preflight = ( hreq->method == HTTPD_METHOD_OPTIONS && hreq->in_headers && allow_origin &&
                        httpd_header_find(hreq->in_headers, "Origin") &&
                        httpd_header_find(hreq->in_headers, "Access-Control-Request-Method") );
  if (!is_cors_preflight)
    return -1;

  cors_headers_add(hreq, allow_origin);

  // In this case there is no reason to go through httpd_send_reply
  httpd_backend_reply_send(hreq->backend, HTTP_OK, "OK", NULL);
  httpd_request_free(hreq);
  return 0;
}

void
httpd_request_handler_set(struct httpd_request *hreq)
{
  struct httpd_uri_map *map;
  int ret;

  // Path with e.g. /api -> JSON module
  hreq->module = modules_search(hreq->path);
  if (!hreq->module)
    {
      return;
    }

  for (map = hreq->module->handlers; map->handler; map++)
    {
      // Check if handler supports the current http request method
      if (map->method && hreq->method && !(map->method & hreq->method))
	continue;

      ret = regexec(map->preg, hreq->path, 0, NULL, 0);
      if (ret != 0)
	continue;

      hreq->handler = map->handler;
      break;
    }
}

void
httpd_redirect_to(struct httpd_request *hreq, const char *path)
{
  httpd_header_add(hreq->out_headers, "Location", path);

  httpd_send_reply(hreq, HTTP_MOVETEMP, "Moved", NULL, HTTPD_SEND_NO_GZIP);
}

/*
 * Checks if the given ETag matches the "If-None-Match" request header
 *
 * If the request does not contains a "If-None-Match" header value or the value
 * does not match the given ETag, it returns false (modified) and adds the
 * "Cache-Control" and "ETag" headers to the response header.
 *
 * @param req The request with request and response headers
 * @param etag The valid ETag for the requested resource
 * @return True if the given ETag matches the request-header-value "If-None-Match", otherwise false
 */
bool
httpd_request_etag_matches(struct httpd_request *hreq, const char *etag)
{
  const char *none_match;

  none_match = httpd_header_find(hreq->in_headers, "If-None-Match");

  // Return not modified, if given timestamp matches "If-Modified-Since" request header
  if (none_match && (strcasecmp(etag, none_match) == 0))
    return true;

  // Add cache headers to allow client side caching
  httpd_header_add(hreq->out_headers, "Cache-Control", "private,no-cache,max-age=0");
  httpd_header_add(hreq->out_headers, "ETag", etag);

  return false;
}

/*
 * Checks if the given timestamp matches the "If-Modified-Since" request header
 *
 * If the request does not contains a "If-Modified-Since" header value or the value
 * does not match the given timestamp, it returns false (modified) and adds the
 * "Cache-Control" and "Last-Modified" headers to the response header.
 *
 * @param req The request with request and response headers
 * @param mtime The last modified timestamp for the requested resource
 * @return True if the given timestamp matches the request-header-value "If-Modified-Since", otherwise false
 */
bool
httpd_request_not_modified_since(struct httpd_request *hreq, time_t mtime)
{
  char last_modified[1000];
  const char *modified_since;
  struct tm timebuf;

  modified_since = httpd_header_find(hreq->in_headers, "If-Modified-Since");

  strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S %Z", gmtime_r(&mtime, &timebuf));

  // Return not modified, if given timestamp matches "If-Modified-Since" request header
  if (modified_since && (strcasecmp(last_modified, modified_since) == 0))
    return true;

  // Add cache headers to allow client side caching
  httpd_header_add(hreq->out_headers, "Cache-Control", "private,no-cache,max-age=0");
  httpd_header_add(hreq->out_headers, "Last-Modified", last_modified);

  return false;
}

void
httpd_response_not_cachable(struct httpd_request *hreq)
{
  // Remove potentially set cache control headers
  httpd_header_remove(hreq->out_headers, "Cache-Control");
  httpd_header_remove(hreq->out_headers, "Last-Modified");
  httpd_header_remove(hreq->out_headers, "ETag");

  // Tell clients that they are not allowed to cache this response
  httpd_header_add(hreq->out_headers, "Cache-Control", "no-store");
}

static void
serve_file(struct httpd_request *hreq)
{
  char *ext;
  char path[PATH_MAX];
  char deref[PATH_MAX];
  char *ctype;
  struct evbuffer *evbuf;
  struct stat sb;
  int fd;
  int i;
  uint8_t buf[4096];
  bool slashed;
  int ret;

  /* Check authentication */
  if (!httpd_admin_check_auth(hreq))
    return;

  ret = snprintf(path, sizeof(path), "%s%s", webroot_directory, hreq->path);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_HTTPD, "Request exceeds PATH_MAX: %s\n", hreq->uri);

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (!realpath(path, deref))
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not dereference %s: %s\n", path, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (strlen(deref) >= PATH_MAX)
    {
      DPRINTF(E_LOG, L_HTTPD, "Dereferenced path exceeds PATH_MAX: %s\n", path);

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  ret = lstat(deref, &sb);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_HTTPD, "Could not lstat() %s: %s\n", deref, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (S_ISDIR(sb.st_mode))
    {
      slashed = (path[strlen(path) - 1] == '/');
      strncat(path, ((slashed) ? "index.html" : "/index.html"), sizeof(path) - strlen(path) - 1);

      if (!realpath(path, deref))
        {
          DPRINTF(E_LOG, L_HTTPD, "Could not dereference %s: %s\n", path, strerror(errno));

          httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
          return;
        }

      if (strlen(deref) >= PATH_MAX)
        {
          DPRINTF(E_LOG, L_HTTPD, "Dereferenced path exceeds PATH_MAX: %s\n", path);

          httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");

          return;
        }

      ret = stat(deref, &sb);
      if (ret < 0)
        {
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", path, strerror(errno));
	  httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
	  return;
	}
    }

  if (path_is_legal(deref) != 0)
    {
      DPRINTF(E_WARN, L_HTTPD, "Access to file outside the web root dir forbidden: %s\n", deref);

      httpd_send_error(hreq, 403, "Forbidden");

      return;
    }

  if (httpd_request_not_modified_since(hreq, sb.st_mtime))
    {
      httpd_send_reply(hreq, HTTP_NOTMODIFIED, NULL, NULL, HTTPD_SEND_NO_GZIP);
      return;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not create evbuffer\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal error");
      return;
    }

  fd = open(deref, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", deref, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      evbuffer_free(evbuf);
      return;
    }

  ret = evbuffer_expand(evbuf, sb.st_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Out of memory for htdocs-file\n");
      goto out_fail;
    }

  while ((ret = read(fd, buf, sizeof(buf))) > 0)
    evbuffer_add(evbuf, buf, ret);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not read file into evbuffer\n");
      goto out_fail;
    }

  ctype = "application/octet-stream";
  ext = strrchr(path, '.');
  if (ext)
    {
      for (i = 0; ext2ctype[i].ext; i++)
	{
	  if (strcmp(ext, ext2ctype[i].ext) == 0)
	    {
	      ctype = ext2ctype[i].ctype;
	      break;
	    }
	}
    }

  httpd_header_add(hreq->out_headers, "Content-Type", ctype);

  httpd_send_reply(hreq, HTTP_OK, "OK", evbuf, HTTPD_SEND_NO_GZIP);

  evbuffer_free(evbuf);
  close(fd);
  return;

 out_fail:
  httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal error");
  evbuffer_free(evbuf);
  close(fd);
}


/* ---------------------------- STREAM HANDLING ----------------------------- */

static void
stream_end(struct stream_ctx *st, int failed)
{
  httpd_request_closecb_set(st->hreq, NULL, NULL);

  if (!failed)
    httpd_send_reply_end(st->hreq);

  evbuffer_free(st->evbuf);
  event_free(st->ev);

  if (st->xcode)
    transcode_cleanup(&st->xcode);
  else
    {
      free(st->buf);
      close(st->fd);
    }

  free(st);
}

static void
stream_end_register(struct stream_ctx *st)
{
  if (!st->marked
      && (st->stream_size > ((st->size * 50) / 100))
      && (st->offset > ((st->size * 80) / 100)))
    {
      st->marked = 1;
      worker_execute(playcount_inc_cb, &st->id, sizeof(int), 0);
#ifdef LASTFM
      worker_execute(scrobble_cb, &st->id, sizeof(int), 1);
#endif
    }
}

static void
stream_chunk_resched_cb(httpd_connection *conn, void *arg)
{
  struct stream_ctx *st;
  struct timeval tv;
  int ret;

  st = (struct stream_ctx *)arg;

  evutil_timerclear(&tv);
  ret = event_add(st->ev, &tv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not re-add one-shot event for streaming\n");

      stream_end(st, 0);
    }
}

static void
stream_chunk_xcode_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st;
  struct timeval tv;
  int xcoded;
  int ret;

  st = (struct stream_ctx *)arg;

  xcoded = transcode(st->evbuf, NULL, st->xcode, STREAM_CHUNK_SIZE);
  if (xcoded <= 0)
    {
      if (xcoded == 0)
	DPRINTF(E_INFO, L_HTTPD, "Done streaming transcoded file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Transcoding error, file id %d\n", st->id);

      stream_end(st, 0);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Got %d bytes from transcode; streaming file id %d\n", xcoded, st->id);

  /* Consume transcoded data until we meet start_offset */
  if (st->start_offset > st->offset)
    {
      ret = st->start_offset - st->offset;

      if (ret < xcoded)
	{
	  evbuffer_drain(st->evbuf, ret);
	  st->offset += ret;

	  ret = xcoded - ret;
	}
      else
	{
	  evbuffer_drain(st->evbuf, xcoded);
	  st->offset += xcoded;

	  goto consume;
	}
    }
  else
    ret = xcoded;

  httpd_send_reply_chunk(st->hreq, st->evbuf, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_end_register(st);

  return;

 consume: /* reschedule immediately - consume up to start_offset */
  evutil_timerclear(&tv);
  ret = event_add(st->ev, &tv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not re-add one-shot event for streaming (xcode)\n");

      stream_end(st, 0);
      return;
    }
}

static void
stream_chunk_raw_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st;
  size_t chunk_size;
  int ret;

  st = (struct stream_ctx *)arg;

  if (st->end_offset && (st->offset > st->end_offset))
    {
      stream_end(st, 0);
      return;
    }

  if (st->end_offset && ((st->offset + STREAM_CHUNK_SIZE) > (st->end_offset + 1)))
    chunk_size = st->end_offset + 1 - st->offset;
  else
    chunk_size = STREAM_CHUNK_SIZE;  

  ret = read(st->fd, st->buf, chunk_size);
  if (ret <= 0)
    {
      if (ret == 0)
	DPRINTF(E_INFO, L_HTTPD, "Done streaming file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Streaming error, file id %d\n", st->id);

      stream_end(st, 0);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Read %d bytes; streaming file id %d\n", ret, st->id);

  evbuffer_add(st->evbuf, st->buf, ret);

  httpd_send_reply_chunk(st->hreq, st->evbuf, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_end_register(st);
}

static void
stream_fail_cb(httpd_connection *conn, void *arg)
{
  struct stream_ctx *st;

  st = (struct stream_ctx *)arg;

  DPRINTF(E_WARN, L_HTTPD, "Connection failed; stopping streaming of file ID %d\n", st->id);

  /* Stop streaming */
  event_del(st->ev);

  stream_end(st, 1);
}


/* ---------------------------- MAIN HTTPD THREAD --------------------------- */

static void
request_cb(struct httpd_request *hreq, void *arg)
{
  // Did we get a CORS preflight request?
  if (handle_cors_preflight(hreq, httpd_allow_origin) == 0)
    {
      return;
    }

  if (!hreq->uri || !hreq->uri_parsed)
    {
      DPRINTF(E_WARN, L_HTTPD, "Invalid URI in request: '%s'\n", hreq->uri);
      httpd_redirect_to(hreq, "/");
      return;
    }
  else if (!hreq->path)
    {
      DPRINTF(E_WARN, L_HTTPD, "Invalid path in request: '%s'\n", hreq->uri);
      httpd_redirect_to(hreq, "/");
      return;
    }

  httpd_request_handler_set(hreq);
  if (hreq->module)
    {
      DPRINTF(E_DBG, hreq->module->logdomain, "%s request: '%s' (thread %ld)\n", hreq->module->name, hreq->uri, syscall(SYS_gettid));
      hreq->module->request(hreq);
    }
  else
    {
      // Serve web interface files
      DPRINTF(E_DBG, L_HTTPD, "HTTP request: '%s'\n", hreq->uri);
      serve_file(hreq);
    }
}


/* ------------------------------- HTTPD API -------------------------------- */

/* Thread: httpd */
void
httpd_stream_file(struct httpd_request *hreq, int id)
{
  struct media_quality quality = { HTTPD_STREAM_SAMPLE_RATE, HTTPD_STREAM_BPS, HTTPD_STREAM_CHANNELS, 0 };
  struct media_file_info *mfi;
  struct stream_ctx *st;
  void (*stream_cb)(int fd, short event, void *arg);
  struct stat sb;
  struct timeval tv;
  struct event_base *evbase;
  const char *param;
  const char *param_end;
  const char *client_codecs;
  char buf[64];
  int64_t offset;
  int64_t end_offset;
  off_t pos;
  int transcode;
  int ret;

  offset = 0;
  end_offset = 0;

  param = httpd_header_find(hreq->in_headers, "Range");
  if (param)
    {
      DPRINTF(E_DBG, L_HTTPD, "Found Range header: %s\n", param);

      /* Start offset */
      ret = safe_atoi64(param + strlen("bytes="), &offset);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Invalid start offset, will stream whole file (%s)\n", param);
	  offset = 0;
	}
      /* End offset, if any */
      else
	{
	  param_end = strchr(param, '-');
	  if (param_end && (strlen(param_end) > 1))
	    {
	      ret = safe_atoi64(param_end + 1, &end_offset);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_HTTPD, "Invalid end offset, will stream to end of file (%s)\n", param);
		  end_offset = 0;
		}

	      if (end_offset < offset)
		{
		  DPRINTF(E_LOG, L_HTTPD, "End offset < start offset, will stream to end of file (%" PRIi64 " < %" PRIi64 ")\n", end_offset, offset);
		  end_offset = 0;
		}
	    }
	}
    }

  mfi = db_file_fetch_byid(id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_HTTPD, "Item %d not found\n", id);

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (mfi->data_kind != DATA_KIND_FILE)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not serve '%s' to client, not a file\n", mfi->path);

      httpd_send_error(hreq, 500, "Cannot stream non-file content");
      goto out_free_mfi;
    }

  st = (struct stream_ctx *)malloc(sizeof(struct stream_ctx));
  if (!st)
    {
      DPRINTF(E_LOG, L_HTTPD, "Out of memory for struct stream_ctx\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto out_free_mfi;
    }
  memset(st, 0, sizeof(struct stream_ctx));
  st->fd = -1;

  client_codecs = httpd_header_find(hreq->in_headers, "Accept-Codecs");

  transcode = transcode_needed(hreq->user_agent, client_codecs, mfi->codectype);

  if (transcode)
    {
      DPRINTF(E_INFO, L_HTTPD, "Preparing to transcode %s\n", mfi->path);

      stream_cb = stream_chunk_xcode_cb;

      st->xcode = transcode_setup(XCODE_PCM16_HEADER, &quality, mfi->data_kind, mfi->path, mfi->song_length, &st->size);
      if (!st->xcode)
	{
	  DPRINTF(E_WARN, L_HTTPD, "Transcoding setup failed, aborting streaming\n");

	  httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
	  goto out_free_st;
	}

      if (!httpd_header_find(hreq->out_headers, "Content-Type"))
	httpd_header_add(hreq->out_headers, "Content-Type", "audio/wav");
    }
  else
    {
      /* Stream the raw file */
      DPRINTF(E_INFO, L_HTTPD, "Preparing to stream %s\n", mfi->path);

      st->buf = (uint8_t *)malloc(STREAM_CHUNK_SIZE);
      if (!st->buf)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Out of memory for raw streaming buffer\n");

	  httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
	  goto out_free_st;
	}

      stream_cb = stream_chunk_raw_cb;

      st->fd = open(mfi->path, O_RDONLY);
      if (st->fd < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", mfi->path, strerror(errno));

	  httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
	  goto out_cleanup;
	}

      ret = stat(mfi->path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", mfi->path, strerror(errno));

	  httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
	  goto out_cleanup;
	}
      st->size = sb.st_size;

      pos = lseek(st->fd, offset, SEEK_SET);
      if (pos == (off_t) -1)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not seek into %s: %s\n", mfi->path, strerror(errno));

	  httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
	  goto out_cleanup;
	}
      st->offset = offset;
      st->end_offset = end_offset;

      /* Content-Type for video files is different than for audio files
       * and overrides whatever may have been set previously, like
       * application/x-dmap-tagged when we're speaking DAAP.
       */
      if (mfi->has_video)
	{
	  /* Front Row and others expect video/<type> */
	  ret = snprintf(buf, sizeof(buf), "video/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    {
	      httpd_header_remove(hreq->out_headers, "Content-Type");
	      httpd_header_add(hreq->out_headers, "Content-Type", buf);
	    }
	}
      /* If no Content-Type has been set and we're streaming audio, add a proper
       * Content-Type for the file we're streaming. Remember DAAP streams audio
       * with application/x-dmap-tagged as the Content-Type (ugh!).
       */
      else if (!httpd_header_find(hreq->out_headers, "Content-Type") && mfi->type)
	{
	  ret = snprintf(buf, sizeof(buf), "audio/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    httpd_header_add(hreq->out_headers, "Content-Type", buf);
	}
    }

  st->evbuf = evbuffer_new();
  if (!st->evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not allocate an evbuffer for streaming\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto out_cleanup;
    }

  ret = evbuffer_expand(st->evbuf, STREAM_CHUNK_SIZE);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not expand evbuffer for streaming\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto out_cleanup;
    }

  evbase = httpd_request_evbase_get(hreq);

  st->ev = event_new(evbase, -1, EV_TIMEOUT, stream_cb, st);
  evutil_timerclear(&tv);
  if (!st->ev || (event_add(st->ev, &tv) < 0))
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not add one-shot event for streaming\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto out_cleanup;
    }

  st->id = mfi->id;
  st->start_offset = offset;
  st->stream_size = st->size;
  st->hreq = hreq;

  if ((offset == 0) && (end_offset == 0))
    {
      /* If we are not decoding, send the Content-Length. We don't do
       * that if we are decoding because we can only guesstimate the
       * size in this case and the error margin is unknown and variable.
       */
      if (!transcode)
	{
	  ret = snprintf(buf, sizeof(buf), "%" PRIi64, (int64_t)st->size);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Length too large for buffer, dropping\n");
	  else
	    httpd_header_add(hreq->out_headers, "Content-Length", buf);
	}

      httpd_send_reply_start(hreq, HTTP_OK, "OK");
    }
  else
    {
      if (offset > 0)
	st->stream_size -= offset;
      if (end_offset > 0)
	st->stream_size -= (st->size - end_offset);

      DPRINTF(E_DBG, L_HTTPD, "Stream request with range %" PRIi64 "-%" PRIi64 "\n", offset, end_offset);

      ret = snprintf(buf, sizeof(buf), "bytes %" PRIi64 "-%" PRIi64 "/%" PRIi64,
		     offset, (end_offset) ? end_offset : (int64_t)st->size, (int64_t)st->size);
      if ((ret < 0) || (ret >= sizeof(buf)))
	DPRINTF(E_LOG, L_HTTPD, "Content-Range too large for buffer, dropping\n");
      else
	httpd_header_add(hreq->out_headers, "Content-Range", buf);

      ret = snprintf(buf, sizeof(buf), "%" PRIi64, ((end_offset) ? end_offset + 1 : (int64_t)st->size) - offset);
      if ((ret < 0) || (ret >= sizeof(buf)))
	DPRINTF(E_LOG, L_HTTPD, "Content-Length too large for buffer, dropping\n");
      else
	httpd_header_add(hreq->out_headers, "Content-Length", buf);

      httpd_send_reply_start(hreq, 206, "Partial Content");
    }

#ifdef HAVE_POSIX_FADVISE
  if (!transcode)
    {
      /* Hint the OS */
      if ( (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_WILLNEED)) != 0 ||
           (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_SEQUENTIAL)) != 0 ||
           (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_NOREUSE)) != 0 )
	DPRINTF(E_DBG, L_HTTPD, "posix_fadvise() failed with error %d\n", ret);
    }
#endif

  httpd_request_closecb_set(hreq, stream_fail_cb, st);

  DPRINTF(E_INFO, L_HTTPD, "Kicking off streaming for %s\n", mfi->path);

  free_mfi(mfi, 0);

  return;

 out_cleanup:
  if (st->evbuf)
    evbuffer_free(st->evbuf);
  if (st->xcode)
    transcode_cleanup(&st->xcode);
  if (st->buf)
    free(st->buf);
  if (st->fd > 0)
    close(st->fd);
 out_free_st:
  free(st);
 out_free_mfi:
  free_mfi(mfi, 0);
}

struct evbuffer *
httpd_gzip_deflate(struct evbuffer *in)
{
  struct evbuffer *out;
  struct evbuffer_iovec iovec[1];
  z_stream strm;
  int ret;

  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;

  // Just to keep Coverity from complaining about uninitialized values
  strm.total_in = 0;
  strm.total_out = 0;

  // Set up a gzip stream (the "+ 16" in 15 + 16), instead of a zlib stream (default)
  ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK)
    {
      DPRINTF(E_LOG, L_HTTPD, "zlib setup failed: %s\n", zError(ret));
      return NULL;
    }

  strm.next_in = evbuffer_pullup(in, -1);
  strm.avail_in = evbuffer_get_length(in);

  out = evbuffer_new();
  if (!out)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not allocate evbuffer for gzipped reply\n");
      goto out_deflate_end;
    }

  // We use this to avoid a memcpy. The 512 is an arbitrary padding to make sure
  // there is enough space, even if the compressed output should be slightly
  // larger than input (could happen with small inputs).
  ret = evbuffer_reserve_space(out, strm.avail_in + 512, iovec, 1);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not reserve memory for gzipped reply\n");
      goto out_evbuf_free;
    }

  strm.next_out = iovec[0].iov_base;
  strm.avail_out = iovec[0].iov_len;

  ret = deflate(&strm, Z_FINISH);
  if (ret != Z_STREAM_END)
    goto out_evbuf_free;

  iovec[0].iov_len -= strm.avail_out;

  evbuffer_commit_space(out, iovec, 1);
  deflateEnd(&strm);

  return out;

 out_evbuf_free:
  evbuffer_free(out);

 out_deflate_end:
  deflateEnd(&strm);

  return NULL;
}

void
httpd_send_reply(struct httpd_request *hreq, int code, const char *reason, struct evbuffer *evbuf, enum httpd_send_flags flags)
{
  struct evbuffer *gzbuf;
  const char *param;
  int do_gzip;

  if (!hreq->backend)
    return;

  do_gzip = ( (!(flags & HTTPD_SEND_NO_GZIP)) &&
              evbuf && (evbuffer_get_length(evbuf) > 512) &&
              (param = httpd_header_find(hreq->in_headers, "Accept-Encoding")) &&
              (strstr(param, "gzip") || strstr(param, "*"))
            );

  cors_headers_add(hreq, httpd_allow_origin);

  if (do_gzip && (gzbuf = httpd_gzip_deflate(evbuf)))
    {
      DPRINTF(E_DBG, L_HTTPD, "Gzipping response\n");

      httpd_header_add(hreq->out_headers, "Content-Encoding", "gzip");
      httpd_backend_reply_send(hreq->backend, code, reason, gzbuf);
      evbuffer_free(gzbuf);

      // Drain original buffer, as would be after evhttp_send_reply()
      evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
    }
  else
    {
      httpd_backend_reply_send(hreq->backend, code, reason, evbuf);
    }

  httpd_request_free(hreq);
}

void
httpd_send_reply_start(struct httpd_request *hreq, int code, const char *reason)
{
  cors_headers_add(hreq, httpd_allow_origin);

  httpd_backend_reply_start_send(hreq->backend, code, reason);
}

void
httpd_send_reply_chunk(struct httpd_request *hreq, struct evbuffer *evbuf, httpd_connection_chunkcb cb, void *arg)
{
  httpd_backend_reply_chunk_send(hreq->backend, evbuf, cb, arg);
}

void
httpd_send_reply_end(struct httpd_request *hreq)
{
  httpd_backend_reply_end_send(hreq->backend);
  httpd_request_free(hreq);
}

// This is a modified version of evhttp_send_error (credit libevent)
void
httpd_send_error(struct httpd_request *hreq, int error, const char *reason)
{
  struct evbuffer *evbuf;

  httpd_headers_clear(hreq->out_headers);

  cors_headers_add(hreq, httpd_allow_origin);

  httpd_header_add(hreq->out_headers, "Content-Type", "text/html");
  httpd_header_add(hreq->out_headers, "Connection", "close");

  evbuf = evbuffer_new();
  if (!evbuf)
    DPRINTF(E_LOG, L_HTTPD, "Could not allocate evbuffer for error page\n");
  else
    evbuffer_add_printf(evbuf, ERR_PAGE, error, reason, reason);

  httpd_backend_reply_send(hreq->backend, error, reason, evbuf);

  if (evbuf)
    evbuffer_free(evbuf);

  httpd_request_free(hreq);
}

bool
httpd_admin_check_auth(struct httpd_request *hreq)
{
  const char *passwd;
  int ret;

  if (net_peer_address_is_trusted(hreq->peer_address))
    return true;

  passwd = cfg_getstr(cfg_getsec(cfg, "general"), "admin_password");
  if (!passwd)
    {
      DPRINTF(E_LOG, L_HTTPD, "Web interface request to '%s' denied: No password set in the config\n", hreq->uri);

      httpd_send_error(hreq, 403, "Forbidden");
      return false;
    }

  DPRINTF(E_DBG, L_HTTPD, "Checking web interface authentication\n");

  ret = httpd_basic_auth(hreq, "admin", passwd, PACKAGE " web interface");
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Web interface request to '%s' denied: Incorrect password\n", hreq->uri);

      // httpd_basic_auth has sent a reply
      return false;
    }

  DPRINTF(E_DBG, L_HTTPD, "Authentication successful\n");

  return true;
}

int
httpd_basic_auth(struct httpd_request *hreq, const char *user, const char *passwd, const char *realm)
{
  struct evbuffer *evbuf;
  char header[256];
  const char *auth;
  char *authuser;
  char *authpwd;
  int ret;

  auth = httpd_header_find(hreq->in_headers, "Authorization");
  if (!auth)
    {
      DPRINTF(E_DBG, L_HTTPD, "No Authorization header\n");

      goto need_auth;
    }

  if (strncmp(auth, "Basic ", strlen("Basic ")) != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Bad Authentication header\n");

      goto need_auth;
    }

  auth += strlen("Basic ");

  authuser = (char *)b64_decode(NULL, auth);
  if (!authuser)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not decode Authentication header\n");

      goto need_auth;
    }

  authpwd = strchr(authuser, ':');
  if (!authpwd)
    {
      DPRINTF(E_LOG, L_HTTPD, "Malformed Authentication header\n");

      free(authuser);
      goto need_auth;
    }

  *authpwd = '\0';
  authpwd++;

  if (user)
    {
      if (strcmp(user, authuser) != 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Username mismatch\n");

	  free(authuser);
	  goto need_auth;
	}
    }

  if (strcmp(passwd, authpwd) != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Bad password\n");

      free(authuser);
      goto need_auth;
    }

  free(authuser);

  return 0;

 need_auth:
  ret = snprintf(header, sizeof(header), "Basic realm=\"%s\"", realm);
  if ((ret < 0) || (ret >= sizeof(header)))
    {
      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  httpd_header_add(hreq->out_headers, "WWW-Authenticate", header);

  evbuffer_add_printf(evbuf, ERR_PAGE, 401, "Unauthorized", "Authorization required");

  httpd_send_reply(hreq, 401, "Unauthorized", evbuf, HTTPD_SEND_NO_GZIP);

  evbuffer_free(evbuf);

  return -1;
}

static void
thread_init_cb(struct evthr *thr, void *shared)
{
  int ret;

  thread_setname(pthread_self(), "httpd");

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Error: DB init failed\n");
      thr->err = EIO;
      return;
    }

  thr->server = httpd_server_new(thr->evbase, httpd_port, request_cb, NULL);
  if (!thr->server)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create HTTP server on port %d (server already running?)\n", httpd_port);
      thr->err = EIO;
      return;
    }

  // For CORS headers
  httpd_server_allow_origin_set(thr->server, httpd_allow_origin);
}

static void
thread_exit_cb(struct evthr *thr, void *shared)
{
  httpd_server_free(thr->server);

  db_perthread_deinit();
}

/* Thread: main */
int
httpd_init(const char *webroot)
{
  struct stat sb;
  int ret;

  DPRINTF(E_DBG, L_HTTPD, "Starting web server with root directory '%s'\n", webroot);
  ret = stat(webroot, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not stat() web root directory '%s': %s\n", webroot, strerror(errno));
      return -1;
    }
  if (!S_ISDIR(sb.st_mode))
    {
      DPRINTF(E_LOG, L_HTTPD, "Web root directory '%s' is not a directory\n", webroot);
      return -1;
    }
  if (!realpath(webroot, webroot_directory))
    {
      DPRINTF(E_LOG, L_HTTPD, "Web root directory '%s' could not be dereferenced: %s\n", webroot, strerror(errno));
      return -1;
    }

  // Read config
  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");
  httpd_allow_origin = cfg_getstr(cfg_getsec(cfg, "general"), "allow_origin");
  if (strlen(httpd_allow_origin) == 0)
    httpd_allow_origin = NULL;

  // Prepare modules, e.g. httpd_daap
  ret = modules_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Modules init failed\n");
      goto error;
    }

#ifdef HAVE_LIBWEBSOCKETS
  ret = websocket_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Websocket init failed\n");
      goto error;
    }
#endif

  httpd_threadpool = evthr_pool_wexit_new(THREADPOOL_NTHREADS, thread_init_cb, thread_exit_cb, NULL);
  if (!httpd_threadpool)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not create httpd thread pool\n");
      goto error;
    }

  ret = evthr_pool_start(httpd_threadpool);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not spawn worker threads\n");
      goto error;
    }

  return 0;

 error:
  httpd_deinit();
  return -1;
}

/* Thread: main */
void
httpd_deinit(void)
{
  // Give modules a chance to hang up connections nicely
  modules_deinit();

#ifdef HAVE_LIBWEBSOCKETS
  websocket_deinit();
#endif

  evthr_pool_stop(httpd_threadpool);
  evthr_pool_free(httpd_threadpool);
}
