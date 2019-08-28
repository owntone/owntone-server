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
#include "logger.h"
#include "input.h"

static int
setup(struct input_source *source)
{
  struct transcode_ctx *ctx;

  ctx = transcode_setup(XCODE_PCM_NATIVE, NULL, source->data_kind, source->path, source->len_ms, NULL);
  if (!ctx)
    return -1;

  CHECK_NULL(L_PLAYER, source->evbuf = evbuffer_new());

  source->quality.sample_rate = transcode_encode_query(ctx->encode_ctx, "sample_rate");
  source->quality.bits_per_sample = transcode_encode_query(ctx->encode_ctx, "bits_per_sample");
  source->quality.channels = transcode_encode_query(ctx->encode_ctx, "channels");

  source->input_ctx = ctx;

  return 0;
}

static int
setup_http(struct input_source *source)
{
  char *url;

  if (http_stream_setup(&url, source->path) < 0)
    return -1;

  free(source->path);
  source->path = url;

  return setup(source);
}

static int
stop(struct input_source *source)
{
  struct transcode_ctx *ctx = source->input_ctx;

  transcode_cleanup(&ctx);

  if (source->evbuf)
    evbuffer_free(source->evbuf);

  source->input_ctx = NULL;
  source->evbuf = NULL;

  return 0;
}

static int
play(struct input_source *source)
{
  struct transcode_ctx *ctx = source->input_ctx;
  int icy_timer;
  int ret;
  short flags;

  // We set "wanted" to 1 because the read size doesn't matter to us
  // TODO optimize?
  ret = transcode(source->evbuf, &icy_timer, ctx, 1);
  if (ret == 0)
    {
      input_write(source->evbuf, &source->quality, INPUT_FLAG_EOF);
      stop(source);
      return -1;
    }
  else if (ret < 0)
    {
      input_write(NULL, NULL, INPUT_FLAG_ERROR);
      stop(source);
      return -1;
    }

  flags = (icy_timer ? INPUT_FLAG_METADATA : 0);

  input_write(source->evbuf, &source->quality, flags);

  return 0;
}

static int
seek(struct input_source *source, int seek_ms)
{
  return transcode_seek(source->input_ctx, seek_ms);
}

static int
seek_http(struct input_source *source, int seek_ms)
{
  // Stream is live/unknown length so can't seek. We return 0 anyway, because
  // it is valid for the input to request a seek, since the input is not
  // supposed to concern itself about this.
  if (source->len_ms == 0)
    return 0;

  return transcode_seek(source->input_ctx, seek_ms);
}

static int
metadata_get_http(struct input_metadata *metadata, struct input_source *source)
{
  struct http_icy_metadata *m;
  int changed;

  m = transcode_metadata(source->input_ctx, &changed);
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
  .play = play,
  .stop = stop,
  .seek = seek,
};

struct input_definition input_http =
{
  .name = "http",
  .type = INPUT_TYPE_HTTP,
  .disabled = 0,
  .setup = setup_http,
  .play = play,
  .stop = stop,
  .metadata_get = metadata_get_http,
  .seek = seek_http
};
