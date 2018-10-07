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
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <time.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef HAVE_EVENTFD
# include <sys/eventfd.h>
#endif
#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#ifdef HAVE_LIBEVENT2_OLD
# include <event2/bufferevent.h>
# include <event2/bufferevent_struct.h>
#endif
#include <zlib.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "worker.h"
#include "httpd.h"
#include "httpd_rsp.h"
#include "httpd_daap.h"
#include "httpd_dacp.h"
#include "httpd_jsonapi.h"
#include "httpd_streaming.h"
#include "httpd_oauth.h"
#include "httpd_artworkapi.h"
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

struct content_type_map {
  char *ext;
  char *ctype;
};

struct stream_ctx {
  struct evhttp_request *req;
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

static const char *http_reply_401 = "<html><head><title>401 Unauthorized</title></head><body>Authorization required</body></html>";

static const char *webroot_directory;
struct event_base *evbase_httpd;

#ifdef HAVE_EVENTFD
static int exit_efd;
#else
static int exit_pipe[2];
#endif
static int httpd_exit;
static struct event *exitev;
static struct evhttp *evhttpd;
static pthread_t tid_httpd;

static const char *allow_origin;
static int httpd_port;

#ifdef HAVE_LIBEVENT2_OLD
struct stream_ctx *g_st;
#endif


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

/*
 * This disabled in the commit after d8cdc89 because my tests work fine without
 * it, and it seems that nowadays iTunes and Remote encodes the query just fine.
 * However, I'm keeping it around for a while in case problems show up. If you
 * are from the future, you can probably safely remove it for good.
 * 
static char *
httpd_fixup_uri(struct evhttp_request *req)
{
  struct evkeyvalq *headers;
  const char *ua;
  const char *uri;
  const char *u;
  const char *q;
  char *fixed;
  char *f;
  int len;

  uri = evhttp_request_get_uri(req);
  if (!uri)
    return NULL;

  // No query string, nothing to do
  q = strchr(uri, '?');
  if (!q)
    return strdup(uri);

  headers = evhttp_request_get_input_headers(req);
  ua = evhttp_find_header(headers, "User-Agent");
  if (!ua)
    return strdup(uri);

  if ((strncmp(ua, "iTunes", strlen("iTunes")) != 0)
      && (strncmp(ua, "Remote", strlen("Remote")) != 0)
      && (strncmp(ua, "Roku", strlen("Roku")) != 0))
    return strdup(uri);

  // Reencode + as %2B and space as + in the query,
  // which iTunes and Roku devices don't do
  len = strlen(uri);

  u = q;
  while (*u)
    {
      if (*u == '+')
	len += 2;

      u++;
    }

  fixed = (char *)malloc(len + 1);
  if (!fixed)
    return NULL;

  strncpy(fixed, uri, q - uri);

  f = fixed + (q - uri);
  while (*q)
    {
      switch (*q)
	{
	  case '+':
	    *f = '%';
	    f++;
	    *f = '2';
	    f++;
	    *f = 'B';
	    break;

	  case ' ':
	    *f = '+';
	    break;

	  default:
	    *f = *q;
	    break;
	}

      q++;
      f++;
    }

  *f = '\0';

  return fixed;
}
*/


/* --------------------------- REQUEST HELPERS ------------------------------ */



void
httpd_redirect_to_admin(struct evhttp_request *req)
{
  struct evkeyvalq *headers;

  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "Location", "/admin.html");

  httpd_send_reply(req, HTTP_MOVETEMP, "Moved", NULL, HTTPD_SEND_NO_GZIP);
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
httpd_request_etag_matches(struct evhttp_request *req, const char *etag)
{
  struct evkeyvalq *input_headers;
  struct evkeyvalq *output_headers;
  const char *none_match;

  input_headers = evhttp_request_get_input_headers(req);
  none_match = evhttp_find_header(input_headers, "If-None-Match");

  // Return not modified, if given timestamp matches "If-Modified-Since" request header
  if (none_match && (strcasecmp(etag, none_match) == 0))
    return true;

  // Add cache headers to allow client side caching
  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Cache-Control", "private");
  evhttp_add_header(output_headers, "ETag", etag);

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
httpd_request_not_modified_since(struct evhttp_request *req, const time_t *mtime)
{
  struct evkeyvalq *input_headers;
  struct evkeyvalq *output_headers;
  char last_modified[1000];
  const char *modified_since;

  input_headers = evhttp_request_get_input_headers(req);
  modified_since = evhttp_find_header(input_headers, "If-Modified-Since");

  strftime(last_modified, sizeof(last_modified), "%a, %d %b %Y %H:%M:%S %Z", gmtime(mtime));

  // Return not modified, if given timestamp matches "If-Modified-Since" request header
  if (modified_since && (strcasecmp(last_modified, modified_since) == 0))
    return true;

  // Add cache headers to allow client side caching
  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Cache-Control", "private");
  evhttp_add_header(output_headers, "Last-Modified", last_modified);

  return false;
}

static void
serve_file(struct evhttp_request *req, const char *uri)
{
  char *ext;
  char path[PATH_MAX];
  char deref[PATH_MAX];
  char *ctype;
  struct evbuffer *evbuf;
  struct evkeyvalq *output_headers;
  struct stat sb;
  int fd;
  int i;
  uint8_t buf[4096];
  bool slashed;
  int ret;

  /* Check authentication */
  if (!httpd_admin_check_auth(req))
    return;

  ret = snprintf(path, sizeof(path), "%s%s", webroot_directory, uri);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_HTTPD, "Request exceeds PATH_MAX: %s\n", uri);

      httpd_send_error(req, HTTP_NOTFOUND, "Not Found");

      return;
    }

  ret = lstat(path, &sb);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_HTTPD, "Could not lstat() %s: %s\n", path, strerror(errno));

      httpd_send_error(req, HTTP_NOTFOUND, "Not Found");

      return;
    }

  if (S_ISLNK(sb.st_mode))
    {
      if (!realpath(path, deref))
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not dereference %s: %s\n", path, strerror(errno));

	  httpd_send_error(req, HTTP_NOTFOUND, "Not Found");

	  return;
	}

      if (strlen(deref) + 1 > PATH_MAX)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Dereferenced path exceeds PATH_MAX: %s\n", path);

	  httpd_send_error(req, HTTP_NOTFOUND, "Not Found");

	  return;
	}

      strcpy(path, deref);

      ret = stat(path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", path, strerror(errno));

	  httpd_send_error(req, HTTP_NOTFOUND, "Not Found");

	  return;
	}
    }

  if (S_ISDIR(sb.st_mode))
    {
      slashed = (path[strlen(path) - 1] == '/');

      ret = snprintf(deref, sizeof(deref), "%s%sindex.html", path, (slashed) ? "" : "/");
      if ((ret < 0) || (ret >= sizeof(deref)))
        {
	  DPRINTF(E_LOG, L_HTTPD, "Redirection URL exceeds buffer length\n");

	  httpd_send_error(req, HTTP_NOTFOUND, "Not Found");
	  return;
	}

      strcpy(path, deref);

      ret = stat(path, &sb);
      if (ret < 0)
        {
	  if (strcmp(uri, "/") == 0)
	    {
	      httpd_redirect_to_admin(req);
	      return;
	    }
	  else
	    {
	      DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", path, strerror(errno));
	      httpd_send_error(req, HTTP_NOTFOUND, "Not Found");
	      return;
	    }
	}
    }

  if (path_is_legal(path) != 0)
    {
      httpd_send_error(req, 403, "Forbidden");

      return;
    }

  if (httpd_request_not_modified_since(req, &sb.st_mtime))
    {
      httpd_send_reply(req, HTTP_NOTMODIFIED, NULL, NULL, HTTPD_SEND_NO_GZIP);
      return;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not create evbuffer\n");

      httpd_send_error(req, HTTP_SERVUNAVAIL, "Internal error");
      return;
    }

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", path, strerror(errno));

      httpd_send_error(req, HTTP_NOTFOUND, "Not Found");
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

  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Content-Type", ctype);

  httpd_send_reply(req, HTTP_OK, "OK", evbuf, HTTPD_SEND_NO_GZIP);

  evbuffer_free(evbuf);
  close(fd);
  return;

 out_fail:
  httpd_send_error(req, HTTP_SERVUNAVAIL, "Internal error");
  evbuffer_free(evbuf);
  close(fd);
}


/* ---------------------------- STREAM HANDLING ----------------------------- */

static void
stream_end(struct stream_ctx *st, int failed)
{
  struct evhttp_connection *evcon;

  evcon = evhttp_request_get_connection(st->req);

  if (evcon)
    evhttp_connection_set_closecb(evcon, NULL, NULL);

  if (!failed)
    evhttp_send_reply_end(st->req);

  evbuffer_free(st->evbuf);
  event_free(st->ev);

  if (st->xcode)
    transcode_cleanup(&st->xcode);
  else
    {
      free(st->buf);
      close(st->fd);
    }

#ifdef HAVE_LIBEVENT2_OLD
  if (g_st == st)
    g_st = NULL;
#endif

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
stream_chunk_resched_cb(struct evhttp_connection *evcon, void *arg)
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

#ifdef HAVE_LIBEVENT2_OLD
static void
stream_chunk_resched_cb_wrapper(struct bufferevent *bufev, void *arg)
{
  if (g_st)
    stream_chunk_resched_cb(NULL, g_st);
}
#endif

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

#ifdef HAVE_LIBEVENT2_OLD
  evhttp_send_reply_chunk(st->req, st->evbuf);

  struct evhttp_connection *evcon = evhttp_request_get_connection(st->req);
  struct bufferevent *bufev = evhttp_connection_get_bufferevent(evcon);

  g_st = st; // Can't pass st to callback so use global - limits libevent 2.0 to a single stream
  bufev->writecb = stream_chunk_resched_cb_wrapper;
#else
  evhttp_send_reply_chunk_with_cb(st->req, st->evbuf, stream_chunk_resched_cb, st);
#endif

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

#ifdef HAVE_LIBEVENT2_OLD
  evhttp_send_reply_chunk(st->req, st->evbuf);

  struct evhttp_connection *evcon = evhttp_request_get_connection(st->req);
  struct bufferevent *bufev = evhttp_connection_get_bufferevent(evcon);

  g_st = st; // Can't pass st to callback so use global - limits libevent 2.0 to a single stream
  bufev->writecb = stream_chunk_resched_cb_wrapper;
#else
  evhttp_send_reply_chunk_with_cb(st->req, st->evbuf, stream_chunk_resched_cb, st);
#endif

  st->offset += ret;

  stream_end_register(st);
}

static void
stream_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct stream_ctx *st;

  st = (struct stream_ctx *)arg;

  DPRINTF(E_WARN, L_HTTPD, "Connection failed; stopping streaming of file ID %d\n", st->id);

  /* Stop streaming */
  event_del(st->ev);

  stream_end(st, 1);
}


/* ---------------------------- MAIN HTTPD THREAD --------------------------- */

static void *
httpd(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_httpd);

  if (!httpd_exit)
    DPRINTF(E_FATAL, L_HTTPD, "HTTPd event loop terminated ahead of time!\n");

  db_perthread_deinit();

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short event, void *arg)
{
  event_base_loopbreak(evbase_httpd);

  httpd_exit = 1;
}

static void
httpd_gen_cb(struct evhttp_request *req, void *arg)
{
  struct evkeyvalq *input_headers;
  struct evkeyvalq *output_headers;
  struct httpd_uri_parsed *parsed;
  const char *uri;

  // Clear the proxy request flag set by evhttp if the request URI was absolute.
  // It has side-effects on Connection: keep-alive
  req->flags &= ~EVHTTP_PROXY_REQUEST;

  // Did we get a CORS preflight request?
  input_headers = evhttp_request_get_input_headers(req);
  if ( input_headers && allow_origin &&
       (evhttp_request_get_command(req) == EVHTTP_REQ_OPTIONS) &&
       evhttp_find_header(input_headers, "Origin") &&
       evhttp_find_header(input_headers, "Access-Control-Request-Method") )
    {
      output_headers = evhttp_request_get_output_headers(req);

      evhttp_add_header(output_headers, "Access-Control-Allow-Origin", allow_origin);
      evhttp_add_header(output_headers, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
      evhttp_add_header(output_headers, "Access-Control-Allow-Headers", "authorization");

      // In this case there is no reason to go through httpd_send_reply
      evhttp_send_reply(req, HTTP_OK, "OK", NULL);
      return;
    }

  uri = evhttp_request_get_uri(req);
  if (!uri)
    {
      DPRINTF(E_WARN, L_HTTPD, "No URI in request\n");
      httpd_redirect_to_admin(req);
      return;
    }

  parsed = httpd_uri_parse(uri);
  if (!parsed || !parsed->path)
    {
      httpd_redirect_to_admin(req);
      goto out;
    }

  if (strcmp(parsed->path, "/") == 0)
    {
      goto serve_file;
    }

  /* Dispatch protocol-specific handlers */
  if (dacp_is_request(parsed->path))
    {
      dacp_request(req, parsed);
      goto out;
    }
  else if (daap_is_request(parsed->path))
    {
      daap_request(req, parsed);
      goto out;
    }
  else if (jsonapi_is_request(parsed->path))
    {
      jsonapi_request(req, parsed);
      goto out;
    }
  else if (artworkapi_is_request(parsed->path))
    {
      artworkapi_request(req, parsed);
      goto out;
    }
  else if (streaming_is_request(parsed->path))
    {
      streaming_request(req, parsed);
      goto out;
    }
  else if (oauth_is_request(parsed->path))
    {
      oauth_request(req, parsed);
      goto out;
    }
  else if (rsp_is_request(parsed->path))
    {
      rsp_request(req, parsed);
      goto out;
    }

  DPRINTF(E_DBG, L_HTTPD, "HTTP request: '%s'\n", parsed->uri);

  /* Serve web interface files */
 serve_file:
  serve_file(req, parsed->path);

 out:
  httpd_uri_free(parsed);
}


/* ------------------------------- HTTPD API -------------------------------- */

void
httpd_uri_free(struct httpd_uri_parsed *parsed)
{
  if (!parsed)
    return;

  free(parsed->uri_decoded);
  free(parsed->path);
  free(parsed->path_parts[0]);

  evhttp_clear_headers(&(parsed->ev_query));

  if (parsed->ev_uri)
    evhttp_uri_free(parsed->ev_uri);

  free(parsed);
}

struct httpd_uri_parsed *
httpd_uri_parse(const char *uri)
{
  struct httpd_uri_parsed *parsed;
  const char *path;
  const char *query;
  char *ptr;
  int i;
  int ret;

  CHECK_NULL(L_HTTPD, parsed = calloc(1, sizeof(struct httpd_uri_parsed)));

  parsed->uri = uri;

  parsed->ev_uri = evhttp_uri_parse_with_flags(parsed->uri, EVHTTP_URI_NONCONFORMANT);
  if (!parsed->ev_uri)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not parse request: '%s'\n", parsed->uri);
      goto error;
    }

  parsed->uri_decoded = evhttp_uridecode(parsed->uri, 0, NULL);
  if (!parsed->uri_decoded)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not URI decode request: '%s'\n", parsed->uri);
      goto error;
    }

  query = evhttp_uri_get_query(parsed->ev_uri);
  if (query && strchr(query, '='))
    {
      ret = evhttp_parse_query_str(query, &(parsed->ev_query));
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Invalid query '%s' in request: '%s'\n", query, parsed->uri);
	  goto error;
	}
    }

  path = evhttp_uri_get_path(parsed->ev_uri);
  if (!path)
    {
      DPRINTF(E_WARN, L_HTTPD, "No path in request: '%s'\n", parsed->uri);
      return parsed;
    }

  parsed->path = evhttp_uridecode(path, 0, NULL);
  if (!parsed->path)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not URI decode path: '%s'\n", path);
      goto error;
    }

  CHECK_NULL(L_HTTPD, parsed->path_parts[0] = strdup(parsed->path));

  strtok_r(parsed->path_parts[0], "/", &ptr);
  for (i = 1; (i < sizeof(parsed->path_parts) / sizeof(parsed->path_parts[0])) && parsed->path_parts[i - 1]; i++)
    {
      parsed->path_parts[i] = strtok_r(NULL, "/", &ptr);
    }

  if (!parsed->path_parts[0] || parsed->path_parts[i - 1] || (i < 2))
    {
      DPRINTF(E_LOG, L_HTTPD, "URI path has too many/few components (%d): '%s'\n", (parsed->path_parts[0]) ? i : 0, parsed->path);
      goto error;
    }

  return parsed;

 error:
  httpd_uri_free(parsed);
  return NULL;
}

struct httpd_request *
httpd_request_parse(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed, const char *user_agent, struct httpd_uri_map *uri_map)
{
  struct httpd_request *hreq;
  struct evhttp_connection *evcon;
  struct evkeyvalq *headers;
  int req_method;
  int i;
  int ret;

  CHECK_NULL(L_HTTPD, hreq = calloc(1, sizeof(struct httpd_request)));

  // Note req is allowed to be NULL
  hreq->req = req;
  hreq->uri_parsed = uri_parsed;
  hreq->query = &(uri_parsed->ev_query);
  req_method = 0;

  if (req)
    {
      headers = evhttp_request_get_input_headers(req);
      hreq->user_agent = evhttp_find_header(headers, "User-Agent");

      evcon = evhttp_request_get_connection(req);
      if (evcon)
	evhttp_connection_get_peer(evcon, &hreq->peer_address, &hreq->peer_port);
      else
	DPRINTF(E_LOG, L_HTTPD, "Connection to client lost or missing\n");

      req_method = evhttp_request_get_command(req);
    }

  if (user_agent)
    hreq->user_agent = user_agent;

  // Find a handler for the path
  for (i = 0; uri_map[i].handler; i++)
    {
      // Check if handler supports the current http request method
      if (uri_map[i].method && req_method && !(req_method & uri_map[i].method))
	continue;

      ret = regexec(&uri_map[i].preg, uri_parsed->path, 0, NULL, 0);
      if (ret == 0)
        {
          hreq->handler = uri_map[i].handler;
          return hreq; // Success
        }
    }

  // Handler not found, that's an error
  free(hreq);

  return NULL;
}

/* Thread: httpd */
void
httpd_stream_file(struct evhttp_request *req, int id)
{
  struct media_file_info *mfi;
  struct stream_ctx *st;
  void (*stream_cb)(int fd, short event, void *arg);
  struct stat sb;
  struct timeval tv;
  struct evhttp_connection *evcon;
  struct evkeyvalq *input_headers;
  struct evkeyvalq *output_headers;
  const char *param;
  const char *param_end;
  const char *ua;
  const char *client_codecs;
  char buf[64];
  int64_t offset;
  int64_t end_offset;
  off_t pos;
  int transcode;
  int ret;

  offset = 0;
  end_offset = 0;

  input_headers = evhttp_request_get_input_headers(req);

  param = evhttp_find_header(input_headers, "Range");
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

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (mfi->data_kind != DATA_KIND_FILE)
    {
      evhttp_send_error(req, 500, "Cannot stream radio station");

      goto out_free_mfi;
    }

  st = (struct stream_ctx *)malloc(sizeof(struct stream_ctx));
  if (!st)
    {
      DPRINTF(E_LOG, L_HTTPD, "Out of memory for struct stream_ctx\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_free_mfi;
    }
  memset(st, 0, sizeof(struct stream_ctx));
  st->fd = -1;

  ua = evhttp_find_header(input_headers, "User-Agent");
  client_codecs = evhttp_find_header(input_headers, "Accept-Codecs");

  transcode = transcode_needed(ua, client_codecs, mfi->codectype);

  output_headers = evhttp_request_get_output_headers(req);

  if (transcode)
    {
      DPRINTF(E_INFO, L_HTTPD, "Preparing to transcode %s\n", mfi->path);

      stream_cb = stream_chunk_xcode_cb;

      st->xcode = transcode_setup(XCODE_PCM16_HEADER, mfi->data_kind, mfi->path, mfi->song_length, &st->size);
      if (!st->xcode)
	{
	  DPRINTF(E_WARN, L_HTTPD, "Transcoding setup failed, aborting streaming\n");

	  evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

	  goto out_free_st;
	}

      if (!evhttp_find_header(output_headers, "Content-Type"))
	evhttp_add_header(output_headers, "Content-Type", "audio/wav");
    }
  else
    {
      /* Stream the raw file */
      DPRINTF(E_INFO, L_HTTPD, "Preparing to stream %s\n", mfi->path);

      st->buf = (uint8_t *)malloc(STREAM_CHUNK_SIZE);
      if (!st->buf)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Out of memory for raw streaming buffer\n");

	  evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

	  goto out_free_st;
	}

      stream_cb = stream_chunk_raw_cb;

      st->fd = open(mfi->path, O_RDONLY);
      if (st->fd < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  goto out_cleanup;
	}

      ret = stat(mfi->path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  goto out_cleanup;
	}
      st->size = sb.st_size;

      pos = lseek(st->fd, offset, SEEK_SET);
      if (pos == (off_t) -1)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not seek into %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

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
	      evhttp_remove_header(output_headers, "Content-Type");
	      evhttp_add_header(output_headers, "Content-Type", buf);
	    }
	}
      /* If no Content-Type has been set and we're streaming audio, add a proper
       * Content-Type for the file we're streaming. Remember DAAP streams audio
       * with application/x-dmap-tagged as the Content-Type (ugh!).
       */
      else if (!evhttp_find_header(output_headers, "Content-Type") && mfi->type)
	{
	  ret = snprintf(buf, sizeof(buf), "audio/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    evhttp_add_header(output_headers, "Content-Type", buf);
	}
    }

  st->evbuf = evbuffer_new();
  if (!st->evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not allocate an evbuffer for streaming\n");

      evhttp_clear_headers(output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  ret = evbuffer_expand(st->evbuf, STREAM_CHUNK_SIZE);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not expand evbuffer for streaming\n");

      evhttp_clear_headers(output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  st->ev = event_new(evbase_httpd, -1, EV_TIMEOUT, stream_cb, st);
  evutil_timerclear(&tv);
  if (!st->ev || (event_add(st->ev, &tv) < 0))
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not add one-shot event for streaming\n");

      evhttp_clear_headers(output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  st->id = mfi->id;
  st->start_offset = offset;
  st->stream_size = st->size;
  st->req = req;

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
	    evhttp_add_header(output_headers, "Content-Length", buf);
	}

      evhttp_send_reply_start(req, HTTP_OK, "OK");
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
	evhttp_add_header(output_headers, "Content-Range", buf);

      ret = snprintf(buf, sizeof(buf), "%" PRIi64, ((end_offset) ? end_offset + 1 : (int64_t)st->size) - offset);
      if ((ret < 0) || (ret >= sizeof(buf)))
	DPRINTF(E_LOG, L_HTTPD, "Content-Length too large for buffer, dropping\n");
      else
	evhttp_add_header(output_headers, "Content-Length", buf);

      evhttp_send_reply_start(req, 206, "Partial Content");
    }

#ifdef HAVE_POSIX_FADVISE
  if (!transcode)
    {
      /* Hint the OS */
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_WILLNEED);
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_SEQUENTIAL);
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_NOREUSE);
    }
#endif

  evcon = evhttp_request_get_connection(req);

  evhttp_connection_set_closecb(evcon, stream_fail_cb, st);

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
httpd_send_reply(struct evhttp_request *req, int code, const char *reason, struct evbuffer *evbuf, enum httpd_send_flags flags)
{
  struct evbuffer *gzbuf;
  struct evkeyvalq *input_headers;
  struct evkeyvalq *output_headers;
  const char *param;
  int do_gzip;

  if (!req)
    return;

  input_headers = evhttp_request_get_input_headers(req);
  output_headers = evhttp_request_get_output_headers(req);

  do_gzip = ( (!(flags & HTTPD_SEND_NO_GZIP)) &&
              evbuf && (evbuffer_get_length(evbuf) > 512) &&
              (param = evhttp_find_header(input_headers, "Accept-Encoding")) &&
              (strstr(param, "gzip") || strstr(param, "*"))
            );

  if (allow_origin)
    evhttp_add_header(output_headers, "Access-Control-Allow-Origin", allow_origin);

  if (do_gzip && (gzbuf = httpd_gzip_deflate(evbuf)))
    {
      DPRINTF(E_DBG, L_HTTPD, "Gzipping response\n");

      evhttp_add_header(output_headers, "Content-Encoding", "gzip");
      evhttp_send_reply(req, code, reason, gzbuf);
      evbuffer_free(gzbuf);

      // Drain original buffer, as would be after evhttp_send_reply()
      evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
    }
  else
    {
      evhttp_send_reply(req, code, reason, evbuf);
    }
}

// This is a modified version of evhttp_send_error (credit libevent)
void
httpd_send_error(struct evhttp_request* req, int error, const char* reason)
{
  struct evkeyvalq *output_headers;
  struct evbuffer *evbuf;

  if (!allow_origin)
    {
      evhttp_send_error(req, error, reason);
      return;
    }

  output_headers = evhttp_request_get_output_headers(req);

  evhttp_clear_headers(output_headers);

  evhttp_add_header(output_headers, "Access-Control-Allow-Origin", allow_origin);
  evhttp_add_header(output_headers, "Content-Type", "text/html");
  evhttp_add_header(output_headers, "Connection", "close");

  evbuf = evbuffer_new();
  if (!evbuf)
    DPRINTF(E_LOG, L_HTTPD, "Could not allocate evbuffer for error page\n");
  else
    evbuffer_add_printf(evbuf, ERR_PAGE, error, reason, reason);

  evhttp_send_reply(req, error, reason, evbuf);

  if (evbuf)
    evbuffer_free(evbuf);
}

bool
httpd_admin_check_auth(struct evhttp_request *req)
{
  struct evhttp_connection *evcon;
  char *addr;
  uint16_t port;
  const char *passwd;
  int ret;

  evcon = evhttp_request_get_connection(req);
  if (!evcon)
    {
      DPRINTF(E_LOG, L_HTTPD, "Connection to client lost or missing\n");
      return false;
    }

  evhttp_connection_get_peer(evcon, &addr, &port);

  if (peer_address_is_trusted(addr))
    return true;

  passwd = cfg_getstr(cfg_getsec(cfg, "general"), "admin_password");
  if (!passwd)
    {
      DPRINTF(E_LOG, L_HTTPD, "Web interface request to '%s' denied: No password set in the config\n", evhttp_request_get_uri(req));

      httpd_send_error(req, 403, "Forbidden");
      return false;
    }

  DPRINTF(E_DBG, L_HTTPD, "Checking web interface authentication\n");

  ret = httpd_basic_auth(req, "admin", passwd, PACKAGE " web interface");
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Web interface request to '%s' denied: Incorrect password\n", evhttp_request_get_uri(req));

      // httpd_basic_auth has sent a reply
      return false;
    }

  DPRINTF(E_DBG, L_HTTPD, "Authentication successful\n");

  return true;
}

int
httpd_basic_auth(struct evhttp_request *req, const char *user, const char *passwd, const char *realm)
{
  struct evbuffer *evbuf;
  struct evkeyvalq *headers;
  char header[256];
  const char *auth;
  char *authuser;
  char *authpwd;
  int ret;

  headers = evhttp_request_get_input_headers(req);
  auth = evhttp_find_header(headers, "Authorization");
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

  authuser = b64_decode(auth);
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
      httpd_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      httpd_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "WWW-Authenticate", header);

  evbuffer_add(evbuf, http_reply_401, strlen(http_reply_401));

  httpd_send_reply(req, 401, "Unauthorized", evbuf, HTTPD_SEND_NO_GZIP);

  evbuffer_free(evbuf);

  return -1;
}

/* Thread: main */
int
httpd_init(const char *webroot)
{
  struct stat sb;
  int v6enabled;
  int ret;

  httpd_exit = 0;

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
  webroot_directory = webroot;

  evbase_httpd = event_base_new();
  if (!evbase_httpd)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create an event base\n");

      return -1;
    }

  ret = rsp_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "RSP protocol init failed\n");

      goto rsp_fail;
    }

  ret = daap_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "DAAP protocol init failed\n");

      goto daap_fail;
    }

  ret = dacp_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "DACP protocol init failed\n");

      goto dacp_fail;
    }

  ret = jsonapi_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "JSON api init failed\n");

      goto jsonapi_fail;
    }

  ret = artworkapi_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Artwork init failed\n");

      goto artworkapi_fail;
    }

  ret = oauth_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "OAuth init failed\n");

      goto oauth_fail;
    }

#ifdef HAVE_LIBWEBSOCKETS
  ret = websocket_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Websocket init failed\n");

      goto websocket_fail;
    }
#endif

  streaming_init();

#ifdef HAVE_EVENTFD
  exit_efd = eventfd(0, EFD_CLOEXEC);
  if (exit_efd < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create eventfd: %s\n", strerror(errno));

      goto pipe_fail;
    }

  exitev = event_new(evbase_httpd, exit_efd, EV_READ, exit_cb, NULL);
#else
# ifdef HAVE_PIPE2
  ret = pipe2(exit_pipe, O_CLOEXEC);
# else
  ret = pipe(exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create pipe: %s\n", strerror(errno));

      goto pipe_fail;
    }

  exitev = event_new(evbase_httpd, exit_pipe[0], EV_READ, exit_cb, NULL);
#endif /* HAVE_EVENTFD */
  if (!exitev)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create exit event\n");

      goto event_fail;
    }
  event_add(exitev, NULL);

  evhttpd = evhttp_new(evbase_httpd);
  if (!evhttpd)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create HTTP server\n");

      goto event_fail;
    }

  v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");
  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");

  // For CORS headers
  allow_origin = cfg_getstr(cfg_getsec(cfg, "general"), "allow_origin");
  if (allow_origin)
    {
      if (strlen(allow_origin) != 0)
	evhttp_set_allowed_methods(evhttpd, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_HEAD | EVHTTP_REQ_OPTIONS);
      else
	allow_origin = NULL;
    }

  if (v6enabled)
    {
      ret = evhttp_bind_socket(evhttpd, "::", httpd_port);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not bind to port %d with IPv6, falling back to IPv4\n", httpd_port);
	  v6enabled = 0;
	}
    }

  ret = evhttp_bind_socket(evhttpd, "0.0.0.0", httpd_port);
  if (ret < 0)
    {
      if (!v6enabled)
	{
	  DPRINTF(E_FATAL, L_HTTPD, "Could not bind to port %d (forked-daapd already running?)\n", httpd_port);
	  goto bind_fail;
	}

#ifndef __linux__
      // Linux will listen on both ipv6 and ipv4, but FreeBSD won't
      DPRINTF(E_LOG, L_HTTPD, "Could not bind to port %d with IPv4, listening on IPv6 only\n", httpd_port);
#endif
    }

  evhttp_set_gencb(evhttpd, httpd_gen_cb, NULL);

  ret = pthread_create(&tid_httpd, NULL, httpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not spawn HTTPd thread: %s\n", strerror(errno));

      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_httpd, "httpd");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_httpd, "httpd");
#endif

  return 0;

 thread_fail:
 bind_fail:
  evhttp_free(evhttpd);
 event_fail:
#ifdef HAVE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
 pipe_fail:
  streaming_deinit();
#ifdef HAVE_LIBWEBSOCKETS
  websocket_deinit();
 websocket_fail:
#endif
  oauth_deinit();
 oauth_fail:
  artworkapi_deinit();
 artworkapi_fail:
  jsonapi_deinit();
 jsonapi_fail:
  dacp_deinit();
 dacp_fail:
  daap_deinit();
 daap_fail:
  rsp_deinit();
 rsp_fail:
  event_base_free(evbase_httpd);

  return -1;
}

/* Thread: main */
void
httpd_deinit(void)
{
  int ret;

#ifdef HAVE_EVENTFD
  ret = eventfd_write(exit_efd, 1);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not send exit event: %s\n", strerror(errno));

      return;
    }
#else
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }
#endif

  ret = pthread_join(tid_httpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not join HTTPd thread: %s\n", strerror(errno));

      return;
    }

  streaming_deinit();
#ifdef HAVE_LIBWEBSOCKETS
  websocket_deinit();
#endif
  oauth_deinit();
  jsonapi_deinit();
  rsp_deinit();
  dacp_deinit();
  daap_deinit();

#ifdef HAVE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
  evhttp_free(evhttpd);
  event_base_free(evbase_httpd);
}
