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

/*
 * PipeWire output module for OwnTone.
 *
 * Architecture overview
 * ---------------------
 * OwnTone registers a single "PipeWire" output device and opens a pw_stream
 * with PW_ID_ANY (no explicit target node).  WirePlumber is responsible for
 * routing that stream to whichever sink the user has configured as default.
 * OwnTone never enumerates PipeWire sinks for the purpose of *streaming* to
 * them; device selection there is entirely delegated to WirePlumber.
 *
 * All streaming PipeWire interaction happens inside the pw_thread_loop
 * thread (the "PW thread").  The OwnTone player thread calls into this
 * module through the public interface functions (pipewire_device_start,
 * etc.).  Those functions lock the PW thread loop before touching any PW
 * objects, exactly as is done with pa_threaded_mainloop_lock() in the
 * PulseAudio module.
 *
 * Streams
 * -------
 * Each active session owns one pw_stream.  Audio data is pushed from the
 * player thread via pipewire_write() -> playback_write(), which queues bytes
 * into a per-session ring buffer.  The PW process callback (on_process)
 * drains that ring buffer and feeds PipeWire, preventing any blocking on the
 * real-time thread.
 *
 * Volume
 * ------
 * Volume handling is selected via the "pipewire_mixer" key in the [audio] section
 * ("pwsink" or "pwstream"); see pipewire_mixer_mode below and the block
 * comment above the pwsink_* functions for how "pwsink" mode finds and
 * drives the actual sink/device volume, as opposed to merely attenuating
 * OwnTone's own stream.
 *
 * "pwsink" mode opens its own independent PipeWire connection (its own
 * pw_thread_loop/pw_context/pw_core, held in pwsinkctx below), separate
 * from the connection used for the audio stream. Sink discovery and volume
 * round-trips therefore never contend with the real-time streaming
 * connection, and a stall or error on one side has no effect on the other.
 *
 * Flush / pause
 * -------------
 * Pausing is done by setting the stream inactive (pw_stream_set_active(false)).
 * A flush drains any queued data and then resumes when new data arrives.
 *
 * Latency / delay
 * ---------------
 * OwnTone's output buffer duration (~2250ms) is an internal scheduling
 * lookahead, not a PipeWire hardware quantum.  We do not set PW_KEY_NODE_LATENCY
 * and instead let PipeWire negotiate its own quantum with the graph (typically
 * 1024 samples, ~21ms).  OwnTone's player delivers audio in ~441-sample (~10ms)
 * chunks on its own timer, which fits comfortably within PipeWire's quantum.
 */

#ifndef HAVE_PIPEWIRE
#define HAVE_PIPEWIRE 1
#endif

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <math.h>

#include <event2/event.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "commands.h"

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/utils/result.h>
#include <spa/utils/string.h>
#include <spa/utils/json.h>
#include <spa/pod/builder.h>
#include <spa/pod/parser.h>
#include <spa/pod/iter.h>

#define PIPEWIRE_LOG_MAX 10

/* How long to wait after a core disconnect (sleep/wake) before attempting to
 * reconnect to PipeWire, in milliseconds.  This gives PipeWire's server time
 * to fully restart after the system wakes before we try to talk to it. */
#define PIPEWIRE_RECONNECT_MS 2000

/* Target ring buffer capacity in milliseconds. Needs to comfortably exceed
 * one PipeWire period (observed up to ~43ms @ 2048 samples/48kHz) plus
 * margin for jitter in the player thread's delivery timing. */
#define PIPEWIRE_RING_MS 250

/* Bounded retry budget for resolving the target sink/device/route after a
 * (re)connect, expressed as a number of pw_core_sync() round-trips. Keeps
 * pwsink_wait_ready() from blocking indefinitely if the target sink never
 * resolves (e.g. no matching node ever appears). */
#define PIPEWIRE_SINK_RESOLVE_ROUNDTRIPS 20

#define PIPEWIRE_MAX_CHANNELS 32

/* ----------------------------- GLOBAL STATE ------------------------------- */

struct pipewire_ctx
{
  struct pw_thread_loop *thread_loop;
  struct pw_context     *context;
  struct pw_core        *core;

  struct spa_hook        core_listener;

  struct commands_base  *cmdbase;

  /* Pending sync seq so we know when the initial core round-trip is done */
  int                    core_seq;
  int                    last_done_seq;

  /*
   * Sleep/wake reconnection timer.
   */
  struct event           *reconnect_ev;
  bool                    reconnect_pending;
};

static struct pipewire_ctx pwctx;

/*
 * Separate PipeWire connection used only for "pwsink" volume control: its
 * own pw_thread_loop/pw_context/pw_core, independent of pwctx above (which
 * carries the audio stream). Registry/metadata lookups and Device/Route
 * writes for sink-volume control run entirely on this connection, so they
 * never contend with -- and can never be blocked by -- the real-time
 * streaming connection, and a fault on either side doesn't affect the
 * other. Only ever populated when pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK;
 * left entirely unused (all pointers NULL) in PWSTREAM or DEFAULT mode.
 */
struct pipewire_sinkctx
{
  struct pw_thread_loop *thread_loop;
  struct pw_context     *context;
  struct pw_core        *core;

  struct spa_hook        core_listener;

  /* Pending sync seq so we know when a round-trip is done */
  int                    last_done_seq;

  /*
   * Set from the core's .error callback; checked by sink-resolution
   * round-trips so a core error unblocks a pw_thread_loop_wait() promptly
   * instead of spinning until the retry budget silently runs out. 0 = no
   * error pending. Cleared at the start of each (re)connect.
   */
  int                    core_error;

  /*
   * Registry and metadata: used to resolve the target Audio/Sink node
   * (either the configured sink_target name, or whatever
   * "default.audio.sink" currently names) and drive its volume natively
   * via the PipeWire API -- no shelling out to wpctl.
   *
   * Two objects end up bound once resolution completes:
   *
   *   sink_proxy   -- the Audio/Sink Node itself. Its own SPA_PROP_Props
   *                   (channelVolumes) is a real but largely inert
   *                   software gain stage for hardware-routed sinks; for
   *                   sinks with no owning Device (virtual/software-only
   *                   sinks) it's the *only* volume path, so we always
   *                   keep it in sync as a fallback.
   *
   *   device_proxy -- the parent Device that owns the sink's active
   *                   hardware Route (if the sink has a device.id at
   *                   all). This is what actually moves the ALSA mixer
   *                   control on hardware-routed sinks; the Node's own
   *                   Props does not. Volume is applied here by writing
   *                   SPA_PARAM_Route with the route's index/device/
   *                   direction echoed back unchanged and an updated
   *                   embedded Props(channelVolumes), plus
   *                   SPA_PARAM_ROUTE_save=true so WirePlumber persists
   *                   it the same way `wpctl set-volume` would.
   */
  struct pw_registry     *registry;
  struct spa_hook         registry_listener;

  struct pw_proxy        *metadata_proxy;
  struct spa_hook         metadata_listener;

  struct pw_proxy        *sink_proxy;
  struct spa_hook         sink_node_listener;
  uint32_t                sink_global_id;

  struct pw_proxy        *device_proxy;
  struct spa_hook         device_listener;
  uint32_t                device_global_id;

  /*
   * Active output Route on device_proxy, as last reported by the server.
   * index/device/direction must be echoed back unchanged in our own
   * SPA_PARAM_Route write; only the embedded Props(channelVolumes) differs.
   *
   * For a device with more than one selectable output route (built-in
   * speaker + headphone jack, say) simply taking the first Output-direction
   * route seen is not fully correct -- it should be matched against
   * whichever route is actually selected. Not implemented; unaffected for
   * the common single-route USB/HAT DAC case.
   */
  int32_t                 route_index;
  int32_t                 route_device;
  uint32_t                route_direction;
  bool                    have_route;

  /* false if the resolved sink has no device.id at all (e.g. a virtual/
   * software-only sink) -- then there is no Route to wait for, and Node
   * Props is the only volume path available. */
  bool                    has_hw_route;

  /*
   * Every Audio/Sink seen in the registry so far, keyed by global id, so
   * that once "default.audio.sink" (or sink_target) names a node we can
   * match against ones already seen as well as ones that show up later.
   */
  struct pwsink_seen
  {
    uint32_t id;
    char     name[256];
    uint32_t device_id;
    struct pwsink_seen *next;
  } *known_sinks;

  /*
   * Name of the node we're trying to bind: either the configured
   * sink_target override, or whatever "default.audio.sink" metadata
   * last resolved to. Empty until known.
   */
  char                    target_name[256];

  /* Explicit "sink_target" config override; empty = follow the system
   * default sink via metadata. Never overwritten after init. */
  char                    configured_target[256];

  /* Last channelVolumes read back from the sink Node's own Props. */
  float                   node_volumes[PIPEWIRE_MAX_CHANNELS];
  uint32_t                n_node_volumes;
  bool                    have_node_volume;

  /* Last channelVolumes read back from the Device's active Route -- the
   * ones that actually reflect real hardware/ALSA state. */
  float                   route_volumes[PIPEWIRE_MAX_CHANNELS];
  uint32_t                n_route_volumes;

  /*
   * Cached 0-100 value to return from a hypothetical GetVolume() / used to
   * avoid re-deriving our own just-set value from float round-tripping.
   * -1 = unknown, ask PipeWire's last-read-back state instead.
   */
  int                     cached_volume_pct;

  /*
   * Sleep/wake reconnection timer, separate from pwctx's -- the two
   * connections can fail and recover independently.
   */
  struct event           *reconnect_ev;
  bool                    reconnect_pending;
};

static struct pipewire_sinkctx pwsinkctx;

/*
 * Volume control mode, selected via the "pipewire_mixer" key in the [audio] config
 * section:
 *
 *   mixer = "pwsink" -- drive the actual PipeWire/WirePlumber SINK volume
 *     (Device Route when the sink has a hardware mixer control, Node Props
 *     as fallback for routeless/virtual sinks), shared system-wide with
 *     every other PipeWire client. The stream itself is pinned to unity
 *     gain (1.0) whenever this mode is active, so there is exactly one
 *     gain stage in effect, at the sink.
 *
 *   mixer = "pwstream" -- drive OwnTone's own STREAM volume via
 *     SPA_PROP_channelVolumes. Always software, never hardware-backed, and
 *     not shared with other PipeWire clients -- but doesn't depend on the
 *     sink having a usable hardware volume route at all.
 *
 *   (unset / not "pwsink" or "pwstream") -- no PipeWire-specific volume
 *     handling of any kind: device_volume_set() is a no-op, and the stream
 *     is left at whatever PipeWire's own default is.
 */
enum pipewire_mixer_mode
{
  PIPEWIRE_MIXER_PWSINK,
  PIPEWIRE_MIXER_PWSTREAM,
};

static enum pipewire_mixer_mode pipewire_mixer_mode = PIPEWIRE_MIXER_PWSTREAM;

/*
 * Volume curve used for pwsink mode, selected via "sink_volume_curve" in
 * [audio] ("cubic", the default, or "linear").
 *
 * WirePlumber/wpctl display and set volume on a cubic scale rather than
 * linear PCM gain -- this is what gives `wpctl set-volume 0.5` roughly
 * "half loudness" rather than "half amplitude" perceptually. Matching it
 * here means a given OwnTone volume percentage lines up with what
 * `wpctl get-volume` shows for the same node. "linear" bypasses that and
 * writes the percentage straight into channelVolumes.
 */
enum pipewire_volume_curve
{
  PIPEWIRE_CURVE_CUBIC,
  PIPEWIRE_CURVE_LINEAR,
};

static enum pipewire_volume_curve pipewire_volume_curve = PIPEWIRE_CURVE_CUBIC;

static inline float
pct_to_volume(int pct, enum pipewire_volume_curve curve)
{
  float linear;

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  linear = (float)pct / 100.0f;
  return (curve == PIPEWIRE_CURVE_CUBIC) ? (linear * linear * linear) : linear;
}

static inline int
volume_to_pct(float vol, enum pipewire_volume_curve curve)
{
  float linear;

  if (vol < 0.0f)
    vol = 0.0f;

  linear = (curve == PIPEWIRE_CURVE_CUBIC) ? cbrtf(vol) : vol;

  return (int)lroundf(linear * 100.0f);
}

/* ----------------------------- SESSION ------------------------------------ */

struct pipewire_session
{
  uint64_t device_id;
  int      callback_id;

  struct pw_stream      *stream;
  struct spa_hook        stream_listener;

  enum pw_stream_state   state;

  /*
   * Last volume set via pipewire_device_volume_set(), as a 0.0-1.0 linear
   * fraction. Only meaningful in PIPEWIRE_MIXER_PWSTREAM mode, where it is
   * re-applied every time the stream (re)connects -- otherwise a reconnect
   * (e.g. on a quality change via playback_restart()) would leave the new
   * stream instance at PipeWire's own default volume until the next
   * explicit volume() call.  Defaults to 1.0 (unity) so a stream that's
   * never had an explicit volume command starts at full scale.
   */
  float    stream_volume;

  struct media_quality quality;

  int      logcount;

  /*
   * Ring buffer of audio bytes, owned by the session.
   *
   * OwnTone's player thread calls playback_write() roughly every ~10ms with
   * small chunks (~1764 bytes at 44100/16/2), while PipeWire's data-loop
   * thread calls on_process() at its own period (observed: 2048 samples,
   * i.e. ~43ms @ 48kHz, wanting ~49152 bytes of F32 after conversion). The
   * two rates don't line up 1:1, so a single-slot buffer either drops most
   * writes (overwritten before being read) or mostly returns silence
   * (on_process wants more bytes per call than one chunk provides). A ring
   * buffer lets bytes accumulate from many small writes and be drained in
   * however-large a slice on_process needs.
   *
   * Capacity is sized for ~250ms of audio at the highest quality we expect,
   * recomputed in stream_open() once we know the quality.
   */
  uint8_t *ring;
  size_t   ring_capacity;
  size_t   ring_head;   /* next byte to write */
  size_t   ring_tail;   /* next byte to read  */
  size_t   ring_fill;   /* bytes currently buffered (avoids head==tail ambiguity) */

  struct pipewire_session *next;
};

/* From player.c */
extern struct event_base *evbase_player;

/* Active sessions list */
static struct pipewire_session *sessions;

static struct media_quality pipewire_last_quality;
static struct media_quality pipewire_fallback_quality = { 44100, 16, 2, 0 };

/* ----------------------------- HELPERS ------------------------------------ */

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

/*
 * Build a Props pod to set per-stream volume. Only used in "pwstream" mixer
 * mode, and for pinning the stream to unity gain in "pwsink" mode -- in the
 * unset/default mode neither this nor any PipeWire sink call is ever made.
 */
static const struct spa_pod *
build_stream_volume_param(struct spa_pod_builder *b, float vol, uint32_t channels)
{
  float vols[8];
  uint32_t i;

  channels = (channels < 8) ? channels : 8;
  if (channels == 0)
    channels = 2;

  for (i = 0; i < channels; i++)
    vols[i] = vol;

  return spa_pod_builder_add_object(b,
    SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
    SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, channels, vols),
    0);
}

/* ------------------- PWSINK: DEVICE/ROUTE VOLUME CONTROL ------------------
 *
 * The functions in this section implement "pwsink" mixer mode: driving the
 * system's actual sink/device volume over PipeWire's native protocol,
 * rather than merely attenuating OwnTone's own stream.
 *
 * Why Device/Route and not just Node Props
 * -----------------------------------------
 * A PipeWire stream or node's own SPA_PROP_channelVolumes is a real
 * parameter, and setting it does change what that object reports -- but for
 * a *sink* backed by a hardware device with its own mixer control (the
 * common case: any ALSA card WirePlumber has given a Route with
 * route.hw-volume = true, e.g. a USB DAC or HAT's onboard "Digital" or
 * "PCM" control), the Node's Props is not what WirePlumber actually wires
 * up to move that hardware control. The thing that does is the *parent
 * Device's* active Route: WirePlumber listens for SPA_PARAM_Route writes
 * with an embedded Props(channelVolumes) and pushes that down to the ALSA
 * mixer element itself.
 *
 * This is also exactly what `wpctl set-volume` and pavucontrol do under the
 * hood for a hardware sink -- they are Device/Route writes, not Node Props
 * writes. Writing only Node Props is why volume changes can appear to
 * silently do nothing on hardware-routed sinks: the write "succeeds"
 * against an object nothing downstream is listening to.
 *
 * For sinks with no owning Device at all (device.id absent -- typically a
 * virtual/software-only sink, e.g. a null-sink or a Bluetooth profile
 * without hardware mixer support), there is no Route to write, and Node
 * Props is the only volume path; has_hw_route tracks this and
 * pwsink_set_volume() falls back to Node-Props-only for such sinks.
 *
 * Resolution flow
 * ---------------
 *  1. Registry + "default" Metadata object give us either the configured
 *     sink_target name, or whatever "default.audio.sink" currently names
 *     (on_metadata_property).
 *  2. Every Audio/Sink Node seen in the registry is recorded in
 *     known_sinks (on_registry_global) regardless of whether it matches
 *     yet, since metadata and matching sink nodes can arrive in either
 *     order.
 *  3. pwsink_maybe_bind() matches target_name/configured_target against
 *     known_sinks; once matched, binds the Node (for Props fallback +
 *     read-back) and, if it has a device.id, the parent Device (for Route
 *     read-back/write).
 *  4. pwsink_wait_ready() spins pw_core_sync() round-trips (bounded by
 *     PIPEWIRE_SINK_RESOLVE_ROUNDTRIPS) until both are bound and their
 *     initial volume has arrived, or the core reports an error via
 *     pwsinkctx.core_error.
 *
 * Connection
 * ----------
 * All of this runs on pwsinkctx's own connection (its own pw_thread_loop,
 * pw_context, and pw_core), independent of pwctx which carries the audio
 * stream. Every function below must be called with pwsinkctx.thread_loop's
 * lock held, exactly as pwctx.thread_loop's lock guards access to streaming
 * objects elsewhere in this file -- the two locks are never held at once
 * and never need to be, since the two connections don't share any object.
 */

static void pwsink_reset_state(void);

/* Defined further down (after the callbacks they reference); forward
 * declared here since pwsink_maybe_bind() (above them, needed by the
 * metadata/registry callbacks) must pass their addresses to
 * pw_node_add_listener()/pw_device_add_listener(). */
static const struct pw_node_events sink_node_events;
static const struct pw_device_events device_events;

/* Must be called with pwsinkctx.thread_loop locked. */
static bool
pwsink_roundtrip(void)
{
  int seq;

  seq = pw_core_sync(pwsinkctx.core, PW_ID_CORE, 0);
  while (pwsinkctx.last_done_seq != seq)
    {
      if (pwsinkctx.core_error != 0)
        return false;
      pw_thread_loop_wait(pwsinkctx.thread_loop);
    }

  return (pwsinkctx.core_error == 0);
}

static bool
pwsink_is_ready(void)
{
  if (!pwsinkctx.sink_proxy || !pwsinkctx.have_node_volume)
    return false;
  if (!pwsinkctx.has_hw_route)
    return true;
  return (pwsinkctx.device_proxy != NULL) && pwsinkctx.have_route;
}

/* Must be called with pwsinkctx.thread_loop locked. */
static bool
pwsink_wait_ready(void)
{
  int i;

  for (i = 0; i < PIPEWIRE_SINK_RESOLVE_ROUNDTRIPS && !pwsink_is_ready(); i++)
    {
      if (!pwsink_roundtrip())
        return false;
    }

  return pwsink_is_ready();
}

/* Must be called with pwsinkctx.thread_loop locked. */
static void
pwsink_maybe_bind(void)
{
  const char *wanted;
  struct pwsink_seen *s;

  if (pwsinkctx.sink_proxy)
    return;

  wanted = pwsinkctx.configured_target[0] ? pwsinkctx.configured_target : pwsinkctx.target_name;
  if (!wanted[0])
    return;

  for (s = pwsinkctx.known_sinks; s; s = s->next)
    {
      if (!spa_streq(s->name, wanted))
        continue;

      DPRINTF(E_DBG, L_LAUDIO, "PipeWire: binding sink id=%u name='%s' device.id=%u\n",
        s->id, s->name, s->device_id);

      pwsinkctx.sink_global_id = s->id;
      pwsinkctx.sink_proxy = pw_registry_bind(pwsinkctx.registry, s->id,
                              PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
      if (!pwsinkctx.sink_proxy)
        {
          DPRINTF(E_LOG, L_LAUDIO, "PipeWire: failed to bind sink node %u\n", s->id);
          return;
        }

      pw_node_add_listener((struct pw_node *)pwsinkctx.sink_proxy,
                           &pwsinkctx.sink_node_listener, &sink_node_events, NULL);
      pw_node_enum_params((struct pw_node *)pwsinkctx.sink_proxy,
                          0, SPA_PARAM_Props, 0, UINT32_MAX, NULL);

      /*
       * The Node's own Props (above) reflects real-but-largely-inert
       * software state for a hardware-routed sink; the object that
       * actually drives the audible/ALSA mixer-control volume is the
       * parent Device's active Route, so bind that too when available.
       */
      if (s->device_id != SPA_ID_INVALID)
        {
          pwsinkctx.device_global_id = s->device_id;
          pwsinkctx.device_proxy = pw_registry_bind(pwsinkctx.registry, s->device_id,
                                     PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
          if (pwsinkctx.device_proxy)
            {
              pw_device_add_listener((struct pw_device *)pwsinkctx.device_proxy,
                                     &pwsinkctx.device_listener, &device_events, NULL);
              pw_device_enum_params((struct pw_device *)pwsinkctx.device_proxy,
                                    0, SPA_PARAM_Route, 0, UINT32_MAX, NULL);
            }
          else
            {
              DPRINTF(E_LOG, L_LAUDIO, "PipeWire: failed to bind device %u for sink %u\n",
                s->device_id, s->id);
              pwsinkctx.has_hw_route = false;
            }
        }
      else
        {
          pwsinkctx.has_hw_route = false;
          DPRINTF(E_DBG, L_LAUDIO,
            "PipeWire: sink node id=%u has no device.id -- no hardware Route "
            "available, only software Node volume will be used\n", s->id);
        }

      return;
    }
}

/*
 * Write channelVolumes to the sink Node's own Props. Always kept in sync as
 * a fallback / for routeless sinks; on hardware-routed sinks this is largely
 * inert (see the big comment above) but harmless to also set.
 *
 * Must be called with pwsinkctx.thread_loop locked.
 */
static void
pwsink_apply_node_volume(void)
{
  uint8_t buf[512];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod_frame obj_frame, array_frame;
  const struct spa_pod *param;
  uint32_t n, i;

  if (!pwsinkctx.sink_proxy)
    return;

  n = (pwsinkctx.n_node_volumes > 0) ? pwsinkctx.n_node_volumes : 2;

  spa_pod_builder_push_object(&b, &obj_frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
  spa_pod_builder_push_array(&b, &array_frame);
  for (i = 0; i < n; i++)
    spa_pod_builder_float(&b, pwsinkctx.node_volumes[i]);
  spa_pod_builder_pop(&b, &array_frame);
  param = spa_pod_builder_pop(&b, &obj_frame);

  pw_node_set_param((struct pw_node *)pwsinkctx.sink_proxy, SPA_PARAM_Props, 0, param);
}

/*
 * Write the Device's active Route back with updated channelVolumes and
 * ROUTE_save=true, so WirePlumber both applies it to the real hardware
 * mixer control and persists it exactly as `wpctl set-volume` would.
 * index/device/direction are echoed back unchanged, as required.
 *
 * Must be called with pwsinkctx.thread_loop locked.
 */
static void
pwsink_apply_route_volume(void)
{
  uint8_t buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  struct spa_pod_frame route_frame, props_frame, array_frame;
  const struct spa_pod *param;
  uint32_t n, i;

  if (!pwsinkctx.device_proxy || !pwsinkctx.have_route)
    return;

  n = (pwsinkctx.n_node_volumes > 0) ? pwsinkctx.n_node_volumes : 2;

  spa_pod_builder_push_object(&b, &route_frame, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_index, 0);
  spa_pod_builder_int(&b, pwsinkctx.route_index);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_device, 0);
  spa_pod_builder_int(&b, pwsinkctx.route_device);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
  spa_pod_builder_push_object(&b, &props_frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
  spa_pod_builder_push_array(&b, &array_frame);
  for (i = 0; i < n; i++)
    spa_pod_builder_float(&b, pwsinkctx.node_volumes[i]);
  spa_pod_builder_pop(&b, &array_frame);
  spa_pod_builder_pop(&b, &props_frame);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_save, 0);
  spa_pod_builder_bool(&b, true);

  param = spa_pod_builder_pop(&b, &route_frame);

  pw_device_set_param((struct pw_device *)pwsinkctx.device_proxy, SPA_PARAM_Route, 0, param);
}

/*
 * Set the resolved sink's volume to vol (0.0-1.0 linear), applying both the
 * Node Props write (fallback/always) and the Device Route write (the one
 * that actually matters for hardware-routed sinks, when available).
 *
 * Must be called with pwsinkctx.thread_loop locked. Returns 0 on success
 * (writes queued; a round-trip is issued to detect a core error promptly),
 * -1 if the sink isn't resolved yet.
 */
static int
pwsink_set_volume(float vol)
{
  uint32_t n, i;

  if (vol < 0.0f) vol = 0.0f;
  if (vol > 1.0f) vol = 1.0f;

  if (!pwsinkctx.sink_proxy)
    {
      DPRINTF(E_WARN, L_LAUDIO,
        "PipeWire: sink not yet resolved, cannot set volume\n");
      return -1;
    }

  n = (pwsinkctx.n_node_volumes > 0) ? pwsinkctx.n_node_volumes : 2;
  n = (n < PIPEWIRE_MAX_CHANNELS) ? n : PIPEWIRE_MAX_CHANNELS;
  for (i = 0; i < n; i++)
    pwsinkctx.node_volumes[i] = vol;
  pwsinkctx.n_node_volumes = n;

  pwsink_apply_node_volume();
  pwsink_apply_route_volume();

  /* Best-effort: confirm the writes didn't race a core error. Not fatal if
   * this particular round-trip fails; the next volume/status call will
   * surface a persistent problem. */
  pwsink_roundtrip();

  pwsinkctx.cached_volume_pct = volume_to_pct(vol, pipewire_volume_curve);

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire: set sink volume to %.4f (%d%%)\n",
    (double)vol, pwsinkctx.cached_volume_pct);

  return 0;
}

/*
 * Best-effort read of the sink's current volume as a 0-100 percentage,
 * using the curve configured via sink_volume_curve. Prefers the Route's
 * volumes (the "true" hardware-reflecting value) over the Node's Props,
 * and falls back to the last value OwnTone itself set if nothing has been
 * read back from PipeWire yet.
 */
static int
pwsink_get_volume_pct(void)
{
  if (pwsinkctx.cached_volume_pct >= 0)
    return pwsinkctx.cached_volume_pct;

  if (pwsinkctx.have_route && pwsinkctx.n_route_volumes > 0)
    return volume_to_pct(pwsinkctx.route_volumes[0], pipewire_volume_curve);

  if (pwsinkctx.have_node_volume)
    return volume_to_pct(pwsinkctx.node_volumes[0], pipewire_volume_curve);

  return -1;
}

/* Frees known_sinks and clears all resolution state, but does not touch the
 * connection itself (thread_loop/context/core) or destroy any bound
 * proxies -- callers do that separately, in the order required by
 * PipeWire's proxy lifetime rules, before calling this. */
static void
pwsink_reset_state(void)
{
  struct pwsink_seen *s, *next;

  for (s = pwsinkctx.known_sinks; s; s = next)
    {
      next = s->next;
      free(s);
    }
  pwsinkctx.known_sinks = NULL;

  pwsinkctx.sink_global_id   = SPA_ID_INVALID;
  pwsinkctx.device_global_id = SPA_ID_INVALID;
  pwsinkctx.have_node_volume = false;
  pwsinkctx.have_route        = false;
  pwsinkctx.has_hw_route       = true;
  pwsinkctx.n_route_volumes   = 0;
  pwsinkctx.n_node_volumes    = 0;
  pwsinkctx.target_name[0]    = '\0';
  pwsinkctx.core_error        = 0;
  pwsinkctx.cached_volume_pct = -1;
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

  free(ps->ring);
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
  ps->stream_volume = 1.0f;

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

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire stream state: %s -> %s%s%s\n",
    pw_stream_state_as_string(old),
    pw_stream_state_as_string(state),
    error ? " (" : "", error ? error : "");

  ps->state = state;

  switch (state)
    {
      case PW_STREAM_STATE_ERROR:
        DPRINTF(E_LOG, L_LAUDIO, "PipeWire stream failed: %s\n",
          error ? error : "(unknown)");
        pipewire_session_shutdown(ps);
        break;

      case PW_STREAM_STATE_UNCONNECTED:
        pipewire_session_shutdown(ps);
        break;

      case PW_STREAM_STATE_PAUSED:
        /*
         * PAUSED is the first state entered after a successful connect
         * (before the stream goes to STREAMING once we call
         * pw_stream_set_active). We use it as the "ready" signal -- send
         * status back to the player -- and also (re-)assert the correct
         * stream-level volume for the active mixer mode, since a fresh
         * pw_stream connection always starts at PipeWire's own default
         * volume regardless of what was set on a previous stream instance
         * for this same session (e.g. after playback_restart() on a
         * quality change).
         *
         *   - pwsink mode: pin the stream to unity (1.0) so the sink/
         *     device volume is the only gain stage in effect.
         *   - pwstream mode: re-apply the last volume OwnTone asked for,
         *     so a reconnect doesn't silently reset to full scale (or
         *     whatever PipeWire's default is) until the next explicit
         *     volume() call.
         *   - default mode: do nothing, leave PipeWire's own default in
         *     effect.
         */
        if (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK ||
            pipewire_mixer_mode == PIPEWIRE_MIXER_PWSTREAM)
          {
            uint8_t buf[256];
            struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
            float vol = (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK) ? 1.0f : ps->stream_volume;
            const struct spa_pod *param = build_stream_volume_param(&b, vol,
                                            (uint32_t)ps->quality.channels);
            pw_stream_set_param(ps->stream, SPA_PARAM_Props, param);
          }
        pipewire_status(ps);
        break;

      case PW_STREAM_STATE_STREAMING:
      case PW_STREAM_STATE_CONNECTING:
        break;
    }
}

/*
 * PipeWire calls this on its RT data thread whenever it wants more audio.
 * PipeWire holds the thread loop lock while calling us, so access to the
 * ring buffer is serialised with playback_write() which also takes that lock.
 *
 * pwbuf->requested tells us how many samples PipeWire actually wants to fill
 * this period (NOT sbuf->datas[0].maxsize, which is the buffer's allocated
 * capacity and can be much larger than what's needed per cycle). If
 * requested is 0 (some drivers don't set it), we fall back to maxsize.
 *
 * We drain up to that many bytes from the ring; if the ring has less than
 * requested, we drain what we have and pad the remainder with silence.
 */
static void
on_process(void *userdata)
{
  struct pipewire_session *ps = userdata;
  struct pw_buffer *pwbuf;
  struct spa_buffer *sbuf;
  uint8_t *dst;
  uint32_t n_bytes;
  uint32_t stride;
  uint32_t want_bytes;
  size_t avail;
  size_t take;
  size_t first_chunk;

  if (!ps->stream)
    return;

  pwbuf = pw_stream_dequeue_buffer(ps->stream);
  if (!pwbuf)
    return;

  sbuf = pwbuf->buffer;
  dst  = sbuf->datas[0].data;
  if (!dst)
    goto queue;

  stride  = (uint32_t)((ps->quality.bits_per_sample / 8) * ps->quality.channels);
  n_bytes = sbuf->datas[0].maxsize;

  want_bytes = (uint32_t)(pwbuf->requested * stride);
  if (want_bytes == 0 || want_bytes > n_bytes)
    want_bytes = n_bytes;

  avail = ps->ring_fill;
  take  = (avail < want_bytes) ? avail : want_bytes;

  if (take > 0)
    {
      /* Ring may wrap; copy in at most two contiguous pieces */
      first_chunk = ps->ring_capacity - ps->ring_tail;
      if (first_chunk > take)
        first_chunk = take;

      memcpy(dst, ps->ring + ps->ring_tail, first_chunk);
      if (take > first_chunk)
        memcpy(dst + first_chunk, ps->ring, take - first_chunk);

      ps->ring_tail = (ps->ring_tail + take) % ps->ring_capacity;
      ps->ring_fill -= take;
      ps->logcount = 0;
    }

  if (take < want_bytes)
    memset(dst + take, 0, want_bytes - take);

  sbuf->datas[0].chunk->offset = 0;
  sbuf->datas[0].chunk->stride = stride;
  sbuf->datas[0].chunk->size   = want_bytes;

  if (take < want_bytes && ps->logcount < PIPEWIRE_LOG_MAX)
    {
      ps->logcount++;
      DPRINTF(E_DBG, L_LAUDIO, "PipeWire: ring underrun, wanted %u had %zu (%d/%d)\n",
        want_bytes, take, ps->logcount, PIPEWIRE_LOG_MAX);
    }

 queue:
  pw_stream_queue_buffer(ps->stream, pwbuf);
}

static const struct pw_stream_events stream_events = {
  PW_VERSION_STREAM_EVENTS,
  .state_changed = on_stream_state_changed,
  .process       = on_process,
};

/* ---- REGISTRY & METADATA CALLBACKS (pwsink volume mode, PW THREAD) ------- */

/*
 * Parse the "name" field from a JSON object string like:
 *   { "name": "alsa_output.platform-soc_sound.stereo-fallback" }
 *
 * Writes the name into out (up to out_len bytes, null-terminated).
 * Returns true on success, false if the JSON couldn't be parsed.
 *
 * We use SPA's own spa_json parser (the same library PipeWire and WirePlumber
 * use internally) rather than any external JSON dependency.
 */
static bool
parse_default_sink_name(const char *json, char *out, size_t out_len)
{
  struct spa_json it[2];
  char key[64];

  if (!json || out_len == 0)
    return false;

  spa_json_init(&it[0], json, strlen(json));
  if (spa_json_enter_object(&it[0], &it[1]) <= 0)
    return false;

  while (spa_json_get_string(&it[1], key, sizeof(key)) > 0)
    {
      if (spa_streq(key, "name"))
        {
          if (spa_json_get_string(&it[1], out, out_len) > 0)
            return true;
          return false;
        }
      /* skip value for keys we don't care about */
      {
        const char *dummy;
        if (spa_json_next(&it[1], &dummy) <= 0)
          break;
      }
    }
  return false;
}

/*
 * Called by the metadata listener whenever a "default" metadata property
 * changes. We look for "default.audio.sink" (the effective default), and,
 * only if no sink_target override is configured and nothing has resolved
 * yet, fall back to "default.configured.audio.sink" too.
 *
 * Runs on the pwsinkctx thread loop thread.
 */
static int
on_metadata_property(void *data, uint32_t subject, const char *key,
                     const char *type, const char *value)
{
  bool is_effective;
  bool is_configured_fallback;
  char parsed[256];

  if (subject != PW_ID_CORE || !key)
    return 0;

  is_effective = spa_streq(key, "default.audio.sink");
  is_configured_fallback = !pwsinkctx.configured_target[0] && !pwsinkctx.target_name[0]
                            && spa_streq(key, "default.configured.audio.sink");

  if (!is_effective && !is_configured_fallback)
    return 0;

  if (!value)
    {
      if (is_effective)
        {
          pwsinkctx.target_name[0] = '\0';
          /* An explicit unset of the effective default invalidates any
           * previously-bound sink too, since it may no longer be current. */
          pwsinkctx.sink_global_id = SPA_ID_INVALID;
        }
      return 0;
    }

  if (!parse_default_sink_name(value, parsed, sizeof(parsed)))
    {
      DPRINTF(E_WARN, L_LAUDIO,
        "PipeWire: could not parse default sink name from '%s'\n", value);
      return 0;
    }

  if (spa_streq(parsed, pwsinkctx.target_name))
    return 0; /* no change */

  snprintf(pwsinkctx.target_name, sizeof(pwsinkctx.target_name), "%s", parsed);

  DPRINTF(E_DBG, L_LAUDIO,
    "PipeWire: resolved target sink name to '%s' (via %s)\n",
    pwsinkctx.target_name, key);

  pwsink_maybe_bind();

  return 0;
}

static const struct pw_metadata_events metadata_events = {
  PW_VERSION_METADATA_EVENTS,
  .property = on_metadata_property,
};

/*
 * Called by the registry listener when a new global object appears.
 * We look for two kinds:
 *
 *  1. PW_TYPE_INTERFACE_Metadata with metadata.name "default" -- this is
 *     WirePlumber's metadata store for the default sink/source.  We bind
 *     to it and add the metadata listener.
 *
 *  2. PW_TYPE_INTERFACE_Node with media.class "Audio/Sink" -- recorded into
 *     known_sinks regardless of whether it currently matches our target, so
 *     that pwsink_maybe_bind() can match it whenever the target is (or
 *     becomes) known, independent of arrival order.
 *
 * Runs on the pwsinkctx thread loop thread.
 */
static void
on_registry_global(void *data, uint32_t id, uint32_t permissions,
                   const char *type, uint32_t version,
                   const struct spa_dict *props)
{
  const char *name;
  const char *class;
  const char *device_id_str;
  uint32_t device_id;
  struct pwsink_seen *s;

  if (!props)
    return;

  if (spa_streq(type, PW_TYPE_INTERFACE_Metadata))
    {
      name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
      if (!name || !spa_streq(name, "default"))
        return;
      if (pwsinkctx.metadata_proxy)
        {
          DPRINTF(E_WARN, L_LAUDIO,
            "PipeWire: found duplicate 'default' metadata, ignoring id=%u\n", id);
          return;
        }

      pwsinkctx.metadata_proxy = pw_registry_bind(pwsinkctx.registry,
                                   id, type, PW_VERSION_METADATA, 0);
      if (!pwsinkctx.metadata_proxy)
        {
          DPRINTF(E_LOG, L_LAUDIO,
            "PipeWire: failed to bind metadata object %u\n", id);
          return;
        }

      pw_metadata_add_listener((struct pw_metadata *)pwsinkctx.metadata_proxy,
                               &pwsinkctx.metadata_listener,
                               &metadata_events, NULL);

      DPRINTF(E_DBG, L_LAUDIO, "PipeWire: bound to default metadata object %u\n", id);
      return;
    }

  if (spa_streq(type, PW_TYPE_INTERFACE_Node))
    {
      class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
      if (!class || !spa_streq(class, "Audio/Sink"))
        return;
      name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
      if (!name)
        return;

      device_id = SPA_ID_INVALID;
      device_id_str = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
      if (device_id_str)
        device_id = (uint32_t)strtoul(device_id_str, NULL, 10);

      DPRINTF(E_DBG, L_LAUDIO, "PipeWire: saw sink node id=%u name='%s' device.id=%u\n",
        id, name, device_id);

      CHECK_NULL(L_LAUDIO, s = calloc(1, sizeof(struct pwsink_seen)));
      s->id = id;
      snprintf(s->name, sizeof(s->name), "%s", name);
      s->device_id = device_id;
      s->next = pwsinkctx.known_sinks;
      pwsinkctx.known_sinks = s;

      pwsink_maybe_bind();
      return;
    }
}

static void
on_registry_global_remove(void *data, uint32_t id)
{
  struct pwsink_seen *s, *prev = NULL;

  for (s = pwsinkctx.known_sinks; s; prev = s, s = s->next)
    {
      if (s->id != id)
        continue;

      if (prev) prev->next = s->next;
      else      pwsinkctx.known_sinks = s->next;
      free(s);
      break;
    }

  if (id == pwsinkctx.sink_global_id)
    {
      /* The sink we were bound to disappeared (unplugged, etc.). Drop it;
       * volume_set() will report failure until resolution completes again
       * (either from a later registry event, or after a reconnect). */
      DPRINTF(E_DBG, L_LAUDIO,
        "PipeWire: bound sink node %u removed, awaiting new default\n", id);

      if (pwsinkctx.sink_proxy)
        {
          spa_hook_remove(&pwsinkctx.sink_node_listener);
          pw_proxy_destroy(pwsinkctx.sink_proxy);
          pwsinkctx.sink_proxy = NULL;
        }
      if (pwsinkctx.device_proxy)
        {
          spa_hook_remove(&pwsinkctx.device_listener);
          pw_proxy_destroy(pwsinkctx.device_proxy);
          pwsinkctx.device_proxy = NULL;
        }
      pwsinkctx.sink_global_id   = SPA_ID_INVALID;
      pwsinkctx.device_global_id = SPA_ID_INVALID;
      pwsinkctx.have_node_volume = false;
      pwsinkctx.have_route        = false;
      pwsinkctx.has_hw_route       = true;
    }
}

static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global        = on_registry_global,
  .global_remove = on_registry_global_remove,
};

/*
 * Called whenever the bound sink Node reports a param -- we only care about
 * SPA_PARAM_Props / SPA_PROP_channelVolumes, read back so pwsink_get_volume_pct()
 * has real data and pwsink_set_volume() knows the current channel count.
 */
static void
on_sink_node_param(void *data, int seq, uint32_t id, uint32_t index,
                   uint32_t next, const struct spa_pod *param)
{
  const struct spa_pod_object *obj;
  const struct spa_pod_prop *prop;
  const void *raw;
  uint32_t n;

  if (!param || !spa_pod_is_object(param))
    return;

  obj = (const struct spa_pod_object *)param;
  if (obj->body.id != SPA_PARAM_Props)
    return;

  SPA_POD_OBJECT_FOREACH(obj, prop)
    {
      if (prop->key != SPA_PROP_channelVolumes)
        continue;

      n = 0;
      raw = spa_pod_get_array(&prop->value, &n);
      if (!raw || n == 0)
        return;

      n = (n < PIPEWIRE_MAX_CHANNELS) ? n : PIPEWIRE_MAX_CHANNELS;
      memcpy(pwsinkctx.node_volumes, raw, n * sizeof(float));
      pwsinkctx.n_node_volumes  = n;
      pwsinkctx.have_node_volume = true;

      DPRINTF(E_DBG, L_LAUDIO,
        "PipeWire: read back node volume n=%u volumes[0]=%.4f\n",
        n, (double)pwsinkctx.node_volumes[0]);
      return;
    }
}

static const struct pw_node_events sink_node_events = {
  PW_VERSION_NODE_EVENTS,
  .param = on_sink_node_param,
};

/*
 * Called whenever the bound Device reports a param -- we only care about
 * SPA_PARAM_Route for the first Output-direction route seen (see the
 * caveat on route_index et al. above for multi-route devices).
 */
static void
on_device_param(void *data, int seq, uint32_t id, uint32_t index,
                uint32_t next, const struct spa_pod *param)
{
  const struct spa_pod_object *obj;
  const struct spa_pod_prop *prop;
  int32_t found_index = -1, found_device = -1;
  uint32_t found_direction = 0;
  bool have_index = false, have_direction = false;
  float volumes[PIPEWIRE_MAX_CHANNELS];
  uint32_t n_volumes = 0;
  bool have_volumes = false;

  if (!param || !spa_pod_is_object(param))
    return;

  obj = (const struct spa_pod_object *)param;
  if (obj->body.id != SPA_PARAM_Route)
    return;

  if (pwsinkctx.have_route)
    return; /* only take the first Output-direction route we see */

  SPA_POD_OBJECT_FOREACH(obj, prop)
    {
      switch (prop->key)
        {
          case SPA_PARAM_ROUTE_index:
            if (spa_pod_get_int(&prop->value, &found_index) >= 0)
              have_index = true;
            break;

          case SPA_PARAM_ROUTE_device:
            spa_pod_get_int(&prop->value, &found_device);
            break;

          case SPA_PARAM_ROUTE_direction:
            if (spa_pod_get_id(&prop->value, &found_direction) >= 0)
              have_direction = true;
            break;

          case SPA_PARAM_ROUTE_props:
            {
              const struct spa_pod_object *props_obj;
              const struct spa_pod_prop *pp;
              const void *raw;
              uint32_t n;

              if (!spa_pod_is_object(&prop->value))
                break;

              props_obj = (const struct spa_pod_object *)&prop->value;
              SPA_POD_OBJECT_FOREACH(props_obj, pp)
                {
                  if (pp->key != SPA_PROP_channelVolumes)
                    continue;

                  n = 0;
                  raw = spa_pod_get_array(&pp->value, &n);
                  if (raw && n > 0)
                    {
                      n = (n < PIPEWIRE_MAX_CHANNELS) ? n : PIPEWIRE_MAX_CHANNELS;
                      memcpy(volumes, raw, n * sizeof(float));
                      n_volumes = n;
                      have_volumes = true;
                    }
                }
              break;
            }

          default:
            break;
        }
    }

  if (!have_index || !have_direction || found_direction != SPA_DIRECTION_OUTPUT)
    return;

  pwsinkctx.route_index     = found_index;
  pwsinkctx.route_device    = found_device;
  pwsinkctx.route_direction = found_direction;
  pwsinkctx.have_route       = true;

  if (have_volumes)
    {
      memcpy(pwsinkctx.route_volumes, volumes, n_volumes * sizeof(float));
      pwsinkctx.n_route_volumes = n_volumes;
    }

  DPRINTF(E_DBG, L_LAUDIO,
    "PipeWire: resolved route index=%d device=%d direction=%u volumes[0]=%.4f\n",
    pwsinkctx.route_index, pwsinkctx.route_device, pwsinkctx.route_direction,
    have_volumes ? (double)pwsinkctx.route_volumes[0] : -1.0);
}

static const struct pw_device_events device_events = {
  PW_VERSION_DEVICE_EVENTS,
  .param = on_device_param,
};

/* ----------------------- CORE CALLBACKS (PW THREAD) ----------------------- */

static void
on_core_done(void *userdata, uint32_t id, int seq)
{
  if (id == PW_ID_CORE)
    {
      pwctx.last_done_seq = seq;
      pw_thread_loop_signal(pwctx.thread_loop, false);
    }
}

/* Forward declarations -- pipewire_core_reconnect() references both of these
 * which are defined later in this file */
static int stream_open(struct pipewire_session *ps, const struct media_quality *quality);
static const struct pw_core_events core_events;

/*
 * Reconnect to the PipeWire daemon after a core disconnect on the streaming
 * connection (typically caused by the system going to sleep and PipeWire's
 * server being suspended/killed).
 *
 * Called on the player thread via the libevent timer set up in on_core_error.
 * We hold no PW locks here; we take the thread loop lock for the short window
 * where we touch PW objects, matching the pattern used throughout this file.
 *
 * This only concerns the audio-streaming connection (pwctx); the pwsink
 * volume-control connection (pwsinkctx) is entirely separate and recovers
 * independently via pwsink_core_reconnect() below.
 *
 * Recovery sequence:
 *  1. Tear down the stale pw_core (socket is already dead, so we just clean
 *     up our end -- no need to send a disconnect message).
 *  2. Re-connect pw_core via pw_context_connect() (the pw_context itself
 *     is purely a local C object and survives sleep/wake intact).
 *  3. For any active sessions, close their stale pw_stream objects and
 *     re-open fresh ones against the new core connection.
 */
static void
pipewire_core_reconnect(void)
{
  struct pipewire_session *ps;
  int ret;

  DPRINTF(E_LOG, L_LAUDIO, "PipeWire: attempting stream reconnect to daemon after sleep/wake\n");

  pw_thread_loop_lock(pwctx.thread_loop);

  /* Tear down stale core */
  if (pwctx.core)
    {
      spa_hook_remove(&pwctx.core_listener);
      pw_core_disconnect(pwctx.core);
      pwctx.core = NULL;
    }

  /* Reconnect using the surviving pw_context */
  pwctx.core = pw_context_connect(pwctx.context, NULL, 0);
  if (!pwctx.core)
    {
      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: stream reconnect failed (%s) -- will retry in %d ms\n",
        strerror(errno), PIPEWIRE_RECONNECT_MS);
      pw_thread_loop_unlock(pwctx.thread_loop);

      struct timeval tv = {
        .tv_sec  = PIPEWIRE_RECONNECT_MS / 1000,
        .tv_usec = (PIPEWIRE_RECONNECT_MS % 1000) * 1000,
      };
      event_add(pwctx.reconnect_ev, &tv);
      return;
    }

  pw_core_add_listener(pwctx.core, &pwctx.core_listener, &core_events, NULL);

  /* Sync to confirm the connection is live before touching streams */
  pwctx.core_seq = pw_core_sync(pwctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwctx.thread_loop);

  pw_thread_loop_unlock(pwctx.thread_loop);

  DPRINTF(E_LOG, L_LAUDIO, "PipeWire: stream reconnected to daemon\n");

  /* Re-open streams for all active sessions.  Each session's stale pw_stream
   * was already destroyed by the PW_STREAM_STATE_ERROR path in
   * on_stream_state_changed, which sets ps->stream = NULL.  We just need to
   * re-open fresh ones.  Sessions whose quality is zeroed (never had a
   * stream opened yet) are skipped -- playback_restart() will open them
   * when audio first arrives. */
  for (ps = sessions; ps; ps = ps->next)
    {
      if (ps->quality.sample_rate == 0)
        continue; /* never had a stream, nothing to restore */

      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: re-opening stream for session after reconnect\n");

      ret = stream_open(ps, &ps->quality);
      if (ret < 0)
        {
          DPRINTF(E_LOG, L_LAUDIO,
            "PipeWire: failed to re-open stream after reconnect\n");
          /* Leave session in error state -- player will notice and stop */
        }
    }

  pwctx.reconnect_pending = false;
}

/*
 * libevent timer callback: fired PIPEWIRE_RECONNECT_MS after a streaming
 * core disconnect to give PipeWire's daemon time to restart after a
 * sleep/wake. Runs on the player thread (evbase_player), same thread that
 * drives commands_base, so it's safe to call into the PW thread loop here.
 */
static void
pipewire_reconnect_cb(int fd, short what, void *arg)
{
  pipewire_core_reconnect();
}

static void
on_core_error(void *userdata, uint32_t id, int seq, int res, const char *message)
{
  DPRINTF(E_LOG, L_LAUDIO, "PipeWire stream core error id=%" PRIu32 " seq=%d res=%d: %s\n",
    id, seq, res, message);

  if (id == PW_ID_CORE)
    {
      /*
       * The streaming core connection dropped -- most likely a sleep/wake
       * event killed PipeWire's server process. Shut down all active
       * sessions (marking them failed so the player knows audio stopped)
       * and schedule a reconnect attempt after a short delay to give
       * PipeWire time to restart.
       *
       * We signal the thread loop so any in-progress pw_thread_loop_wait()
       * (e.g. during pipewire_init's initial sync) can unblock cleanly.
       *
       * This only affects the streaming connection; the separate pwsink
       * connection (if any) is unaffected and recovers independently.
       */
      pipewire_session_shutdown_all(PW_STREAM_STATE_ERROR);
      pw_thread_loop_signal(pwctx.thread_loop, false);

      if (!pwctx.reconnect_pending && pwctx.reconnect_ev)
        {
          struct timeval tv = {
            .tv_sec  = PIPEWIRE_RECONNECT_MS / 1000,
            .tv_usec = (PIPEWIRE_RECONNECT_MS % 1000) * 1000,
          };
          pwctx.reconnect_pending = true;
          event_add(pwctx.reconnect_ev, &tv);
          DPRINTF(E_LOG, L_LAUDIO,
            "PipeWire: scheduled stream reconnect in %d ms\n", PIPEWIRE_RECONNECT_MS);
        }
    }
}

static const struct pw_core_events core_events = {
  PW_VERSION_CORE_EVENTS,
  .done  = on_core_done,
  .error = on_core_error,
};

/* ------------------- PWSINK CORE CALLBACKS (PWSINK THREAD) ----------------
 *
 * Mirror of the streaming core callbacks above, but for pwsinkctx's own
 * connection. Kept as a fully separate set of functions/timer/state rather
 * than parameterizing the ones above, since the two connections have
 * different teardown needs (this one also tears down the registry/
 * metadata/sink/device proxies and re-resolves the target sink) and
 * different consequences on failure (a stream reconnect must reopen
 * pw_stream objects; a pwsink reconnect must re-resolve a node/device
 * and has no stream to reopen).
 */

static void
pwsink_on_core_done(void *userdata, uint32_t id, int seq)
{
  if (id == PW_ID_CORE)
    {
      pwsinkctx.last_done_seq = seq;
      pw_thread_loop_signal(pwsinkctx.thread_loop, false);
    }
}

static const struct pw_core_events pwsink_core_events;

/*
 * Reconnect to the PipeWire daemon after a core disconnect on the pwsink
 * volume-control connection. Called on the player thread via the libevent
 * timer set up in pwsink_on_core_error(); we hold no PW locks here and take
 * pwsinkctx.thread_loop's lock for the window where we touch PW objects.
 *
 * Recovery sequence:
 *  1. Tear down stale metadata/sink/device/registry proxies and the stale
 *     core (the socket is already dead; no need to send a disconnect
 *     message).
 *  2. Reconnect pw_core via pw_context_connect() (pwsinkctx.context is a
 *     local object and survives sleep/wake intact).
 *  3. Re-subscribe to the registry and let sink/device/route resolution
 *     start over from scratch -- node/device IDs are reassigned on every
 *     daemon restart, so nothing from before the disconnect can be reused.
 */
static void
pwsink_core_reconnect(void)
{
  DPRINTF(E_LOG, L_LAUDIO, "PipeWire: attempting pwsink reconnect to daemon after sleep/wake\n");

  pw_thread_loop_lock(pwsinkctx.thread_loop);

  if (pwsinkctx.metadata_proxy)
    {
      spa_hook_remove(&pwsinkctx.metadata_listener);
      pw_proxy_destroy(pwsinkctx.metadata_proxy);
      pwsinkctx.metadata_proxy = NULL;
    }

  if (pwsinkctx.sink_proxy)
    {
      spa_hook_remove(&pwsinkctx.sink_node_listener);
      pw_proxy_destroy(pwsinkctx.sink_proxy);
      pwsinkctx.sink_proxy = NULL;
    }

  if (pwsinkctx.device_proxy)
    {
      spa_hook_remove(&pwsinkctx.device_listener);
      pw_proxy_destroy(pwsinkctx.device_proxy);
      pwsinkctx.device_proxy = NULL;
    }

  if (pwsinkctx.registry)
    {
      spa_hook_remove(&pwsinkctx.registry_listener);
      pw_proxy_destroy((struct pw_proxy *)pwsinkctx.registry);
      pwsinkctx.registry = NULL;
    }

  pwsink_reset_state();

  if (pwsinkctx.core)
    {
      spa_hook_remove(&pwsinkctx.core_listener);
      pw_core_disconnect(pwsinkctx.core);
      pwsinkctx.core = NULL;
    }

  pwsinkctx.core = pw_context_connect(pwsinkctx.context, NULL, 0);
  if (!pwsinkctx.core)
    {
      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: pwsink reconnect failed (%s) -- will retry in %d ms\n",
        strerror(errno), PIPEWIRE_RECONNECT_MS);
      pw_thread_loop_unlock(pwsinkctx.thread_loop);

      struct timeval tv = {
        .tv_sec  = PIPEWIRE_RECONNECT_MS / 1000,
        .tv_usec = (PIPEWIRE_RECONNECT_MS % 1000) * 1000,
      };
      event_add(pwsinkctx.reconnect_ev, &tv);
      return;
    }

  pw_core_add_listener(pwsinkctx.core, &pwsinkctx.core_listener, &pwsink_core_events, NULL);

  pwsinkctx.registry = pw_core_get_registry(pwsinkctx.core, PW_VERSION_REGISTRY, 0);
  if (pwsinkctx.registry)
    pw_registry_add_listener(pwsinkctx.registry, &pwsinkctx.registry_listener,
                             &registry_events, NULL);
  else
    DPRINTF(E_LOG, L_LAUDIO,
      "PipeWire: failed to re-get registry on pwsink reconnect\n");

  /* If a sink_target override is configured, pwsink_maybe_bind() will
   * match it directly as registry globals arrive without waiting on
   * metadata. */
  if (pwsinkctx.configured_target[0])
    pwsink_maybe_bind();

  /* Sync, then give sink/device/route resolution a bounded budget to
   * complete -- mirrors the same bounded wait done in pipewire_init(). Not
   * fatal if it doesn't complete in time; a later volume_set() will just
   * report the sink as not-yet-resolved and the registry listener keeps
   * working in the background. */
  pw_core_sync(pwsinkctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwsinkctx.thread_loop);
  pwsink_wait_ready();

  pw_thread_loop_unlock(pwsinkctx.thread_loop);

  DPRINTF(E_LOG, L_LAUDIO, "PipeWire: pwsink reconnected to daemon\n");

  pwsinkctx.reconnect_pending = false;
}

/*
 * libevent timer callback: fired PIPEWIRE_RECONNECT_MS after a pwsink core
 * disconnect to give PipeWire's daemon time to restart after a sleep/wake.
 * Runs on the player thread (evbase_player); safe to call into pwsinkctx's
 * thread loop from here, same as the streaming reconnect callback.
 */
static void
pwsink_reconnect_cb(int fd, short what, void *arg)
{
  pwsink_core_reconnect();
}

static void
pwsink_on_core_error(void *userdata, uint32_t id, int seq, int res, const char *message)
{
  DPRINTF(E_LOG, L_LAUDIO, "PipeWire pwsink core error id=%" PRIu32 " seq=%d res=%d: %s\n",
    id, seq, res, message);

  if (id == PW_ID_CORE)
    {
      /*
       * The pwsink connection dropped -- most likely a sleep/wake event
       * killed PipeWire's server process. Record the error so any blocked
       * sink-resolution round-trip (pwsink_roundtrip) fails fast instead of
       * spinning until its retry budget silently expires, and schedule a
       * reconnect attempt after a short delay to give PipeWire time to
       * restart.
       *
       * This is entirely independent of the audio-streaming connection:
       * playback continues (or is separately recovered by pwctx's own
       * on_core_error) regardless of what happens here.
       */
      pwsinkctx.core_error = (res != 0) ? res : -EIO;
      pw_thread_loop_signal(pwsinkctx.thread_loop, false);

      if (!pwsinkctx.reconnect_pending && pwsinkctx.reconnect_ev)
        {
          struct timeval tv = {
            .tv_sec  = PIPEWIRE_RECONNECT_MS / 1000,
            .tv_usec = (PIPEWIRE_RECONNECT_MS % 1000) * 1000,
          };
          pwsinkctx.reconnect_pending = true;
          event_add(pwsinkctx.reconnect_ev, &tv);
          DPRINTF(E_LOG, L_LAUDIO,
            "PipeWire: scheduled pwsink reconnect in %d ms\n", PIPEWIRE_RECONNECT_MS);
        }
    }
}

static const struct pw_core_events pwsink_core_events = {
  PW_VERSION_CORE_EVENTS,
  .done  = pwsink_on_core_done,
  .error = pwsink_on_core_error,
};

/* ----------------------------- MISC HELPERS ------------------------------- */

static void
pipewire_free(void)
{
  if (pwctx.reconnect_ev)
    {
      event_del(pwctx.reconnect_ev);
      event_free(pwctx.reconnect_ev);
      pwctx.reconnect_ev = NULL;
    }

  if (pwctx.thread_loop)
    pw_thread_loop_stop(pwctx.thread_loop);

  /* spa_hook_remove + pw_proxy_destroy must be called while the thread loop
   * is stopped (so no concurrent callbacks) but before pw_core_disconnect
   * (which invalidates all proxies). */
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
 * Tear down pwsinkctx's connection: stop its thread loop, destroy any
 * bound proxies (metadata/sink/device/registry) before disconnecting the
 * core (which would otherwise invalidate them out from under us), then
 * disconnect the core and destroy the context/thread loop themselves.
 * Entirely independent of pipewire_free() above -- called whenever pwsink
 * mode isn't (or is no longer) in use, and during pipewire_deinit().
 */
static void
pwsink_free(void)
{
  if (pwsinkctx.reconnect_ev)
    {
      event_del(pwsinkctx.reconnect_ev);
      event_free(pwsinkctx.reconnect_ev);
      pwsinkctx.reconnect_ev = NULL;
    }

  if (pwsinkctx.thread_loop)
    pw_thread_loop_stop(pwsinkctx.thread_loop);

  if (pwsinkctx.metadata_proxy)
    {
      spa_hook_remove(&pwsinkctx.metadata_listener);
      pw_proxy_destroy(pwsinkctx.metadata_proxy);
      pwsinkctx.metadata_proxy = NULL;
    }

  if (pwsinkctx.sink_proxy)
    {
      spa_hook_remove(&pwsinkctx.sink_node_listener);
      pw_proxy_destroy(pwsinkctx.sink_proxy);
      pwsinkctx.sink_proxy = NULL;
    }

  if (pwsinkctx.device_proxy)
    {
      spa_hook_remove(&pwsinkctx.device_listener);
      pw_proxy_destroy(pwsinkctx.device_proxy);
      pwsinkctx.device_proxy = NULL;
    }

  if (pwsinkctx.registry)
    {
      spa_hook_remove(&pwsinkctx.registry_listener);
      pw_proxy_destroy((struct pw_proxy *)pwsinkctx.registry);
      pwsinkctx.registry = NULL;
    }

  if (pwsinkctx.core)
    {
      spa_hook_remove(&pwsinkctx.core_listener);
      pw_core_disconnect(pwsinkctx.core);
      pwsinkctx.core = NULL;
    }

  if (pwsinkctx.context)
    {
      pw_context_destroy(pwsinkctx.context);
      pwsinkctx.context = NULL;
    }

  if (pwsinkctx.thread_loop)
    {
      pw_thread_loop_destroy(pwsinkctx.thread_loop);
      pwsinkctx.thread_loop = NULL;
    }

  pwsink_reset_state();
}

/*
 * Open (or reopen) a PipeWire stream for the given session and quality.
 * The stream is created with PW_ID_ANY so WirePlumber routes it to the
 * default sink; OwnTone does not select a target node for streaming
 * purposes (independent of, and not to be confused with, the sink resolved
 * for pwsink volume control on the separate pwsinkctx connection above --
 * the stream always follows whatever WirePlumber's routing policy decides,
 * even if sink_target pins volume control to a specific node).
 */
static int
stream_open(struct pipewire_session *ps, const struct media_quality *quality)
{
  uint8_t buf[1024];
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
  const struct spa_pod *params[1];
  struct pw_properties *props;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "Opening PipeWire stream (%d/%d/%d)\n",
    quality->sample_rate, quality->bits_per_sample, quality->channels);

  pw_thread_loop_lock(pwctx.thread_loop);

  props = pw_properties_new(
    PW_KEY_MEDIA_TYPE,     "Audio",
    PW_KEY_MEDIA_CATEGORY, "Playback",
    PW_KEY_MEDIA_ROLE,     "Music",
    PW_KEY_APP_NAME,       PACKAGE_NAME,
    NULL);

  if (!props)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not allocate stream properties\n");
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  /*
   * Do NOT set PW_KEY_NODE_LATENCY here.  outputs_buffer_duration_ms_get()
   * returns OwnTone's internal scheduling lookahead (~2250ms), which is not
   * a meaningful PipeWire hardware quantum.  Setting it caused PipeWire to
   * request 99225-sample buffers while OwnTone delivers 441-sample chunks,
   * so on_process only fired once per ~225 writes -- producing short bursts.
   * Let PipeWire negotiate its own quantum with the graph (typically 1024
   * samples at the graph rate, ~21ms), which matches OwnTone's delivery cadence.
   */

  ps->stream = pw_stream_new(pwctx.core, PACKAGE_NAME " audio", props);
  if (!ps->stream)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not create stream\n");
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  pw_stream_add_listener(ps->stream, &ps->stream_listener, &stream_events, ps);

  params[0] = build_format_param(&b, quality);

  /*
   * Connect with PW_ID_ANY -- no explicit target node.  WirePlumber will
   * link this stream to the session-manager's default audio sink
   * automatically.
   */
  ret = pw_stream_connect(ps->stream,
    PW_DIRECTION_OUTPUT,
    PW_ID_ANY,
    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS,
    params, 1);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not connect stream: %s\n",
        spa_strerror(ret));
      pw_stream_destroy(ps->stream);
      ps->stream = NULL;
      pw_thread_loop_unlock(pwctx.thread_loop);
      return -1;
    }

  ps->quality = *quality;
  ps->state   = PW_STREAM_STATE_CONNECTING;

  /*
   * (Re)allocate the ring buffer sized for this quality. Done here, not in
   * pipewire_session_make(), since we don't know the real quality until the
   * first write/restart determines it.
   */
  {
    size_t bytes_per_sec = (size_t)quality->sample_rate
                          * (quality->bits_per_sample / 8)
                          * quality->channels;
    size_t want_capacity = (bytes_per_sec * PIPEWIRE_RING_MS) / 1000;

    if (want_capacity != ps->ring_capacity)
      {
        uint8_t *newring = realloc(ps->ring, want_capacity);
        if (!newring)
          {
            DPRINTF(E_LOG, L_LAUDIO, "PipeWire: out of memory for ring buffer\n");
            pw_stream_destroy(ps->stream);
            ps->stream = NULL;
            pw_thread_loop_unlock(pwctx.thread_loop);
            return -1;
          }
        ps->ring          = newring;
        ps->ring_capacity = want_capacity;
      }
    ps->ring_head = 0;
    ps->ring_tail = 0;
    ps->ring_fill = 0;
  }

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

  /* Discard any buffered audio; keep the allocation for reuse on reopen */
  ps->ring_head = 0;
  ps->ring_tail = 0;
  ps->ring_fill = 0;

  pw_thread_loop_unlock(pwctx.thread_loop);
}

static void
playback_restart(struct pipewire_session *ps, struct output_buffer *obuf)
{
  int ret;

  stream_close(ps);

  ps->quality = obuf->data[0].quality;
  ret = stream_open(ps, &ps->quality);
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_LAUDIO,
        "PipeWire: input quality (%d/%d/%d) not supported, falling back\n",
        ps->quality.sample_rate, ps->quality.bits_per_sample, ps->quality.channels);

      ps->quality = pipewire_fallback_quality;
      ret = stream_open(ps, &ps->quality);
      if (ret < 0)
        {
          DPRINTF(E_LOG, L_LAUDIO, "PipeWire device failed on fallback quality\n");
          ps->state = PW_STREAM_STATE_ERROR;
          pipewire_session_shutdown(ps);
          return;
        }
    }
}

/*
 * Push a chunk of audio into the session's ring buffer (FIFO, byte-oriented).
 * Called from the player thread. If the ring doesn't have room, we drop the
 * OLDEST buffered bytes to make room -- on_process() is a live real-time
 * sink, so we must never block here, and dropping old samples (a brief skip)
 * is far less audible than dropping new ones (which would desync timing).
 */
static void
ring_push(struct pipewire_session *ps, const uint8_t *src, size_t len)
{
  size_t free_space;
  size_t drop;
  size_t first_chunk;

  if (len > ps->ring_capacity)
    {
      /* Single chunk bigger than the whole ring: keep only the tail end */
      src += (len - ps->ring_capacity);
      len  = ps->ring_capacity;
    }

  free_space = ps->ring_capacity - ps->ring_fill;
  if (len > free_space)
    {
      drop = len - free_space;
      ps->ring_tail  = (ps->ring_tail + drop) % ps->ring_capacity;
      ps->ring_fill -= drop;

      if (ps->logcount < PIPEWIRE_LOG_MAX)
        {
          ps->logcount++;
          DPRINTF(E_DBG, L_LAUDIO, "PipeWire: ring buffer full, dropped %zu bytes (%d/%d)\n",
            drop, ps->logcount, PIPEWIRE_LOG_MAX);
        }
    }

  first_chunk = ps->ring_capacity - ps->ring_head;
  if (first_chunk > len)
    first_chunk = len;

  memcpy(ps->ring + ps->ring_head, src, first_chunk);
  if (len > first_chunk)
    memcpy(ps->ring, src + first_chunk, len - first_chunk);

  ps->ring_head  = (ps->ring_head + len) % ps->ring_capacity;
  ps->ring_fill += len;
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

  if (!ps->ring)
    return; /* stream not open yet */

  /*
   * Take the loop lock before touching the ring: on_process() runs with this
   * lock already held, so this serialises the two sides of the handoff.
   */
  pw_thread_loop_lock(pwctx.thread_loop);
  ring_push(ps, obuf->data[i].buffer, obuf->data[i].bufsize);
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

/*
 * outputs_device_start() refuses to start a device if device_probe is NULL,
 * so we must provide one.  Since the PipeWire core connection was already
 * verified in pipewire_init(), a probe is trivially successful: make a
 * temporary session, report STOPPED (= probe ok, not streaming), and clean up.
 */
static int
pipewire_device_probe(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps;

  ps = pipewire_session_make(device, callback_id);
  if (!ps)
    return -1;

  ps->state       = PW_STREAM_STATE_UNCONNECTED; /* maps to OUTPUT_STATE_STOPPED */
  ps->callback_id = callback_id;

  pipewire_session_shutdown(ps);

  return 1;
}

static int
pipewire_device_start(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire starting\n");

  ps = pipewire_session_make(device, callback_id);
  if (!ps)
    return -1;

  /*
   * The stream is not opened until the first write (playback_restart).
   * Report CONNECTED / startup so the player can proceed.
   */
  pipewire_status(ps);

  return 1;
}

static int
pipewire_device_stop(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire stopping\n");

  ps->callback_id = callback_id;

  stream_close(ps);
  pipewire_session_shutdown(ps);

  return 1;
}

static int
pipewire_device_flush(struct output_device *device, int callback_id)
{
  struct pipewire_session *ps = device->session;

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire flush\n");

  ps->callback_id = callback_id;

  if (!ps->stream)
    {
      pipewire_status(ps);
      return 1;
    }

  pw_thread_loop_lock(pwctx.thread_loop);

  /* Pause and discard any buffered audio */
  pw_stream_set_active(ps->stream, false);
  pw_stream_flush(ps->stream, false);
  ps->ring_head = 0;
  ps->ring_tail = 0;
  ps->ring_fill = 0;

  pw_thread_loop_unlock(pwctx.thread_loop);

  pipewire_status(ps);

  return 1;
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
  float vol;
  int ret;

  if (!ps)
    return 0;

  ps->callback_id = callback_id;

  switch (pipewire_mixer_mode)
    {
      case PIPEWIRE_MIXER_PWSINK:
        vol = pct_to_volume(device->volume, pipewire_volume_curve);

        pw_thread_loop_lock(pwsinkctx.thread_loop);
        ret = pwsink_set_volume(vol);
        pw_thread_loop_unlock(pwsinkctx.thread_loop);

        if (ret < 0)
          DPRINTF(E_LOG, L_LAUDIO,
            "PipeWire: failed to set sink volume (sink not yet resolved?)\n");
        break;

      case PIPEWIRE_MIXER_PWSTREAM:
        /*
         * Drive our own stream's software gain directly via
         * SPA_PROP_channelVolumes. Remember the value so it can be
         * re-applied if the stream reconnects (see on_stream_state_changed,
         * PW_STREAM_STATE_PAUSED) -- otherwise a reconnect would silently
         * drop back to whatever volume a brand new pw_stream defaults to.
         *
         * pwstream mode intentionally always uses a plain linear curve:
         * pipewire_volume_curve only applies to pwsink mode, where matching
         * wpctl's own perceptual curve is what makes the number in
         * OwnTone's UI agree with `wpctl get-volume`. A plain software
         * gain stage on our own stream has no such external reference
         * point to match.
         */
        vol = pct_to_volume(device->volume, PIPEWIRE_CURVE_LINEAR);

        DPRINTF(E_DBG, L_LAUDIO, "PipeWire setting stream volume to %d\n", device->volume);

        ps->stream_volume = vol;

        if (ps->stream)
          {
            uint8_t buf[256];
            struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
            const struct spa_pod *param = build_stream_volume_param(&b, vol,
                                            (uint32_t)ps->quality.channels);

            pw_thread_loop_lock(pwctx.thread_loop);
            pw_stream_set_param(ps->stream, SPA_PARAM_Props, param);
            pw_thread_loop_unlock(pwctx.thread_loop);
          }
        break;

      default:
        /*
         * No "pipewire_mixer" setting configured: no PipeWire-specific volume
         * handling at all. Leave whatever OwnTone's own upstream default
         * volume behaviour is; just acknowledge the callback so the
         * player doesn't hang.
         */
        DPRINTF(E_DBG, L_LAUDIO,
          "PipeWire: mixer not configured ('pwsink'/'pwstream'), ignoring volume change\n");
        break;
    }

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

/*
 * Open pwsinkctx's own PipeWire connection and, if possible, resolve the
 * target sink/device/route before returning. Called once from
 * pipewire_init() when pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK; a
 * no-op call site in any other mode (pwsinkctx stays entirely unused).
 *
 * Failure here is logged but not fatal to the output as a whole: streaming
 * still works via pwctx regardless of whether pwsink resolved successfully,
 * volume_set() will just report failure until/unless resolution completes
 * in the background.
 */
static int
pwsink_init(void)
{
  int ret;

  pwsinkctx.thread_loop = pw_thread_loop_new("pipewire-pwsink", NULL);
  if (!pwsinkctx.thread_loop)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire pwsink thread loop\n");
      return -1;
    }

  /*
   * Create the libevent timer used by pwsink_on_core_error() to schedule a
   * reconnect after sleep/wake events. Fires on evbase_player, same as the
   * streaming reconnect timer, so it's safe to call into pwsinkctx's thread
   * loop from the callback. EV_PERSIST is NOT set -- one-shot per attempt;
   * pwsink_core_reconnect() re-adds the event itself if it fails.
   */
  pwsinkctx.reconnect_ev = event_new(evbase_player, -1, 0,
                                     pwsink_reconnect_cb, NULL);
  if (!pwsinkctx.reconnect_ev)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire pwsink reconnect timer\n");
      pwsink_free();
      return -1;
    }
  pwsinkctx.reconnect_pending = false;

  struct pw_properties *ctx_props;

  ctx_props = pw_properties_new(PW_KEY_APP_NAME, "owntone-pwsink-mixer", NULL);
  if (!ctx_props)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire could not allocate pwsink context properties\n");
      pwsink_free();
      return -1;
    }

  pwsinkctx.context = pw_context_new(pw_thread_loop_get_loop(pwsinkctx.thread_loop), ctx_props, 0);
  if (!pwsinkctx.context)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire pwsink context\n");
      pwsink_free();
      return -1;
    }

  ret = pw_thread_loop_start(pwsinkctx.thread_loop);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not start PipeWire pwsink thread loop: %s\n", spa_strerror(ret));
      pwsink_free();
      return -1;
    }

  pw_thread_loop_lock(pwsinkctx.thread_loop);

  pwsinkctx.core = pw_context_connect(pwsinkctx.context, NULL, 0);
  if (!pwsinkctx.core)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not connect PipeWire pwsink: %s\n", strerror(errno));
      pw_thread_loop_unlock(pwsinkctx.thread_loop);
      pwsink_free();
      return -1;
    }

  pw_core_add_listener(pwsinkctx.core, &pwsinkctx.core_listener, &pwsink_core_events, NULL);

  pwsinkctx.sink_global_id   = SPA_ID_INVALID;
  pwsinkctx.device_global_id = SPA_ID_INVALID;
  pwsinkctx.cached_volume_pct = -1;
  pwsinkctx.has_hw_route = true;

  /*
   * Subscribe to the registry so we can discover the target Audio/Sink
   * node (and its owning Device, for Route-level volume) plus the
   * WirePlumber "default" metadata object. Registry events arrive
   * asynchronously; pwsink_wait_ready() below gives resolution a bounded
   * budget to complete before we return, but this is best-effort --
   * resolution keeps running in the background via the registry/metadata
   * listeners either way, and the first volume_set() call (which happens
   * well after init, only once the user changes volume) will simply
   * report failure if it's still pending.
   */
  pwsinkctx.registry = pw_core_get_registry(pwsinkctx.core, PW_VERSION_REGISTRY, 0);
  if (!pwsinkctx.registry)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get PipeWire pwsink registry\n");
      pw_thread_loop_unlock(pwsinkctx.thread_loop);
      pwsink_free();
      return -1;
    }

  pw_registry_add_listener(pwsinkctx.registry, &pwsinkctx.registry_listener,
                           &registry_events, NULL);

  /* If a target override is configured, registry globals can match it
   * directly without needing to wait on "default.audio.sink" metadata
   * at all. */
  if (pwsinkctx.configured_target[0])
    pwsink_maybe_bind();

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire: pwsink registry active, watching for target Audio/Sink\n");

  /* Sync: wait for the initial core round-trip to complete. */
  pw_core_sync(pwsinkctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwsinkctx.thread_loop);

  /*
   * Give resolution of the target sink/device/route a bounded budget of
   * additional round-trips here, so pwsinkctx.sink_proxy is likely already
   * valid by the time the first volume_set() call arrives (well after
   * init) rather than only becoming ready some indeterminate time later in
   * the background.
   */
  if (!pwsink_wait_ready())
    {
      if (pwsinkctx.core_error != 0)
        DPRINTF(E_LOG, L_LAUDIO,
          "PipeWire: core error while resolving sink volume target: %s\n",
          strerror(-pwsinkctx.core_error));
      else
        DPRINTF(E_WARN, L_LAUDIO,
          "PipeWire: timed out resolving sink volume target '%s' at startup "
          "-- will keep trying in the background\n",
          pwsinkctx.configured_target[0] ? pwsinkctx.configured_target : "(default)");
      /* Not fatal: registry/metadata listeners remain active and may still
       * resolve the target later; volume_set() just reports failure until
       * then. */
    }

  pw_thread_loop_unlock(pwsinkctx.thread_loop);

  return 0;
}

static int
pipewire_init(void)
{
  struct output_device *device;
  cfg_t *cfg_audio;
  char *type;
  char *server;
  char *nickname;
  char *mixer;
  char *target;
  char *curve;
  int offset_ms;
  int ret;

  cfg_audio = cfg_getsec(cfg, "audio");
  if (!cfg_audio)
    return -1;

  type = cfg_getstr(cfg_audio, "type");
  if (!type || strcasecmp(type, "pipewire") != 0)
    return -1;

  server = cfg_getstr(cfg_audio, "server");

  mixer = cfg_getstr(cfg_audio, "mixer");
  if (!mixer || strcasecmp(mixer, "stream") == 0)
    pipewire_mixer_mode = PIPEWIRE_MIXER_PWSTREAM;
  else if (strcasecmp(mixer, "sink") == 0)
    pipewire_mixer_mode = PIPEWIRE_MIXER_PWSINK;
  else
    {
      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: unrecognized 'mixer' value '%s' (expected 'stream' or 'sink'), defaulting to 'stream'\n",
        mixer);
      pipewire_mixer_mode = PIPEWIRE_MIXER_PWSTREAM;
    }

  /*
   * sink_target: literal node.name to pin sink-volume control to,
   * bypassing "default.audio.sink" resolution entirely. Unset/"default"
   * means follow the system default sink. Only meaningful in pwsink mode.
   */
  pwsinkctx.configured_target[0] = '\0';
  target = cfg_getstr(cfg_audio, "sink_target");
  if (target && target[0] && strcasecmp(target, "default") != 0)
    snprintf(pwsinkctx.configured_target, sizeof(pwsinkctx.configured_target), "%s", target);

  /*
   * sink_volume_curve: "cubic" (default, matches wpctl/WirePlumber's own
   * perceptual scale) or "linear". Only meaningful in pwsink mode.
   */
  curve = cfg_getstr(cfg_audio, "sink_volume_curve");
  if (!curve || strcasecmp(curve, "cubic") == 0)
    pipewire_volume_curve = PIPEWIRE_CURVE_CUBIC;
  else if (strcasecmp(curve, "linear") == 0)
    pipewire_volume_curve = PIPEWIRE_CURVE_LINEAR;
  else
    {
      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: unrecognized sink_volume_curve '%s' (expected 'cubic' or 'linear'), using 'cubic'\n",
        curve);
      pipewire_volume_curve = PIPEWIRE_CURVE_CUBIC;
    }

  DPRINTF(E_LOG, L_LAUDIO,
    "PipeWire: volume control mode is '%s'%s%s, curve is '%s'\n",
    (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK) ? "sink" : "stream",
    pwsinkctx.configured_target[0] ? ", target=" : "",
    pwsinkctx.configured_target[0] ? pwsinkctx.configured_target : "",
    (pipewire_volume_curve == PIPEWIRE_CURVE_CUBIC) ? "cubic" : "linear");

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

  /*
   * Create the libevent timer used by on_core_error to schedule a reconnect
   * after sleep/wake events.  The timer fires on evbase_player (the player
   * thread's event loop) so pipewire_core_reconnect() runs on the same thread
   * as commands_base, avoiding any concurrency issues with the player.
   * EV_PERSIST is NOT set -- we want a one-shot timer per reconnect attempt;
   * if reconnect fails, pipewire_core_reconnect() re-adds the event itself.
   */
  pwctx.reconnect_ev = event_new(evbase_player, -1, 0,
                                  pipewire_reconnect_cb, NULL);
  if (!pwctx.reconnect_ev)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not create PipeWire reconnect timer\n");
      goto fail;
    }
  pwctx.reconnect_pending = false;

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

  /* Sync: wait for the initial core round-trip to complete. */
  pwctx.core_seq = pw_core_sync(pwctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwctx.thread_loop);

  pw_thread_loop_unlock(pwctx.thread_loop);

  /*
   * In pwsink volume mode, bring up pwsinkctx's own independent connection
   * for sink-volume control. Not fatal if this fails -- streaming via pwctx
   * above is unaffected either way; only volume_set() calls are impacted.
   */
  if (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK && pwsink_init() < 0)
    DPRINTF(E_LOG, L_LAUDIO,
      "PipeWire: pwsink connection failed to initialise -- volume control "
      "will not work until this is resolved\n");

  /*
   * Register the single PipeWire output device.  WirePlumber will route our
   * stream to the default sink; OwnTone does not enumerate sinks itself for
   * streaming (independent of pwsink volume-control resolution above).
   */
  nickname = cfg_getstr(cfg_audio, "nickname");
  if (!nickname || nickname[0] == '\0')
    nickname = "PipeWire";

  offset_ms = cfg_getint(cfg_audio, "offset_ms");
  if (abs(offset_ms) > 1000)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire offset_ms (%d) is out of bounds (-1000 -> 1000)\n", offset_ms);
      offset_ms = 0;
    }

  CHECK_NULL(L_LAUDIO, device = calloc(1, sizeof(struct output_device)));

  device->id               = 1; /* Fixed ID for the single PipeWire device */
  device->name             = strdup(nickname);
  device->type             = OUTPUT_TYPE_PIPEWIRE;
  device->type_name        = outputs_name(OUTPUT_TYPE_PIPEWIRE);
  device->supported_formats = MEDIA_FORMAT_PCM;
  device->offset_ms        = offset_ms;
  if (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK)
    {
      int pct = pwsink_get_volume_pct();
      if (pct >= 0)
        {
          device->volume = pct;
          device->volume_is_external = 1;
        }
      else
        DPRINTF(E_WARN, L_LAUDIO,
          "PipeWire: pwsink volume not resolved at startup, using stored/default volume instead\n");
    }
  player_device_add(device);

  DPRINTF(E_LOG, L_LAUDIO, "PipeWire output initialised\n");

  return 0;

 fail:
  pipewire_free();
  return -1;
}

static void
pipewire_deinit(void)
{
  pipewire_free();
  pwsink_free();
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
  .device_cb_set    = pipewire_device_cb_set,
  .device_volume_set = pipewire_device_volume_set,
  .write            = pipewire_write,
};
