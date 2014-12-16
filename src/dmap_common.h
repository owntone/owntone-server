
#ifndef __DMAP_HELPERS_H__
#define __DMAP_HELPERS_H__

#ifdef HAVE_LIBEVENT2
# include <event2/buffer.h>
# include <event2/http.h>
#else
# include <event.h>
# include "evhttp/evhttp.h"
#endif

#include "db.h"

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

struct dmap_field_map {
  ssize_t mfi_offset;
  ssize_t pli_offset;
  ssize_t gri_offset;
};

struct dmap_field {
  char *desc;
  char *tag;
  const struct dmap_field_map *dfm;
  enum dmap_type type;
};


extern const struct dmap_field_map dfm_dmap_mimc;
extern const struct dmap_field_map dfm_dmap_aeSP;


const struct dmap_field *
dmap_get_fields_table(int *nfields);

/* From dmap_fields.gperf - keep in sync, don't alter */
const struct dmap_field *
dmap_find_field (register const char *str, register unsigned int len);


void
dmap_add_container(struct evbuffer *evbuf, const char *tag, int len);

void
dmap_add_long(struct evbuffer *evbuf, const char *tag, int64_t val);

void
dmap_add_int(struct evbuffer *evbuf, const char *tag, int val);

void
dmap_add_short(struct evbuffer *evbuf, const char *tag, short val);

void
dmap_add_char(struct evbuffer *evbuf, const char *tag, char val);

void
dmap_add_literal(struct evbuffer *evbuf, const char *tag, char *str, int len);

void
dmap_add_raw_uint32(struct evbuffer *evbuf, uint32_t val);

void
dmap_add_string(struct evbuffer *evbuf, const char *tag, const char *str);

void
dmap_add_field(struct evbuffer *evbuf, const struct dmap_field *df, char *strval, int32_t intval);


void
dmap_send_error(struct evhttp_request *req, const char *container, const char *errmsg);


int
dmap_encode_file_metadata(struct evbuffer *songlist, struct evbuffer *song, struct db_media_file_info *dbmfi, const struct dmap_field **meta, int nmeta, int sort_tags, int force_wav);

#endif /* !__DMAP_HELPERS_H__ */
