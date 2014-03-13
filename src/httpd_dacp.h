
#ifndef __HTTPD_DACP_H__
#define __HTTPD_DACP_H__

#include <event.h>
#ifdef HAVE_LIBEVENT2
# include <evhttp.h>
#else
# include "evhttp/evhttp.h"
#endif

int
dacp_init(void);

void
dacp_deinit(void);

void
dacp_request(struct evhttp_request *req);

int
dacp_is_request(struct evhttp_request *req, char *uri);

#endif /* !__HTTPD_DACP_H__ */
