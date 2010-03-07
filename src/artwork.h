
#ifndef __ARTWORK_H__
#define __ARTWORK_H__

int
artwork_get_item(int id, int max_w, int max_h, struct evbuffer *evbuf);

int
artwork_get_group(int id, int max_w, int max_h, struct evbuffer *evbuf);

#endif /* !__ARTWORK_H__ */
