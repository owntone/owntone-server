#ifndef __HTTPD_ARTWORK_H__
#define __HTTPD_ARTWORK_H__

#include "httpd.h"

int
artworkapi_init(void);

void
artworkapi_deinit(void);

void
artworkapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
artworkapi_is_request(const char *path);

#endif
