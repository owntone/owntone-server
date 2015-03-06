
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

#ifdef HAVE_LIBEVENT2
# include <event2/http.h>
#else
# include "evhttp/evhttp_compat.h"
#endif

int
daap_init(void);

void
daap_deinit(void);

void
daap_request(struct evhttp_request *req);

int
daap_is_request(struct evhttp_request *req, char *uri);

struct evbuffer *
daap_reply_build(char *full_uri, const char *ua);

#endif /* !__HTTPD_DAAP_H__ */
