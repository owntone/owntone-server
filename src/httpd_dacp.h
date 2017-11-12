
#ifndef __HTTPD_DACP_H__
#define __HTTPD_DACP_H__

#include "httpd.h"

int
dacp_init(void);

void
dacp_deinit(void);

void
dacp_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
dacp_is_request(const char *path);

#endif /* !__HTTPD_DACP_H__ */
