#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>

#include "misc.h" // For net_evhttp_bind
#include "httpd_internal.h"

struct httpd_uri_parsed
{
  struct evhttp_uri *ev_uri;
  struct evkeyvalq query;
  char *path;
  httpd_uri_path_parts path_parts;
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
  if (conn)
    return NULL;

  return evhttp_connection_get_base(conn);
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

  evhttp_connection_get_peer(conn, (char **)addr, port);
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

void
httpd_backend_preprocess(httpd_backend *backend)
{
  // Clear the proxy request flag set by evhttp if the request URI was absolute.
  // It has side-effects on Connection: keep-alive
  backend->flags &= ~EVHTTP_PROXY_REQUEST;
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
