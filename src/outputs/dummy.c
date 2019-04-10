/*
 * Copyright (C) 2016 Espen Jürgensen <espenjurgensen@gmail.com>
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


/* This file includes much of the boilerplate code required for making an 
 * audio output for forked-daapd.
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

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

struct dummy_session
{
  enum output_device_state state;

  uint64_t device_id;
  int callback_id;
};

struct dummy_session *sessions;

/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
dummy_session_free(struct dummy_session *ds)
{
  if (!ds)
    return;

  free(ds);
}

static void
dummy_session_cleanup(struct dummy_session *ds)
{
  // Normally some here code to remove from linked list - here we just say:
  sessions = NULL;

  outputs_device_session_remove(ds->device_id);

  dummy_session_free(ds);
}

static struct dummy_session *
dummy_session_make(struct output_device *device, int callback_id)
{
  struct dummy_session *ds;

  CHECK_NULL(L_LAUDIO, ds = calloc(1, sizeof(struct dummy_session)));

  ds->state = OUTPUT_STATE_CONNECTED;
  ds->device_id = device->id;
  ds->callback_id = callback_id;

  sessions = ds;

  outputs_device_session_add(device->id, ds);

  return ds;
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

static void
dummy_status(struct dummy_session *ds)
{
  outputs_cb(ds->callback_id, ds->device_id, ds->state);

  if (ds->state == OUTPUT_STATE_STOPPED)
    dummy_session_cleanup(ds);
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
dummy_device_start(struct output_device *device, int callback_id)
{
  struct dummy_session *ds;

  ds = dummy_session_make(device, callback_id);
  if (!ds)
    return -1;

  dummy_status(ds);

  return 0;
}

static int
dummy_device_stop(struct output_device *device, int callback_id)
{
  struct dummy_session *ds = device->session;

  ds->callback_id = callback_id;
  ds->state = OUTPUT_STATE_STOPPED;

  dummy_status(ds);

  return 0;
}

static int
dummy_device_flush(struct output_device *device, int callback_id)
{
  struct dummy_session *ds = device->session;

  ds->callback_id = callback_id;
  ds->state = OUTPUT_STATE_STOPPED;

  dummy_status(ds);

  return 0;
}

static int
dummy_device_probe(struct output_device *device, int callback_id)
{
  struct dummy_session *ds;

  ds = dummy_session_make(device, callback_id);
  if (!ds)
    return -1;

  ds->callback_id = callback_id;
  ds->state = OUTPUT_STATE_STOPPED;

  dummy_status(ds);

  return 0;
}

static int
dummy_device_volume_set(struct output_device *device, int callback_id)
{
  struct dummy_session *ds = device->session;

  if (!ds)
    return 0;

  ds->callback_id = callback_id;
  dummy_status(ds);

  return 1;
}

static void
dummy_device_cb_set(struct output_device *device, int callback_id)
{
  struct dummy_session *ds = device->session;

  ds->callback_id = callback_id;
}

static int
dummy_init(void)
{
  struct output_device *device;
  cfg_t *cfg_audio;
  char *nickname;
  char *type;

  cfg_audio = cfg_getsec(cfg, "audio");
  type = cfg_getstr(cfg_audio, "type");
  if (!type || (strcasecmp(type, "dummy") != 0))
    return -1;

  nickname = cfg_getstr(cfg_audio, "nickname");

  CHECK_NULL(L_LAUDIO, device = calloc(1, sizeof(struct output_device)));

  device->id = 0;
  device->name = strdup(nickname);
  device->type = OUTPUT_TYPE_DUMMY;
  device->type_name = outputs_name(device->type);
  device->has_video = 0;

  DPRINTF(E_INFO, L_LAUDIO, "Adding dummy output device '%s'\n", nickname);

  player_device_add(device);

  return 0;
}

static void
dummy_deinit(void)
{
  return;
}

struct output_definition output_dummy =
{
  .name = "dummy",
  .type = OUTPUT_TYPE_DUMMY,
  .priority = 99,
  .disabled = 0,
  .init = dummy_init,
  .deinit = dummy_deinit,
  .device_start = dummy_device_start,
  .device_stop = dummy_device_stop,
  .device_flush = dummy_device_flush,
  .device_probe = dummy_device_probe,
  .device_volume_set = dummy_device_volume_set,
  .device_cb_set = dummy_device_cb_set,
};
