
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

#include <event.h>
#ifdef HAVE_LIBEVENT2
# include <evhttp.h>
#else
# include "evhttp/evhttp.h"
#endif

int
daap_init(void);

void
daap_deinit(void);

void
daap_request(struct evhttp_request *req);

int
daap_is_request(struct evhttp_request *req, char *uri);

#endif /* !__HTTPD_DAAP_H__ */
