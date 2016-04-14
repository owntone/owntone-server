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

#include <event2/event.h>

#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

struct dummy_session
{
  enum output_device_state state;

  struct event *deferredev;
  output_status_cb defer_cb;

  /* Do not dereference - only passed to the status cb */
  struct output_device *device;
  struct output_session *output_session;
  output_status_cb status_cb;
};

/* From player.c */
extern struct event_base *evbase_player;

struct dummy_session *sessions;

/* Forwards */
static void
defer_cb(int fd, short what, void *arg);

/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
dummy_session_free(struct dummy_session *ds)
{
  event_free(ds->deferredev);

  free(ds->output_session);
  free(ds);

  ds = NULL;
}

static void
dummy_session_cleanup(struct dummy_session *ds)
{
  // Normally some here code to remove from linked list - here we just say:
  sessions = NULL;

  dummy_session_free(ds);
}

static struct dummy_session *
dummy_session_make(struct output_device *device, output_status_cb cb)
{
  struct output_session *os;
  struct dummy_session *ds;

  os = calloc(1, sizeof(struct output_session));
  ds = calloc(1, sizeof(struct dummy_session));
  if (!os || !ds)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for dummy session\n");
      return NULL;
    }

  ds->deferredev = evtimer_new(evbase_player, defer_cb, ds);
  if (!ds->deferredev)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for dummy deferred event\n");
      free(os);
      free(ds);
      return NULL;
    }

  os->session = ds;
  os->type = device->type;

  ds->output_session = os;
  ds->state = OUTPUT_STATE_CONNECTED;
  ds->device = device;
  ds->status_cb = cb;

  sessions = ds;

  return ds;
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

// Maps our internal state to the generic output state and then makes a callback
// to the player to tell that state
static void
defer_cb(int fd, short what, void *arg)
{
  struct dummy_session *ds = arg;

  if (ds->defer_cb)
    ds->defer_cb(ds->device, ds->output_session, ds->state);

  if (ds->state == OUTPUT_STATE_STOPPED)
    dummy_session_cleanup(ds);
}

static void
dummy_status(struct dummy_session *ds)
{
  ds->defer_cb = ds->status_cb;
  event_active(ds->deferredev, 0, 0);
  ds->status_cb = NULL;
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
dummy_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  struct dummy_session *ds;

  ds = dummy_session_make(device, cb);
  if (!ds)
    return -1;

  dummy_status(ds);

  return 0;
}

static void
dummy_device_stop(struct output_session *session)
{
  struct dummy_session *ds = session->session;

  ds->state = OUTPUT_STATE_STOPPED;
  dummy_status(ds);
}

static int
dummy_device_probe(struct output_device *device, output_status_cb cb)
{
  struct dummy_session *ds;

  ds = dummy_session_make(device, cb);
  if (!ds)
    return -1;

  ds->status_cb = cb;
  ds->state = OUTPUT_STATE_STOPPED;

  dummy_status(ds);

  return 0;
}

static int
dummy_device_volume_set(struct output_device *device, output_status_cb cb)
{
  struct dummy_session *ds;

  if (!device->session || !device->session->session)
    return 0;

  ds = device->session->session;

  ds->status_cb = cb;
  dummy_status(ds);

  return 1;
}

static void
dummy_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  struct dummy_session *ds = sessions;

  if (!sessions)
    return;

  ds->state = OUTPUT_STATE_STREAMING;
  dummy_status(ds);
}

static void
dummy_playback_stop(void)
{
  struct dummy_session *ds = sessions;

  if (!sessions)
    return;

  ds->state = OUTPUT_STATE_CONNECTED;
  dummy_status(ds);
}

static void
dummy_set_status_cb(struct output_session *session, output_status_cb cb)
{
  struct dummy_session *ds = session->session;

  ds->status_cb = cb;
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

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for dummy device\n");
      return -1;
    }

  device->id = 0;
  device->name = nickname;
  device->type = OUTPUT_TYPE_DUMMY;
  device->type_name = outputs_name(device->type);
  device->advertised = 1;
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
  .device_probe = dummy_device_probe,
  .device_volume_set = dummy_device_volume_set,
  .playback_start = dummy_playback_start,
  .playback_stop = dummy_playback_stop,
  .status_cb = dummy_set_status_cb,
};
