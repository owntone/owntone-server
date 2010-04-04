/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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

#include <string.h>
#include <stdint.h>

#include <event.h>
#include "evhttp/evhttp.h"

#include "dmap_helpers.h"
#include "logger.h"


void
dmap_add_container(struct evbuffer *evbuf, char *tag, int len)
{
  unsigned char buf[4];

  evbuffer_add(evbuf, tag, 4);

  /* Container length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

void
dmap_add_long(struct evbuffer *evbuf, char *tag, int64_t val)
{
  unsigned char buf[12];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 8;

  /* Value */
  buf[4] = (val >> 56) & 0xff;
  buf[5] = (val >> 48) & 0xff;
  buf[6] = (val >> 40) & 0xff;
  buf[7] = (val >> 32) & 0xff;
  buf[8] = (val >> 24) & 0xff;
  buf[9] = (val >> 16) & 0xff;
  buf[10] = (val >> 8) & 0xff;
  buf[11] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

void
dmap_add_int(struct evbuffer *evbuf, char *tag, int val)
{
  unsigned char buf[8];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 4;

  /* Value */
  buf[4] = (val >> 24) & 0xff;
  buf[5] = (val >> 16) & 0xff;
  buf[6] = (val >> 8) & 0xff;
  buf[7] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

void
dmap_add_short(struct evbuffer *evbuf, char *tag, short val)
{
  unsigned char buf[6];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 2;

  /* Value */
  buf[4] = (val >> 8) & 0xff;
  buf[5] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

void
dmap_add_char(struct evbuffer *evbuf, char *tag, char val)
{
  unsigned char buf[5];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 1;

  /* Value */
  buf[4] = val;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

void
dmap_add_literal(struct evbuffer *evbuf, char *tag, char *str, int len)
{
  char buf[4];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));

  if (str && (len > 0))
    evbuffer_add(evbuf, str, len);
}

void
dmap_add_string(struct evbuffer *evbuf, char *tag, const char *str)
{
  unsigned char buf[4];
  int len;

  if (str)
    len = strlen(str);
  else
    len = 0;

  evbuffer_add(evbuf, tag, 4);

  /* String length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));

  if (len)
    evbuffer_add(evbuf, str, len);
}

void
dmap_send_error(struct evhttp_request *req, char *container, char *errmsg)
{
  struct evbuffer *evbuf;
  int len;
  int ret;

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for DAAP error\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return;
    }

  len = 12 + 8 + 8 + strlen(errmsg);

  ret = evbuffer_expand(evbuf, len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP error\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      evbuffer_free(evbuf);
      return;
    }

  dmap_add_container(evbuf, container, len - 8);
  dmap_add_int(evbuf, "mstt", 500);
  dmap_add_string(evbuf, "msts", errmsg);

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  evbuffer_free(evbuf);
}
