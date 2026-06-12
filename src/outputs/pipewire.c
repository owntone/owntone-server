/*
 * Copyright (C) 2016- Espen Jürgensen <espenjurgensen@gmail.com>
 * PipeWire port (C) 2024 OwnTone contributors
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

/*
 * PipeWire output module for OwnTone.
 *
 * Architecture overview
 * ---------------------
 * All PipeWire interaction happens inside the pw_thread_loop thread (the
 * "PW thread").  The OwnTone player thread calls into this module through the
 * public interface functions (pipewire_device_start, etc.).  Those functions
 * lock the PW thread loop before touching any PW objects, exactly as is done
 * with pa_threaded_mainloop_lock() in the PulseAudio module.
 *
 * Device discovery
 * ----------------
 * We register a pw_registry listener.  Every Audio/Sink node that appears
 * results in a player_device_add() call; every removal triggers
 * player_device_remove().  This mirrors sinklist_cb / subscribe_cb from the
 * PulseAudio module.
 *
 * Streams
 * -------
 * Each active session owns one pw_stream.  Audio data is pushed from the
 * player thread via pipewire_write() → playback_write(), which queues a
 * buffer pointer in a per-session ring.  The PW process callback (on_process)
 * dequeues that buffer and feeds it to PipeWire, preventing any blocking on
 * the real-time thread.
 *
 * Volume
 * ------
 * Per-stream software volume is set via SPA_PARAM_Props / SPA_PROP_channelVolumes
 * using a floating-point linear scale (0.0 – 1.0).
 *
 * Flush / pause
 * -------------
 * Pausing is done by setting the stream inactive (pw_stream_set_active(false)).
 * A flush drains any queued data and then resumes when new data arrives.
 *
 * Latency / delay
 * ---------------
 * We request a specific buffer quantum from PipeWire using the
 * PW_KEY_NODE_LATENCY stream property, derived from outputs_buffer_duration_ms_get().
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
#include <math.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>

#include <event2/event.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "commands.h"

#define PIPEWIRE_MAX_DEVICES 64
#define PIPEWIRE_LOG_MAX 10

/* ----------------------------- GLOBAL STATE ------------------------------- */

struct pipewire_ctx
{
  struct pw_thread_loop *thread_loop;
  struct pw_context     *context;
  struct pw_core        *core;
  struct pw_registry    *registry;

  struct spa_hook        registry_listener;
  struct spa_hook        core_listener;

  struct commands_base  *cmdbase;

  /* Pending sync seq so we know when the registry round-trip is done */
  int                    core_seq;
};

static struct pipewire_ctx pwctx;

/* ----------------------------- SESSION ------------------------------------ */

struct pipewire_session
{
  uint64_t device_id;
  int      callback_id;

  char    *devname;      /* PipeWire target node name  */
  uint32_t node_id;      /* PipeWire node serial       */

  struct pw_stream      *stream;
  struct spa_hook        stream_listener;

  enum pw_stream_state   state;

  float    volume;       /* 0.0 – 1.0 linear           */

  struct media_quality quality;

  uint64_t delay_ms;

  int      logcount;

  /* Single-slot pending write buffer (set by player thread, consumed by PW) */
  const void *pending_buf;
  size_t      pending_size;

  struct pipewire_session *next;
};

/* From player.c */
extern struct event_base *evbase_player;

/* Active sessions list */
static struct pipewire_session *sessions;

/* Known device node IDs (non-zero entries are registered) */
static uint32_t pipewire_known_devices[PIPEWIRE_MAX_DEVICES];

static struct media_quality pipewire_last_quality;
static struct media_quality pipewire_fallback_quality = { 44100, 16, 2, 0 };

/* ----------------------------- HELPERS ------------------------------------ */

static inline float
pipewire_from_device_volume(int device_volume)
{
  /* Map 0-100 → 0.0-1.0 linear (PA used PA_VOLUME_NORM scale; we use float) */
  return (float)device_volume / 100.0f;
}

static inline enum spa_audio_format
bits_to_spa_format(int bits)
{
  switch (bits)
    {
      case 16: return SPA_AUDIO_FORMAT_S16_LE;
      case 24: return SPA_AUDIO_FORMAT_S24_LE;
      case 32: return SPA_AUDIO_FORMAT_S32_LE;
      default: return SPA_AUDIO_FORMAT_UNKNOWN;
    }
}

/* Build a SPA audio info raw pod, used when connecting the stream */
static const struct spa_pod *
build_format_param(struct spa_pod_builder *b, const struct media_quality *q)
{
  struct spa_audio_info_raw info = {
    .format   = bits_to_spa_format(q->bits_per_sample),
    .rate     = (uint32_t)q->sample_rate,
    .channels = (uint32_t)q->channels,
  };

  return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

/* Build a Props pod to set per-stream volume */
static const struct spa_pod *
build_volume_param(struct spa_pod_builder *b, float vol, uint32_t channels)
{
  float vols[8];
  uint32_t i;

  channels = (channels < 8) ? channels : 8;
  for (i = 0; i < channels; i++)
    vols[i] = vol;

  return spa_pod_builder_add_object(b,
    SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
    SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, channels, vols),
    0);
}

/* ----------------------------- SESSION HANDLING --------------------------- */

static void
pipewire_session_free(struct pipewire_session *ps)
{
  if (!ps)
    return;

  if (ps->stream)
    {
      pw_thread_loop_lock(pwctx.thread_loop);
      pw_stream_destroy(ps->stream);
      ps->stream = NULL;
      pw_thread_loop_unlock(pwctx.thread_loop);
    }

  outputs_quality_unsubscribe(&pipewire_fallback_quality);

  free(ps->devname);
  free(ps);
}

static void
pipewire_session_cleanup(struct pipewire_session *ps)
{
  struct pipewire_session *p;

  if (ps == sessions)
    sessions = sessions->next;
  else
    {
      for (p = sessions; p && (p->next != ps); p = p->next)
        ; /* EMPTY */

      if (!p)
        DPRINTF(E_WARN, L_LAUDIO, "WARNING: struct pipewire_session not found in list; BUG!\n");
      else
        p->next = ps->next;
    }

  outputs_device_session_remove(ps->device_id);
  pipewire_session_free(ps);
}

static struct pipewire_session *
pipewire_session_make(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps;
  int ret;

  ret = outputs_quality_subscribe(&pipewire_fallback_quality);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not subscribe to fallback audio quality\n");
      return NULL;
    }

  CHECK_NULL(L_LAUDIO, ps = calloc(1, sizeof(struct pipewire_session)));

  ps->state       = PW_STREAM_STATE_UNCONNECTED;
  ps->device_id   = device->id;
  ps->callback_id = callback_id;
  ps->volume      = pipewire_from_device_volume(device->volume);
  ps->devname     = strdup(device->extra_device_info);
  ps->node_id     = (uint32_t)device->id;

  ps->delay_ms = outputs_buffer_duration_ms_get();
  if ((int64_t)ps->delay_ms + device->offset_ms < 0)
    DPRINTF(E_LOG, L_LAUDIO, "'%s' configured with invalid start time (delay=%" PRIu64 ", offset=%d)\n",
      device->name, ps->delay_ms, device->offset_ms);
  else
    ps->delay_ms += device->offset_ms;

  ps->next = sessions;
  sessions = ps;

  outputs_device_session_add(device->id, ps);

  return ps;
}

/* ----------------------------- COMMAND HANDLERS --------------------------- */

static enum command_state
send_status(void *arg, int *ptr)
{
  struct pipewire_session *ps = arg;
  enum output_device_state state;

  switch (ps->state)
    {
      case PW_STREAM_STATE_ERROR:
        state = OUTPUT_STATE_FAILED;
        break;
      case PW_STREAM_STATE_UNCONNECTED:
        state = OUTPUT_STATE_STOPPED;
        break;
      case PW_STREAM_STATE_CONNECTING:
        state = OUTPUT_STATE_STARTUP;
        break;
      case PW_STREAM_STATE_PAUSED:
      case PW_STREAM_STATE_STREAMING:
        state = OUTPUT_STATE_CONNECTED;
        break;
      default:
        DPRINTF(E_LOG, L_LAUDIO, "Bug! Unhandled PW stream state in send_status()\n");
        state = OUTPUT_STATE_FAILED;
    }

  outputs_cb(ps->callback_id, ps->device_id, state);
  ps->callback_id = -1;

  return COMMAND_PENDING;
}

static enum command_state
session_shutdown(void *arg, int *ptr)
{
  struct pipewire_session *ps = arg;

  send_status(ps, ptr);
  pipewire_session_cleanup(ps);

  return COMMAND_PENDING;
}

/* -------------- HELPERS CALLED FROM THE PIPEWIRE THREAD ------------------- */

static void
pipewire_status(struct pipewire_session *ps)
{
  commands_exec_async(pwctx.cmdbase, send_status, ps);
}

static void
pipewire_session_shutdown(struct pipewire_session *ps)
{
  commands_exec_async(pwctx.cmdbase, session_shutdown, ps);
}

static void
pipewire_session_shutdown_all(enum pw_stream_state state)
{
  struct pipewire_session *ps;
  struct pipewire_session *next;

  for (ps = sessions; ps; ps = next)
    {
      next = ps->next;
      ps->state = state;
      pipewire_session_shutdown(ps);
    }
}

/* ----------------------- STREAM CALLBACKS (PW THREAD) --------------------- */

static void
on_stream_state_changed(void *userdata, enum pw_stream_state old,
                        enum pw_stream_state state, const char *error)
{
  struct pipewire_session *ps = userdata;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire stream '%s' state: %s → %s%s%s\n",
    ps->devname,
    pw_stream_state_as_string(old),
    pw_stream_state_as_string(state),
    error ? " (" : "", error ? error : "");

  ps->state = state;

  switch (state)
    {
      case PW_STREAM_STATE_ERROR:
        DPRINTF(E_LOG, L_LAUDIO, "PipeWire stream to '%s' failed: %s\n",
          ps->devname, error ? error : "(unknown)");
        pipewire_session_shutdown(ps);
        break;

      case PW_STREAM_STATE_UNCONNECTED:
        pipewire_session_shutdown(ps);
        break;

      case PW_STREAM_STATE_PAUSED:
        /*
         * PAUSED is the first state entered after a successful connect (before
         * the stream goes to STREAMING once we call pw_stream_set_active).
         * We use it as the "ready" signal – send status back to the player.
         */
        pipewire_status(ps);
        break;

      case PW_STREAM_STATE_STREAMING:
      case PW_STREAM_STATE_CONNECTING:
        break;
    }
}

/*
 * PipeWire calls this on the real-time thread whenever it wants more audio.
 * We pull from the per-session pending buffer (written by the player thread)
 * and hand it over.  If nothing is pending we output silence to avoid
 * underruns on the wire.
 */
static void
on_process(void *userdata)
{
  struct pipewire_session *ps = userdata;
  struct pw_buffer *pwbuf;
  struct spa_buffer *sbuf;
  void *dst;
  uint32_t n_bytes;

  if (!ps->stream)
    return;

  pwbuf = pw_stream_dequeue_buffer(ps->stream);
  if (!pwbuf)
    return;

  sbuf = pwbuf->buffer;
  dst  = sbuf->datas[0].data;
  if (!dst)
    goto queue;

  n_bytes = sbuf->datas[0].maxsize;

  if (ps->pending_buf && ps->pending_size > 0)
    {
      uint32_t copy = (ps->pending_size < n_bytes) ? (uint32_t)ps->pending_size : n_bytes;
      memcpy(dst, ps->pending_buf, copy);
      if (copy < n_bytes)
        memset((uint8_t *)dst + copy, 0, n_bytes - copy);
      sbuf->datas[0].chunk->offset = 0;
      sbuf->datas[0].chunk->stride = /* bytes per frame */
        (ps->quality.bits_per_sample / 8) * ps->quality.channels;
      sbuf->datas[0].chunk->size = copy;
      ps->pending_buf  = NULL;
      ps->pending_size = 0;
    }
  else
    {
      /* Silence */
      memset(dst, 0, n_bytes);
      sbuf->datas[0].chunk->offset = 0;
      sbuf->datas[0].chunk->stride = (ps->quality.bits_per_sample / 8) * ps->quality.channels;
      sbuf->datas[0].chunk->size   = n_bytes;

      if (ps->logcount <= PIPEWIRE_LOG_MAX)
        {
          ps->logcount++;
          if (ps->logcount < PIPEWIRE_LOG_MAX)
            DPRINTF(E_DBG, L_LAUDIO, "PipeWire buffer underrun on '%s' (no pending data)\n", ps->devname);
          else
            DPRINTF(E_DBG, L_LAUDIO, "PipeWire buffer underrun on '%s' (no further logging)\n", ps->devname);
        }
    }

 queue:
  pw_stream_queue_buffer(ps->stream, pwbuf);
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .process       = on_process,
};

/* ----------------------- REGISTRY CALLBACKS (PW THREAD) ------------------- */

static void
on_registry_global(void *userdata, uint32_t id, uint32_t permissions,
                   const char *type, uint32_t version,
                   const struct spa_dict *props)
{
  struct output_device *device;
  const char *media_class;
  const char *name;
  const char *desc;
  int i, pos;
  int offset_ms;

  if (!props)
    return;

  /* We only care about Audio/Sink nodes */
  media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (!media_class || strcmp(media_class, "Audio/Sink") != 0)
    return;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire registry: Audio/Sink node id=%" PRIu32 "\n", id);

  pos = -1;
  for (i = 0; i < PIPEWIRE_MAX_DEVICES; i++)
    {
      if (pipewire_known_devices[i] == id + 1)
        return; /* already known */
      if (pipewire_known_devices[i] == 0)
        pos = i;
    }

  if (pos == -1)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Maximum PipeWire devices reached (%d), cannot add node %" PRIu32 "\n",
        PIPEWIRE_MAX_DEVICES, id);
      return;
    }

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for new PipeWire sink\n");
      return;
    }

  offset_ms = cfg_getint(cfg_getsec(cfg, "audio"), "offset_ms");
  if (abs(offset_ms) > 1000)
    DPRINTF(E_LOG, L_LAUDIO, "PipeWire offset_ms (%d) is out of bounds (-1000 → 1000)\n", offset_ms);
  else
    device->offset_ms = offset_ms;

  /* Node 0 (default sink) gets the user-configured nickname */
  name = (id == 0)
    ? cfg_getstr(cfg_getsec(cfg, "audio"), "nickname")
    : NULL;

  desc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  if (!desc)
    desc = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  if (!desc)
    desc = "Unknown";

  if (!name)
    name = desc;

  DPRINTF(E_LOG, L_LAUDIO, "Adding PipeWire sink '%s' (node %" PRIu32 ")\n", name, id);

  pipewire_known_devices[pos] = id + 1; /* +1 so that 0 means "empty slot" */

  /* extra_device_info holds the node name used to target the stream */
  const char *node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);

  device->id               = id;
  device->name             = strdup(name);
  device->type             = OUTPUT_TYPE_PIPEWIRE;
  device->type_name        = outputs_name(device->type);
  device->extra_device_info = strdup(node_name ? node_name : desc);
  device->supported_formats = MEDIA_FORMAT_PCM;

  player_device_add(device);
}

static void
on_registry_global_remove(void *userdata, uint32_t id)
{
  struct output_device *device;
  int i;

  for (i = 0; i < PIPEWIRE_MAX_DEVICES; i++)
    {
      if (pipewire_known_devices[i] == id + 1)
        {
          pipewire_known_devices[i] = 0;
          break;
        }
    }

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for temp PipeWire device\n");
      return;
    }

  device->id = id;

  DPRINTF(E_LOG, L_LAUDIO, "Removing PipeWire sink with id %" PRIu32 "\n", id);
  player_device_remove(device);
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global        = on_registry_global,
  .global_remove = on_registry_global_remove,
};

/* ----------------------- CORE CALLBACKS (PW THREAD) ----------------------- */

static void
on_core_done(void *userdata, uint32_t id, int seq)
{
  if (id == PW_ID_CORE && seq == pwctx.core_seq)
    pw_thread_loop_signal(pwctx.thread_loop, false);
}

static void
on_core_error(void *userdata, uint32_t id, int seq, int res, const char *message)
{
  DPRINTF(E_LOG, L_LAUDIO, "PipeWire core error id=%" PRIu32 " seq=%d res=%d: %s\n",
    id, seq, res, message);

  if (id == PW_ID_CORE)
    {
      pipewire_session_shutdown_all(PW_STREAM_STATE_ERROR);
      pw_thread_loop_signal(pwctx.thread_loop, false);
    }
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done  = on_core_done,
  .error = on_core_error,
};

/* ----------------------------- MISC HELPERS ------------------------------- */

static void
pipewire_free(void)
{
  if (pwctx.thread_loop)
    pw_thread_loop_stop(pwctx.thread_loop);

  if (pwctx.registry)
    {
      spa_hook_remove(&pwctx.registry_listener);
      pw_proxy_destroy((struct pw_proxy *)pwctx.registry);
      pwctx.registry = NULL;
    }

  if (pwctx.core)
    {
      spa_hook_remove(&pwctx.core_listener);
      pw_core_disconnect(pwctx.core);
      pwctx.core = NULL;
    }

  if (pwctx.context)
    {
      pw_context_destroy(pwctx.context);
      pwctx.context = NULL;
    }

  if (pwctx.cmdbase)
    {
      commands_base_free(pwctx.cmdbase);
      pwctx.cmdbase = NULL;
    }

  if (pwctx.thread_loop)
    {
      pw_thread_loop_destroy(pwctx.thread_loop);
      pwctx.thread_loop = NULL;
    }
}

/*
 * Open (or reopen) a PipeWire stream for the given session and quality.
 * Equivalent to stream_open() in the PulseAudio module.
 */
static int
stream_open(struct pipewire_session *ps, const struct media_quality *quality,
            bool is_probe)
{
  char lat_str[32];
  uint8_t buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  const struct spa_pod *params[1];
  struct pw_properties *props;
  uint32_t quantum;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "Opening PipeWire stream to '%s' (%d/%d/%d)\n",
    ps->devname, quality->sample_rate, quality->bits_per_sample, quality->channels);

  pw_thread_loop_lock(pwctx.thread_loop);

  props = pw_properties_new(
    PW_KEY_MEDIA_TYPE,     "Audio",
    PW_KEY_MEDIA_CATEGORY, "Playback",
    PW_KEY_MEDIA_ROLE,     "Music",
    PW_KEY_APP_NAME,       PACKAGE_NAME,
    PW_KEY_NODE_TARGET,    ps->devname,
    NULL);

  if (!props)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not allocate stream properties\n");
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  /* Request a buffer size that matches our desired output latency */
  quantum = (uint32_t)(ps->delay_ms * quality->sample_rate / 1000);
  if (quantum < 64)
    quantum = 64;
  snprintf(lat_str, sizeof(lat_str), "%" PRIu32 "/%" PRIu32, quantum,
           (uint32_t)quality->sample_rate);
  pw_properties_set(props, PW_KEY_NODE_LATENCY, lat_str);

  if (is_probe)
    pw_properties_set(props, PW_KEY_NODE_PASSIVE, "true");

  ps->stream = pw_stream_new(pwctx.core, PACKAGE_NAME " audio", props);
  if (!ps->stream)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not create stream for '%s'\n", ps->devname);
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  pw_stream_add_listener(ps->stream, &ps->stream_listener, &stream_events, ps);

  params[0] = build_format_param(&b, quality);

  ret = pw_stream_connect(ps->stream,
    PW_DIRECTION_OUTPUT,
    PW_ID_ANY,
    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
    params, 1);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not connect stream to '%s': %s\n",
        ps->devname, spa_strerror(ret));
      pw_stream_destroy(ps->stream);
      ps->stream = NULL;
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  ps->quality = *quality;
  ps->state   = PW_STREAM_STATE_CONNECTING;

  pw_thread_loop_unlock(pwctx.thread_loop);
  return 0;
}

static void
stream_close(struct pipewire_session *ps)
{
  if (!ps->stream)
    return;

  pw_thread_loop_lock(pwctx.thread_loop);

  spa_hook_remove(&ps->stream_listener);
  pw_stream_destroy(ps->stream);
  ps->stream = NULL;
  ps->state  = PW_STREAM_STATE_UNCONNECTED;

  pw_thread_loop_unlock(pwctx.thread_loop);
}

static void
playback_restart(struct pipewire_session *ps, struct output_buffer *obuf)
{
  int ret;

  stream_close(ps);

  ps->quality = obuf->data[0].quality;
  ret = stream_open(ps, &ps->quality, false);
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_LAUDIO,
        "PipeWire: input quality (%d/%d/%d) not supported, falling back\n",
        ps->quality.sample_rate, ps->quality.bits_per_sample, ps->quality.channels);

      ps->quality = pipewire_fallback_quality;
      ret = stream_open(ps, &ps->quality, false);
      if (ret < 0)
        {
          DPRINTF(E_LOG, L_LAUDIO, "PipeWire device failed on fallback quality\n");
          ps->state = PW_STREAM_STATE_ERROR;
          pipewire_session_shutdown(ps);
          return;
        }
    }
}

static void
playback_write(struct pipewire_session *ps, struct output_buffer *obuf)
{
  int i;

  for (i = 0; obuf->data[i].buffer; i++)
    {
      if (quality_is_equal(&ps->quality, &obuf->data[i].quality))
        break;
    }

  if (!obuf->data[i].buffer)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire: output not delivering required quality, aborting\n");
      ps->state = PW_STREAM_STATE_ERROR;
      pipewire_session_shutdown(ps);
      return;
    }

  /* Hand the buffer pointer to the PW process callback */
  pw_thread_loop_lock(pwctx.thread_loop);
  ps->pending_buf  = obuf->data[i].buffer;
  ps->pending_size = obuf->data[i].bufsize;
  pw_thread_loop_unlock(pwctx.thread_loop);
}

static void
playback_resume(struct pipewire_session *ps)
{
  pw_thread_loop_lock(pwctx.thread_loop);
  pw_stream_set_active(ps->stream, true);
  pw_thread_loop_unlock(pwctx.thread_loop);
}

/* --------------- INTERFACE FUNCTIONS CALLED BY OUTPUTS.C ------------------ */

static int
pipewire_device_start(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire starting '%s'\n", device->name);

  ps = pipewire_session_make(device, callback_id);
  if (!ps)
    return -1;

  /*
   * The stream is not opened until the first write (playback_restart).
   * We immediately report CONNECTED / startup so the player can proceed.
   */
  pipewire_status(ps);

  return 1;
}

static int
pipewire_device_stop(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire stopping '%s'\n", ps->devname);

  ps->callback_id = callback_id;

  stream_close(ps);
  pipewire_session_shutdown(ps);

  return 1;
}

static int
pipewire_device_flush(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire flush '%s'\n", ps->devname);

  ps->callback_id = callback_id;

  if (!ps->stream)
    {
      pipewire_status(ps);
      return 1;
    }

  pw_thread_loop_lock(pwctx.thread_loop);

  /* Pause and clear the pending buffer */
  pw_stream_set_active(ps->stream, false);
  pw_stream_flush(ps->stream, false);
  ps->pending_buf  = NULL;
  ps->pending_size = 0;

  pw_thread_loop_unlock(pwctx.thread_loop);

  pipewire_status(ps);

  return 1;
}

static int
pipewire_device_probe(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire probing '%s'\n", device->name);

  ps = pipewire_session_make(device, callback_id);
  if (!ps)
    return -1;

  ret = stream_open(ps, &pipewire_fallback_quality, true /* is_probe */);
  if (ret < 0)
    {
      pipewire_session_cleanup(ps);
      return -1;
    }

  /*
   * For a probe we just need to confirm the device accepts a connection.
   * The PAUSED state callback will call pipewire_status() → send_status(),
   * which reports SUCCESS.  Then session_shutdown cleans up.
   */
  return 1;
}

static void
pipewire_device_free_extra(struct output_device *device)
{
  free(device->extra_device_info);
}

static void
pipewire_device_cb_set(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;

  ps->callback_id = callback_id;
}

static int
pipewire_device_volume_set(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;
  uint8_t buf[256];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  const struct spa_pod *param;

  if (!ps || !ps->stream)
    return 0;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire setting volume for '%s' to %d\n",
    ps->devname, device->volume);

  ps->volume      = pipewire_from_device_volume(device->volume);
  ps->callback_id = callback_id;

  param = build_volume_param(&b, ps->volume, (uint32_t)ps->quality.channels);

  pw_thread_loop_lock(pwctx.thread_loop);
  pw_stream_set_param(ps->stream, SPA_PARAM_Props, param);
  pw_thread_loop_unlock(pwctx.thread_loop);

  /* Volume is applied synchronously; report success immediately */
  pipewire_status(ps);

  return 1;
}

static void
pipewire_write(struct output_buffer *obuf)
{
  struct pipewire_session *ps;
  struct pipewire_session *next;

  if (!sessions)
    return;

  for (ps = sessions; ps; ps = next)
    {
      next = ps->next;

      if (ps->state == PW_STREAM_STATE_UNCONNECTED
          || !quality_is_equal(&obuf->data[0].quality, &pipewire_last_quality))
        {
          playback_restart(ps, obuf);
          pipewire_last_quality = obuf->data[0].quality;
          continue;
        }
      else if (ps->state == PW_STREAM_STATE_ERROR
               || ps->state == PW_STREAM_STATE_CONNECTING)
        continue;

      if (ps->stream && !pw_stream_is_driving(ps->stream))
        playback_resume(ps);

      playback_write(ps, obuf);
    }
}

/* ----------------------------- INIT / DEINIT ------------------------------ */

static int
pipewire_init(void)
{
  char *type;
  char *server;
  int ret;

  type = cfg_getstr(cfg_getsec(cfg, "audio"), "type");
  if (type && strcasecmp(type, "pipewire") != 0)
    return -1;

  server = cfg_getstr(cfg_getsec(cfg, "audio"), "server");

  pw_init(NULL, NULL);

  pwctx.thread_loop = pw_thread_loop_new("pipewire", NULL);
  if (!pwctx.thread_loop)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire thread loop\n");
      goto fail;
    }

  pwctx.cmdbase = commands_base_new(evbase_player, NULL);
  if (!pwctx.cmdbase)
    goto fail;

  pwctx.context = pw_context_new(pw_thread_loop_get_loop(pwctx.thread_loop), NULL, 0);
  if (!pwctx.context)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire context\n");
      goto fail;
    }

  ret = pw_thread_loop_start(pwctx.thread_loop);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not start PipeWire thread loop: %s\n", spa_strerror(ret));
      goto fail;
    }

  pw_thread_loop_lock(pwctx.thread_loop);

  pwctx.core = pw_context_connect(pwctx.context,
    server ? pw_properties_new(PW_KEY_REMOTE_NAME, server, NULL) : NULL, 0);
  if (!pwctx.core)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not connect to PipeWire: %s\n", strerror(errno));
      pw_thread_loop_unlock(pwctx.thread_loop);
      goto fail;
    }

  pw_core_add_listener(pwctx.core, &pwctx.core_listener, &core_events, NULL);

  pwctx.registry = pw_core_get_registry(pwctx.core, PW_VERSION_REGISTRY, 0);
  if (!pwctx.registry)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get PipeWire registry\n");
      pw_thread_loop_unlock(pwctx.thread_loop);
      goto fail;
    }

  spa_zero(pwctx.registry_listener);
  pw_registry_add_listener(pwctx.registry, &pwctx.registry_listener,
                           &registry_events, NULL);

  /*
   * Sync: wait until PipeWire has processed our registry bind and sent back
   * all initial global objects (Audio/Sink nodes).
   */
  pwctx.core_seq = pw_core_sync(pwctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwctx.thread_loop);

  pw_thread_loop_unlock(pwctx.thread_loop);

  return 0;

 fail:
  pipewire_free();
  return -1;
}

static void
pipewire_deinit(void)
{
  pipewire_free();
  pw_deinit();
}

struct output_definition output_pipewire =
{
  .name             = "PipeWire",
  .cfg_name         = "audio",
  .type             = OUTPUT_TYPE_PIPEWIRE,
  .priority         = 3,
  .disabled         = 0,
  .init             = pipewire_init,
  .deinit           = pipewire_deinit,
  .device_start     = pipewire_device_start,
  .device_stop      = pipewire_device_stop,
  .device_flush     = pipewire_device_flush,
  .device_probe     = pipewire_device_probe,
  .device_free_extra = pipewire_device_free_extra,
  .device_cb_set    = pipewire_device_cb_set,
  .device_volume_set = pipewire_device_volume_set,
  .write            = pipewire_write,
};
