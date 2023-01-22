
#ifndef __STREAMING_H__
#define __STREAMING_H__

#include "misc.h" // struct media_quality

typedef void (*streaming_metadatacb)(char *metadata);

enum streaming_format
{
  STREAMING_FORMAT_MP3,
};

int
streaming_session_register(enum streaming_format format, struct media_quality quality);

void
streaming_session_deregister(int readfd);

void
streaming_metadatacb_register(streaming_metadatacb cb);

#endif /* !__STREAMING_H__ */
