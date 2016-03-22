
#ifndef __HTTPD_STREAMING_H__
#define __HTTPD_STREAMING_H__

#include <event2/http.h>

/* httpd_streaming takes care of incoming requests to /stream.mp3
 * It will receive decoded audio from the player, and encode it, and
 * stream it to one or more clients. It will not be available
 * if a suitable ffmpeg/libav encoder is not present at runtime.
 */

void
streaming_write(uint8_t *buf, uint64_t rtptime);

int
streaming_is_request(struct evhttp_request *req, char *uri);

int
streaming_request(struct evhttp_request *req);

int
streaming_init(void);

void
streaming_deinit(void);

#endif /* !__HTTPD_STREAMING_H__ */
