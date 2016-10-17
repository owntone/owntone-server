
#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <event2/http.h>
#include <event2/buffer.h>

enum httpd_send_flags
{
  HTTPD_SEND_NO_GZIP =   (1 << 0),
};

void
httpd_stream_file(struct evhttp_request *req, int id);

void
httpd_send_reply(struct evhttp_request *req, int code, const char *reason, struct evbuffer *evbuf, enum httpd_send_flags flags);

char *
httpd_fixup_uri(struct evhttp_request *req);

int
httpd_basic_auth(struct evhttp_request *req, char *user, char *passwd, char *realm);

int
httpd_init(void);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
