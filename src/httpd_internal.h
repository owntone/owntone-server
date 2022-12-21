
#ifndef __HTTPD_INTERNAL_H__
#define __HTTPD_INTERNAL_H__

#include <stdbool.h>
#include <time.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>


enum httpd_send_flags
{
  HTTPD_SEND_NO_GZIP =   (1 << 0),
};

/*
 * Contains a parsed version of the URI httpd got. The URI may have been
 * complete:
 *   scheme:[//[user[:password]@]host[:port]][/path][?query][#fragment]
 * or relative:
 *   [/path][?query][#fragment]
 *
 * We are interested in the path and the query, so they are disassembled to
 * path_parts and ev_query. If the request is http://x:3689/foo/bar?key1=val1,
 * then part_parts[0] is "foo", [1] is "bar" and the rest is null.
 *
 * Each path_part is an allocated URI decoded string.
 */
struct httpd_uri_parsed
{
  const char *uri;
  struct evhttp_uri *ev_uri;
  struct evkeyvalq ev_query;
  char *uri_decoded;
  char *path;
  char *path_parts[31];
};

/*
 * A collection of pointers to request data that the reply handlers may need.
 * Also has the function pointer to the reply handler and a pointer to a reply
 * evbuffer.
 */
struct httpd_request {
  // User-agent (if available)
  const char *user_agent;
  // Shortcut to &uri_parsed->uri
  const char *uri;
  // The parsed request URI given to us by httpd_uri_parse
  struct httpd_uri_parsed *uri_parsed;
  // Shortcut to &uri_parsed->ev_query
  struct evkeyvalq *query;
  // http request struct (if available)
  struct evhttp_request *req;
  // Source IP address (ipv4 or ipv6) and port of the request (if available)
  char *peer_address;
  unsigned short peer_port;
  // A pointer to extra data that the module handling the request might need
  void *extra_data;

  // Reply evbuffer
  struct evbuffer *reply;

  // A pointer to the handler that will process the request
  int (*handler)(struct httpd_request *hreq);
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

  // Null-terminated list of URL subpath that the module accepts e.g., /subpath/morepath/file.mp3
  const char *subpaths[16];
  // Null-terminated list of URL fullparhs that the module accepts e.g., /fullpath
  const char *fullpaths[16];
  // Pointer to the module's handler definitions
  struct httpd_uri_map *handlers;

  int (*init)(void);
  void (*deinit)(void);
  void (*request)(struct httpd_request *hreq);
};

/*
 * Maps a regex of the request path to a handler of the request
 */
struct httpd_uri_map
{
  int method;
  char *regexp;
  int (*handler)(struct httpd_request *hreq);
  void *preg;
};


/*
 * Helper to free the parsed uri struct
 */
void
httpd_uri_free(struct httpd_uri_parsed *parsed);

/*
 * Parse an URI into the struct
 */
struct httpd_uri_parsed *
httpd_uri_parse(const char *uri);

void
httpd_stream_file(struct httpd_request *hreq, int id);

int
httpd_request_set(struct httpd_request *hreq, struct httpd_uri_parsed *uri_parsed, const char *user_agent, struct httpd_uri_map *uri_map);

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

#endif /* !__HTTPD_INTERNAL_H__ */
