
#ifndef __HTTPD_DAAP_H__
#define __HTTPD_DAAP_H__

int
daap_session_is_valid(int id);

struct evbuffer *
daap_reply_build(const char *uri, const char *user_agent, int is_remote);

#endif /* !__HTTPD_DAAP_H__ */
