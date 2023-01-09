
#ifndef __HTTPD_INTERNAL_H__
#define __HTTPD_INTERNAL_H__

#include <stdbool.h>
#include <time.h>
#include <event2/event.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/* Response codes from event2/http.h */
#define HTTP_CONTINUE          100	/**< client should proceed to send */
#define HTTP_SWITCH_PROTOCOLS  101	/**< switching to another protocol */
#define HTTP_PROCESSING        102	/**< processing the request, but no response is available yet */
#define HTTP_EARLYHINTS        103	/**< return some response headers */
#define HTTP_OK                200	/**< request completed ok */
#define HTTP_CREATED           201	/**< new resource is created */
#define HTTP_ACCEPTED          202	/**< accepted for processing */
#define HTTP_NONAUTHORITATIVE  203	/**< returning a modified version of the origin's response */
#define HTTP_NOCONTENT         204	/**< request does not have content */
#define HTTP_MOVEPERM          301	/**< the uri moved permanently */
#define HTTP_MOVETEMP          302	/**< the uri moved temporarily */
#define HTTP_NOTMODIFIED       304	/**< page was not modified from last */
#define HTTP_BADREQUEST        400	/**< invalid http request was made */
#define HTTP_UNAUTHORIZED      401	/**< authentication is required */
#define HTTP_PAYMENTREQUIRED   402	/**< user exceeded limit on requests */
#define HTTP_FORBIDDEN         403	/**< user not having the necessary permissions */
#define HTTP_NOTFOUND          404	/**< could not find content for uri */
#define HTTP_BADMETHOD         405 	/**< method not allowed for this uri */
#define HTTP_ENTITYTOOLARGE    413	/**< request is larger than the server is able to process */
#define HTTP_EXPECTATIONFAILED 417	/**< we can't handle this expectation */
#define HTTP_INTERNAL          500     /**< internal error */
#define HTTP_NOTIMPLEMENTED    501     /**< not implemented */
#define HTTP_BADGATEWAY        502	/**< received an invalid response from the upstream */
#define HTTP_SERVUNAVAIL       503	/**< the server is not available */


struct httpd_request;

// Declaring here instead of including event2/http.h makes it easier to support
// other backends than evhttp in the future, e.g. libevhtp
struct evhttp;
struct evhttp_connection;
struct evhttp_request;
struct evkeyvalq;
struct httpd_uri_parsed;

typedef struct evhttp httpd_server;
typedef struct evhttp_connection httpd_connection;
typedef struct evhttp_request httpd_backend;
typedef struct evkeyvalq httpd_headers;
typedef struct evkeyvalq httpd_query;
typedef struct httpd_uri_parsed httpd_uri_parsed;
typedef void httpd_backend_data; // Not used for evhttp

typedef char *httpd_uri_path_parts[31];
typedef void (*httpd_general_cb)(httpd_backend *backend, void *arg);
typedef void (*httpd_connection_closecb)(httpd_connection *conn, void *arg);
typedef void (*httpd_connection_chunkcb)(httpd_connection *conn, void *arg);
typedef void (*httpd_query_iteratecb)(const char *key, const char *val, void *arg);

enum httpd_methods
{
  HTTPD_METHOD_GET     = 1 << 0,
  HTTPD_METHOD_POST    = 1 << 1,
  HTTPD_METHOD_HEAD    = 1 << 2,
  HTTPD_METHOD_PUT     = 1 << 3,
  HTTPD_METHOD_DELETE  = 1 << 4,
  HTTPD_METHOD_OPTIONS = 1 << 5,
  HTTPD_METHOD_TRACE   = 1 << 6,
  HTTPD_METHOD_CONNECT = 1 << 7,
  HTTPD_METHOD_PATCH   = 1 << 8,
};

enum httpd_send_flags
{
  HTTPD_SEND_NO_GZIP =   (1 << 0),
};


/*---------------------------------- MODULES ---------------------------------*/

// Must be in sync with modules[] in httpd.c
enum httpd_modules
{
  MODULE_DACP,
  MODULE_DAAP,
  MODULE_JSONAPI,
  MODULE_ARTWORKAPI,
  MODULE_STREAMING,
  MODULE_OAUTH,
  MODULE_RSP,
};

struct httpd_module
{
  const char *name;
  enum httpd_modules type;
  char initialized;
  int logdomain;

  // Null-terminated list of URL subpath that the module accepts e.g., /subpath/morepath/file.mp3
  const char *subpaths[16];
  // Null-terminated list of URL fullparhs that the module accepts e.g., /fullpath
  const char *fullpaths[16];
  // Pointer to the module's handler definitions
  struct httpd_uri_map *handlers;

  int (*init)(struct event_base *);
  void (*deinit)(void);
  void (*request)(struct httpd_request *);
};

/*
 * Maps a regex of the request path to a handler of the request
 */
struct httpd_uri_map
{
  enum httpd_methods method;
  char *regexp;
  int (*handler)(struct httpd_request *hreq);
  void *preg;
};


/*------------------------------- HTTPD STRUCTS ------------------------------*/

/*
 * A collection of pointers to request data that the reply handlers may need.
 * Also has the function pointer to the reply handler and a pointer to a reply
 * evbuffer.
 */
struct httpd_request {
  // Request method
  enum httpd_methods method;
  // Backend private request object
  httpd_backend *backend;
  // For storing data that the actual backend doesn't have readily available
  // e.g. peer address string for libevhtp
  httpd_backend_data *backend_data;
  // User-agent (if available)
  const char *user_agent;
  // Source IP address (ipv4 or ipv6) and port of the request (if available)
  const char *peer_address;
  unsigned short peer_port;

  // The original, request URI. The URI may have been complete:
  //   scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
  // or relative:
  //   [/path][?query][#fragment]
  const char *uri;
  // URI decoded path from the request URI
  const char *path;
  // If the request is http://x:3689/foo/bar?key1=val1, then part_parts[0] is
  // "foo", [1] is "bar" and the rest is null. Each path_part is an allocated
  // URI decoded string.
  httpd_uri_path_parts path_parts;
  // Struct with the query, used with httpd_query_ functions
  httpd_query *query;
  // Backend private parser URI object
  httpd_uri_parsed *uri_parsed;

  // Request headers
  httpd_headers *in_headers;
  // Request body
  struct evbuffer *in_body;
  // Response headers
  httpd_headers *out_headers;
  // Response body
  struct evbuffer *out_body;

  // Our httpd module that will process this request
  struct httpd_module *module;
  // A pointer to the handler that will process the request
  int (*handler)(struct httpd_request *hreq);
  // A pointer to extra data that the module handling the request might need
  void *extra_data;
};


/*------------------------------ HTTPD FUNCTIONS -----------------------------*/

void
httpd_stream_file(struct httpd_request *hreq, int id);

void
httpd_request_set(struct httpd_request *hreq, const char *uri, const char *user_agent);

void
httpd_request_unset(struct httpd_request *hreq);

bool
httpd_request_not_modified_since(struct httpd_request *hreq, time_t mtime);

bool
httpd_request_etag_matches(struct httpd_request *hreq, const char *etag);

void
httpd_response_not_cachable(struct httpd_request *hreq);

/*
 * This wrapper around evhttp_send_reply should be used whenever a request may
 * come from a browser. It will automatically gzip if feasible, but the caller
 * may direct it not to. It will set CORS headers as appropriate. Should be
 * thread safe.
 *
 * @in  req      The http request struct
 * @in  code     HTTP code, e.g. 200
 * @in  reason   A brief explanation of the error - if NULL the standard meaning
                 of the error code will be used
 * @in  evbuf    Data for the response body
 * @in  flags    See flags above
 */
void
httpd_send_reply(struct httpd_request *hreq, int code, const char *reason, struct evbuffer *evbuf, enum httpd_send_flags flags);

void
httpd_send_reply_start(struct httpd_request *hreq, int code, const char *reason);

void
httpd_send_reply_chunk(struct httpd_request *hreq, struct evbuffer *evbuf, httpd_connection_chunkcb cb, void *arg);

void
httpd_send_reply_end(struct httpd_request *hreq);

/*
 * This is a substitute for evhttp_send_error that should be used whenever an
 * error may be returned to a browser. It will set CORS headers as appropriate,
 * which is not possible with evhttp_send_error, because it clears the headers.
 * Should be thread safe.
 *
 * @in  req      The http request struct
 * @in  error    HTTP code, e.g. 200
 * @in  reason   A brief explanation of the error - if NULL the standard meaning
                 of the error code will be used
 */
void
httpd_send_error(struct httpd_request *hreq, int error, const char *reason);

/*
 * Redirects to the given path
 */
void
httpd_redirect_to(struct httpd_request *hreq, const char *path);

bool
httpd_admin_check_auth(struct httpd_request *hreq);

int
httpd_basic_auth(struct httpd_request *hreq, const char *user, const char *passwd, const char *realm);


/*-------------------------- WRAPPERS FOR EVHTTP -----------------------------*/

const char *
httpd_query_value_find(httpd_query *query, const char *key);

void
httpd_query_iterate(httpd_query *query, httpd_query_iteratecb cb, void *arg);

void
httpd_query_clear(httpd_query *query);

const char *
httpd_header_find(httpd_headers *headers, const char *key);

void
httpd_header_remove(httpd_headers *headers, const char *key);

void
httpd_header_add(httpd_headers *headers, const char *key, const char *val);

void
httpd_headers_clear(httpd_headers *headers);

void
httpd_connection_free(httpd_connection *conn);

httpd_connection *
httpd_request_connection_get(struct httpd_request *hreq);

void
httpd_request_backend_free(struct httpd_request *hreq);

int
httpd_request_closecb_set(struct httpd_request *hreq, httpd_connection_closecb cb, void *arg);

struct event_base *
httpd_request_evbase_get(struct httpd_request *hreq);

void
httpd_server_free(httpd_server *server);

httpd_server *
httpd_server_new(struct event_base *evbase, unsigned short port, httpd_general_cb cb, void *arg);

void
httpd_server_allow_origin_set(httpd_server *server, bool allow);


/*----------------- Only called by httpd.c to send raw replies ---------------*/

void
httpd_backend_reply_send(httpd_backend *backend, int code, const char *reason, struct evbuffer *evbuf);

void
httpd_backend_reply_start_send(httpd_backend *backend, int code, const char *reason);

void
httpd_backend_reply_chunk_send(httpd_backend *backend, struct evbuffer *evbuf, httpd_connection_chunkcb cb, void *arg);

void
httpd_backend_reply_end_send(httpd_backend *backend);


/*---------- Only called by httpd.c to populate struct httpd_request ---------*/

httpd_backend_data *
httpd_backend_data_create(httpd_backend *backend);

void
httpd_backend_data_free(httpd_backend_data *backend_data);

httpd_connection *
httpd_backend_connection_get(httpd_backend *backend);

const char *
httpd_backend_uri_get(httpd_backend *backend, httpd_backend_data *backend_data);

httpd_headers *
httpd_backend_input_headers_get(httpd_backend *backend);

httpd_headers *
httpd_backend_output_headers_get(httpd_backend *backend);

struct evbuffer *
httpd_backend_input_buffer_get(httpd_backend *backend);

int
httpd_backend_peer_get(const char **addr, uint16_t *port, httpd_backend *backend, httpd_backend_data *backend_data);

int
httpd_backend_method_get(enum httpd_methods *method, httpd_backend *backend);

void
httpd_backend_preprocess(httpd_backend *backend);

httpd_uri_parsed *
httpd_uri_parsed_create(httpd_backend *backend);

httpd_uri_parsed *
httpd_uri_parsed_create_fromuri(const char *uri);

void
httpd_uri_parsed_free(httpd_uri_parsed *uri_parsed);

httpd_query *
httpd_uri_query_get(httpd_uri_parsed *parsed);

const char *
httpd_uri_path_get(httpd_uri_parsed *parsed);

void
httpd_uri_path_parts_get(httpd_uri_path_parts *part_parts, httpd_uri_parsed *parsed);

#endif /* !__HTTPD_INTERNAL_H__ */
