/*
 * Copyright (C) 2015-2016 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
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
#include <asoundlib.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

#define PACKET_SIZE STOB(AIRTUNES_V2_PACKET_SAMPLES)
// The maximum number of samples that the output is allowed to get behind (or
// ahead) of the player position, before compensation is attempted
#define ALSA_MAX_LATENCY 352
// If latency is jumping up and down we don't do compensation since we probably
// wouldn't do a good job. This sets the maximum the latency is allowed to vary
// within the 10 seconds where we measure latency each second.
#define ALSA_MAX_LATENCY_VARIANCE 352

// TODO Unglobalise these and add support for multiple sound cards
static char *card_name;
static char *mixer_name;
static char *mixer_device_name;
static snd_pcm_t *hdl;
static snd_mixer_t *mixer_hdl;
static snd_mixer_elem_t *vol_elem;
static long vol_min;
static long vol_max;
static int offset;
static int adjust_period_seconds;

#define ALSA_F_STARTED  (1 << 15)

enum alsa_state
{
  ALSA_STATE_FAILED    = 0,
  ALSA_STATE_STOPPED   = 1,
  ALSA_STATE_STARTED   = ALSA_F_STARTED,
  ALSA_STATE_STREAMING = ALSA_F_STARTED | 0x01,
};

enum alsa_sync_state
{
  ALSA_SYNC_OK,
  ALSA_SYNC_AHEAD,
  ALSA_SYNC_BEHIND,
};

struct alsa_session
{
  enum alsa_state state;

  char *devname;

  uint64_t pos;
  uint64_t start_pos;

  int32_t last_latency;
  int sync_counter;
  unsigned source_sample_rate;  // raw input audio sample rate in Hz
  unsigned target_sample_rate;  // output rate in Hz to configure ALSA device

  // An array that will hold the packets we prebuffer. The length of the array
  // is prebuf_len (measured in rtp_packets)
  uint8_t *prebuf;
  uint32_t prebuf_len;
  uint32_t prebuf_head;
  uint32_t prebuf_tail;

  int volume;

  struct event *deferredev;
  output_status_cb defer_cb;

  /* Do not dereference - only passed to the status cb */
  struct output_device *device;
  struct output_session *output_session;
  output_status_cb status_cb;

  struct alsa_session *next;
};

/* From player.c */
extern struct event_base *evbase_player;

static struct alsa_session *sessions;

/* Forwards */
static void
defer_cb(int fd, short what, void *arg);

/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
prebuf_free(struct alsa_session *as)
{
  if (as->prebuf)
    free(as->prebuf);

  as->prebuf = NULL;
  as->prebuf_len = 0;
  as->prebuf_head = 0;
  as->prebuf_tail = 0;
}

static void
alsa_session_free(struct alsa_session *as)
{
  if (!as)
    return;

  if (as->deferredev)
    event_free(as->deferredev);

  prebuf_free(as);

  free(as->output_session);
  free(as);
}

static void
alsa_session_cleanup(struct alsa_session *as)
{
  struct alsa_session *s;

  if (as == sessions)
    sessions = sessions->next;
  else
    {
      for (s = sessions; s && (s->next != as); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_LAUDIO, "WARNING: struct alsa_session not found in list; BUG!\n");
      else
	s->next = as->next;
    }

  alsa_session_free(as);
}

static struct alsa_session *
alsa_session_make(struct output_device *device, output_status_cb cb)
{
  struct alsa_session *as;

  as = calloc(1, sizeof(struct alsa_session));
  if (!as)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for ALSA session (as)\n");
      return NULL;
    }

  as->output_session = calloc(1, sizeof(struct output_session));
  if (!as->output_session)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for ALSA session (output_session)\n");
      goto failure_cleanup;
    }
  as->output_session->session = as;
  as->output_session->type = device->type;

  as->deferredev = evtimer_new(evbase_player, defer_cb, as);
  if (!as->deferredev)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for ALSA deferred event\n");
      goto failure_cleanup;
    }

  as->state = ALSA_STATE_STOPPED;
  as->device = device;
  as->status_cb = cb;
  as->volume = device->volume;
  as->devname = card_name;
  as->source_sample_rate = 44100;
  as->target_sample_rate = 44100;   // TODO: make ALSA device sample rate configurable

  as->next = sessions;
  sessions = as;
  return as;

 failure_cleanup:
  alsa_session_free(as);
  return NULL;
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

// Maps our internal state to the generic output state and then makes a callback
// to the player to tell that state
static void
defer_cb(int fd, short what, void *arg)
{
  struct alsa_session *as = arg;
  enum output_device_state state;

  switch (as->state)
    {
      case ALSA_STATE_FAILED:
	state = OUTPUT_STATE_FAILED;
	break;
      case ALSA_STATE_STOPPED:
	state = OUTPUT_STATE_STOPPED;
	break;
      case ALSA_STATE_STARTED:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case ALSA_STATE_STREAMING:
	state = OUTPUT_STATE_STREAMING;
	break;
      default:
	DPRINTF(E_LOG, L_LAUDIO, "Bug! Unhandled state in alsa_status()\n");
	state = OUTPUT_STATE_FAILED;
    }

  if (as->defer_cb)
    as->defer_cb(as->device, as->output_session, state);

  if (!(as->state & ALSA_F_STARTED))
    alsa_session_cleanup(as);
}

// Note: alsa_states also nukes the session if it is not ALSA_F_STARTED
static void
alsa_status(struct alsa_session *as)
{
  as->defer_cb = as->status_cb;
  event_active(as->deferredev, 0, 0);
  as->status_cb = NULL;
}


/* ------------------------------- MISC HELPERS ----------------------------- */

/*static int
start_threshold_set(snd_pcm_uframes_t threshold)
{
  snd_pcm_sw_params_t *sw_params;
  int ret;

  ret = snd_pcm_sw_params_malloc(&sw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not allocate sw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_sw_params_current(hdl, sw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not retrieve current sw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_sw_params_set_start_threshold(hdl, sw_params, threshold);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set start threshold: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_sw_params(hdl, sw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set sw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  return 0;

 out_fail:
  snd_pcm_sw_params_free(sw_params);

  return -1;
}
*/

static int
mixer_open(void)
{
  snd_mixer_elem_t *elem;
  snd_mixer_elem_t *master;
  snd_mixer_elem_t *pcm;
  snd_mixer_elem_t *custom;
  snd_mixer_selem_id_t *sid;
  int ret;

  ret = snd_mixer_open(&mixer_hdl, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to open mixer: %s\n", snd_strerror(ret));

      mixer_hdl = NULL;
      return -1;
    }

  ret = snd_mixer_attach(mixer_hdl, mixer_device_name);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to attach mixer: %s\n", snd_strerror(ret));

      goto out_close;
    }

  ret = snd_mixer_selem_register(mixer_hdl, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to register mixer: %s\n", snd_strerror(ret));

      goto out_detach;
    }

  ret = snd_mixer_load(mixer_hdl);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to load mixer: %s\n", snd_strerror(ret));

      goto out_detach;
    }

  // Grab interesting elements
  snd_mixer_selem_id_alloca(&sid);

  pcm = NULL;
  master = NULL;
  custom = NULL;
  for (elem = snd_mixer_first_elem(mixer_hdl); elem; elem = snd_mixer_elem_next(elem))
    {
      snd_mixer_selem_get_id(elem, sid);

      if (mixer_name && (strcmp(snd_mixer_selem_id_get_name(sid), mixer_name) == 0))
	{
	  custom = elem;
	  break;
	}
      else if (strcmp(snd_mixer_selem_id_get_name(sid), "PCM") == 0)
        pcm = elem;
      else if (strcmp(snd_mixer_selem_id_get_name(sid), "Master") == 0)
	master = elem;
    }

  if (mixer_name)
    {
      if (custom)
	vol_elem = custom;
      else
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Failed to open configured mixer element '%s'\n", mixer_name);

	  goto out_detach;
	}
    }
  else if (pcm)
    vol_elem = pcm;
  else if (master)
    vol_elem = master;
  else
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to open PCM or Master mixer element\n");

      goto out_detach;
    }

  // Get min & max volume
  snd_mixer_selem_get_playback_volume_range(vol_elem, &vol_min, &vol_max);

  return 0;

 out_detach:
  snd_mixer_detach(mixer_hdl, card_name);
 out_close:
  snd_mixer_close(mixer_hdl);
  mixer_hdl = NULL;
  vol_elem = NULL;

  return -1;
}

static int
device_open(struct alsa_session *as)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_uframes_t bufsize;
  int ret;

  hw_params = NULL;

  ret = snd_pcm_open(&hdl, card_name, SND_PCM_STREAM_PLAYBACK, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not open playback device: %s\n", snd_strerror(ret));

      return -1;
    }

  // HW params
  ret = snd_pcm_hw_params_malloc(&hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not allocate hw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_any(hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not retrieve hw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_access(hdl, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set access method: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_format(hdl, hw_params, SND_PCM_FORMAT_S16_LE);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set S16LE format: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_channels(hdl, hw_params, 2);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set stereo output: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_rate(hdl, hw_params, as->target_sample_rate, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Hardware doesn't support %u Hz: %s\n", as->target_sample_rate, snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get max buffer size: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_buffer_size_max(hdl, hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set buffer size to max: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params(hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set hw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  snd_pcm_hw_params_free(hw_params);
  hw_params = NULL;

  ret = mixer_open();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not open mixer\n");

      goto out_fail;
    }

  return 0;

 out_fail:
  if (hw_params)
    snd_pcm_hw_params_free(hw_params);

  snd_pcm_close(hdl);
  hdl = NULL;

  return -1;
}

static void
device_close(void)
{
  snd_pcm_close(hdl);
  hdl = NULL;

  if (mixer_hdl)
    {
      snd_mixer_detach(mixer_hdl, card_name);
      snd_mixer_close(mixer_hdl);

      mixer_hdl = NULL;
      vol_elem = NULL;
    }
}

static void
playback_start(struct alsa_session *as, uint64_t pos, uint64_t start_pos)
{
  snd_output_t *output;
  snd_pcm_state_t state;
  char *debug_pcm_cfg;
  int ret;

  state = snd_pcm_state(hdl);
  if (state != SND_PCM_STATE_PREPARED)
    {
      if (state == SND_PCM_STATE_RUNNING)
	snd_pcm_drop(hdl);

      ret = snd_pcm_prepare(hdl);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Could not prepare ALSA device '%s' (state %d): %s\n", as->devname, state, snd_strerror(ret));
	  return;
	}
    }

  // Clear prebuffer in case start somehow got called twice without a stop in between
  prebuf_free(as);

  // Adjust the starting position with the configured value
  start_pos -= offset;

  // The difference between pos and start_pos should match the 2 second
  // buffer that AirPlay uses. We will not use alsa's buffer for the initial
  // buffering, because my sound card's start_threshold is not to be counted on.
  // Instead we allocate our own buffer, and when it is time to play we write as
  // much as we can to alsa's buffer.
  as->prebuf_len = (start_pos - pos) / AIRTUNES_V2_PACKET_SAMPLES + 1;
  if (as->prebuf_len > (3 * 44100 - offset) / AIRTUNES_V2_PACKET_SAMPLES)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Sanity check of prebuf_len (%" PRIu32 " packets) failed\n", as->prebuf_len);
      return;
    }
  DPRINTF(E_DBG, L_LAUDIO, "Will prebuffer %d packets\n", as->prebuf_len);

  as->prebuf = malloc(as->prebuf_len * PACKET_SIZE);
  if (!as->prebuf)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for audio buffer (requested %" PRIu32 " packets)\n", as->prebuf_len);
      return;
    }

  as->pos = pos;
  as->start_pos = start_pos - AIRTUNES_V2_PACKET_SAMPLES;

  // Dump PCM config data for E_DBG logging
  ret = snd_output_buffer_open(&output);
  if (ret == 0)
    {
      if (snd_pcm_dump_setup(hdl, output) == 0)
	{
	  snd_output_buffer_string(output, &debug_pcm_cfg);
	  DPRINTF(E_DBG, L_LAUDIO, "Dump of sound device config:\n%s\n", debug_pcm_cfg);
	}

      snd_output_close(output);
    }

  as->state = ALSA_STATE_STREAMING;
}


// This function writes the sample buf into either the prebuffer or directly to
// ALSA, depending on how much room there is in ALSA, and whether we are
// prebuffering or not. It also transfers from the the prebuffer to ALSA, if
// needed. Returns 0 on success, negative on error.
static int
buffer_write(struct alsa_session *as, uint8_t *buf, snd_pcm_sframes_t *avail, int prebuffering, int prebuf_empty)
{
  uint8_t *pkt;
  int npackets;
  snd_pcm_sframes_t nsamp;
  snd_pcm_sframes_t ret;

  nsamp = AIRTUNES_V2_PACKET_SAMPLES;

  if (prebuffering || !prebuf_empty || *avail < AIRTUNES_V2_PACKET_SAMPLES)
    {
      pkt = &as->prebuf[as->prebuf_head * PACKET_SIZE];

      memcpy(pkt, buf, PACKET_SIZE);

      as->prebuf_head = (as->prebuf_head + 1) % as->prebuf_len;

      if (prebuffering || *avail < AIRTUNES_V2_PACKET_SAMPLES)
	return 0; // No actual writing

      // We will now set buf so that we will transfer as much as possible to ALSA
      buf = &as->prebuf[as->prebuf_tail * PACKET_SIZE];

      if (as->prebuf_head > as->prebuf_tail)
	npackets = as->prebuf_head - as->prebuf_tail;
      else
	npackets = as->prebuf_len - as->prebuf_tail;

      nsamp = npackets * AIRTUNES_V2_PACKET_SAMPLES;
      while (nsamp > *avail)
	{
	  npackets -= 1;
	  nsamp -= AIRTUNES_V2_PACKET_SAMPLES;
	}

      as->prebuf_tail = (as->prebuf_tail + npackets) % as->prebuf_len;
    }

  ret = snd_pcm_writei(hdl, buf, nsamp);
  if (ret < 0)
    return ret;

  if (ret != nsamp)
    DPRINTF(E_WARN, L_LAUDIO, "ALSA partial write detected\n");

  if (avail)
    *avail -= ret;

  return 0;
}

// Checks if ALSA's playback position is ahead or behind the player's
enum alsa_sync_state
sync_check(struct alsa_session *as, uint64_t rtptime, snd_pcm_sframes_t delay, int prebuf_empty)
{
  enum alsa_sync_state sync;
  struct timespec now;
  uint64_t cur_pos;
  uint64_t pb_pos;
  int32_t latency;
  int npackets;

  sync = ALSA_SYNC_OK;

  if (player_get_current_pos(&cur_pos, &now, 0) != 0)
    return sync;

  if (!prebuf_empty)
    npackets = (as->prebuf_head - (as->prebuf_tail + 1) + as->prebuf_len) % as->prebuf_len + 1;
  else
    npackets = 0;

  pb_pos = rtptime - delay - AIRTUNES_V2_PACKET_SAMPLES * npackets;
  latency = cur_pos - (pb_pos - offset);

  // If the latency is low or very different from our last measurement, we reset the sync_counter
  if (abs(latency) < ALSA_MAX_LATENCY || abs(as->last_latency - latency) > ALSA_MAX_LATENCY_VARIANCE)
    {
      as->sync_counter = 0;
      sync = ALSA_SYNC_OK;
    }
  // If we have measured a consistent latency for configured period, then we take action
  else if (as->sync_counter >= adjust_period_seconds * 126)
    {
      DPRINTF(E_INFO, L_LAUDIO, "Taking action to compensate for ALSA latency of %d samples\n", latency);

      as->sync_counter = 0;
      if (latency > 0)
	sync = ALSA_SYNC_BEHIND;
      else
	sync = ALSA_SYNC_AHEAD;
    }

  as->last_latency = latency;

  if (latency)
    DPRINTF(E_SPAM, L_LAUDIO, "Sync %d cur_pos %" PRIu64 ", pb_pos %" PRIu64 " (diff %d, delay %li), pos %" PRIu64 "\n", sync, cur_pos, pb_pos, latency, delay, as->pos);

  return sync;
}

static void
playback_write(struct alsa_session *as, uint8_t *buf, uint64_t rtptime)
{
  snd_pcm_sframes_t ret;
  snd_pcm_sframes_t avail;
  snd_pcm_sframes_t delay;
  enum alsa_sync_state sync;
  int prebuffering;
  int prebuf_empty;

  prebuffering = (as->pos < as->start_pos);
  prebuf_empty = (as->prebuf_head == as->prebuf_tail);

  as->pos += AIRTUNES_V2_PACKET_SAMPLES;

  if (prebuffering)
    {
      buffer_write(as, buf, NULL, prebuffering, prebuf_empty);
      return;
    }

  ret = snd_pcm_avail_delay(hdl, &avail, &delay);
  if (ret < 0)
    goto alsa_error;

  // Every second we do a sync check
  sync = ALSA_SYNC_OK;
  as->sync_counter++;
  if (as->sync_counter % 126 == 0)
    sync = sync_check(as, rtptime, delay, prebuf_empty);

  // Skip write -> reduce the delay
  if (sync == ALSA_SYNC_BEHIND)
    return;

  ret = buffer_write(as, buf, &avail, prebuffering, prebuf_empty);
  // Double write -> increase the delay
  if (sync == ALSA_SYNC_AHEAD && (ret == 0))
    ret = buffer_write(as, buf, &avail, prebuffering, prebuf_empty);
  if (ret < 0)
    goto alsa_error;

  return;

 alsa_error:
  if (ret == -EPIPE)
    {
      DPRINTF(E_WARN, L_LAUDIO, "ALSA buffer underrun\n");

      ret = snd_pcm_prepare(hdl);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_LAUDIO, "ALSA couldn't recover from underrun: %s\n", snd_strerror(ret));
	  return;
	}

      // Fill the prebuf with audio before restarting, so we don't underrun again
      as->start_pos = as->pos + AIRTUNES_V2_PACKET_SAMPLES * (as->prebuf_len - 1);

      return;
    }

  DPRINTF(E_LOG, L_LAUDIO, "ALSA write error: %s\n", snd_strerror(ret));

  as->state = ALSA_STATE_FAILED;
  alsa_status(as);
}

static void
playback_pos_get(uint64_t *pos, uint64_t next_pkt)
{
  uint64_t cur_pos;
  struct timespec now;
  int ret;

  ret = player_get_current_pos(&cur_pos, &now, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get playback position, setting to next_pkt - 2 seconds\n");
      cur_pos = next_pkt - 88200;
    }

  // Make pos the rtptime of the packet containing cur_pos
  *pos = next_pkt;
  while (*pos > cur_pos)
    *pos -= AIRTUNES_V2_PACKET_SAMPLES;
}

/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
alsa_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  struct alsa_session *as;
  int ret;

  as = alsa_session_make(device, cb);
  if (!as)
    return -1;

  ret = device_open(as);
  if (ret < 0)
    {
      alsa_session_cleanup(as);
      return -1;
    }

  as->state = ALSA_STATE_STARTED;
  alsa_status(as);

  return 0;
}

static void
alsa_device_stop(struct output_session *session)
{
  struct alsa_session *as = session->session;

  device_close();

  as->state = ALSA_STATE_STOPPED;
  alsa_status(as);
}

static int
alsa_device_probe(struct output_device *device, output_status_cb cb)
{
  struct alsa_session *as;
  int ret;

  as = alsa_session_make(device, cb);
  if (!as)
    return -1;

  ret = device_open(as);
  if (ret < 0)
    {
      alsa_session_cleanup(as);
      return -1;
    }

  device_close();

  as->state = ALSA_STATE_STOPPED;
  alsa_status(as);

  return 0;
}

static int
alsa_device_volume_set(struct output_device *device, output_status_cb cb)
{
  struct alsa_session *as;
  int pcm_vol;

  if (!device->session || !device->session->session)
    return 0;

  as = device->session->session;

  if (!mixer_hdl || !vol_elem)
    return 0;

  snd_mixer_handle_events(mixer_hdl);

  if (!snd_mixer_selem_is_active(vol_elem))
    return 0;

  switch (device->volume)
    {
      case 0:
	pcm_vol = vol_min;
	break;

      case 100:
	pcm_vol = vol_max;
	break;

      default:
	pcm_vol = vol_min + (device->volume * (vol_max - vol_min)) / 100;
	break;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Setting ALSA volume to %d (%d)\n", pcm_vol, device->volume);

  snd_mixer_selem_set_playback_volume_all(vol_elem, pcm_vol);

  as->status_cb = cb;
  alsa_status(as);

  return 1;
}

static void
alsa_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  struct alsa_session *as;
  uint64_t pos;

  if (!sessions)
    return;

  playback_pos_get(&pos, next_pkt);

  DPRINTF(E_DBG, L_LAUDIO, "Starting ALSA audio (pos %" PRIu64 ", next_pkt %" PRIu64 ")\n", pos, next_pkt);

  for (as = sessions; as; as = as->next)
    playback_start(as, pos, next_pkt);
}

static void
alsa_playback_stop(void)
{
  struct alsa_session *as;

  for (as = sessions; as; as = as->next)
    {
      snd_pcm_drop(hdl);
      prebuf_free(as);

      as->state = ALSA_STATE_STARTED;
      alsa_status(as);
    }
}

static void
alsa_write(uint8_t *buf, uint64_t rtptime)
{
  struct alsa_session *as;
  uint64_t pos;

  for (as = sessions; as; as = as->next)
    {
      if (as->state == ALSA_STATE_STARTED)
	{
	  playback_pos_get(&pos, rtptime);

	  DPRINTF(E_DBG, L_LAUDIO, "Starting ALSA device '%s' (pos %" PRIu64 ", rtptime %" PRIu64 ")\n", as->devname, pos, rtptime);

	  playback_start(as, pos, rtptime);
	}

      playback_write(as, buf, rtptime);
    }
}

static int
alsa_flush(output_status_cb cb, uint64_t rtptime)
{
  struct alsa_session *as;
  int i;

  i = 0;
  for (as = sessions; as; as = as->next)
    {
      i++;

      snd_pcm_drop(hdl);
      prebuf_free(as);

      as->status_cb = cb;
      as->state = ALSA_STATE_STARTED;
      alsa_status(as);
    }

  return i;
}

static void
alsa_set_status_cb(struct output_session *session, output_status_cb cb)
{
  struct alsa_session *as = session->session;

  as->status_cb = cb;
}

static int
alsa_init(void)
{
  struct output_device *device;
  cfg_t *cfg_audio;
  char *nickname;
  char *type;
  int original_adjust;

  cfg_audio = cfg_getsec(cfg, "audio");
  type = cfg_getstr(cfg_audio, "type");

  if (type && (strcasecmp(type, "alsa") != 0))
    return -1;

  card_name = cfg_getstr(cfg_audio, "card");
  mixer_name = cfg_getstr(cfg_audio, "mixer");
  mixer_device_name = cfg_getstr(cfg_audio, "mixer_device");
  if (mixer_device_name == NULL || strlen(mixer_device_name) == 0)
    mixer_device_name = card_name;
  nickname = cfg_getstr(cfg_audio, "nickname");
  offset = cfg_getint(cfg_audio, "offset");
  if (abs(offset) > 44100)
    {
      DPRINTF(E_LOG, L_LAUDIO, "The ALSA offset (%d) set in the configuration is out of bounds\n", offset);
      offset = 44100 * (offset/abs(offset));
    }

  original_adjust = adjust_period_seconds = cfg_getint(cfg_audio, "adjust_period_seconds");
  if (adjust_period_seconds < 1)
    adjust_period_seconds = 1;
  else if (adjust_period_seconds > 20)
    adjust_period_seconds = 20;
  if (original_adjust != adjust_period_seconds)
    DPRINTF(E_LOG, L_LAUDIO, "Clamped ALSA adjust_period_seconds from %d to %d\n", original_adjust, adjust_period_seconds);

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for ALSA device\n");
      return -1;
    }

  device->id = 0;
  device->name = strdup(nickname);
  device->type = OUTPUT_TYPE_ALSA;
  device->type_name = outputs_name(device->type);
  device->advertised = 1;
  device->has_video = 0;

  DPRINTF(E_INFO, L_LAUDIO, "Adding ALSA device '%s' with name '%s'\n", card_name, nickname);

  player_device_add(device);

  snd_lib_error_set_handler(logger_alsa);

  hdl = NULL;
  mixer_hdl = NULL;
  vol_elem = NULL;

  return 0;
}

static void
alsa_deinit(void)
{
  snd_lib_error_set_handler(NULL);
}

struct output_definition output_alsa =
{
  .name = "ALSA",
  .type = OUTPUT_TYPE_ALSA,
  .priority = 3,
  .disabled = 0,
  .init = alsa_init,
  .deinit = alsa_deinit,
  .device_start = alsa_device_start,
  .device_stop = alsa_device_stop,
  .device_probe = alsa_device_probe,
  .device_volume_set = alsa_device_volume_set,
  .playback_start = alsa_playback_start,
  .playback_stop = alsa_playback_stop,
  .write = alsa_write,
  .flush = alsa_flush,
  .status_cb = alsa_set_status_cb,
};
