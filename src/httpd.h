
#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <stdbool.h>
#include <regex.h>
#include <time.h>
#include <event2/http.h>
#include <event2/buffer.h>
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
 * then part_parts[1] is "foo", [2] is "bar" and the rest is null (the first
 * element points to the copy of the path so it can be freed).
 *
 * The allocated strings are URI decoded.
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

/*
 * Maps a regex of the request path to a handler of the request
 */
struct httpd_uri_map
{
  int method;
  char *regexp;
  int (*handler)(struct httpd_request *hreq);
  regex_t preg;
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

/*
 * Parse a request into the httpd_request struct. It can later be freed with
 * free(), unless the module has allocated something to *extra_data. Note that
 * the pointers in the returned struct are only valid as long as the inputs are
 * still valid. If req is not null, then we will find the user-agent from the 
 * request headers, except if provided as an argument to this function.
 */
struct httpd_request *
httpd_request_parse(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed, const char *user_agent, struct httpd_uri_map *uri_map);

void
httpd_stream_file(struct evhttp_request *req, int id);

bool
httpd_request_not_modified_since(struct evhttp_request *req, const time_t *mtime);

bool
httpd_request_etag_matches(struct evhttp_request *req, const char *etag);

/*
 * Gzips an evbuffer
 *
 * @in  in       Data to be compressed
 * @return       Compressed data - must be freed by caller
 */
struct evbuffer *
httpd_gzip_deflate(struct evbuffer *in);

/*
 * This wrapper around evhttp_send_reply should be used whenever a request may
 * come from a browser. It will automatically gzip if feasible, but the caller
 * may direct it not to. It will set CORS headers as appropriate. Should be
 * thread safe.
 *
 * @in  req      The evhttp request struct
 * @in  code     HTTP code, e.g. 200
 * @in  reason   A brief explanation of the error - if NULL the standard meaning
                 of the error code will be used
 * @in  evbuf    Data for the response body
 * @in  flags    See flags above
 */
void
httpd_send_reply(struct evhttp_request *req, int code, const char *reason, struct evbuffer *evbuf, enum httpd_send_flags flags);

/*
 * This is a substitute for evhttp_send_error that should be used whenever an
 * error may be returned to a browser. It will set CORS headers as appropriate,
 * which is not possible with evhttp_send_error, because it clears the headers.
 * Should be thread safe.
 *
 * @in  req      The evhttp request struct
 * @in  error    HTTP code, e.g. 200
 * @in  reason   A brief explanation of the error - if NULL the standard meaning
                 of the error code will be used
 */
void
httpd_send_error(struct evhttp_request *req, int error, const char *reason);

/*
 * Redirects to /admin.html
 */
void
httpd_redirect_to_admin(struct evhttp_request *req);


bool
httpd_admin_check_auth(struct evhttp_request *req);

int
httpd_basic_auth(struct evhttp_request *req, const char *user, const char *passwd, const char *realm);

int
httpd_init(const char *webroot);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
