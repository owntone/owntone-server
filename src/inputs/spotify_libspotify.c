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
#include "conffile.h"
#include "logger.h"
#include "spotify.h"
#include "libspotify/libspotify.h"

// How many retries to start playback if resource is still loading
#define LIBSPOTIFY_SETUP_RETRIES 5
// How long to wait between retries in microseconds (500000 = 0.5 seconds)
#define LIBSPOTIFY_SETUP_RETRY_WAIT 500000

static int
init(void)
{
  return cfg_getbool(cfg_getsec(cfg, "spotify"), "use_libspotify") ? 0 : -1;
}

static int
setup(struct input_source *source)
{
  int i = 0;
  int ret;

  while((ret = libspotify_playback_setup(source->path)) == LIBSPOTIFY_SETUP_ERROR_IS_LOADING)
    {
      if (i >= LIBSPOTIFY_SETUP_RETRIES)
	break;

      DPRINTF(E_DBG, L_SPOTIFY, "Resource still loading (%d)\n", i);
      usleep(LIBSPOTIFY_SETUP_RETRY_WAIT);
      i++;
    }

  if (ret < 0)
    return -1;

  ret = libspotify_playback_play();
  if (ret < 0)
    return -1;

  return 0;
}

static int
stop(struct input_source *source)
{
  int ret;

  ret = libspotify_playback_stop();
  if (ret < 0)
    return -1;

  return 0;
}

static int
seek(struct input_source *source, int seek_ms)
{
  int ret;

  ret = libspotify_playback_seek(seek_ms);
  if (ret < 0)
    return -1;

  return ret;
}

struct input_definition input_libspotify =
{
  .name = "libspotify",
  .type = INPUT_TYPE_LIBSPOTIFY,
  .disabled = 0,
  .init = init,
  .setup = setup,
  .stop = stop,
  .seek = seek,
};


// No-op for libspotify since it doesn't support logging in with the web api token
static int
login_token(const char *username, const char *token, const char **errmsg)
{
  return 0;
}

static void
status_get(struct spotify_status *status)
{
  struct spotify_status_info info = { 0 };

  libspotify_status_info_get(&info);

  status->installed = info.libspotify_installed;
  status->logged_in = info.libspotify_logged_in;
  snprintf(status->username, sizeof(status->username), "%s", info.libspotify_user);
}

struct spotify_backend spotify_libspotify =
{
  .init = libspotify_init,
  .deinit = libspotify_deinit,
  .login = libspotify_login,
  .login_token = login_token,
  .logout = libspotify_logout,
  .relogin = libspotify_relogin,
  .uri_register = libspotify_uri_register,
  .status_get = status_get,
};

