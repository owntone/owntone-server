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
 * OwnTone registers a single "PipeWire" output device and opens a pw_stream
 * with PW_ID_ANY (no explicit target node).  WirePlumber is responsible for
 * routing that stream to whichever sink the user has configured as default.
 * OwnTone never enumerates PipeWire sinks; device selection is entirely
 * delegated to the WirePlumber session manager.
 *
 * All PipeWire interaction happens inside the pw_thread_loop thread (the
 * "PW thread").  The OwnTone player thread calls into this module through the
 * public interface functions (pipewire_device_start, etc.).  Those functions
 * lock the PW thread loop before touching any PW objects, exactly as is done
 * with pa_threaded_mainloop_lock() in the PulseAudio module.
 *
 * Streams
 * -------
 * Each active session owns one pw_stream.  Audio data is pushed from the
 * player thread via pipewire_write() → playback_write(), which queues a
 * buffer pointer in a per-session pending slot.  The PW process callback
 * (on_process) dequeues that buffer and feeds it to PipeWire, preventing
 * any blocking on the real-time thread.
 *
 * Volume
 * ------
 * OwnTone does NOT apply software gain on the stream. Volume changes from
 * the player are forwarded to WirePlumber by invoking `wpctl set-volume` on
 * the default audio sink. WirePlumber/PipeWire then apply that volume at the
 * sink level, which uses a hardware mixer control when the ALSA device
 * exposes one (e.g. the IQaudIO DAC's "Digital" control) and falls back to
 * software attenuation otherwise. This keeps a single source of truth for
 * volume shared across every PipeWire client (OwnTone, MPD, etc.), rather
 * than each client applying its own independent gain stage.
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
#include <inttypes.h>
#include <math.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <event2/event.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "commands.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <spa/utils/result.h>

#define PIPEWIRE_LOG_MAX 10

/* Target ring buffer capacity, in milliseconds of audio at the stream's
 * configured quality. Needs to comfortably exceed one PipeWire period
 * (observed up to ~43ms @ 2048 samples/48kHz) plus margin for jitter in
 * the player thread's delivery timing. */
#define PIPEWIRE_RING_MS 250

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
};

static struct pipewire_ctx pwctx;

/*
 * Volume control mode, selected via the "mixer" key in the [audio] config
 * section:
 *
 *   mixer = "pwsink" -- drive the PipeWire/WirePlumber SINK volume via
 *     `wpctl set-volume`, shared system-wide with every other PipeWire
 *     client, using the ALSA hardware mixer when the sink's Route has one
 *     and falling back to PipeWire's own software volume otherwise. The
 *     stream itself is pinned to unity gain (1.0) whenever this mode is
 *     active, so there is exactly one gain stage in effect, at the sink.
 *
 *   mixer = "pwstream" -- drive OwnTone's own STREAM volume via
 *     SPA_PROP_channelVolumes. Always software, never hardware-backed, and
 *     not shared with other PipeWire clients -- but doesn't depend on the
 *     sink having a usable hardware volume route at all.
 *
 *   (unset / not "pwsink" or "pwstream") -- no PipeWire-specific volume
 *     handling of any kind: device_volume_set() is a no-op, and the stream
 *     is left at whatever PipeWire's own default is. This is the fallback
 *     when "mixer" isn't configured at all, so that OwnTone's own upstream
 *     default behaviour (whatever Espen decides that should be) is what
 *     happens absent an explicit opt-in to either PipeWire-aware mode.
 */
enum pipewire_mixer_mode
{
  PIPEWIRE_MIXER_DEFAULT,
  PIPEWIRE_MIXER_PWSINK,
  PIPEWIRE_MIXER_PWSTREAM,
};

static enum pipewire_mixer_mode pipewire_mixer_mode = PIPEWIRE_MIXER_DEFAULT;

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
   * explicit volume() call, exactly the "need to bump volume manually"
   * symptom seen with an equivalent gap in another PipeWire client's
   * volume handling. Defaults to 1.0 (unity) so a stream that's never had
   * an explicit volume command starts at full scale rather than silence.
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

/*
 * Set the system's default audio sink volume via `wpctl set-volume`.
 *
 * Rationale: a PipeWire stream's own Props/channelVolumes only attenuates
 * that one client's signal in software, before it ever reaches the sink.
 * That doesn't give us hardware mixer control, and it means every PipeWire
 * client (OwnTone, MPD, etc.) has its own independent gain stage instead of
 * sharing one system volume. Driving the sink volume through wpctl instead:
 *   - Uses WirePlumber's existing ALSA device session, which maps onto a
 *     hardware mixer control when the card exposes one (e.g. the IQaudIO
 *     DAC's "Digital" control), and falls back to software otherwise.
 *   - Is shared with any other PipeWire/ALSA client on the system, so
 *     volume set from OwnTone, MPD, or `wpctl` directly all agree.
 *
 * Forks and execs directly (no shell) so there's no quoting/injection risk
 * and @DEFAULT_AUDIO_SINK@ is passed to wpctl literally for it to resolve.
 * Tries /usr/bin/wpctl directly first since a systemd service user's $PATH
 * is often minimal; falls back to a $PATH search if that's not where it
 * lives on a given distro.
 * This call blocks the calling thread for the lifetime of the child process
 * (typically a few ms); it must not be called from PipeWire's RT thread.
 */
static int
wpctl_set_volume(float vol)
{
  char volstr[32];
  pid_t pid;
  int status;
  int ret;

  if (vol < 0.0f)
    vol = 0.0f;
  if (vol > 1.0f)
    vol = 1.0f;

  snprintf(volstr, sizeof(volstr), "%.4f", vol);

  pid = fork();
  if (pid < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire: fork() failed for wpctl set-volume: %s\n", strerror(errno));
      return -1;
    }

  if (pid == 0)
    {
      char *const argv[] = { (char *)"wpctl", (char *)"set-volume",
                              (char *)"@DEFAULT_AUDIO_SINK@", volstr, NULL };
      execv("/usr/bin/wpctl", argv);
      execvp("wpctl", argv);
      _exit(127);
    }

  /* Retry on EINTR; treat ECHILD as success -- OwnTone's libevent SIGCHLD
   * handler may have already reaped the child before we get here, which
   * means wpctl ran and exited (successfully, since it rarely fails once
   * found).  Log at DBG rather than LOG to avoid spurious noise. */
  do {
    ret = waitpid(pid, &status, 0);
  } while (ret < 0 && errno == EINTR);

  if (ret < 0)
    {
      if (errno == ECHILD)
        {
          DPRINTF(E_DBG, L_LAUDIO,
            "PipeWire: wpctl child already reaped by SIGCHLD handler (harmless)\n");
          return 0;
        }
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire: waitpid() failed for wpctl set-volume: %s\n", strerror(errno));
      return -1;
    }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire: wpctl set-volume exited abnormally (status=%d)\n",
        WEXITSTATUS(status));
      return -1;
    }

  return 0;
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

/*
 * Build a Props pod to set per-stream volume. Only used in "pwstream" mixer
 * mode (see pipewire_mixer_mode) -- in "pwsink" mode the stream is pinned to
 * unity gain and wpctl drives the sink instead; in the unset/default mode
 * neither this nor wpctl is ever called.
 */
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

  DPRINTF(E_DBG, L_LAUDIO, "PipeWire stream state: %s → %s%s%s\n",
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
         * volume regardless of what we'd set on a previous stream instance
         * for this same session (e.g. after playback_restart() on a
         * quality change).
         *
         *   - pwsink mode: pin the stream to unity (1.0) so wpctl's
         *     sink-level volume is the only gain stage in effect.
         *   - pwstream mode: re-apply the last volume OwnTone asked for,
         *     so a reconnect doesn't silently reset to full scale (or
         *     whatever PipeWire's default is) until the next explicit
         *     volume() call.
         *   - default mode: do nothing, leave PipeWire's own default in
         *     effect, matching "no PipeWire-specific volume handling".
         */
        if (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK ||
            pipewire_mixer_mode == PIPEWIRE_MIXER_PWSTREAM)
          {
            uint8_t buf[256];
            struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
            float vol = (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK) ? 1.0f : ps->stream_volume;
            const struct spa_pod *param = build_volume_param(&b, vol,
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
 * capacity and can be much larger than what's needed per cycle -- using
 * maxsize was the cause of severe silence-padding we saw in diagnostics).
 * If requested is 0 (some drivers don't set it), we fall back to maxsize.
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
 * The stream is created with PW_ID_ANY so WirePlumber routes it to the
 * default sink; OwnTone does not select a target node.
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
   * so on_process only fired once per ~225 writes — producing short bursts.
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
   * Connect with PW_ID_ANY — no explicit target node.  WirePlumber will link
   * this stream to the session-manager's default audio sink automatically.
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

  vol = (float)device->volume / 100.0f;
  if (vol < 0.0f)
    vol = 0.0f;
  if (vol > 1.0f)
    vol = 1.0f;

  switch (pipewire_mixer_mode)
    {
      case PIPEWIRE_MIXER_PWSINK:
        /*
         * device->volume is OwnTone's 0-100 scale. wpctl set-volume takes a
         * linear 0.0-1.0 fraction and WirePlumber/ALSA apply whatever curve
         * the hardware mixer uses; no cubic/log conversion needed here
         * since we're not doing the perceptual mapping ourselves -- the
         * sink's own volume control does. The stream itself stays pinned
         * to unity (asserted in on_stream_state_changed()), so this is the
         * only gain stage in effect.
         */
        DPRINTF(E_DBG, L_LAUDIO, "PipeWire setting sink volume to %d via wpctl\n", device->volume);

        ret = wpctl_set_volume(vol);
        if (ret < 0)
          DPRINTF(E_LOG, L_LAUDIO, "PipeWire: failed to set sink volume via wpctl\n");
          /* Still report status below so the player doesn't hang waiting
             for a callback */
        break;

      case PIPEWIRE_MIXER_PWSTREAM:
        /*
         * Drive our own stream's software gain directly via
         * SPA_PROP_channelVolumes. Remember the value so it can be
         * re-applied if the stream reconnects (see on_stream_state_changed,
         * PW_STREAM_STATE_PAUSED) -- otherwise a reconnect would silently
         * drop back to whatever volume a brand new pw_stream defaults to.
         */
        DPRINTF(E_DBG, L_LAUDIO, "PipeWire setting stream volume to %d\n", device->volume);

        ps->stream_volume = vol;

        if (ps->stream)
          {
            uint8_t buf[256];
            struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
            const struct spa_pod *param = build_volume_param(&b, vol,
                                            (uint32_t)ps->quality.channels);

            pw_thread_loop_lock(pwctx.thread_loop);
            pw_stream_set_param(ps->stream, SPA_PARAM_Props, param);
            pw_thread_loop_unlock(pwctx.thread_loop);
          }
        break;

      case PIPEWIRE_MIXER_DEFAULT:
      default:
        /*
         * No "mixer" setting configured: no PipeWire-specific volume
         * handling at all. Leave whatever OwnTone's own upstream default
         * volume behaviour is (this function simply doesn't touch
         * PipeWire in this mode); just acknowledge the callback so the
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

static int
pipewire_init(void)
{
  struct output_device *device;
  cfg_t *cfg_audio;
  char *type;
  char *server;
  char *nickname;
  char *mixer;
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
  if (!mixer)
    pipewire_mixer_mode = PIPEWIRE_MIXER_DEFAULT;
  else if (strcasecmp(mixer, "pwsink") == 0)
    pipewire_mixer_mode = PIPEWIRE_MIXER_PWSINK;
  else if (strcasecmp(mixer, "pwstream") == 0)
    pipewire_mixer_mode = PIPEWIRE_MIXER_PWSTREAM;
  else
    {
      DPRINTF(E_LOG, L_LAUDIO,
        "PipeWire: unrecognized mixer '%s' (expected 'pwsink' or 'pwstream'), using OwnTone's default volume behaviour\n",
        mixer);
      pipewire_mixer_mode = PIPEWIRE_MIXER_DEFAULT;
    }

  DPRINTF(E_INFO, L_LAUDIO, "PipeWire: volume control mode is '%s'\n",
    (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSINK)   ? "pwsink"   :
    (pipewire_mixer_mode == PIPEWIRE_MIXER_PWSTREAM) ? "pwstream" :
                                                        "default (none)");

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

  /*
   * Sync: wait for the initial core round-trip to complete before proceeding.
   * (No registry is used; this just confirms the connection is live.)
   */
  pwctx.core_seq = pw_core_sync(pwctx.core, PW_ID_CORE, 0);
  pw_thread_loop_wait(pwctx.thread_loop);

  pw_thread_loop_unlock(pwctx.thread_loop);

  /*
   * Register the single PipeWire output device.  WirePlumber will route our
   * stream to the default sink; OwnTone does not enumerate sinks itself.
   */
  nickname = cfg_getstr(cfg_audio, "nickname");
  if (!nickname || nickname[0] == '\0')
    nickname = "PipeWire";

  offset_ms = cfg_getint(cfg_audio, "offset_ms");
  if (abs(offset_ms) > 1000)
    {
      DPRINTF(E_LOG, L_LAUDIO, "PipeWire offset_ms (%d) is out of bounds (-1000 → 1000)\n", offset_ms);
      offset_ms = 0;
    }

  CHECK_NULL(L_LAUDIO, device = calloc(1, sizeof(struct output_device)));

  device->id               = 1; /* Fixed ID for the single PipeWire device */
  device->name             = strdup(nickname);
  device->type             = OUTPUT_TYPE_PIPEWIRE;
  device->type_name        = outputs_name(OUTPUT_TYPE_PIPEWIRE);
  device->supported_formats = MEDIA_FORMAT_PCM;
  device->offset_ms        = offset_ms;

  player_device_add(device);

  DPRINTF(E_LOG, L_LAUDIO, "PipeWire output initialised (WirePlumber manages sink selection)\n");

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
  .device_cb_set    = pipewire_device_cb_set,
  .device_volume_set = pipewire_device_volume_set,
  .write            = pipewire_write,
};
