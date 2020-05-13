/*
 * Copyright (C) 2020 Espen Jurgensen
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
#include <string.h>

#include <event2/buffer.h>

#include "misc.h"
#include "logger.h"
#include "input.h"

#define TIMER_NOISE_INTERVAL 5
#define TIMER_METADATA_INTERVAL 30
#define TIMER_SAMPLE_RATE 44100
#define TIMER_BPS 16
#define TIMER_CHANNELS 2
#define TIMER_BUFSIZE (STOB(TIMER_SAMPLE_RATE, TIMER_BPS, TIMER_CHANNELS) / 20)

struct timer_ctx
{
  uint8_t silence[TIMER_BUFSIZE];
  uint8_t noise[TIMER_BUFSIZE];
  uint64_t pos;
};

static int
setup(struct input_source *source)
{
  struct timer_ctx *ctx;

  ctx = calloc(1, sizeof(struct timer_ctx));
  if (!ctx)
    return -1;

  memset(&ctx->noise, 0x88, TIMER_BUFSIZE);

  CHECK_NULL(L_PLAYER, source->evbuf = evbuffer_new());

  source->quality.sample_rate = TIMER_SAMPLE_RATE;
  source->quality.bits_per_sample = TIMER_BPS;
  source->quality.channels = TIMER_CHANNELS;

  source->input_ctx = ctx;

  return 0;
}

static int
stop(struct input_source *source)
{
  struct timer_ctx *ctx = source->input_ctx;

  free(ctx);

  if (source->evbuf)
    evbuffer_free(source->evbuf);

  source->input_ctx = NULL;
  source->evbuf = NULL;

  return 0;
}

static int
play(struct input_source *source)
{
  struct timer_ctx *ctx = source->input_ctx;
  short flags;

  if (ctx->pos % (TIMER_METADATA_INTERVAL * source->quality.sample_rate) == 0)
    flags = INPUT_FLAG_METADATA;
  else
    flags = 0;

  if (ctx->pos % (TIMER_NOISE_INTERVAL * source->quality.sample_rate) == 0)
    evbuffer_add(source->evbuf, ctx->noise, TIMER_BUFSIZE);
  else
    evbuffer_add(source->evbuf, ctx->silence, TIMER_BUFSIZE);

  ctx->pos += BTOS(TIMER_BUFSIZE, TIMER_BPS, TIMER_CHANNELS);

  input_write(source->evbuf, &source->quality, flags);

  return 0;
}

static int
metadata_get(struct input_metadata *metadata, struct input_source *source)
{
  metadata->title = strdup("Timing test");

  metadata->pos_is_updated = true;
  metadata->pos_ms = 0;
  metadata->len_ms = TIMER_METADATA_INTERVAL * 1000;
  return 0;
}

struct input_definition input_timer =
{
  .name = "timer",
  .type = INPUT_TYPE_TIMER,
  .disabled = 0,
  .setup = setup,
  .play = play,
  .stop = stop,
  .metadata_get = metadata_get,
};

