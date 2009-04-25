
#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <event.h>
#include <evhttp.h>


void
httpd_stream_file(struct evhttp_request *req, int id);

int
httpd_init(void);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
