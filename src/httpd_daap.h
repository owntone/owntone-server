
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

#include "httpd.h"

int
daap_init(void);

void
daap_deinit(void);

void
daap_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
daap_is_request(const char *path);

int
daap_session_is_valid(int id);

struct evbuffer *
daap_reply_build(const char *uri, const char *user_agent, int is_remote);

#endif /* !__HTTPD_DAAP_H__ */
