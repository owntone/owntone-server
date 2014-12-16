/*
 * Copyright (C) 2011 Julien BLACHE <jb@jblache.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>

#include <libavformat/avformat.h>

#include "logger.h"
#include "avio_evbuffer.h"

/*
 * libav AVIO interface for evbuffers
 */

#define BUFFER_SIZE 4096

struct avio_evbuffer {
  struct evbuffer *evbuf;
  uint8_t *buffer;
};


static int
avio_evbuffer_write(void *opaque, uint8_t *buf, int size)
{
  struct avio_evbuffer *ae;
  int ret;

  ae = (struct avio_evbuffer *)opaque;

  ret = evbuffer_add(ae->evbuf, buf, size);

  return (ret == 0) ? size : -1;
}

AVIOContext *
avio_evbuffer_open(struct evbuffer *evbuf)
{
  struct avio_evbuffer *ae;
  AVIOContext *s;

  ae = (struct avio_evbuffer *)malloc(sizeof(struct avio_evbuffer));
  if (!ae)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Out of memory for avio_evbuffer\n");

      return NULL;
    }

  ae->buffer = av_mallocz(BUFFER_SIZE);
  if (!ae->buffer)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Out of memory for avio buffer\n");

      free(ae);
      return NULL;
    }

  ae->evbuf = evbuf;

  s = avio_alloc_context(ae->buffer, BUFFER_SIZE, 1, ae, NULL, avio_evbuffer_write, NULL);
  if (!s)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Could not allocate AVIOContext\n");

      av_free(ae->buffer);
      free(ae);
      return NULL;
    }

  s->seekable = 0;

  return s;
}

void
avio_evbuffer_close(AVIOContext *s)
{
  struct avio_evbuffer *ae;

  ae = (struct avio_evbuffer *)s->opaque;

  avio_flush(s);

  av_free(ae->buffer);
  free(ae);

  av_free(s);
}
