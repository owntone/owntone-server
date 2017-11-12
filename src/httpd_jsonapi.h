
#ifndef __HTTPD_JSONAPI_H__
#define __HTTPD_JSONAPI_H__

#include "httpd.h"

int
jsonapi_init(void);

void
jsonapi_deinit(void);

void
jsonapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
jsonapi_is_request(const char *path);

#endif /* !__HTTPD_JSONAPI_H__ */
