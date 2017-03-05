/*
 * Copyright (C) 2017 Espen Jurgensen
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include <event2/buffer.h>

#include "transcode.h"
#include "http.h"
#include "misc.h"
#include "input.h"

static int
setup(struct player_source *ps)
{
  ps->input_ctx = transcode_setup(XCODE_PCM16_NOHEADER, ps->data_kind, ps->path, ps->len_ms, NULL);
  if (!ps->input_ctx)
    return -1;

  ps->setup_done = 1;

  return 0;
}

static int
setup_http(struct player_source *ps)
{
  char *url;

  if (http_stream_setup(&url, ps->path) < 0)
    return -1;

  free(ps->path);
  ps->path = url;

  return setup(ps);
}

static int
start(struct player_source *ps)
{
  struct evbuffer *evbuf;
  short flags;
  int ret;
  int icy_timer;

  evbuf = evbuffer_new();

  ret = -1;
  flags = 0;
  while (!input_loop_break && !(flags & INPUT_FLAG_EOF))
    {
      // We set "wanted" to 1 because the read size doesn't matter to us
      // TODO optimize?
      ret = transcode(evbuf, &icy_timer, ps->input_ctx, 1);
      if (ret < 0)
	break;

      flags = ((ret == 0) ? INPUT_FLAG_EOF : 0) |
               (icy_timer ? INPUT_FLAG_METADATA : 0);

      ret = input_write(evbuf, flags);
      if (ret < 0)
	break;
    }

  evbuffer_free(evbuf);

  return ret;
}

static int
stop(struct player_source *ps)
{
  struct transcode_ctx *ctx = ps->input_ctx;

  transcode_cleanup(&ctx);

  ps->input_ctx = NULL;
  ps->setup_done = 0;

  return 0;
}

static int
seek(struct player_source *ps, int seek_ms)
{
  return transcode_seek(ps->input_ctx, seek_ms);
}

static int
metadata_get_http(struct input_metadata *metadata, struct player_source *ps, uint64_t rtptime)
{
  struct http_icy_metadata *m;
  int changed;

  m = transcode_metadata(ps->input_ctx, &changed);
  if (!m)
    return -1;

  if (!changed)
    {
      http_icy_metadata_free(m, 0);
      return -1; // TODO Perhaps a problem since this prohibits the player updating metadata
    }

  if (m->artist)
    swap_pointers(&metadata->artist, &m->artist);
  // Note we map title to album, because clients should show stream name as titel
  if (m->title)
    swap_pointers(&metadata->album, &m->title);
  if (m->artwork_url)
    swap_pointers(&metadata->artwork_url, &m->artwork_url);

  http_icy_metadata_free(m, 0);
  return 0;
}

struct input_definition input_file =
{
  .name = "file",
  .type = INPUT_TYPE_FILE,
  .disabled = 0,
  .setup = setup,
  .start = start,
  .stop = stop,
  .seek = seek,
};

struct input_definition input_http =
{
  .name = "http",
  .type = INPUT_TYPE_HTTP,
  .disabled = 0,
  .setup = setup_http,
  .start = start,
  .stop = stop,
  .metadata_get = metadata_get_http,
};
