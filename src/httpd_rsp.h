
#ifndef __HTTPD_RSP_H__
#define __HTTPD_RSP_H__

#include "httpd.h"

int
rsp_init(void);

void
rsp_deinit(void);

void
rsp_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
rsp_is_request(const char *path);

#endif /* !__HTTPD_RSP_H__ */
