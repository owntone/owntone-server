
#ifndef __ARTWORK_H__
#define __ARTWORK_H__

#define ART_CAN_PNG     (1 << 0)
#define ART_CAN_JPEG    (1 << 1)

#define ART_FMT_PNG     1
#define ART_FMT_JPEG    2


int
artwork_get_item_filename(char *filename, int max_w, int max_h, int format, struct evbuffer *evbuf);

int
artwork_get_item(int id, int max_w, int max_h, int format, struct evbuffer *evbuf);

int
artwork_get_group(int id, int max_w, int max_h, int format, struct evbuffer *evbuf);

#endif /* !__ARTWORK_H__ */
