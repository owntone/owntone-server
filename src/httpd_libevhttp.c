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

#include <json.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> // listen()
#include <arpa/inet.h> // inet_pton()

#include <event2/http.h>
#include <event2/http_struct.h> // flags in struct evhttp
#include <event2/keyvalq_struct.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#ifdef HAVE_LIBEVENT22
#include <event2/ws.h>
#endif

#include <pthread.h>

#include "conffile.h"
#include "misc.h"
#include "listener.h"
#include "logger.h"
#include "commands.h"
#include "httpd_internal.h"

// #define DEBUG_ALLOC 1

#ifdef DEBUG_ALLOC
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
  struct commands_base *cmdbase;
  httpd_request_cb request_cb;
  void *request_cb_arg;
};

struct httpd_reply
{
  struct httpd_request *hreq;
  enum httpd_reply_type type;
  int code;
  const char *reason;
  httpd_connection_chunkcb chunkcb;
  void *cbarg;
};

struct httpd_disconnect
{
  pthread_mutex_t lock;
  struct event *ev;
  httpd_close_cb cb;
  void *cbarg;
};

struct httpd_backend_data
{
  // Pointer to server instance processing the request
  struct httpd_server *server;
  // If caller wants a callback on disconnect
  struct httpd_disconnect disconnect;
};

// Forward
static void
closecb_worker(evutil_socket_t fd, short event, void *arg);


#ifdef HAVE_LIBEVENT22

/*
 * Each session of the "notify" protocol holds this event mask
 *
 * The client sends the events it wants to be notified of and the event mask is
 * set accordingly translating them to the LISTENER enum (see listener.h)
 */
struct ws_client
{
  struct evws_connection *evws;
  char name[INET6_ADDRSTRLEN];
  short requested_events;
  struct ws_client *next;
};
static struct ws_client *ws_clients = NULL;

/*
 * Notify clients of the notify-protocol about occurred events
 *
 * Sends a JSON message of the form:
 *
 * {
 *   "notify": [ "update" ]
 * }
 */
static char *
ws_create_notify_reply(short events, short *requested_events)
{
  char *json_response;
  json_object *reply;
  json_object *notify;

  DPRINTF(E_DBG, L_WEB, "notify callback reply: %d\n", events);

  notify = json_object_new_array();
  if (events & LISTENER_UPDATE)
    {
      json_object_array_add(notify, json_object_new_string("update"));
    }
  if (events & LISTENER_DATABASE)
    {
      json_object_array_add(notify, json_object_new_string("database"));
    }
  if (events & LISTENER_PAIRING)
    {
      json_object_array_add(notify, json_object_new_string("pairing"));
    }
  if (events & LISTENER_SPOTIFY)
    {
      json_object_array_add(notify, json_object_new_string("spotify"));
    }
  if (events & LISTENER_LASTFM)
    {
      json_object_array_add(notify, json_object_new_string("lastfm"));
    }
  if (events & LISTENER_SPEAKER)
    {
      json_object_array_add(notify, json_object_new_string("outputs"));
    }
  if (events & LISTENER_PLAYER)
    {
      json_object_array_add(notify, json_object_new_string("player"));
    }
  if (events & LISTENER_OPTIONS)
    {
      json_object_array_add(notify, json_object_new_string("options"));
    }
  if (events & LISTENER_VOLUME)
    {
      json_object_array_add(notify, json_object_new_string("volume"));
    }
  if (events & LISTENER_QUEUE)
    {
      json_object_array_add(notify, json_object_new_string("queue"));
    }

  reply = json_object_new_object();
  json_object_object_add(reply, "notify", notify);

  json_response = strdup(json_object_to_json_string(reply));

  json_object_put(reply);

  return json_response;
}

/* Thread: library, player, etc. (the thread the event occurred) */
static enum command_state
ws_listener_cb(void *arg, int *ret)
{
  struct ws_client *client = NULL;
  char *reply = NULL;
  short *event_mask = arg;

  for (client = ws_clients; client; client = client->next)
    {
      reply = ws_create_notify_reply(*event_mask, &client->requested_events);
      evws_send_text(client->evws, reply);
      free(reply);
    }
  return COMMAND_END;
}

static void
listener_cb(short event_mask, void *ctx)
{
  httpd_server *server = ctx;
  commands_exec_sync(server->cmdbase, ws_listener_cb, NULL, &event_mask);
}

/*
 * Processes client requests to the notify-protocol
 *
 * Expects the message in "in" to be a JSON string of the form:
 *
 * {
 *   "notify": [ "update" ]
 * }
 */
static int
ws_process_notify_request(short *requested_events, const char *in, size_t len)
{
  json_tokener *tokener;
  json_object *request;
  json_object *item;
  int count, i;
  enum json_tokener_error jerr;
  json_object *needle;
  const char *event_type;

  *requested_events = 0;

  tokener = json_tokener_new();
  request = json_tokener_parse_ex(tokener, in, len);
  jerr = json_tokener_get_error(tokener);

  if (jerr != json_tokener_success)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request: %s\n", json_tokener_error_desc(jerr));
      json_tokener_free(tokener);
      return -1;
    }

  DPRINTF(E_DBG, L_WEB, "notify callback request: %s\n", json_object_to_json_string(request));

  if (json_object_object_get_ex(request, "notify", &needle) && json_object_get_type(needle) == json_type_array)
    {
      count = json_object_array_length(needle);
      for (i = 0; i < count; i++)
	{
	  item = json_object_array_get_idx(needle, i);

	  if (json_object_get_type(item) == json_type_string)
	    {
	      event_type = json_object_get_string(item);
	      DPRINTF(E_SPAM, L_WEB, "notify callback event received: %s\n", event_type);

	      if (0 == strcmp(event_type, "update"))
		{
		  *requested_events |= LISTENER_UPDATE;
		}
	      else if (0 == strcmp(event_type, "database"))
		{
		  *requested_events |= LISTENER_DATABASE;
		}
	      else if (0 == strcmp(event_type, "pairing"))
		{
		  *requested_events |= LISTENER_PAIRING;
		}
	      else if (0 == strcmp(event_type, "spotify"))
		{
		  *requested_events |= LISTENER_SPOTIFY;
		}
	      else if (0 == strcmp(event_type, "lastfm"))
		{
		  *requested_events |= LISTENER_LASTFM;
		}
	      else if (0 == strcmp(event_type, "outputs"))
		{
		  *requested_events |= LISTENER_SPEAKER;
		}
	      else if (0 == strcmp(event_type, "player"))
		{
		  *requested_events |= LISTENER_PLAYER;
		}
	      else if (0 == strcmp(event_type, "options"))
		{
		  *requested_events |= LISTENER_OPTIONS;
		}
	      else if (0 == strcmp(event_type, "volume"))
		{
		  *requested_events |= LISTENER_VOLUME;
		}
	      else if (0 == strcmp(event_type, "queue"))
		{
		  *requested_events |= LISTENER_QUEUE;
		}
	    }
	}
    }

  json_tokener_free(tokener);
  json_object_put(request);

  return 0;
}

static void
ws_client_msg_cb(struct evws_connection *evws, int type, const unsigned char *data, size_t len, void *arg)
{
  struct ws_client *self = arg;
  const char *msg = (const char *)data;

  ws_process_notify_request(&self->requested_events, msg, len);
}

static void
ws_client_close_cb(struct evws_connection *evws, void *arg)
{
  struct ws_client *client = NULL;
  struct ws_client *prev = NULL;

  for (client = ws_clients; client && client != arg; client = ws_clients->next)
    {
      prev = client;
    }

  if (client)
    {
      if (prev)
	prev->next = client->next;
      else
	ws_clients = client->next;

      free(client);
    }
}

static void
ws_gencb(struct evhttp_request *req, void *arg)
{
  struct ws_client *client;

  client = calloc(1, sizeof(*client));

  client->evws = evws_new_session(req, ws_client_msg_cb, client, 0);
  if (!client->evws)
    {
      free(client);
      return;
    }

  evws_connection_set_closecb(client->evws, ws_client_close_cb, client);
  client->next = ws_clients;
  ws_clients = client;
}

static int
ws_init(httpd_server *server)
{
  int websocket_port = cfg_getint(cfg_getsec(cfg, "general"), "websocket_port");

  if (websocket_port > 0)
    {
      DPRINTF(E_DBG, L_WEB,
	      "Libevent websocket disabled, using libwebsockets instead. Set "
	      "websocket_port to 0 to enable it.\n");
      return 0;
    }

  evhttp_set_cb(server->evhttp, "/ws", ws_gencb, NULL);

  listener_add(listener_cb, LISTENER_UPDATE | LISTENER_DATABASE | LISTENER_PAIRING | LISTENER_SPOTIFY | LISTENER_LASTFM
				| LISTENER_SPEAKER | LISTENER_PLAYER | LISTENER_OPTIONS | LISTENER_VOLUME
				| LISTENER_QUEUE, server);

  return 0;
}

static void
ws_deinit(void)
{
  listener_remove(listener_cb);
}
#endif


const char *
httpd_query_value_find(httpd_query *query, const char *key)
{
  return evhttp_find_header(query, key);
}

void
httpd_query_iterate(httpd_query *query, httpd_query_iteratecb cb, void *arg)
{
  struct evkeyval *param;

  // musl libc doesn't have sys/queue.h so don't use TAILQ_FOREACH
  for (param = query->tqh_first; param; param = param->next.tqe_next)
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
httpd_request_close_cb_set(struct httpd_request *hreq, httpd_close_cb cb, void *arg)
{
  struct httpd_disconnect *disconnect = &hreq->backend_data->disconnect;

  pthread_mutex_lock(&disconnect->lock);

  disconnect->cb = cb;
  disconnect->cbarg = arg;

  if (hreq->is_async)
    {
      if (disconnect->ev)
	event_free(disconnect->ev);

      if (disconnect->cb)
	disconnect->ev = event_new(hreq->evbase, -1, 0, closecb_worker, hreq);
      else
	disconnect->ev = NULL;
    }

  pthread_mutex_unlock(&disconnect->lock);
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
httpd_request_new(httpd_backend *backend, httpd_server *server, const char *uri, const char *user_agent)
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
      backend_data = httpd_backend_data_create(backend, server);
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

// Since this is async, libevent will already have closed the connection, so
// the parts of hreq that are from httpd_connection will now be invalid e.g.
// peer_address.
static void
closecb_worker(evutil_socket_t fd, short event, void *arg)
{
  struct httpd_request *hreq = arg;
  struct httpd_disconnect *disconnect = &hreq->backend_data->disconnect;

  pthread_mutex_lock(&disconnect->lock);

  if (disconnect->cb)
    disconnect->cb(disconnect->cbarg);

  pthread_mutex_unlock(&disconnect->lock);

  httpd_send_reply_end(hreq); // hreq is now deallocated
}

static void
closecb_httpd(httpd_connection *conn, void *arg)
{
  struct httpd_request *hreq = arg;
  struct httpd_disconnect *disconnect = &hreq->backend_data->disconnect;

  DPRINTF(E_WARN, hreq->module->logdomain, "Connection to '%s' was closed\n", hreq->peer_address);

  // The disconnect event may occur while a worker thread is accessing hreq, or
  // has an event scheduled that will do so, so we have to be careful to let it
  // finish and cancel events.
  pthread_mutex_lock(&disconnect->lock);
  if (hreq->is_async)
    {
      if (disconnect->cb)
	event_active(disconnect->ev, 0, 0);

      pthread_mutex_unlock(&disconnect->lock);
      return;
    }
  pthread_mutex_unlock(&disconnect->lock);

  if (!disconnect->cb)
    return;

  disconnect->cb(disconnect->cbarg);
  httpd_send_reply_end(hreq); // hreq is now deallocated
}

static void
gencb_httpd(httpd_backend *backend, void *arg)
{
  httpd_server *server = arg;
  struct httpd_request *hreq;
  struct bufferevent *bufev;

#ifndef HAVE_LIBEVENT22
  // Clear the proxy request flag set by evhttp if the request URI was absolute.
  // It has side-effects on Connection: keep-alive
  backend->flags &= ~EVHTTP_PROXY_REQUEST;
#endif

  // This is a workaround for some versions of libevent (2.0 and 2.1) that don't
  // detect if the client hangs up, and thus don't clean up and never call the
  // connection close cb(). See github issue #870 and
  // https://github.com/libevent/libevent/issues/666. It should probably be
  // removed again in the future.
  bufev = evhttp_connection_get_bufferevent(evhttp_request_get_connection(backend));
  if (bufev)
    bufferevent_enable(bufev, EV_READ);

  hreq = httpd_request_new(backend, server, NULL, NULL);
  if (!hreq)
    {
      evhttp_send_error(backend, HTTP_INTERNAL, "Internal error");
      return;
    }

  // We must hook connection close, so we can assure that conn close callbacks
  // to handlers running in a worker are made in the same thread.
  evhttp_connection_set_closecb(evhttp_request_get_connection(backend), closecb_httpd, hreq);

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
    {
#ifdef HAVE_LIBEVENT22
      ws_deinit();
#endif
      evhttp_free(server->evhttp);
    }

  commands_base_free(server->cmdbase);
  free(server);
}

httpd_server *
httpd_server_new(struct event_base *evbase, unsigned short port, httpd_request_cb cb, void *arg)
{
  httpd_server *server;
  int ret;

  CHECK_NULL(L_HTTPD, server = calloc(1, sizeof(httpd_server)));
  CHECK_NULL(L_HTTPD, server->evhttp = evhttp_new(evbase));
  CHECK_NULL(L_HTTPD, server->cmdbase = commands_base_new(evbase, NULL));

  server->request_cb = cb;
  server->request_cb_arg = arg;

  server->fd = net_bind_with_reuseport(&port, SOCK_STREAM, "httpd");
  if (server->fd <= 0)
    goto error;

  // Backlog of 128 is the same that libevent uses
  ret = listen(server->fd, 128);
  if (ret < 0)
    goto error;

  ret = evhttp_accept_socket(server->evhttp, server->fd);
  if (ret < 0)
    goto error;

  evhttp_set_gencb(server->evhttp, gencb_httpd, server);
#ifdef HAVE_LIBEVENT22
  ws_init(server);
#endif

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

// No locking of hreq required here, we're in the httpd thread, and the worker
// thread is waiting at commands_exec_sync()
static void
send_reply_and_free(struct httpd_reply *reply)
{
  struct httpd_request *hreq = reply->hreq;
  httpd_connection *conn;

//  DPRINTF(E_DBG, L_HTTPD, "Send from httpd thread, type %d, backend %p\n", reply->type, hreq->backend);

  if (reply->type & HTTPD_F_REPLY_LAST)
    {
      conn = evhttp_request_get_connection(hreq->backend);
      if (conn)
	evhttp_connection_set_closecb(conn, NULL, NULL);
    }

  switch (reply->type)
    {
      case HTTPD_REPLY_COMPLETE:
	evhttp_send_reply(hreq->backend, reply->code, reply->reason, hreq->out_body);
	break;
      case HTTPD_REPLY_START:
	evhttp_send_reply_start(hreq->backend, reply->code, reply->reason);
	break;
      case HTTPD_REPLY_CHUNK:
        evhttp_send_reply_chunk_with_cb(hreq->backend, hreq->out_body, reply->chunkcb, reply->cbarg);
	break;
      case HTTPD_REPLY_END:
	evhttp_send_reply_end(hreq->backend);
	break;
    }
}

static enum command_state
send_reply_and_free_cb(void *arg, int *retval)
{
  struct httpd_reply *reply = arg;

  send_reply_and_free(reply);

  return COMMAND_END;
}

void
httpd_send(struct httpd_request *hreq, enum httpd_reply_type type, int code, const char *reason, httpd_connection_chunkcb cb, void *cbarg)
{
  struct httpd_server *server = hreq->backend_data->server;
  struct httpd_reply reply = {
    .hreq = hreq,
    .type = type,
    .code = code,
    .chunkcb = cb,
    .cbarg = cbarg,
    .reason = reason,
  };

  if (type & HTTPD_F_REPLY_LAST)
    httpd_request_close_cb_set(hreq, NULL, NULL);

  // Sending async is not a option, because then the worker thread might touch
  // hreq before we have completed sending the current chunk
  if (hreq->is_async)
    commands_exec_sync(server->cmdbase, send_reply_and_free_cb, NULL, &reply);
  else
    send_reply_and_free(&reply);

  if (type & HTTPD_F_REPLY_LAST)
    httpd_request_free(hreq);
}

httpd_backend_data *
httpd_backend_data_create(httpd_backend *backend, httpd_server *server)
{
  httpd_backend_data *backend_data;

  CHECK_NULL(L_HTTPD, backend_data = calloc(1, sizeof(httpd_backend_data)));
  CHECK_ERR(L_HTTPD, mutex_init(&backend_data->disconnect.lock));
  backend_data->server = server;

  return backend_data;
}

void
httpd_backend_data_free(httpd_backend_data *backend_data)
{
  if (!backend_data)
    return;

  if (backend_data->disconnect.ev)
    event_free(backend_data->disconnect.ev);

  free(backend_data);
}

struct event_base *
httpd_backend_evbase_get(httpd_backend *backend)
{
  httpd_connection *conn = evhttp_request_get_connection(backend);
  if (!conn)
    return NULL;

  return evhttp_connection_get_base(conn);
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
#define IPV4_MAPPED_IPV6_PREFIX "::ffff:"
  httpd_connection *conn = evhttp_request_get_connection(backend);
  if (!conn)
    return -1;

#ifdef HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR
  evhttp_connection_get_peer(conn, addr, port);
#else
  evhttp_connection_get_peer(conn, (char **)addr, port);
#endif

  // Just use the pure ipv4 address if it's mapped
  if (strncmp(*addr, IPV4_MAPPED_IPV6_PREFIX, strlen(IPV4_MAPPED_IPV6_PREFIX)) == 0)
    *addr += strlen(IPV4_MAPPED_IPV6_PREFIX);

  return 0;
}

// When removing this workaround then also remove the include of arpa/inet.h
static bool
address_is_trusted_workaround(httpd_backend *backend)
{
  union net_sockaddr naddr = { 0 };
  const char *saddr;
  uint16_t port;

  DPRINTF(E_DBG, L_HTTPD, "Detected libevent version with buggy evhttp_connection_get_addr()\n");

  if (httpd_backend_peer_get(&saddr, &port, backend, NULL) < 0)
    return false;

  if (inet_pton(AF_INET, saddr, &naddr.sin.sin_addr) == 1)
    naddr.sa.sa_family = AF_INET;
  else if (inet_pton(AF_INET6, saddr, &naddr.sin6.sin6_addr) == 1)
    naddr.sa.sa_family = AF_INET6;
  else
    return false;

  return net_peer_address_is_trusted(&naddr);
}

bool
httpd_backend_peer_is_trusted(httpd_backend *backend)
{
  const struct sockaddr *addr;

  httpd_connection *conn = evhttp_request_get_connection(backend);
  if (!conn)
    return false;

  addr = evhttp_connection_get_addr(conn);
  if (!addr)
    return false;

  // Workaround for bug in libevent 2.1.6 and .8, see #1775
  if (addr->sa_family == AF_UNSPEC)
    return address_is_trusted_workaround(backend);

  return net_peer_address_is_trusted((union net_sockaddr *)addr);
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
