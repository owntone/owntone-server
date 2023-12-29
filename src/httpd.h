
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

/*
 * Passthrough to transcode, which will create a transcoded file header for path
 *
 * @out header   Newly created evbuffer with the header
 * @in  format   Which format caller wants a header for
 * @in  path     Path to the file
 * @return       0 if ok, otherwise -1
 */
int
httpd_prepare_header(struct evbuffer **header, const char *format, const char *path);

int
httpd_init(const char *webroot);

void
httpd_deinit(void);

#endif /* !__HTTPD_H__ */
