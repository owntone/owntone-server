
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

#include <event.h>
#include "evhttp/evhttp.h"

int
daap_init(void);

void
daap_deinit(void);

void
daap_request(struct evhttp_request *req);

int
daap_is_request(struct evhttp_request *req, char *uri);

#endif /* !__HTTPD_DAAP_H__ */
