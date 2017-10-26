
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

#include <event2/http.h>

int
daap_init(void);

void
daap_deinit(void);

void
daap_request(struct evhttp_request *req);

int
daap_is_request(struct evhttp_request *req, char *uri);

int
daap_session_is_valid(int id);

struct evbuffer *
daap_reply_build(const char *source_uri, const char *user_agent);

#endif /* !__HTTPD_DAAP_H__ */
