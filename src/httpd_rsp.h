
#ifndef __HTTPD_RSP_H__
#define __HTTPD_RSP_H__

#include <event.h>
#ifdef HAVE_LIBEVENT2
# include <event2/http.h>
#else
# include "evhttp/evhttp_compat.h"
#endif

int
rsp_init(void);

void
rsp_deinit(void);

void
rsp_request(struct evhttp_request *req);

int
rsp_is_request(struct evhttp_request *req, char *uri);

#endif /* !__HTTPD_RSP_H__ */
