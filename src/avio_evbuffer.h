
#ifndef __AVIO_EVBUFFER_H__
#define __AVIO_EVBUFFER_H__

#include <event2/buffer.h>

AVIOContext *
avio_input_evbuffer_open(struct evbuffer *evbuf);

AVIOContext *
avio_output_evbuffer_open(struct evbuffer *evbuf);

void
avio_evbuffer_close(AVIOContext *s);

#endif /* !__AVIO_EVBUFFER_H__ */
