
#ifndef __HTTPD_JSONAPI_H__
#define __HTTPD_JSONAPI_H__

#include <event2/http.h>

int
jsonapi_init(void);

void
jsonapi_deinit(void);

void
jsonapi_request(struct evhttp_request *req);

int
jsonapi_is_request(struct evhttp_request *req, char *uri);

#endif /* !__HTTPD_JSONAPI_H__ */
