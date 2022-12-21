
#ifndef __HTTPD_H__
#define __HTTPD_H__

#include <event2/buffer.h>

/*
 * Gzips an evbuffer
 *
 * @in  in       Data to be compressed
 * @return       Compressed data - must be freed by caller
 */
struct evbuffer *
httpd_gzip_deflate(struct evbuffer *in);

int
httpd_init(const char *webroot);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
