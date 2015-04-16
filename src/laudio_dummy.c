/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "conffile.h"
#include "logger.h"
#include "misc.h"
#include "player.h"
#include "laudio.h"


static enum laudio_state pcm_status;
static laudio_status_cb status_cb;

static struct timespec timer_res;
static struct timespec ts;
static uint64_t pcmpos;



static uint64_t
laudio_dummy_get_pos(void)
{
  struct timespec cur_timer_res;
  struct timespec cur_ts;
  uint64_t delta;
  int ret;

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, &cur_ts, &cur_timer_res);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Couldn't get clock: %s\n", strerror(errno));
      return -1;
    }

  delta = (cur_ts.tv_sec - ts.tv_sec) * 1000000 + (cur_ts.tv_nsec - ts.tv_nsec) / 1000;
  delta = (delta * 44100) / 1000000;

  DPRINTF(E_DBG, L_LAUDIO, "Start: %" PRIu64 ", Pos: %" PRIu64 "\n", pcmpos, delta);

  return (pcmpos + delta);
}

static void
laudio_dummy_write(uint8_t *buf, uint64_t rtptime)
{
  uint64_t pos;

  pos = laudio_dummy_get_pos();

  if (pcm_status != LAUDIO_RUNNING && pos > (pcmpos + 88200))
    {
      pcm_status = LAUDIO_RUNNING;
      status_cb(LAUDIO_RUNNING);
    }
}

static void
laudio_dummy_set_volume(int vol)
{
}

static int
laudio_dummy_start(uint64_t cur_pos, uint64_t next_pkt)
{
  int ret;

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, &ts, &timer_res);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Couldn't get current clock: %s\n", strerror(errno));
      return -1;
    }

  pcmpos = cur_pos;

  pcm_status = LAUDIO_STARTED;
  status_cb(LAUDIO_STARTED);

  return 0;
}

static void
laudio_dummy_stop(void)
{
  pcm_status = LAUDIO_STOPPING;
  status_cb(LAUDIO_STOPPING);
  pcm_status = LAUDIO_OPEN;
  status_cb(LAUDIO_OPEN);
}

static int
laudio_dummy_open(void)
{
  pcm_status = LAUDIO_OPEN;
  status_cb(LAUDIO_OPEN);
  return 0;
}

static void
laudio_dummy_close(void)
{
  pcm_status = LAUDIO_CLOSED;
  status_cb(LAUDIO_CLOSED);
}


static int
laudio_dummy_init(laudio_status_cb cb, cfg_t *cfg_audio)
{
  status_cb = cb;
  return 0;
}

static void
laudio_dummy_deinit(void)
{
}

audio_output audio_dummy = {
    .name = "dummy",
    .init = &laudio_dummy_init,
    .deinit = &laudio_dummy_deinit,
    .start = &laudio_dummy_start,
    .stop = &laudio_dummy_stop,
    .open = &laudio_dummy_open,
    .close = &laudio_dummy_close,
    .pos = &laudio_dummy_get_pos,
    .write = &laudio_dummy_write,
    .volume = &laudio_dummy_set_volume,
    };
