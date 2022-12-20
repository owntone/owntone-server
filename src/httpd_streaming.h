
#ifndef __HTTPD_STREAMING_H__
#define __HTTPD_STREAMING_H__

#include "outputs.h"

/* httpd_streaming takes care of incoming requests to /stream.mp3
 * It will receive decoded audio from the player, and encode it, and
 * stream it to one or more clients. It will not be available
 * if a suitable ffmpeg/libav encoder is not present at runtime.
 */

void
streaming_write(struct output_buffer *obuf);

#endif /* !__HTTPD_STREAMING_H__ */
