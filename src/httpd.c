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
#include <time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>

#include <event2/event.h>

#include <regex.h>
#include <zlib.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "worker.h"
#include "evthr.h"
#include "httpd.h"
#include "httpd_internal.h"
#include "transcode.h"
#include "cache.h"
#include "listener.h"
#include "player.h"
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
  enum transcode_profile profile;
  char *ctype;
};

struct stream_ctx {
  struct httpd_request *hreq;
  struct event *ev;
  int id;
  int fd;
  off_t size;
  off_t stream_size;
  off_t offset;
  off_t start_offset;
  off_t end_offset;
  bool no_register_playback;
  struct transcode_ctx *xcode;
};

static const struct content_type_map ext2ctype[] =
  {
    { ".html", XCODE_NONE,      "text/html; charset=utf-8" },
    { ".xml",  XCODE_NONE,      "text/xml; charset=utf-8" },
    { ".css",  XCODE_NONE,      "text/css; charset=utf-8" },
    { ".txt",  XCODE_NONE,      "text/plain; charset=utf-8" },
    { ".js",   XCODE_NONE,      "application/javascript; charset=utf-8" },
    { ".gif",  XCODE_NONE,      "image/gif" },
    { ".ico",  XCODE_NONE,      "image/x-ico" },
    { ".png",  XCODE_PNG,       "image/png" },
    { ".jpg",  XCODE_JPEG,      "image/jpeg" },
    { ".mp3",  XCODE_MP3,       "audio/mpeg" },
    { ".m4a",  XCODE_MP4_ALAC,  "audio/mp4" },
    { ".wav",  XCODE_WAV,       "audio/wav" },
    { NULL,    XCODE_NONE,      NULL }
  };

static char webroot_directory[PATH_MAX];

static const char *httpd_allow_origin;
static int httpd_port;


// The server is designed around a single thread listening for requests. When
// received, the request is passed to a thread from the worker pool, where a
// handler will process it and prepare a response for the httpd thread to send
// back. The idea is that the httpd thread never blocks. The handler in the
// worker thread can block, but shouldn't hold the thread if it is a long-
// running request (e.g. a long poll), because then we can run out of worker
// threads. The handler should use events to avoid this. Handlers, that are non-
// blocking and where the response must not be delayed can use
// HTTPD_HANDLER_REALTIME, then the httpd thread calls it directly (sync)
// instead of the async worker. In short, you shouldn't need to increase the
// below.
#define THREADPOOL_NTHREADS 1

static struct evthr_pool *httpd_threadpool;


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

static const char *
content_type_from_ext(const char *ext)
{
  int i;

  if (!ext)
    return NULL;

  for (i = 0; ext2ctype[i].ext; i++)
    {
      if (strcmp(ext, ext2ctype[i].ext) == 0)
	return ext2ctype[i].ctype;
    }

  return NULL;
}

static const char *
content_type_from_profile(enum transcode_profile profile)
{
  int i;

  if (profile == XCODE_NONE)
    return NULL;

  for (i = 0; ext2ctype[i].ext; i++)
    {
      if (profile == ext2ctype[i].profile)
	return ext2ctype[i].ctype;
    }

  return NULL;
}

static int
basic_auth_cred_extract(char **user, char **pwd, const char *auth)
{
  char *decoded = NULL;
  regex_t preg = { 0 };
  regmatch_t matchptr[3]; // Room for entire string, username substring and password substring
  int ret;

  decoded = (char *)b64_decode(NULL, auth);
  if (!decoded)
    goto error;

  // Apple Music gives is "(dt:1):password", which we need to support even if it
  // isn't according to the basic auth RFC that says the username cannot include
  // a colon
  ret = regcomp(&preg, "(\\(.*?\\)|[^:]*):(.*)", REG_EXTENDED);
  if (ret != 0)
    goto error;

  ret = regexec(&preg, decoded, ARRAY_SIZE(matchptr), matchptr, 0);
  if (ret != 0 || matchptr[1].rm_so == -1 || matchptr[2].rm_so == -1)
    goto error;

  *user = strndup(decoded + matchptr[1].rm_so, matchptr[1].rm_eo - matchptr[1].rm_so);
  *pwd = strndup(decoded + matchptr[2].rm_so, matchptr[2].rm_eo - matchptr[2].rm_so);

  free(decoded);
  return 0;

 error:
  free(decoded);
  return -1;
}


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
      if (!m->request)
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

static bool
is_cors_preflight(struct httpd_request *hreq, const char *allow_origin)
{
  return ( hreq->method == HTTPD_METHOD_OPTIONS && hreq->in_headers && allow_origin &&
           httpd_header_find(hreq->in_headers, "Origin") &&
           httpd_header_find(hreq->in_headers, "Access-Control-Request-Method") );
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
      hreq->is_async = !(map->flags & HTTPD_HANDLER_REALTIME);
      break;
    }
}

void
httpd_redirect_to(struct httpd_request *hreq, const char *path)
{
  httpd_header_add(hreq->out_headers, "Location", path);

  httpd_send_reply(hreq, HTTP_MOVETEMP, "Moved", HTTPD_SEND_NO_GZIP);
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
  char path[PATH_MAX];
  char deref[PATH_MAX];
  const char *ctype;
  struct stat sb;
  int fd;
  uint8_t buf[4096];
  bool slashed;
  int ret;

  if (!httpd_request_is_authorized(hreq))
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

      httpd_send_error(hreq, HTTP_FORBIDDEN, "Forbidden");

      return;
    }

  if (httpd_request_not_modified_since(hreq, sb.st_mtime))
    {
      httpd_send_reply(hreq, HTTP_NOTMODIFIED, NULL, HTTPD_SEND_NO_GZIP);
      return;
    }

  fd = open(deref, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", deref, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      return;
    }

  ret = evbuffer_expand(hreq->out_body, sb.st_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Out of memory for htdocs-file\n");
      goto out_fail;
    }

  while ((ret = read(fd, buf, sizeof(buf))) > 0)
    evbuffer_add(hreq->out_body, buf, ret);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not read file into evbuffer\n");
      goto out_fail;
    }

  ctype = content_type_from_ext(strrchr(path, '.'));
  if (!ctype)
    ctype = "application/octet-stream";

  httpd_header_add(hreq->out_headers, "Content-Type", ctype);

  httpd_send_reply(hreq, HTTP_OK, "OK", HTTPD_SEND_NO_GZIP);

  close(fd);
  return;

 out_fail:
  httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal error");
  close(fd);
}

/* ---------------------------- STREAM HANDLING ----------------------------- */

// This will triggered in a httpd thread, but since the reading may be in a
// worker thread we just want to trigger the read loop
static void
stream_chunk_resched_cb(httpd_connection *conn, void *arg)
{
  struct stream_ctx *st = arg;

  // TODO not thread safe if st was freed in worker thread, but maybe not possible?
  event_active(st->ev, 0, 0);
}

static void
stream_free(struct stream_ctx *st)
{
  if (!st)
    return;

  if (st->ev)
    event_free(st->ev);
  if (st->fd >= 0)
    close(st->fd);

  transcode_cleanup(&st->xcode);
  free(st);
}

static void
stream_end(struct stream_ctx *st)
{
  DPRINTF(E_DBG, L_HTTPD, "Ending stream %d\n", st->id);

  httpd_send_reply_end(st->hreq);

  stream_free(st);
}

static void
stream_end_register(struct stream_ctx *st)
{
  if (!st->no_register_playback
      && (st->stream_size > ((st->size * 50) / 100))
      && (st->offset > ((st->size * 80) / 100)))
    {
      st->no_register_playback = true;
      worker_execute(playcount_inc_cb, &st->id, sizeof(int), 0);
#ifdef LASTFM
      worker_execute(scrobble_cb, &st->id, sizeof(int), 1);
#endif
    }
}

static struct stream_ctx *
stream_new(struct media_file_info *mfi, struct httpd_request *hreq, event_callback_fn stream_cb)
{
  struct stream_ctx *st;

  CHECK_NULL(L_HTTPD, st = calloc(1, sizeof(struct stream_ctx)));
  st->fd = -1;

  st->ev = event_new(hreq->evbase, -1, EV_PERSIST, stream_cb, st);
  if (!st->ev)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not create event for streaming\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto error;
    }

  event_active(st->ev, 0, 0);

  if (httpd_query_value_find(hreq->query, "no_register_playback"))
    st->no_register_playback = true;

  st->id = mfi->id;
  st->hreq = hreq;
  return st;

 error:
  stream_free(st);
  return NULL;
}

static struct stream_ctx *
stream_new_transcode(struct media_file_info *mfi, enum transcode_profile profile, struct httpd_request *hreq,
                     int64_t offset, int64_t end_offset, event_callback_fn stream_cb)
{
  struct transcode_decode_setup_args decode_args = { 0 };
  struct transcode_encode_setup_args encode_args = { 0 };
  struct media_quality quality = { 0 };
  struct evbuffer *prepared_header = NULL;
  struct stream_ctx *st;
  int cached;
  int ret;

  // We use source sample rate etc, but for MP3 we must set a bit rate
  quality.bit_rate = 1000 * cfg_getint(cfg_getsec(cfg, "streaming"), "bit_rate");

  st = stream_new(mfi, hreq, stream_cb);
  if (!st)
    {
      goto error;
    }

  if (profile == XCODE_MP4_ALAC)
    {
      CHECK_NULL(L_HTTPD, prepared_header = evbuffer_new());

      ret = cache_xcode_header_get(prepared_header, &cached, mfi->id, "mp4");
      if (ret < 0 || !cached) // Error or not found
	{
	  evbuffer_free(prepared_header);
	  prepared_header = NULL;
	}
    }

  decode_args.profile = profile;
  decode_args.is_http = (mfi->data_kind == DATA_KIND_HTTP);
  decode_args.path    = mfi->path;
  decode_args.len_ms  = mfi->song_length;
  encode_args.profile = profile;
  encode_args.quality = &quality;
  encode_args.prepared_header = prepared_header;

  st->xcode = transcode_setup(decode_args, encode_args);
  if (!st->xcode)
    {
      DPRINTF(E_WARN, L_HTTPD, "Transcoding setup failed, aborting streaming\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto error;
    }

  st->size = transcode_encode_query(st->xcode->encode_ctx, "estimated_size");
  if (st->size < 0)
    {
      DPRINTF(E_WARN, L_HTTPD, "Transcoding setup failed, could not determine estimated size\n");

      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      goto error;
    }

  st->stream_size = st->size - offset;
  if (end_offset > 0)
    st->stream_size -= (st->size - end_offset);

  st->start_offset = offset;

  if (prepared_header)
    evbuffer_free(prepared_header);
  return st;

 error:
  if (prepared_header)
    evbuffer_free(prepared_header);
  stream_free(st);
  return NULL;
}

static struct stream_ctx *
stream_new_raw(struct media_file_info *mfi, struct httpd_request *hreq, int64_t offset, int64_t end_offset, event_callback_fn stream_cb)
{
  struct stream_ctx *st;
  struct stat sb;
  off_t pos;
  int ret;

  st = stream_new(mfi, hreq, stream_cb);
  if (!st)
    {
      goto error;
    }

  st->fd = open(mfi->path, O_RDONLY);
  if (st->fd < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", mfi->path, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      goto error;
    }

  ret = stat(mfi->path, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", mfi->path, strerror(errno));

      httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
      goto error;
    }

  st->size = sb.st_size;

  st->stream_size = st->size - offset;
  if (end_offset > 0)
    st->stream_size -= (st->size - end_offset);

  st->start_offset = offset;
  st->offset = offset;
  st->end_offset = end_offset;

  pos = lseek(st->fd, offset, SEEK_SET);
  if (pos == (off_t) -1)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not seek into %s: %s\n", mfi->path, strerror(errno));

      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      goto error;
    }

  return st;

 error:
  stream_free(st);
  return NULL;
}

static void
stream_chunk_xcode_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st = arg;
  int xcoded;
  int ret;

  xcoded = transcode(st->hreq->out_body, NULL, st->xcode, STREAM_CHUNK_SIZE);
  if (xcoded <= 0)
    {
      if (xcoded == 0)
	DPRINTF(E_INFO, L_HTTPD, "Done streaming transcoded file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Transcoding error, file id %d\n", st->id);

      stream_end(st);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Got %d bytes from transcode; streaming file id %d\n", xcoded, st->id);

  // Consume transcoded data until we meet start_offset
  if (st->start_offset > st->offset)
    {
      ret = st->start_offset - st->offset;

      if (ret < xcoded)
	{
	  evbuffer_drain(st->hreq->out_body, ret);
	  st->offset += ret;

	  ret = xcoded - ret;
	}
      else
	{
	  evbuffer_drain(st->hreq->out_body, xcoded);
	  st->offset += xcoded;

	  // Reschedule immediately - consume up to start_offset
	  event_active(st->ev, 0, 0);
	  return;
	}
    }
  else
    ret = xcoded;

  httpd_send_reply_chunk(st->hreq, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_end_register(st);
}

static void
stream_chunk_raw_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st = arg;
  size_t chunk_size;
  int ret;

  if (st->end_offset && (st->offset > st->end_offset))
    {
      stream_end(st);
      return;
    }

  if (st->end_offset && ((st->offset + STREAM_CHUNK_SIZE) > (st->end_offset + 1)))
    chunk_size = st->end_offset + 1 - st->offset;
  else
    chunk_size = STREAM_CHUNK_SIZE;  

  ret = evbuffer_read(st->hreq->out_body, st->fd, chunk_size);
  if (ret <= 0)
    {
      if (ret == 0)
	DPRINTF(E_INFO, L_HTTPD, "Done streaming file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Streaming error, file id %d\n", st->id);

      stream_end(st);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Read %d bytes; streaming file id %d\n", ret, st->id);

  httpd_send_reply_chunk(st->hreq, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_end_register(st);
}

static void
stream_fail_cb(void *arg)
{
  struct stream_ctx *st = arg;

  stream_free(st);
}


/* -------------------------- SPEAKER/CACHE HANDLING ------------------------ */

// Thread: player (must not block)
static void
speaker_enum_cb(struct player_speaker_info *spk, void *arg)
{
  bool *want_mp4 = arg;

  *want_mp4 = *want_mp4 || (spk->format == MEDIA_FORMAT_ALAC && strcmp(spk->output_type, "RCP/SoundBridge") == 0);
}

// Thread: worker
static void
speaker_update_handler_cb(void *arg)
{
  const char *prefer_format = cfg_getstr(cfg_getsec(cfg, "library"), "prefer_format");
  bool want_mp4;

  want_mp4 = (prefer_format && (strcmp(prefer_format, "alac") == 0));
  if (!want_mp4)
    player_speaker_enumerate(speaker_enum_cb, &want_mp4);

  cache_xcode_toggle(want_mp4);
}

// Thread: player (must not block)
static void
httpd_speaker_update_handler(short event_mask, void *ctx)
{
  worker_execute(speaker_update_handler_cb, NULL, 0, 0);
}


/* ---------------------------- REQUEST CALLBACKS --------------------------- */

// Worker thread, invoked by request_cb() below
static void
request_async_cb(void *arg)
{
  struct httpd_request *hreq = *(struct httpd_request **)arg;

#ifdef HAVE_GETTID
  DPRINTF(E_DBG, hreq->module->logdomain, "%s request '%s' in worker thread %d\n", hreq->module->name, hreq->uri, (int)gettid());
#endif

  // Some handlers require an evbase to schedule events
  hreq->evbase = worker_evbase_get();
  hreq->module->request(hreq);
}

// httpd thread
static void
request_cb(struct httpd_request *hreq, void *arg)
{
  if (is_cors_preflight(hreq, httpd_allow_origin))
    {
      httpd_send_reply(hreq, HTTP_OK, "OK", HTTPD_SEND_NO_GZIP);
      return;
    }
  else if (!hreq->uri || !hreq->uri_parsed)
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
  if (hreq->module && hreq->is_async)
    {
      worker_execute(request_async_cb, &hreq, sizeof(struct httpd_request *), 0);
    }
  else if (hreq->module)
    {
      DPRINTF(E_DBG, hreq->module->logdomain, "%s request: '%s'\n", hreq->module->name, hreq->uri);
      hreq->evbase = httpd_backend_evbase_get(hreq->backend);
      hreq->module->request(hreq);
    }
  else
    {
      // Serve web interface files
      DPRINTF(E_DBG, L_HTTPD, "HTTP request: '%s'\n", hreq->uri);
      serve_file(hreq);
    }

  // Don't touch hreq here, if async it has been passed to a worker thread
}


/* ------------------------------- HTTPD API -------------------------------- */

void
httpd_stream_file(struct httpd_request *hreq, int id)
{
  struct media_file_info *mfi = NULL;
  struct stream_ctx *st = NULL;
  enum transcode_profile profile;
  enum transcode_profile spk_profile;
  const char *param;
  const char *param_end;
  const char *ctype;
  char buf[64];
  int64_t offset = 0;
  int64_t end_offset = 0;
  int ret;

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
      goto error;
    }

  if (mfi->data_kind != DATA_KIND_FILE)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not serve '%s' to client, not a file\n", mfi->path);

      httpd_send_error(hreq, HTTP_INTERNAL, "Cannot stream non-file content");
      goto error;
    }

  param = httpd_header_find(hreq->in_headers, "Accept-Codecs");
  profile = transcode_needed(hreq->user_agent, param, mfi->codectype);
  if (profile == XCODE_UNKNOWN)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not serve '%s' to client, unable to determine output format\n", mfi->path);

      httpd_send_error(hreq, HTTP_INTERNAL, "Cannot stream, unable to determine output format");
      goto error;
    }

  if (profile != XCODE_NONE)
    {
      DPRINTF(E_INFO, L_HTTPD, "Preparing to transcode %s\n", mfi->path);

      spk_profile = httpd_xcode_profile_get(hreq);
      if (spk_profile != XCODE_NONE)
	profile = spk_profile;

      st = stream_new_transcode(mfi, profile, hreq, offset, end_offset, stream_chunk_xcode_cb);
      if (!st)
	goto error;

      ctype = content_type_from_profile(profile);
      if (!ctype)
	goto error;

      if (!httpd_header_find(hreq->out_headers, "Content-Type"))
	httpd_header_add(hreq->out_headers, "Content-Type", ctype);
    }
  else
    {
      DPRINTF(E_INFO, L_HTTPD, "Preparing to stream %s\n", mfi->path);

      st = stream_new_raw(mfi, hreq, offset, end_offset, stream_chunk_raw_cb);
      if (!st)
	goto error;

      // Content-Type for video files is different than for audio files and
      // overrides whatever may have been set previously, like
      // application/x-dmap-tagged when we're speaking DAAP.
      if (mfi->has_video)
	{
	  // Front Row and others expect video/<type>
	  ret = snprintf(buf, sizeof(buf), "video/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    {
	      httpd_header_remove(hreq->out_headers, "Content-Type");
	      httpd_header_add(hreq->out_headers, "Content-Type", buf);
	    }
	}
      // If no Content-Type has been set and we're streaming audio, add a proper
      // Content-Type for the file we're streaming. Remember DAAP streams audio
      // with application/x-dmap-tagged as the Content-Type (ugh!).
      else if (!httpd_header_find(hreq->out_headers, "Content-Type") && mfi->type)
	{
	  ret = snprintf(buf, sizeof(buf), "audio/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    httpd_header_add(hreq->out_headers, "Content-Type", buf);
	}
    }

  if ((offset == 0) && (end_offset == 0))
    {
      // If we are not decoding, send the Content-Length. We don't do that if we
      // are decoding because we can only guesstimate the size in this case and
      // the error margin is unknown and variable.
      if (profile == XCODE_NONE)
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
  if (profile == XCODE_NONE)
    {
      // Hint the OS
      if ( (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_WILLNEED)) != 0 ||
           (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_SEQUENTIAL)) != 0 ||
           (ret = posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_NOREUSE)) != 0 )
	DPRINTF(E_DBG, L_HTTPD, "posix_fadvise() failed with error %d\n", ret);
    }
#endif

  httpd_request_close_cb_set(hreq, stream_fail_cb, st);

  DPRINTF(E_INFO, L_HTTPD, "Kicking off streaming for %s\n", mfi->path);

  free_mfi(mfi, 0);
  return;

 error:
  stream_free(st);
  free_mfi(mfi, 0);
}

// Returns enum transcode_profile, but is just declared with int so we don't
// need to include transcode.h in httpd_internal.h
int
httpd_xcode_profile_get(struct httpd_request *hreq)
{
  struct player_speaker_info spk;
  int ret;

  // No peer address if the function is called from httpd_daap.c when the DAAP
  // cache is being updated
  if (!hreq->peer_address || !hreq->user_agent)
    return XCODE_NONE;

  // A Roku Soundbridge may also be RCP device/speaker for which the user may
  // have set a prefered streaming format, but in all other cases we don't use
  // speaker configuration (so caller will let transcode_needed decide)
  if (strncmp(hreq->user_agent, "Roku", strlen("Roku")) != 0)
    return XCODE_NONE;

  ret = player_speaker_get_byaddress(&spk, hreq->peer_address);
  if (ret < 0)
    return XCODE_NONE;

  if (spk.format == MEDIA_FORMAT_WAV)
    return XCODE_WAV;
  if (spk.format == MEDIA_FORMAT_MP3)
    return XCODE_MP3;
  if (spk.format == MEDIA_FORMAT_ALAC)
    return XCODE_MP4_ALAC;

  return XCODE_NONE;
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

// The httpd_send functions below can be called from a worker thread (with
// hreq->is_async) or directly from the httpd thread. In the former case, they
// will command sending from the httpd thread, since it is not safe to access
// the backend (evhttp) from a worker thread. hreq will be freed (again,
// possibly async) if the type is either _COMPLETE or _END.

void
httpd_send_reply(struct httpd_request *hreq, int code, const char *reason, enum httpd_send_flags flags)
{
  struct evbuffer *gzbuf;
  struct evbuffer *save;
  const char *param;
  int do_gzip;

  if (!hreq->backend)
    return;

  do_gzip = ( (!(flags & HTTPD_SEND_NO_GZIP)) &&
              (evbuffer_get_length(hreq->out_body) > 512) &&
              (param = httpd_header_find(hreq->in_headers, "Accept-Encoding")) &&
              (strstr(param, "gzip") || strstr(param, "*"))
            );

  cors_headers_add(hreq, httpd_allow_origin);

  if (do_gzip && (gzbuf = httpd_gzip_deflate(hreq->out_body)))
    {
      DPRINTF(E_DBG, L_HTTPD, "Gzipping response\n");

      httpd_header_add(hreq->out_headers, "Content-Encoding", "gzip");
      save = hreq->out_body;
      hreq->out_body = gzbuf;
      evbuffer_free(save);
    }

  httpd_send(hreq, HTTPD_REPLY_COMPLETE, code, reason, NULL, NULL);
}

void
httpd_send_reply_start(struct httpd_request *hreq, int code, const char *reason)
{
  cors_headers_add(hreq, httpd_allow_origin);

  httpd_send(hreq, HTTPD_REPLY_START, code, reason, NULL, NULL);
}

void
httpd_send_reply_chunk(struct httpd_request *hreq, httpd_connection_chunkcb cb, void *arg)
{
  httpd_send(hreq, HTTPD_REPLY_CHUNK, 0, NULL, cb, arg);
}

void
httpd_send_reply_end(struct httpd_request *hreq)
{
  httpd_send(hreq, HTTPD_REPLY_END, 0, NULL, NULL, NULL);
}

// This is a modified version of evhttp_send_error (credit libevent)
void
httpd_send_error(struct httpd_request *hreq, int error, const char *reason)
{
  evbuffer_drain(hreq->out_body, -1);
  httpd_headers_clear(hreq->out_headers);

  cors_headers_add(hreq, httpd_allow_origin);

  httpd_header_add(hreq->out_headers, "Content-Type", "text/html");
  httpd_header_add(hreq->out_headers, "Connection", "close");

  evbuffer_add_printf(hreq->out_body, ERR_PAGE, error, reason, reason);

  httpd_send(hreq, HTTPD_REPLY_COMPLETE, error, reason, NULL, NULL);
}

bool
httpd_request_is_trusted(struct httpd_request *hreq)
{
  return httpd_backend_peer_is_trusted(hreq->backend);
}

bool
httpd_request_is_authorized(struct httpd_request *hreq)
{
  const char *passwd;
  int ret;

  if (httpd_request_is_trusted(hreq))
    return true;

  passwd = cfg_getstr(cfg_getsec(cfg, "general"), "admin_password");
  if (!passwd)
    {
      DPRINTF(E_LOG, L_HTTPD, "Web interface request to '%s' denied: No password set in the config\n", hreq->uri);

      httpd_send_error(hreq, HTTP_FORBIDDEN, "Forbidden");
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

  ret = basic_auth_cred_extract(&authuser, &authpwd, auth);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Malformed Authentication header\n");

      goto need_auth;
    }

  if (user)
    {
      if (strcmp(user, authuser) != 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Username mismatch\n");

	  free(authuser);
	  free(authpwd);
	  goto need_auth;
	}
    }

  if (strcmp(passwd, authpwd) != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Bad password\n");

      free(authuser);
      free(authpwd);
      goto need_auth;
    }

  free(authuser);
  free(authpwd);
  return 0;

 need_auth:
  ret = snprintf(header, sizeof(header), "Basic realm=\"%s\"", realm);
  if ((ret < 0) || (ret >= sizeof(header)))
    {
      httpd_send_error(hreq, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  httpd_header_add(hreq->out_headers, "WWW-Authenticate", header);

  evbuffer_add_printf(hreq->out_body, ERR_PAGE, HTTP_UNAUTHORIZED, "Unauthorized", "Authorization required");

  httpd_send_reply(hreq, HTTP_UNAUTHORIZED, "Unauthorized", HTTPD_SEND_NO_GZIP);

  return -1;
}

static int
bind_test(short unsigned port)
{
  int fd;

  fd = net_bind(&port, SOCK_STREAM, "httpd init");
  if (fd < 0)
    return -1;

  close(fd);
  return 0;
}

static void
thread_init_cb(struct evthr *thr, void *shared)
{
  struct event_base *evbase;
  httpd_server *server;

  thread_setname(pthread_self(), "httpd");

  CHECK_ERR(L_HTTPD, db_perthread_init());
  CHECK_NULL(L_HTTPD, evbase = evthr_get_base(thr));
  CHECK_NULL(L_HTTPD, server = httpd_server_new(evbase, httpd_port, request_cb, NULL));

  // For CORS headers
  httpd_server_allow_origin_set(server, httpd_allow_origin);

  evthr_set_aux(thr, server);
}

static void
thread_exit_cb(struct evthr *thr, void *shared)
{
  httpd_server *server;

  server = evthr_get_aux(thr);
  httpd_server_free(server);

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

  // Test that the port is free. We do it here because we can make a nicer exit
  // than we can in thread_init_cb(), where the actual binding takes place.
  ret = bind_test(httpd_port);
  if (ret < 0)
   {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create HTTP server on port %d (server already running?)\n", httpd_port);
      return -1;
   }

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

  // We need to know about speaker format changes so we can ask the cache to
  // start preparing headers for mp4/alac if selected
  listener_add(httpd_speaker_update_handler, LISTENER_SPEAKER, NULL);

  return 0;

 error:
  httpd_deinit();
  return -1;
}

/* Thread: main */
void
httpd_deinit(void)
{
  listener_remove(httpd_speaker_update_handler);

  // Give modules a chance to hang up connections nicely
  modules_deinit();

#ifdef HAVE_LIBWEBSOCKETS
  websocket_deinit();
#endif

  evthr_pool_stop(httpd_threadpool);
  evthr_pool_free(httpd_threadpool);
}
