
#ifndef __ARTWORK_H__
#define __ARTWORK_H__

#define ART_CAN_PNG     (1 << 0)
#define ART_CAN_JPEG    (1 << 1)

#define ART_FMT_PNG     1
#define ART_FMT_JPEG    2

#ifdef HAVE_LIBEVENT2
# include <event2/buffer.h>
#else
# include <event.h>
#endif

/* Get artwork for individual track */
int
artwork_get_item(int id, int max_w, int max_h, int format, struct evbuffer *evbuf);

/* Get artwork for album or artist */
int
artwork_get_group(int id, int max_w, int max_h, int format, struct evbuffer *evbuf);

#endif /* !__ARTWORK_H__ */
