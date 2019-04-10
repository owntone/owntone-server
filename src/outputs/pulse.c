/*
 * Copyright (C) 2016- Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <event2/event.h>
#include <pulse/pulseaudio.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "commands.h"

#define PULSE_MAX_DEVICES 64
#define PULSE_LOG_MAX 10

/* TODO for Pulseaudio
   - Add real sync with AirPlay
   - Allow per-sink latency config
*/

struct pulse
{
  pa_threaded_mainloop *mainloop;
  pa_context *context;

  struct commands_base *cmdbase;

  int operation_success;
} pulse;

struct pulse_session
{
  uint64_t device_id;
  int callback_id;

  char *devname;

  pa_stream_state_t state;
  pa_stream *stream;

  pa_buffer_attr attr;
  pa_volume_t volume;

  struct media_quality quality;

  int logcount;

  struct pulse_session *next;
};

// From player.c
extern struct event_base *evbase_player;

// Globals
static struct pulse_session *sessions;

// Internal list with indeces of the Pulseaudio devices (sinks) we have registered
static uint32_t pulse_known_devices[PULSE_MAX_DEVICES];

static struct media_quality pulse_last_quality;
static struct media_quality pulse_fallback_quality = { 44100, 16, 2 };

// Converts from 0 - 100 to Pulseaudio's scale
static inline pa_volume_t
pulse_from_device_volume(int device_volume)
{
  return (PA_VOLUME_MUTED + (device_volume * (PA_VOLUME_NORM - PA_VOLUME_MUTED)) / 100);
}

/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
pulse_session_free(struct pulse_session *ps)
{
  if (!ps)
    return;

  if (ps->stream)
    {
      pa_threaded_mainloop_lock(pulse.mainloop);

      pa_stream_set_underflow_callback(ps->stream, NULL, NULL);
      pa_stream_set_overflow_callback(ps->stream, NULL, NULL);
      pa_stream_set_state_callback(ps->stream, NULL, NULL);
      pa_stream_disconnect(ps->stream);
      pa_stream_unref(ps->stream);

      pa_threaded_mainloop_unlock(pulse.mainloop);
    }

  outputs_quality_unsubscribe(&pulse_fallback_quality);

  free(ps->devname);

  free(ps);
}

static void
pulse_session_cleanup(struct pulse_session *ps)
{
  struct pulse_session *p;

  if (ps == sessions)
    sessions = sessions->next;
  else
    {
      for (p = sessions; p && (p->next != ps); p = p->next)
	; /* EMPTY */

      if (!p)
	DPRINTF(E_WARN, L_LAUDIO, "WARNING: struct pulse_session not found in list; BUG!\n");
      else
	p->next = ps->next;
    }

  outputs_device_session_remove(ps->device_id);

  pulse_session_free(ps);
}

static struct pulse_session *
pulse_session_make(struct output_device *device, int callback_id)
{
  struct pulse_session *ps;
  int ret;

  ret = outputs_quality_subscribe(&pulse_fallback_quality);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not subscribe to fallback audio quality\n");
      return NULL;
    }

  CHECK_NULL(L_LAUDIO, ps = calloc(1, sizeof(struct pulse_session)));

  ps->state = PA_STREAM_UNCONNECTED;
  ps->device_id = device->id;
  ps->callback_id = callback_id;
  ps->volume = pulse_from_device_volume(device->volume);
  ps->devname = strdup(device->extra_device_info);

  ps->next = sessions;
  sessions = ps;

  outputs_device_session_add(device->id, ps);

  return ps;
}

/* ---------------------------- COMMAND HANDLERS ---------------------------- */

// Maps our internal state to the generic output state and then makes a callback
// to the player to tell that state. Should always be called deferred.
static enum command_state
send_status(void *arg, int *ptr)
{
  struct pulse_session *ps = arg;
  enum output_device_state state;

  switch (ps->state)
    {
      case PA_STREAM_FAILED:
	state = OUTPUT_STATE_FAILED;
	break;
      case PA_STREAM_UNCONNECTED:
      case PA_STREAM_TERMINATED:
	state = OUTPUT_STATE_STOPPED;
	break;
      case PA_STREAM_READY:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case PA_STREAM_CREATING:
	state = OUTPUT_STATE_STARTUP;
	break;
      default:
	DPRINTF(E_LOG, L_LAUDIO, "Bug! Unhandled state in send_status()\n");
	state = OUTPUT_STATE_FAILED;
    }

  outputs_cb(ps->callback_id, ps->device_id, state);
  ps->callback_id = -1;

  return COMMAND_PENDING; // Don't want the command module to clean up ps
}

static enum command_state
session_shutdown(void *arg, int *ptr)
{
  struct pulse_session *ps = arg;

  send_status(ps, ptr);
  pulse_session_cleanup(ps);

  return COMMAND_PENDING; // Don't want the command module to clean up ps
}

/* ---------------------- EXECUTED IN PULSEAUDIO THREAD --------------------- */

static void
pulse_status(struct pulse_session *ps)
{
  // async to avoid risk of deadlock if the player should make calls back to Pulseaudio
  commands_exec_async(pulse.cmdbase, send_status, ps);
}

static void
pulse_session_shutdown(struct pulse_session *ps)
{
  // async to avoid risk of deadlock if the player should make calls back to Pulseaudio
  commands_exec_async(pulse.cmdbase, session_shutdown, ps);
}

static void
pulse_session_shutdown_all(pa_stream_state_t state)
{
  struct pulse_session *ps;
  struct pulse_session *next;

  for (ps = sessions; ps; ps = next)
    {
      next = ps->next;
      ps->state = state;
      pulse_session_shutdown(ps);
    }
}

/* --------------------- CALLBACKS FROM PULSEAUDIO THREAD ------------------- */


// This will be called if something happens to the stream after it was opened
static void
stream_state_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio stream to '%s' changed state (%d)\n", ps->devname, ps->state);

  ps->state = pa_stream_get_state(s);
  if (!PA_STREAM_IS_GOOD(ps->state))
    {
      if (ps->state == PA_STREAM_FAILED)
	{
	  errno = pa_context_errno(pulse.context);
          DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio stream to '%s' failed with error: %s\n", ps->devname, pa_strerror(errno));
	}
      else
        DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio stream to '%s' aborted (%d)\n", ps->devname, ps->state);

      pulse_session_shutdown(ps);
      return;
    }
}

static void
underrun_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  if (ps->logcount > PULSE_LOG_MAX)
    return;

  ps->logcount++;

  if (ps->logcount < PULSE_LOG_MAX)
    DPRINTF(E_WARN, L_LAUDIO, "Pulseaudio reports buffer underrun on '%s'\n", ps->devname);
  else if (ps->logcount == PULSE_LOG_MAX)
    DPRINTF(E_WARN, L_LAUDIO, "Pulseaudio reports buffer underrun on '%s' (no further logging)\n", ps->devname);
}

static void
overrun_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  if (ps->logcount > PULSE_LOG_MAX)
    return;

  ps->logcount++;

  if (ps->logcount < PULSE_LOG_MAX)
    DPRINTF(E_WARN, L_LAUDIO, "Pulseaudio reports buffer overrun on '%s'\n", ps->devname);
  else if (ps->logcount == PULSE_LOG_MAX)
    DPRINTF(E_WARN, L_LAUDIO, "Pulseaudio reports buffer overrun on '%s' (no further logging)\n", ps->devname);
}

// This will be called our request to open the stream has completed
static void
start_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  ps->state = pa_stream_get_state(s);
  if (ps->state == PA_STREAM_CREATING)
    return;

  if (ps->state != PA_STREAM_READY)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Error starting Pulseaudio stream to '%s' (%d)\n", ps->devname, ps->state);
      pulse_session_shutdown(ps);
      return;
    }

  pa_stream_set_underflow_callback(ps->stream, underrun_cb, ps);
  pa_stream_set_overflow_callback(ps->stream, overrun_cb, ps);
  pa_stream_set_state_callback(ps->stream, stream_state_cb, ps);

  pulse_status(ps);
}

static void
close_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  pulse_session_shutdown(ps);
}

// This will be called our request to probe the stream has completed
static void
probe_cb(pa_stream *s, void *userdata)
{
  struct pulse_session *ps = userdata;

  ps->state = pa_stream_get_state(s);
  if (ps->state == PA_STREAM_CREATING)
    return;

  if (ps->state != PA_STREAM_READY)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Error probing Pulseaudio stream to '%s' (%d)\n", ps->devname, ps->state);
      pulse_session_shutdown(ps);
      return;
    }

  // This will callback to the player with succes and then remove the session
  pulse_session_shutdown(ps);
}

static void
flush_cb(pa_stream *s, int success, void *userdata)
{
  struct pulse_session *ps = userdata;

  pulse_status(ps);
}

static void
volume_cb(pa_context *c, int success, void *userdata)
{
  struct pulse_session *ps = userdata;

  pulse_status(ps);
}

static void
sinklist_cb(pa_context *ctx, const pa_sink_info *info, int eol, void *userdata)
{
  struct output_device *device;
  const char *name;
  int i;
  int pos;

  if (eol > 0)
    return;

  DPRINTF(E_DBG, L_LAUDIO, "Callback for Pulseaudio sink '%s' (id %" PRIu32 ")\n", info->name, info->index);

  pos = -1;
  for (i = 0; i < PULSE_MAX_DEVICES; i++)
    {
      if (pulse_known_devices[i] == (info->index + 1))
	return;

      if (pulse_known_devices[i] == 0)
	pos = i;
    }

  if (pos == -1)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Maximum number of Pulseaudio devices reached (%d), cannot add '%s'\n", PULSE_MAX_DEVICES, info->name);
      return;
    }

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for new Pulseaudio sink\n");
      return;
    }

  if (info->index == 0)
    {
      name = cfg_getstr(cfg_getsec(cfg, "audio"), "nickname");

      DPRINTF(E_LOG, L_LAUDIO, "Adding Pulseaudio sink '%s' (%s) with name '%s'\n", info->description, info->name, name);
    }
  else
    {
      name = info->description;

      DPRINTF(E_LOG, L_LAUDIO, "Adding Pulseaudio sink '%s' (%s)\n", info->description, info->name);
    }

  pulse_known_devices[pos] = info->index + 1; // Array values of 0 mean no device, so we add 1 to make sure the value is > 0

  device->id = info->index;
  device->name = strdup(name);
  device->type = OUTPUT_TYPE_PULSE;
  device->type_name = outputs_name(device->type);
  device->extra_device_info = strdup(info->name);

  player_device_add(device);
}

static void
subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata)
{
  struct output_device *device;
  pa_operation *o;
  int i;

  DPRINTF(E_DBG, L_LAUDIO, "Callback for Pulseaudio subscribe (id %" PRIu32 ", event %d)\n", index, t);

  if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SINK)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio subscribe called back with unknown event\n");
      return;
    }

  if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
    {
      device = calloc(1, sizeof(struct output_device));
      if (!device)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Out of memory for temp Pulseaudio device\n");
	  return;
	}

      device->id = index;

      DPRINTF(E_LOG, L_LAUDIO, "Removing Pulseaudio sink with id %" PRIu32 "\n", index);

      for (i = 0; i < PULSE_MAX_DEVICES; i++)
	{
	  if (pulse_known_devices[i] == index)
	    pulse_known_devices[i] = 0;
	}

      player_device_remove(device);
      return;
    }

  o = pa_context_get_sink_info_by_index(c, index, sinklist_cb, NULL);
  if (!o)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio error getting sink info for id %" PRIu32 "\n", index);
      return;
    }
  pa_operation_unref(o);
}

static void
context_state_cb(pa_context *c, void *userdata)
{
  pa_context_state_t state;
  pa_operation *o;

  state = pa_context_get_state(c);

  switch (state)
    {
      case PA_CONTEXT_READY:
	DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio context state changed to ready\n");

	o = pa_context_get_sink_info_list(c, sinklist_cb, NULL);
	if (!o)
	  {
	    DPRINTF(E_LOG, L_LAUDIO, "Could not list Pulseaudio sink info\n");
	    return;
	  }
	pa_operation_unref(o);

	pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
	o = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
	if (!o)
	  {
	    DPRINTF(E_LOG, L_LAUDIO, "Could not subscribe to Pulseaudio sink info\n");
	    return;
	  }
	pa_operation_unref(o);

	pa_threaded_mainloop_signal(pulse.mainloop, 0);
	break;

      case PA_CONTEXT_FAILED:
	DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio failed with error: %s\n", pa_strerror(pa_context_errno(c)));
	pulse_session_shutdown_all(PA_STREAM_FAILED);
	pa_threaded_mainloop_signal(pulse.mainloop, 0);
	break;

      case PA_CONTEXT_TERMINATED:
	DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio terminated\n");
	pulse_session_shutdown_all(PA_STREAM_UNCONNECTED);
	pa_threaded_mainloop_signal(pulse.mainloop, 0);
	break;

      case PA_CONTEXT_UNCONNECTED:
      case PA_CONTEXT_CONNECTING:
      case PA_CONTEXT_AUTHORIZING:
      case PA_CONTEXT_SETTING_NAME:
	break;
    }
}


/* ------------------------------- MISC HELPERS ----------------------------- */

// Used by init and deinit to stop main thread
static void
pulse_free(void)
{
  if (pulse.mainloop)
    pa_threaded_mainloop_stop(pulse.mainloop);

  if (pulse.context)
    {
      pa_context_disconnect(pulse.context);
      pa_context_unref(pulse.context);
    }

  if (pulse.cmdbase)
    commands_base_free(pulse.cmdbase);

  if (pulse.mainloop)
    pa_threaded_mainloop_free(pulse.mainloop);
}

static int
stream_open(struct pulse_session *ps, struct media_quality *quality, pa_stream_notify_cb_t cb)
{
  pa_stream_flags_t flags;
  pa_sample_spec ss;
  pa_cvolume cvol;
  int offset_ms;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "Opening Pulseaudio stream to '%s'\n", ps->devname);

  if (quality->bits_per_sample == 16)
    ss.format = PA_SAMPLE_S16LE;
  else if (quality->bits_per_sample == 24)
    ss.format = PA_SAMPLE_S24LE;
  else if (quality->bits_per_sample == 32)
    ss.format = PA_SAMPLE_S32LE;
  else
    ss.format = 0;

  ss.channels = quality->channels;
  ss.rate = quality->sample_rate;

  offset_ms = cfg_getint(cfg_getsec(cfg, "audio"), "offset_ms");
  if (abs(offset_ms) > 1000)
    {
      DPRINTF(E_LOG, L_LAUDIO, "The audio offset (%d) set in the configuration is out of bounds\n", offset_ms);
      offset_ms = 1000 * (offset_ms/abs(offset_ms));
    }

  pa_threaded_mainloop_lock(pulse.mainloop);

  if (!(ps->stream = pa_stream_new(pulse.context, "forked-daapd audio", &ss, NULL)))
    goto unlock_and_fail;

  pa_stream_set_state_callback(ps->stream, cb, ps);

  flags = PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_AUTO_TIMING_UPDATE;

  ps->attr.tlength   = STOB((OUTPUTS_BUFFER_DURATION * 1000 + offset_ms) * ss.rate / 1000, quality->bits_per_sample, quality->channels);
  ps->attr.maxlength = 2 * ps->attr.tlength;
  ps->attr.prebuf    = (uint32_t)-1;
  ps->attr.minreq    = (uint32_t)-1;
  ps->attr.fragsize  = (uint32_t)-1;

  pa_cvolume_set(&cvol, 2, ps->volume);

  ret = pa_stream_connect_playback(ps->stream, ps->devname, &ps->attr, flags, &cvol, NULL);
  if (ret < 0)
    goto unlock_and_fail;

  ps->state = pa_stream_get_state(ps->stream);
  if (!PA_STREAM_IS_GOOD(ps->state))
    goto unlock_and_fail;

  pa_threaded_mainloop_unlock(pulse.mainloop);

  return 0;

 unlock_and_fail:
  ret = pa_context_errno(pulse.context);

  DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio could not start '%s' using quality %d/%d/%d: %s\n",
    ps->devname, quality->sample_rate, quality->bits_per_sample, quality->channels, pa_strerror(ret));

  pa_threaded_mainloop_unlock(pulse.mainloop);

  return -1;
}

static void
stream_close(struct pulse_session *ps, pa_stream_notify_cb_t cb)
{
  if (!ps->stream)
    return;

  pa_threaded_mainloop_lock(pulse.mainloop);

  pa_stream_set_underflow_callback(ps->stream, NULL, NULL);
  pa_stream_set_overflow_callback(ps->stream, NULL, NULL);
  pa_stream_set_state_callback(ps->stream, cb, ps);

  pa_stream_disconnect(ps->stream);
  pa_stream_unref(ps->stream);

  ps->state = PA_STREAM_TERMINATED;
  ps->stream = NULL;

  pa_threaded_mainloop_unlock(pulse.mainloop);
}

static void
playback_restart(struct pulse_session *ps, struct output_buffer *obuf)
{
  int ret;

  stream_close(ps, NULL);

  // Negotiate quality (sample rate) with device - first we try to use the source quality
  ps->quality = obuf->data[0].quality;
  ret = stream_open(ps, &ps->quality, start_cb);
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_LAUDIO, "Input quality (%d/%d/%d) not supported, falling back to default\n",
        ps->quality.sample_rate, ps->quality.bits_per_sample, ps->quality.channels);

      ps->quality = pulse_fallback_quality;
      ret = stream_open(ps, &ps->quality, start_cb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio device failed setting fallback quality\n");
	  ps->state = PA_STREAM_FAILED;
	  pulse_session_shutdown(ps);
	  return;
	}
    }
}

static void
playback_write(struct pulse_session *ps, struct output_buffer *obuf)
{
  int i;
  int ret;

  // Find the quality we want
  for (i = 0; obuf->data[i].buffer; i++)
    {
      if (quality_is_equal(&ps->quality, &obuf->data[i].quality))
	break;
    }

  if (!obuf->data[i].buffer)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Output not delivering required data quality, aborting\n");
      ps->state = PA_STREAM_FAILED;
      pulse_session_shutdown(ps);
      return;
    }

  pa_threaded_mainloop_lock(pulse.mainloop);

  ret = pa_stream_write(ps->stream, obuf->data[i].buffer, obuf->data[i].bufsize, NULL, 0LL, PA_SEEK_RELATIVE);
  if (ret < 0)
    {
      ret = pa_context_errno(pulse.context);
      DPRINTF(E_LOG, L_LAUDIO, "Error writing Pulseaudio stream data to '%s': %s\n", ps->devname, pa_strerror(ret));
      ps->state = PA_STREAM_FAILED;
      pulse_session_shutdown(ps);
      goto unlock;
    }

 unlock:
  pa_threaded_mainloop_unlock(pulse.mainloop);
}

static void
playback_resume(struct pulse_session *ps)
{
  pa_operation* o;

  pa_threaded_mainloop_lock(pulse.mainloop);

  o = pa_stream_cork(ps->stream, 0, NULL, NULL);
  if (!o)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio could not resume '%s': %s\n", ps->devname, pa_strerror(pa_context_errno(pulse.context)));
      goto unlock;
    }

  pa_operation_unref(o);

 unlock:
  pa_threaded_mainloop_unlock(pulse.mainloop);
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
pulse_device_start(struct output_device *device, int callback_id)
{
  struct pulse_session *ps;

  DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio starting '%s'\n", device->name);

  ps = pulse_session_make(device, callback_id);
  if (!ps)
    return -1;

  pulse_status(ps);

  return 0;
}

static int
pulse_device_stop(struct output_device *device, int callback_id)
{
  struct pulse_session *ps = device->session;

  DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio stopping '%s'\n", ps->devname);

  ps->callback_id = callback_id;

  stream_close(ps, close_cb);

  return 0;
}

static int
pulse_device_flush(struct output_device *device, int callback_id)
{
  struct pulse_session *ps = device->session;
  pa_operation* o;

  DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio flush\n");

  pa_threaded_mainloop_lock(pulse.mainloop);

  ps->callback_id = callback_id;

  o = pa_stream_cork(ps->stream, 1, NULL, NULL);
  if (!o)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio could not pause '%s': %s\n", ps->devname, pa_strerror(pa_context_errno(pulse.context)));
      return -1;
    }
  pa_operation_unref(o);

  o = pa_stream_flush(ps->stream, flush_cb, ps);
  if (!o)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio could not flush '%s': %s\n", ps->devname, pa_strerror(pa_context_errno(pulse.context)));
      return -1;
    }
  pa_operation_unref(o);

  pa_threaded_mainloop_unlock(pulse.mainloop);

  return 0;
}

static int
pulse_device_probe(struct output_device *device, int callback_id)
{
  struct pulse_session *ps;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "Pulseaudio probing '%s'\n", device->name);

  ps = pulse_session_make(device, callback_id);
  if (!ps)
    return -1;

  ret = stream_open(ps, &pulse_fallback_quality, probe_cb);
  if (ret < 0)
    {
      pulse_session_cleanup(ps);
      return -1;
    }

  return 0;
}

static void
pulse_device_free_extra(struct output_device *device)
{
  free(device->extra_device_info);
}

static void
pulse_device_cb_set(struct output_device *device, int callback_id)
{
  struct pulse_session *ps = device->session;

  ps->callback_id = callback_id;
}

static int
pulse_device_volume_set(struct output_device *device, int callback_id)
{
  struct pulse_session *ps = device->session;
  uint32_t idx;
  pa_operation* o;
  pa_cvolume cvol;

  if (!ps)
    return 0;

  idx = pa_stream_get_index(ps->stream);

  ps->volume = pulse_from_device_volume(device->volume);
  pa_cvolume_set(&cvol, 2, ps->volume);

  DPRINTF(E_DBG, L_LAUDIO, "Setting Pulseaudio volume for stream %" PRIu32 " to %d (%d)\n", idx, (int)ps->volume, device->volume);

  pa_threaded_mainloop_lock(pulse.mainloop);

  ps->callback_id = callback_id;

  o = pa_context_set_sink_input_volume(pulse.context, idx, &cvol, volume_cb, ps);
  if (!o)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Pulseaudio could not set volume: %s\n", pa_strerror(pa_context_errno(pulse.context)));
      pa_threaded_mainloop_unlock(pulse.mainloop);
      return 0;
    }
  pa_operation_unref(o);

  pa_threaded_mainloop_unlock(pulse.mainloop);

  return 1;
}

static void
pulse_write(struct output_buffer *obuf)
{
  struct pulse_session *ps;
  struct pulse_session *next;

  if (!sessions)
    return;

  for (ps = sessions; ps; ps = next)
    {
      next = ps->next;

      // We have not set up a stream OR the quality changed, so we need to set it up again
      if (ps->state == PA_STREAM_UNCONNECTED || !quality_is_equal(&obuf->data[0].quality, &pulse_last_quality))
	{
	  playback_restart(ps, obuf);
	  pulse_last_quality = obuf->data[0].quality;
	  continue; // Async, so the device won't be ready for writing just now
	}
      else if (ps->state != PA_STREAM_READY)
	continue;

      if (ps->stream && pa_stream_is_corked(ps->stream))
	playback_resume(ps);

      playback_write(ps, obuf);
    }
}

static int
pulse_init(void)
{
  char *type;
  char *server;
  int state;
  int ret;

  type = cfg_getstr(cfg_getsec(cfg, "audio"), "type");
  if (type && (strcasecmp(type, "pulseaudio") != 0))
    return -1;

  server = cfg_getstr(cfg_getsec(cfg, "audio"), "server");

  ret = 0;

  if (!(pulse.mainloop = pa_threaded_mainloop_new()))
    goto fail;

  if (!(pulse.cmdbase = commands_base_new(evbase_player, NULL)))
    goto fail;

#ifdef HAVE_PA_THREADED_MAINLOOP_SET_NAME
  pa_threaded_mainloop_set_name(pulse.mainloop, "pulseaudio");
#endif

  if (!(pulse.context = pa_context_new(pa_threaded_mainloop_get_api(pulse.mainloop), "forked-daapd")))
    goto fail;

  pa_context_set_state_callback(pulse.context, context_state_cb, NULL);
  
  if (pa_context_connect(pulse.context, server, 0, NULL) < 0)
    {
      ret = pa_context_errno(pulse.context);
      goto fail;
    }

  pa_threaded_mainloop_lock(pulse.mainloop);

  if (pa_threaded_mainloop_start(pulse.mainloop) < 0)
    goto unlock_and_fail;

  for (;;)
    {
      state = pa_context_get_state(pulse.context);

      if (state == PA_CONTEXT_READY)
	break;

      if (!PA_CONTEXT_IS_GOOD(state))
	{
	  ret = pa_context_errno(pulse.context);
	  goto unlock_and_fail;
	}

      /* Wait until the context is ready */
      pa_threaded_mainloop_wait(pulse.mainloop);
    }

  pa_threaded_mainloop_unlock(pulse.mainloop);

  return 0;

 unlock_and_fail:
  pa_threaded_mainloop_unlock(pulse.mainloop);

 fail:
  if (ret)
    DPRINTF(E_LOG, L_LAUDIO, "Error initializing Pulseaudio: %s\n", pa_strerror(ret));

  pulse_free();
  return -1;
}

static void
pulse_deinit(void)
{
  pulse_free();
}

struct output_definition output_pulse =
{
  .name = "Pulseaudio",
  .type = OUTPUT_TYPE_PULSE,
  .priority = 3,
  .disabled = 0,
  .init = pulse_init,
  .deinit = pulse_deinit,
  .device_start = pulse_device_start,
  .device_stop = pulse_device_stop,
  .device_flush = pulse_device_flush,
  .device_probe = pulse_device_probe,
  .device_free_extra = pulse_device_free_extra,
  .device_cb_set = pulse_device_cb_set,
  .device_volume_set = pulse_device_volume_set,
  .write = pulse_write,
};

