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

#include "input.h"
#include "logger.h"
#include "spotify.h"

// How many retries to start playback if resource is still loading
#define SPOTIFY_SETUP_RETRIES 5
// How long to wait between retries in microseconds (500000 = 0.5 seconds)
#define SPOTIFY_SETUP_RETRY_WAIT 500000

static int
setup(struct player_source *ps)
{
  int i = 0;
  int ret;

  while((ret = spotify_playback_setup(ps->path)) == SPOTIFY_SETUP_ERROR_IS_LOADING)
    {
      if (i >= SPOTIFY_SETUP_RETRIES)
	break;

      DPRINTF(E_DBG, L_SPOTIFY, "Resource still loading (%d)\n", i);
      usleep(SPOTIFY_SETUP_RETRY_WAIT);
      i++;
    }

  if (ret < 0)
    return -1;

  ps->setup_done = 1;

  return 0;
}

static int
start(struct player_source *ps)
{
  int ret;

  ret = spotify_playback_play();
  if (ret < 0)
    return -1;

  while (!input_loop_break)
    {
      input_wait();
    }

  ret = spotify_playback_pause();

  return ret;
}

static int
stop(struct player_source *ps)
{
  int ret;

  ret = spotify_playback_stop();
  if (ret < 0)
    return -1;

  ps->setup_done = 0;

  return 0;
}

static int
seek(struct player_source *ps, int seek_ms)
{
  int ret;

  ret = spotify_playback_seek(seek_ms);
  if (ret < 0)
    return -1;

  return ret;
}

struct input_definition input_spotify =
{
  .name = "Spotify",
  .type = INPUT_TYPE_SPOTIFY,
  .disabled = 0,
  .setup = setup,
  .start = start,
  .stop = stop,
  .seek = seek,
};

