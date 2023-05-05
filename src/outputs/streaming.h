
#ifndef __STREAMING_H__
#define __STREAMING_H__

typedef void (*streaming_metadatacb)(char *metadata);

enum streaming_format
{
  STREAMING_FORMAT_MP3,
};

void
streaming_metadatacb_register(streaming_metadatacb cb);

#endif /* !__STREAMING_H__ */
