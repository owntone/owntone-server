#include <sys/queue.h>

#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>

#include "misc.h" // For net_evhttp_bind
#include "httpd_internal.h"

int
httpd_connection_closecb_set(httpd_connection *conn, httpd_connection_closecb cb, void *arg)
{
  evhttp_connection_set_closecb(conn, cb, arg);
  return 0;
}

int
httpd_connection_peer_get(char **addr, uint16_t *port, httpd_connection *conn)
{
  evhttp_connection_get_peer(conn, addr, port);
  return 0;
}

void
httpd_connection_free(httpd_connection *conn)
{
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

  return httpd_connection_closecb_set(conn, cb, arg);
}

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

httpd_headers *
httpd_request_output_headers_get(struct httpd_request *hreq)
{
  return evhttp_request_get_output_headers(hreq->backend);
}

void
httpd_reply_backend_send(struct httpd_request *hreq, int code, const char *reason, struct evbuffer *evbuf)
{
  evhttp_send_reply(hreq->backend, code, reason, evbuf);
}

void
httpd_reply_start_send(struct httpd_request *hreq, int code, const char *reason)
{
  evhttp_send_reply_start(hreq->backend, code, reason);
}

void
httpd_reply_chunk_send(struct httpd_request *hreq, struct evbuffer *evbuf, httpd_connection_chunkcb cb, void *arg)
{
  evhttp_send_reply_chunk_with_cb(hreq->backend, evbuf, cb, arg);
}

void
httpd_reply_end_send(struct httpd_request *hreq)
{
  evhttp_send_reply_end(hreq->backend);
}

void
httpd_server_free(httpd_server *server)
{
  if (!server)
    return;

  evhttp_free(server);
}

httpd_server *
httpd_server_new(struct event_base *evbase, unsigned short port, httpd_general_cb cb, void *arg)
{
  int ret;
  struct evhttp *server = evhttp_new(evbase);

  if (!server)
    goto error;

  ret = net_evhttp_bind(server, port, "httpd");
  if (ret < 0)
    goto error;

  evhttp_set_gencb(server, cb, arg);

  return server;

 error:
  httpd_server_free(server);
  return NULL;
}

void
httpd_server_allow_origin_set(httpd_server *server, bool allow)
{
  evhttp_set_allowed_methods(server, EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_HEAD | EVHTTP_REQ_OPTIONS);
}

httpd_connection *
httpd_backend_connection_get(httpd_backend *backend)
{
  return evhttp_request_get_connection(backend);
}

const char *
httpd_backend_uri_get(httpd_backend *backend)
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
httpd_backend_peer_get(char **addr, uint16_t *port, httpd_backend *backend)
{
  httpd_connection *conn = httpd_backend_connection_get(backend);
  if (!conn)
    return -1;

  return httpd_connection_peer_get(addr, port, conn);
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

void
httpd_backend_preprocess(httpd_backend *backend)
{
  // Clear the proxy request flag set by evhttp if the request URI was absolute.
  // It has side-effects on Connection: keep-alive
  backend->flags &= ~EVHTTP_PROXY_REQUEST;
}
