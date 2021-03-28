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

static int
setup(struct input_source *source)
{
  return -1;
}

static int
stop(struct input_source *source)
{
  return -1;
}

static int
seek(struct input_source *source, int seek_ms)
{
  return -1;
}

struct input_definition input_spotify =
{
  .name = "Spotify",
  .type = INPUT_TYPE_SPOTIFY,
  .disabled = 0,
  .setup = setup,
  .stop = stop,
  .seek = seek,
};


/* ------------ Functions exposed via spotify.h, mocked for now ------------- */
// Th
int
spotify_login_user(const char *user, const char *password, char **errmsg)
{
  return -1;
}

void
spotify_login(char **arglist)
{
  return;
}


void
spotify_logout(void)
{
  return;
}

void
spotify_status_info_get(struct spotify_status_info *info)
{
  return;
}
