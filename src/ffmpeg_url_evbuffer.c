/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
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

#include <event.h>

#include "logger.h"
#include "ffmpeg_url_evbuffer.h"

/*
 * FFmpeg URL Protocol handler for evbuffers
 *
 * URL: evbuffer:0x03FB33DA ("evbuffer:%p")
 */


static int
url_evbuffer_open(URLContext *h, const char *filename, int flags)
{
  const char *p;
  char *end;
  unsigned long evbuffer_addr;

  if (flags != URL_WRONLY)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Flags other than URL_WRONLY not supported while opening '%s'\n", filename);

      return AVERROR(EIO);
    }

  p = strchr(filename, ':');
  if (!p)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Malformed evbuffer URL: '%s'\n", filename);

      return AVERROR(EIO);
    }

  p++;

  errno = 0;
  evbuffer_addr = strtoul(p, &end, 16);
  if (((errno == ERANGE) && (evbuffer_addr == ULONG_MAX))
      || ((errno != 0) && (evbuffer_addr == 0)))
    {
      DPRINTF(E_LOG, L_FFMPEG, "Invalid buffer address in URL: '%s'\n", filename);

      return AVERROR(EIO);
    }

  if (end == p)
    {
      DPRINTF(E_LOG, L_FFMPEG, "No buffer address found in URL: '%s'\n", filename);

      return AVERROR(EIO);
    }

  h->priv_data = (void *)evbuffer_addr;
  if (!h->priv_data)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Got a NULL buffer address from URL '%s'\n", filename);

      return AVERROR(EIO);
    }

  /* Seek not supported */
  h->is_streamed = 1;

  return 0;
}

static int
url_evbuffer_close(URLContext *h)
{
  h->priv_data = NULL;

  return 0;
}

static int
url_evbuffer_write(URLContext *h, unsigned char *buf, int size)
{
  struct evbuffer *evbuf;
  int ret;

  evbuf = (struct evbuffer *)h->priv_data;

  if (!evbuf)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Write called on evbuffer URL with priv_data = NULL!\n");

      return -1;
    }

  ret = evbuffer_add(evbuf, buf, size);

  return (ret == 0) ? size : -1;
}

URLProtocol evbuffer_protocol = {
  .name      = "evbuffer",
  .url_open  = url_evbuffer_open,
  .url_close = url_evbuffer_close,
  .url_write = url_evbuffer_write,
};

int
register_ffmpeg_evbuffer_url_protocol(void)
{
  int ret;

  ret = av_register_protocol(&evbuffer_protocol);

  return ret;
}
