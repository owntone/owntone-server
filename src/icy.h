
#ifndef __ICY_H__
#define __ICY_H__

#include <libavformat/avformat.h>

struct icy_metadata
{
  /* Static stream metadata from icy_metadata_headers */
  char *name;
  char *description;
  char *genre;

  /* Track specific, comes from icy_metadata_packet */
  char *title;
  char *artist;
  char *artwork_url;

  uint32_t hash;
};

void
icy_metadata_free(struct icy_metadata *metadata);

struct icy_metadata *
icy_metadata_get(AVFormatContext *fmtctx, int packet_only);

#endif /* !__ICY_H__ */
