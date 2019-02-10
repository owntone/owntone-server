
#ifndef __HTTPD_STREAMING_H__
#define __HTTPD_STREAMING_H__

#include "httpd.h"
#include "outputs.h"

/* httpd_streaming takes care of incoming requests to /stream.mp3
 * It will receive decoded audio from the player, and encode it, and
 * stream it to one or more clients. It will not be available
 * if a suitable ffmpeg/libav encoder is not present at runtime.
 */

void
streaming_write(struct output_buffer *obuf);

int
streaming_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed);

int
streaming_is_request(const char *path);

int
streaming_init(void);

void
streaming_deinit(void);

#endif /* !__HTTPD_STREAMING_H__ */
