#ifndef __HTTPD_OAUTH_H__
#define __HTTPD_OAUTH_H__

#include "httpd.h"

void
oauth_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
oauth_is_request(const char *path);

int
oauth_init(void);

void
oauth_deinit(void);

#endif /* !__HTTPD_OAUTH_H__ */
