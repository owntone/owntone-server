
#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <event2/http.h>
#include <event2/buffer.h>
#include <stdbool.h>

enum httpd_send_flags
{
  HTTPD_SEND_NO_GZIP =   (1 << 0),
};

void
httpd_stream_file(struct evhttp_request *req, int id);

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

char *
httpd_fixup_uri(struct evhttp_request *req);

int
httpd_basic_auth(struct evhttp_request *req, const char *user, const char *passwd, const char *realm);

bool
httpd_admin_check_auth(struct evhttp_request *req);

int
httpd_init(void);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
