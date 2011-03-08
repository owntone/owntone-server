
#ifndef __DMAP_HELPERS_H__
#define __DMAP_HELPERS_H__

#include <event.h>
#include "evhttp/evhttp.h"

enum dmap_type
  {
    DMAP_TYPE_UBYTE   = 0x01,
    DMAP_TYPE_BYTE    = 0x02,
    DMAP_TYPE_USHORT  = 0x03,
    DMAP_TYPE_SHORT   = 0x04,
    DMAP_TYPE_UINT    = 0x05,
    DMAP_TYPE_INT     = 0x06,
    DMAP_TYPE_ULONG   = 0x07,
    DMAP_TYPE_LONG    = 0x08,
    DMAP_TYPE_STRING  = 0x09,
    DMAP_TYPE_DATE    = 0x0a,
    DMAP_TYPE_VERSION = 0x0b,
    DMAP_TYPE_LIST    = 0x0c,
  };

void
dmap_add_container(struct evbuffer *evbuf, char *tag, int len);

void
dmap_add_long(struct evbuffer *evbuf, char *tag, int64_t val);

void
dmap_add_int(struct evbuffer *evbuf, char *tag, int val);

void
dmap_add_short(struct evbuffer *evbuf, char *tag, short val);

void
dmap_add_char(struct evbuffer *evbuf, char *tag, char val);

void
dmap_add_literal(struct evbuffer *evbuf, char *tag, char *str, int len);

void
dmap_add_string(struct evbuffer *evbuf, char *tag, const char *str);

void
dmap_send_error(struct evhttp_request *req, char *container, char *errmsg);

#endif /* !__DMAP_HELPERS_H__ */
