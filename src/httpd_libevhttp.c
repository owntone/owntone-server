/*
 * Copyright (C) 2023 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/queue.h> // TAILQ_FOREACH
#include <sys/socket.h> // listen()

#include <event2/http.h>
#include <event2/http_struct.h> // flags in struct evhttp
#include <event2/keyvalq_struct.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include "misc.h"
#include "logger.h"
#include "httpd_internal.h"

#define DEBUG_ALLOC 1

#ifdef DEBUG_ALLOC
#include <pthread.h>
static pthread_mutex_t debug_alloc_lck = PTHREAD_MUTEX_INITIALIZER;
static int debug_alloc_count;
#endif

struct httpd_uri_parsed
{
  struct evhttp_uri *ev_uri;
  struct evkeyvalq query;
  char *path;
  httpd_uri_path_parts path_parts;
};

struct httpd_server
{
  int fd;
  struct evhttp *evhttp;
  httpd_request_cb request_cb;
  void *request_cb_arg;
};


const char *
httpd_query_value_find(httpd_query *query, const char *key)
{
  return evhttp_find_header(query, key);
}

void
httpd_query_iterate(httpd_query *query, httpd_query_iteratecb cb, void *arg)
{
  struct evkeyval *param;

  TAILQ_FOREACH(param, query, next)
    {
      cb(param->key, param->value, arg);
    }
}

void
httpd_query_clear(httpd_query *query)
{
  evhttp_clear_headers(query);
}

const char *
httpd_header_find(httpd_headers *headers, const char *key)
{
  return evhttp_find_header(headers, key);
}

void
httpd_header_remove(httpd_headers *headers, const char *key)
{
  evhttp_remove_header(headers, key);
}

void
httpd_header_add(httpd_headers *headers, const char *key, const char *val)
{
  evhttp_add_header(headers, key, val);
}

void
httpd_headers_clear(httpd_headers *headers)
{
  evhttp_clear_headers(headers);
}

void
httpd_connection_free(httpd_connection *conn)
{
  if (!conn)
    return;

  evhttp_connection_free(conn);
}

httpd_connection *
httpd_request_connection_get(struct httpd_request *hreq)
{
  return httpd_backend_connection_get(hreq->backend);
}

void
httpd_request_backend_free(struct httpd_request *hreq)
{
  evhttp_request_free(hreq->backend);
}

int
httpd_request_closecb_set(struct httpd_request *hreq, httpd_connection_closecb cb, void *arg)
{
  httpd_connection *conn = httpd_request_connection_get(hreq);
  if (!conn)
    return -1;

  evhttp_connection_set_closecb(conn, cb, arg);
  return 0;
}

struct event_base *
httpd_request_evbase_get(struct httpd_request *hreq)
{
  httpd_connection *conn = httpd_request_connection_get(hreq);
  if (!conn)
    return NULL;

  return evhttp_connection_get_base(conn);
}

void
httpd_request_free(struct httpd_request *hreq)
{
#ifdef DEBUG_ALLOC
  pthread_mutex_lock(&debug_alloc_lck);
  debug_alloc_count--;
  pthread_mutex_unlock(&debug_alloc_lck);
  DPRINTF(E_DBG, L_HTTPD, "DEALLOC hreq - count is %d\n", debug_alloc_count);
#endif

  if (!hreq)
    return;

  if (hreq->out_body)
    evbuffer_free(hreq->out_body);

  httpd_uri_parsed_free(hreq->uri_parsed);
  httpd_backend_data_free(hreq->backend_data);
  free(hreq);
}

struct httpd_request *
httpd_request_new(httpd_backend *backend, const char *uri, const char *user_agent)
{
  struct httpd_request *hreq;
  httpd_backend_data *backend_data;

  CHECK_NULL(L_HTTPD, hreq = calloc(1, sizeof(struct httpd_request)));

#ifdef DEBUG_ALLOC
  pthread_mutex_lock(&debug_alloc_lck);
  debug_alloc_count++;
  pthread_mutex_unlock(&debug_alloc_lck);
  DPRINTF(E_DBG, L_HTTPD, "ALLOC hreq - count is %d\n", debug_alloc_count);
#endif

  // Populate hreq by getting values from the backend (or from the caller)
  hreq->backend = backend;
  if (backend)
    {
      backend_data = httpd_backend_data_create(backend);
      hreq->backend_data = backend_data;

      hreq->uri = httpd_backend_uri_get(backend, backend_data);
      hreq->uri_parsed = httpd_uri_parsed_create(backend);

      hreq->in_headers = httpd_backend_input_headers_get(backend);
      hreq->out_headers = httpd_backend_output_headers_get(backend);
      hreq->in_body = httpd_backend_input_buffer_get(backend);
      httpd_backend_method_get(&hreq->method, backend);
      httpd_backend_peer_get(&hreq->peer_address, &hreq->peer_port, backend, backend_data);

      hreq->user_agent = httpd_header_find(hreq->in_headers, "User-Agent");
    }
  else
    {
      hreq->uri = uri;
      hreq->uri_parsed = httpd_uri_parsed_create_fromuri(uri);

      hreq->user_agent = user_agent;
    }

  if (!hreq->uri_parsed)
    {
      DPRINTF(E_LOG, L_HTTPD, "Unable to parse URI '%s' in request from '%s'\n", hreq->uri, hreq->peer_address);
      goto error;
    }

  // Don't write directly to backend's buffer. This way we are sure we own the
  // buffer even if there is no backend.
  CHECK_NULL(L_HTTPD, hreq->out_body = evbuffer_new());

  hreq->path = httpd_uri_path_get(hreq->uri_parsed);
  hreq->query = httpd_uri_query_get(hreq->uri_parsed);
  httpd_uri_path_parts_get(&hreq->path_parts, hreq->uri_parsed);

  return hreq;

 error:
  httpd_request_free(hreq);
  return NULL;
}

static void
gencb_httpd(httpd_backend *backend, void *arg)
{
  httpd_server *server = arg;
  struct httpd_request *hreq;
  struct bufferevent *bufev;

  // Clear the proxy request flag set by evhttp if the request URI was absolute.
  // It has side-effects on Connection: keep-alive
  backend->flags &= ~EVHTTP_PROXY_REQUEST;

  // This is a workaround for some versions of libevent (2.0 and 2.1) that don't
  // detect if the client hangs up, and thus don't clean up and never call the
  // connection close cb(). See github issue #870 and
  // https://github.com/libevent/libevent/issues/666. It should probably be
  // removed again in the future.
  bufev = evhttp_connection_get_bufferevent(evhttp_request_get_connection(backend));
  if (bufev)
    bufferevent_enable(bufev, EV_READ);

  hreq = httpd_request_new(backend, NULL, NULL);
  if (!hreq)
    {
      evhttp_send_error(backend, HTTP_INTERNAL, "Internal error");
      return;
    }

  server->request_cb(hreq, server->request_cb_arg);
}

void
httpd_server_free(httpd_server *server)
{
  if (!server)
    return;

  if (server->fd > 0)
    close(server->fd);

  if (server->evhttp)
    evhttp_free(server->evhttp);

  free(server);
}

httpd_server *
httpd_server_new(struct event_base *evbase, unsigned short port, httpd_request_cb cb, void *arg)
{
  httpd_server *server;
  int ret;

  CHECK_NULL(L_HTTPD, server = calloc(1, sizeof(httpd_server)));
  CHECK_NULL(L_HTTPD, server->evhttp = evhttp_new(evbase));

  server->request_cb = cb;
  server->request_cb_arg = arg;

  server->fd = net_bind_with_reuseport(&port, SOCK_STREAM | SOCK_NONBLOCK, "httpd");
  if (server->fd <= 0)
    goto error;

  // Backlog of 128 is the same libevent uses
  ret = listen(server->fd, 128);
  if (ret < 0)
    goto error;

  ret = evhttp_accept_socket(server->evhttp, server->fd);
  if (ret < 0)
    goto error;

  evhttp_set_gencb(server->evhttp, gencb_httpd, server);

  return server;

 error:
  httpd_server_free(server);
  return NULL;
}

void
httpd_server_allow_origin_set(httpd_server *server, bool allow)
{
  evhttp_set_allowed_methods(server->evhttp, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_HEAD | EVHTTP_REQ_OPTIONS);
}

void
httpd_backend_reply_send(httpd_backend *backend, int code, const char *reason, struct evbuffer *evbuf)
{
  evhttp_send_reply(backend, code, reason, evbuf);
}

void
httpd_backend_reply_start_send(httpd_backend *backend, int code, const char *reason)
{
  evhttp_send_reply_start(backend, code, reason);
}

void
httpd_backend_reply_chunk_send(httpd_backend *backend, struct evbuffer *evbuf, httpd_connection_chunkcb cb, void *arg)
{
  evhttp_send_reply_chunk_with_cb(backend, evbuf, cb, arg);
}

void
httpd_backend_reply_end_send(httpd_backend *backend)
{
  evhttp_send_reply_end(backend);
}

httpd_backend_data *
httpd_backend_data_create(httpd_backend *backend)
{
  return "dummy";
}

void
httpd_backend_data_free(httpd_backend_data *backend_data)
{
  // Nothing to do
}

httpd_connection *
httpd_backend_connection_get(httpd_backend *backend)
{
  return evhttp_request_get_connection(backend);
}

const char *
httpd_backend_uri_get(httpd_backend *backend, httpd_backend_data *backend_data)
{
  return evhttp_request_get_uri(backend);
}

httpd_headers *
httpd_backend_input_headers_get(httpd_backend *backend)
{
  return evhttp_request_get_input_headers(backend);
}

httpd_headers *
httpd_backend_output_headers_get(httpd_backend *backend)
{
  return evhttp_request_get_output_headers(backend);
}

struct evbuffer *
httpd_backend_input_buffer_get(httpd_backend *backend)
{
  return evhttp_request_get_input_buffer(backend);
}

struct evbuffer *
httpd_backend_output_buffer_get(httpd_backend *backend)
{
  return evhttp_request_get_output_buffer(backend);
}

int
httpd_backend_peer_get(const char **addr, uint16_t *port, httpd_backend *backend, httpd_backend_data *backend_data)
{
  httpd_connection *conn = httpd_backend_connection_get(backend);
  if (!conn)
    return -1;

#ifdef HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR
  evhttp_connection_get_peer(conn, addr, port);
#else
  evhttp_connection_get_peer(conn, (char **)addr, port);
#endif
  return 0;
}

int
httpd_backend_method_get(enum httpd_methods *method, httpd_backend *backend)
{
  enum evhttp_cmd_type cmd = evhttp_request_get_command(backend);

  switch (cmd)
    {
      case EVHTTP_REQ_GET:     *method = HTTPD_METHOD_GET; break;
      case EVHTTP_REQ_POST:    *method = HTTPD_METHOD_POST; break;
      case EVHTTP_REQ_HEAD:    *method = HTTPD_METHOD_HEAD; break;
      case EVHTTP_REQ_PUT:     *method = HTTPD_METHOD_PUT; break;
      case EVHTTP_REQ_DELETE:  *method = HTTPD_METHOD_DELETE; break;
      case EVHTTP_REQ_OPTIONS: *method = HTTPD_METHOD_OPTIONS; break;
      case EVHTTP_REQ_TRACE:   *method = HTTPD_METHOD_TRACE; break;
      case EVHTTP_REQ_CONNECT: *method = HTTPD_METHOD_CONNECT; break;
      case EVHTTP_REQ_PATCH:   *method = HTTPD_METHOD_PATCH; break;
      default:                 *method = HTTPD_METHOD_GET; return -1;
    }

  return 0;
}

httpd_uri_parsed *
httpd_uri_parsed_create(httpd_backend *backend)
{
  const char *uri = evhttp_request_get_uri(backend);

  return httpd_uri_parsed_create_fromuri(uri);
}

httpd_uri_parsed *
httpd_uri_parsed_create_fromuri(const char *uri)
{
  struct httpd_uri_parsed *parsed;
  const char *query;
  char *path = NULL;
  char *path_part;
  char *ptr;
  int i;

  parsed = calloc(1, sizeof(struct httpd_uri_parsed));
  if (!parsed)
    goto error;

  parsed->ev_uri = evhttp_uri_parse_with_flags(uri, EVHTTP_URI_NONCONFORMANT);
  if (!parsed->ev_uri)
    goto error;

  query = evhttp_uri_get_query(parsed->ev_uri);
  if (query && strchr(query, '=') && evhttp_parse_query_str(query, &(parsed->query)) < 0)
    goto error;

  path = strdup(evhttp_uri_get_path(parsed->ev_uri));
  if (!path || !(parsed->path = evhttp_uridecode(path, 0, NULL)))
    goto error;

  path_part = strtok_r(path, "/", &ptr);
  for (i = 0; (i < ARRAY_SIZE(parsed->path_parts) && path_part); i++)
    {
      parsed->path_parts[i] = evhttp_uridecode(path_part, 0, NULL);
      path_part = strtok_r(NULL, "/", &ptr);
    }

  // If "path_part" is not NULL, we have path tokens that could not be parsed into the "parsed->path_parts" array
  if (path_part)
    goto error;

  free(path);
  return parsed;

 error:
  free(path);
  httpd_uri_parsed_free(parsed);
  return NULL;
}

void
httpd_uri_parsed_free(httpd_uri_parsed *parsed)
{
  int i;

  if (!parsed)
    return;

  free(parsed->path);
  for (i = 0; i < ARRAY_SIZE(parsed->path_parts); i++)
    free(parsed->path_parts[i]);

  httpd_query_clear(&(parsed->query));

  if (parsed->ev_uri)
    evhttp_uri_free(parsed->ev_uri);

  free(parsed);
}

httpd_query *
httpd_uri_query_get(httpd_uri_parsed *parsed)
{
  return &parsed->query;
}

const char *
httpd_uri_path_get(httpd_uri_parsed *parsed)
{
  return parsed->path;
}

void
httpd_uri_path_parts_get(httpd_uri_path_parts *path_parts, httpd_uri_parsed *parsed)
{
  memcpy(path_parts, parsed->path_parts, sizeof(httpd_uri_path_parts));
}
