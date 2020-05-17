/*
 * Copyright (C) 2017-2020 Espen Jurgensen
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
#include "misc.h"
#include "logger.h"
#include "input.h"

/*---------------------------- Input implementation --------------------------*/

// Important! If you change any of the below then consider if the change also
// should be made in http.c

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
  int ret;

  // We set "wanted" to 1 because the read size doesn't matter to us
  // TODO optimize?
  ret = transcode(source->evbuf, NULL, ctx, 1);
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

  input_write(source->evbuf, &source->quality, 0);

  return 0;
}

static int
seek(struct input_source *source, int seek_ms)
{
  return transcode_seek(source->input_ctx, seek_ms);
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
