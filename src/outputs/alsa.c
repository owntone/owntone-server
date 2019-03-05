/*
 * Copyright (C) 2015-2019 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <asoundlib.h>

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

// The maximum number of samples that the output is allowed to get behind (or
// ahead) of the player position, before compensation is attempted
#define ALSA_MAX_LATENCY 480
// If latency is jumping up and down we don't do compensation since we probably
// wouldn't do a good job. This sets the maximum the latency is allowed to vary
// within the 10 seconds where we measure latency each second.
#define ALSA_MAX_LATENCY_VARIANCE 480

#define ALSA_F_STARTED  (1 << 15)

enum alsa_sync_state
{
  ALSA_SYNC_OK,
  ALSA_SYNC_AHEAD,
  ALSA_SYNC_BEHIND,
};

struct alsa_session
{
  enum output_device_state state;

  uint64_t device_id;
  int callback_id;

  const char *devname;
  const char *card_name;
  const char *mixer_name;
  const char *mixer_device_name;

  snd_pcm_status_t *pcm_status;

  struct media_quality quality;

  int buffer_nsamp;

  uint32_t pos;

  uint32_t last_pos;
  uint32_t last_buflen;

  struct timespec start_pts;
  struct timespec last_pts;

  int last_latency;
  int sync_counter;

  // Here we buffer samples during startup
  struct ringbuffer prebuf;

  int offset_ms;
  int adjust_period_seconds;

  int volume;
  long vol_min;
  long vol_max;

  snd_pcm_t *hdl;
  snd_mixer_t *mixer_hdl;
  snd_mixer_elem_t *vol_elem;

  struct alsa_session *next;
};

static struct alsa_session *sessions;

// We will try to play the music with the source quality, but if the card
// doesn't support that we resample to the fallback quality
static struct media_quality alsa_fallback_quality = { 44100, 16, 2 };
static struct media_quality alsa_last_quality;


/* -------------------------------- FORWARDS -------------------------------- */

static void
alsa_status(struct alsa_session *as);


/* ------------------------------- MISC HELPERS ----------------------------- */

static void
dump_config(struct alsa_session *as)
{
  snd_output_t *output;
  char *debug_pcm_cfg;
  int ret;

  // Dump PCM config data for E_DBG logging
  ret = snd_output_buffer_open(&output);
  if (ret == 0)
    {
      if (snd_pcm_dump_setup(as->hdl, output) == 0)
	{
	  snd_output_buffer_string(output, &debug_pcm_cfg);
	  DPRINTF(E_DBG, L_LAUDIO, "Dump of sound device config:\n%s\n", debug_pcm_cfg);
	}

      snd_output_close(output);
    }
}

static int
mixer_open(struct alsa_session *as)
{
  snd_mixer_elem_t *elem;
  snd_mixer_elem_t *master;
  snd_mixer_elem_t *pcm;
  snd_mixer_elem_t *custom;
  snd_mixer_selem_id_t *sid;
  int ret;

  ret = snd_mixer_open(&as->mixer_hdl, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to open mixer: %s\n", snd_strerror(ret));
      as->mixer_hdl = NULL;
      return -1;
    }

  ret = snd_mixer_attach(as->mixer_hdl, as->mixer_device_name);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to attach mixer: %s\n", snd_strerror(ret));
      goto out_close;
    }

  ret = snd_mixer_selem_register(as->mixer_hdl, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to register mixer: %s\n", snd_strerror(ret));
      goto out_detach;
    }

  ret = snd_mixer_load(as->mixer_hdl);
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
  for (elem = snd_mixer_first_elem(as->mixer_hdl); elem; elem = snd_mixer_elem_next(elem))
    {
      snd_mixer_selem_get_id(elem, sid);

      if (as->mixer_name && (strcmp(snd_mixer_selem_id_get_name(sid), as->mixer_name) == 0))
	{
	  custom = elem;
	  break;
	}
      else if (strcmp(snd_mixer_selem_id_get_name(sid), "PCM") == 0)
        pcm = elem;
      else if (strcmp(snd_mixer_selem_id_get_name(sid), "Master") == 0)
	master = elem;
    }

  if (as->mixer_name)
    {
      if (custom)
	as->vol_elem = custom;
      else
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Failed to open configured mixer element '%s'\n", as->mixer_name);

	  goto out_detach;
	}
    }
  else if (pcm)
    as->vol_elem = pcm;
  else if (master)
    as->vol_elem = master;
  else
    {
      DPRINTF(E_LOG, L_LAUDIO, "Failed to open PCM or Master mixer element\n");

      goto out_detach;
    }

  // Get min & max volume
  snd_mixer_selem_get_playback_volume_range(as->vol_elem, &as->vol_min, &as->vol_max);

  return 0;

 out_detach:
  snd_mixer_detach(as->mixer_hdl, as->devname);
 out_close:
  snd_mixer_close(as->mixer_hdl);
  as->mixer_hdl = NULL;
  as->vol_elem = NULL;

  return -1;
}

static int
device_open(struct alsa_session *as)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_uframes_t bufsize;
  int ret;

  hw_params = NULL;

  ret = snd_pcm_open(&as->hdl, as->devname, SND_PCM_STREAM_PLAYBACK, 0);
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

  ret = snd_pcm_hw_params_any(as->hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not retrieve hw params: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_access(as->hdl, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set access method: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get max buffer size: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_hw_params_set_buffer_size_max(as->hdl, hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set buffer size to max: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_hw_params(as->hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set hw params: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  snd_pcm_hw_params_free(hw_params);
  hw_params = NULL;

  ret = mixer_open(as);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not open mixer\n");
      goto out_fail;
    }

  return 0;

 out_fail:
  if (hw_params)
    snd_pcm_hw_params_free(hw_params);

  snd_pcm_close(as->hdl);
  as->hdl = NULL;

  return -1;
}

static int
device_quality_set(struct alsa_session *as, struct media_quality *quality, char **errmsg)
{
  snd_pcm_hw_params_t *hw_params;
  snd_pcm_format_t format;
  int ret;

  ret = snd_pcm_hw_params_malloc(&hw_params);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Could not allocate hw params: %s", snd_strerror(ret));
      return -1;
    }

  ret = snd_pcm_hw_params_any(as->hdl, hw_params);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Could not retrieve hw params: %s", snd_strerror(ret));
      goto free_params;
    }

  ret = snd_pcm_hw_params_set_rate(as->hdl, hw_params, quality->sample_rate, 0);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Hardware doesn't support %d Hz: %s", quality->sample_rate, snd_strerror(ret));
      goto free_params;
    }

  switch (quality->bits_per_sample)
    {
      case 16:
	format = SND_PCM_FORMAT_S16_LE;
	break;
      case 24:
	format = SND_PCM_FORMAT_S24_LE;
	break;
      case 32:
	format = SND_PCM_FORMAT_S32_LE;
	break;
      default:
	*errmsg = safe_asprintf("Unrecognized number of bits per sample: %d", quality->bits_per_sample);
	goto free_params;
    }

  ret = snd_pcm_hw_params_set_format(as->hdl, hw_params, format);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Could not set %d bits per sample: %s", quality->bits_per_sample, snd_strerror(ret));
      goto free_params;
    }

  ret = snd_pcm_hw_params_set_channels(as->hdl, hw_params, quality->channels);
  if (ret < 0)
    {
      *errmsg = safe_asprintf("Could not set channel number (%d): %s", quality->channels, snd_strerror(ret));
      goto free_params;
    }

  ret = snd_pcm_hw_params(as->hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set hw params: %s\n", snd_strerror(ret));
      goto free_params;
    }

  snd_pcm_hw_params_free(hw_params);
  return 0;

 free_params:
  snd_pcm_hw_params_free(hw_params);
  return -1;
}

static int
device_configure(struct alsa_session *as)
{
  snd_pcm_sw_params_t *sw_params;
  int ret;

  ret = snd_pcm_sw_params_malloc(&sw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not allocate sw params: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_sw_params_current(as->hdl, sw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not retrieve current sw params: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_sw_params_set_tstamp_type(as->hdl, sw_params, SND_PCM_TSTAMP_TYPE_MONOTONIC);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set tstamp type: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_sw_params_set_tstamp_mode(as->hdl, sw_params, SND_PCM_TSTAMP_ENABLE);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set tstamp mode: %s\n", snd_strerror(ret));
      goto out_fail;
    }

  ret = snd_pcm_sw_params(as->hdl, sw_params);
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

static void
device_close(struct alsa_session *as)
{
  snd_pcm_close(as->hdl);
  as->hdl = NULL;

  if (as->mixer_hdl)
    {
      snd_mixer_detach(as->mixer_hdl, as->devname);
      snd_mixer_close(as->mixer_hdl);

      as->mixer_hdl = NULL;
      as->vol_elem = NULL;
    }
}

static void
playback_restart(struct alsa_session *as, struct output_buffer *obuf)
{
  struct timespec ts;
  snd_pcm_state_t state;
  snd_pcm_sframes_t offset_nsamp;
  size_t size;
  char *errmsg;
  int ret;

  DPRINTF(E_INFO, L_LAUDIO, "Starting ALSA device '%s'\n", as->devname);

  state = snd_pcm_state(as->hdl);
  if (state != SND_PCM_STATE_PREPARED)
    {
      if (state == SND_PCM_STATE_RUNNING)
	snd_pcm_drop(as->hdl); // FIXME not great to do this during playback - would mean new quality drops audio?

      ret = snd_pcm_prepare(as->hdl);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Could not prepare ALSA device '%s' (state %d): %s\n", as->devname, state, snd_strerror(ret));
	  return;
	}
    }

  // Negotiate quality (sample rate) with device - first we try to use the source quality
  as->quality = obuf->data[0].quality;
  ret = device_quality_set(as, &as->quality, &errmsg);
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_LAUDIO, "Input quality (%d/%d/%d) not supported, falling back to default. ALSA said: %s\n",
        as->quality.sample_rate, as->quality.bits_per_sample, as->quality.channels, errmsg);
      free(errmsg);
      as->quality = alsa_fallback_quality;
      ret = device_quality_set(as, &as->quality, &errmsg);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "ALSA device failed setting fallback quality: %s\n", errmsg);
	  free(errmsg);
	  as->state = OUTPUT_STATE_FAILED;
	  alsa_status(as);
	  return;
	}
    }

  dump_config(as);

  // Clear prebuffer in case start got called twice without a stop in between
  ringbuffer_free(&as->prebuf, 1);

  as->pos = 0;

  // Time stamps used for syncing, here we set when playback should start
  ts.tv_sec = OUTPUTS_BUFFER_DURATION;
  ts.tv_nsec = (uint64_t)as->offset_ms * 1000000UL;
  as->start_pts = timespec_add(obuf->pts, ts);

  // The difference between pos and start pos should match the 2 second buffer
  // that AirPlay uses (OUTPUTS_BUFFER_DURATION) + user configured offset_ms. We
  // will not use alsa's buffer for the initial buffering, because my sound
  // card's start_threshold is not to be counted on. Instead we allocate our own
  // buffer, and when it is time to play we write as much as we can to alsa's
  // buffer.
  offset_nsamp = (as->offset_ms * as->quality.sample_rate / 1000);

  as->buffer_nsamp = OUTPUTS_BUFFER_DURATION * as->quality.sample_rate + offset_nsamp;
  size = STOB(as->buffer_nsamp, as->quality.bits_per_sample, as->quality.channels);
  ringbuffer_init(&as->prebuf, size);

  as->state = OUTPUT_STATE_STREAMING;
}

// This function writes the sample buf into either the prebuffer or directly to
// ALSA, depending on how much room there is in ALSA, and whether we are
// prebuffering or not. It also transfers from the the prebuffer to ALSA, if
// needed. Returns 0 on success, negative on error.
static int
buffer_write(struct alsa_session *as, struct output_data *odata, snd_pcm_sframes_t avail)
{
  uint8_t *buf;
  size_t bufsize;
  size_t wrote;
  snd_pcm_sframes_t nsamp;
  snd_pcm_sframes_t ret;

  // Prebuffering, no actual writing
  if (avail == 0)
    {
      wrote = ringbuffer_write(&as->prebuf, odata->buffer, odata->bufsize);
      nsamp = BTOS(wrote, as->quality.bits_per_sample, as->quality.channels);
      return nsamp;
    }

  // Read from prebuffer if it has data and write to device
  if (as->prebuf.read_avail != 0)
    {
      // Maximum amount of bytes we want to read
      bufsize = STOB(avail, as->quality.bits_per_sample, as->quality.channels);

      bufsize = ringbuffer_read(&buf, bufsize, &as->prebuf);
      if (bufsize == 0)
	return 0;

      nsamp = BTOS(bufsize, as->quality.bits_per_sample, as->quality.channels);
      ret = snd_pcm_writei(as->hdl, buf, nsamp);
      if (ret < 0)
	return -1;

      avail -= ret;
    }

  // Write to prebuffer if device buffer does not have availability. Note that
  // if the prebuffer doesn't have enough room, which can happen if avail stays
  // low, i.e. device buffer is overrunning, then the extra samples get dropped
  if (odata->samples > avail)
    {
      ringbuffer_write(&as->prebuf, odata->buffer, odata->bufsize);
      return odata->samples;
    }

  ret = snd_pcm_writei(as->hdl, odata->buffer, odata->samples);
  if (ret < 0)
    return ret;

  if (ret != odata->samples)
    DPRINTF(E_WARN, L_LAUDIO, "ALSA partial write detected\n");

  return ret;
}

static enum alsa_sync_state
sync_check(struct alsa_session *as)
{
  enum alsa_sync_state sync;
  snd_pcm_sframes_t delay;
  struct timespec ts;
  int elapsed;
  uint64_t cur_pos;
  uint64_t exp_pos;
  int32_t latency;
  int ret;

  as->sync_counter++;

  ret = snd_pcm_delay(as->hdl, &delay);
  if (ret < 0)
    return ALSA_SYNC_OK;

  // Would be nice to use snd_pcm_status_get_audio_htstamp here, but it doesn't
  // seem to be supported on my computer
  clock_gettime(CLOCK_MONOTONIC, &ts);

  // Here we calculate elapsed time since playback was supposed to start, taking
  // into account buffer time and configuration of offset_ms. We then calculate
  // our expected position based on elapsed time, and if it is different from
  // where we are + what is the buffers, then ALSA is out of sync.
  elapsed = (ts.tv_sec - as->start_pts.tv_sec) * 1000L + (ts.tv_nsec - as->start_pts.tv_nsec) / 1000000;
  if (elapsed < 0)
    return ALSA_SYNC_OK;

  cur_pos = (uint64_t)as->pos - (delay + BTOS(as->prebuf.read_avail, as->quality.bits_per_sample, as->quality.channels));
  exp_pos = (uint64_t)elapsed * as->quality.sample_rate / 1000;
  latency = cur_pos - exp_pos;

  // If the latency is low or very different from our last measurement, we reset the sync_counter
  if (abs(latency) < ALSA_MAX_LATENCY || abs(as->last_latency - latency) > ALSA_MAX_LATENCY_VARIANCE)
    {
      as->sync_counter = 0;
      sync = ALSA_SYNC_OK;
    }
  // If we have measured a consistent latency for configured period, then we take action
  else if (as->sync_counter >= as->adjust_period_seconds)
    {
      as->sync_counter = 0;
      if (latency < 0)
	sync = ALSA_SYNC_BEHIND;
      else
	sync = ALSA_SYNC_AHEAD;
    }
  else
    sync = ALSA_SYNC_OK;

  // The will be used by sync_correct, so it knows how much we are out of sync
  as->last_latency = latency;

  DPRINTF(E_DBG, L_LAUDIO, "start %lu:%lu, now %lu:%lu, elapsed is %d ms, cur_pos=%" PRIu64 ", exp_pos=%" PRIu64 ", latency=%d\n",
    as->start_pts.tv_sec, as->start_pts.tv_nsec / 1000000, ts.tv_sec, ts.tv_nsec / 1000000, elapsed, cur_pos, exp_pos, latency);

  return sync;
}

static void
sync_correct(struct alsa_session *as)
{
  DPRINTF(E_INFO, L_LAUDIO, "Here we should take action to compensate for ALSA latency of %d samples\n", as->last_latency);
  // Not implemented yet
}

static void
playback_write(struct alsa_session *as, struct output_buffer *obuf)
{
  snd_pcm_sframes_t ret;
  snd_pcm_sframes_t avail;
  enum alsa_sync_state sync;
  bool prebuffering;
  int i;

  // Find the quality we want
  for (i = 0; obuf->data[i].buffer; i++)
    {
      if (quality_is_equal(&as->quality, &obuf->data[i].quality))
	break;
    }

  if (!obuf->data[i].buffer)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Output not delivering required data quality, aborting\n");
      as->state = OUTPUT_STATE_FAILED;
      alsa_status(as);
      return;
    }

  prebuffering = (as->pos < as->buffer_nsamp);
  if (prebuffering)
    {
      // Can never fail since we don't actually write to the device
      as->pos += buffer_write(as, &obuf->data[i], 0);
      return;
    }

  // Check sync each second (or if this is first write where last_pts is zero)
  if (obuf->pts.tv_sec != as->last_pts.tv_sec)
    {
      sync = sync_check(as);
      if (sync != ALSA_SYNC_OK)
	sync_correct(as);

      as->last_pts = obuf->pts;
    }

  avail = snd_pcm_avail(as->hdl);

  ret = buffer_write(as, &obuf->data[i], avail);
  if (ret < 0)
    goto alsa_error;

  as->pos += ret;

  return;

 alsa_error:
  if (ret == -EPIPE)
    {
      DPRINTF(E_WARN, L_LAUDIO, "ALSA buffer underrun\n");

      ret = snd_pcm_prepare(as->hdl);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_LAUDIO, "ALSA couldn't recover from underrun: %s\n", snd_strerror(ret));
	  return;
	}

      // Fill the prebuf with audio before restarting, so we don't underrun again
      playback_restart(as, obuf);
      return;
    }

  DPRINTF(E_LOG, L_LAUDIO, "ALSA write error: %s\n", snd_strerror(ret));

  as->state = OUTPUT_STATE_FAILED;
  alsa_status(as);
}


/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
alsa_session_free(struct alsa_session *as)
{
  if (!as)
    return;

  device_close(as);

  outputs_quality_unsubscribe(&alsa_fallback_quality);

  ringbuffer_free(&as->prebuf, 1);
  snd_pcm_status_free(as->pcm_status);

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

  outputs_device_session_remove(as->device_id);

  alsa_session_free(as);
}

static struct alsa_session *
alsa_session_make(struct output_device *device, int callback_id)
{
  struct alsa_session *as;
  cfg_t *cfg_audio;
  char *errmsg;
  int original_adjust;
  int ret;

  CHECK_NULL(L_LAUDIO, as = calloc(1, sizeof(struct alsa_session)));

  as->device_id = device->id;
  as->callback_id = callback_id;
  as->volume = device->volume;

  cfg_audio = cfg_getsec(cfg, "audio");

  as->devname = cfg_getstr(cfg_audio, "card");
  as->mixer_name = cfg_getstr(cfg_audio, "mixer");
  as->mixer_device_name = cfg_getstr(cfg_audio, "mixer_device");
  if (!as->mixer_device_name || strlen(as->mixer_device_name) == 0)
    as->mixer_device_name = cfg_getstr(cfg_audio, "card");

  as->offset_ms = cfg_getint(cfg_audio, "offset_ms");
  if (abs(as->offset_ms) > 1000)
    {
      DPRINTF(E_LOG, L_LAUDIO, "The ALSA offset_ms (%d) set in the configuration is out of bounds\n", as->offset_ms);
      as->offset_ms = 1000 * (as->offset_ms/abs(as->offset_ms));
    }

  original_adjust = cfg_getint(cfg_audio, "adjust_period_seconds");
  if (original_adjust < 1)
    as->adjust_period_seconds = 1;
  else if (original_adjust > 20)
    as->adjust_period_seconds = 20;
  else
    as->adjust_period_seconds = original_adjust;
  if (as->adjust_period_seconds != original_adjust)
    DPRINTF(E_LOG, L_LAUDIO, "Clamped ALSA adjust_period_seconds to %d\n", as->adjust_period_seconds);

  snd_pcm_status_malloc(&as->pcm_status);

  ret = device_open(as);
  if (ret < 0)
    goto out_free_session;

  ret = device_quality_set(as, &alsa_fallback_quality, &errmsg);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "%s\n", errmsg);
      free(errmsg);
      goto out_device_close;
    }

  // If this fails it just means we won't get timestamps, which we can handle
  device_configure(as);

  ret = outputs_quality_subscribe(&alsa_fallback_quality);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not subscribe to fallback audio quality\n");
      goto out_device_close;
    }

  as->state = OUTPUT_STATE_CONNECTED;
  as->next = sessions;
  sessions = as;

  // as is now the official device session
  outputs_device_session_add(device->id, as);

  return as;

 out_device_close:
  device_close(as);
 out_free_session:
  free(as);
  return NULL;
}

static void
alsa_status(struct alsa_session *as)
{
  outputs_cb(as->callback_id, as->device_id, as->state);
  as->callback_id = -1;

  if (as->state == OUTPUT_STATE_FAILED || as->state == OUTPUT_STATE_STOPPED)
    alsa_session_cleanup(as);
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
alsa_device_start(struct output_device *device, int callback_id)
{
  struct alsa_session *as;

  as = alsa_session_make(device, callback_id);
  if (!as)
    return -1;

  as->state = OUTPUT_STATE_CONNECTED;
  alsa_status(as);

  return 0;
}

static int
alsa_device_stop(struct output_device *device, int callback_id)
{
  struct alsa_session *as = device->session;

  as->callback_id = callback_id;
  as->state = OUTPUT_STATE_STOPPED;
  alsa_status(as); // Will terminate the session since the state is STOPPED

  return 0;
}

static int
alsa_device_flush(struct output_device *device, int callback_id)
{
  struct alsa_session *as = device->session;

  snd_pcm_drop(as->hdl);

  ringbuffer_free(&as->prebuf, 1);

  as->callback_id = callback_id;
  as->state = OUTPUT_STATE_CONNECTED;
  alsa_status(as);

  return 0;
}

static int
alsa_device_probe(struct output_device *device, int callback_id)
{
  struct alsa_session *as;

  as = alsa_session_make(device, callback_id);
  if (!as)
    return -1;

  as->state = OUTPUT_STATE_STOPPED;
  alsa_status(as); // Will terminate the session since the state is STOPPED

  return 0;
}

static int
alsa_device_volume_set(struct output_device *device, int callback_id)
{
  struct alsa_session *as = device->session;
  int pcm_vol;

  if (!as)
    return 0;

  snd_mixer_handle_events(as->mixer_hdl);

  if (!snd_mixer_selem_is_active(as->vol_elem))
    return 0;

  switch (device->volume)
    {
      case 0:
	pcm_vol = as->vol_min;
	break;

      case 100:
	pcm_vol = as->vol_max;
	break;

      default:
	pcm_vol = as->vol_min + (device->volume * (as->vol_max - as->vol_min)) / 100;
	break;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Setting ALSA volume to %d (%d)\n", pcm_vol, device->volume);

  snd_mixer_selem_set_playback_volume_all(as->vol_elem, pcm_vol);

  as->callback_id = callback_id;
  alsa_status(as);

  return 1;
}

static void
alsa_device_cb_set(struct output_device *device, int callback_id)
{
  struct alsa_session *as = device->session;

  as->callback_id = callback_id;
}

static void
alsa_write(struct output_buffer *obuf)
{
  struct alsa_session *as;
  struct alsa_session *next;

  for (as = sessions; as; as = next)
    {
      next = as->next;
      // Need to adjust buffers and device params if sample rate changed, or if
      // this was the first write to the device
      if (!quality_is_equal(&obuf->data[0].quality, &alsa_last_quality) || as->state == OUTPUT_STATE_CONNECTED)
	playback_restart(as, obuf); 

      playback_write(as, obuf);

      alsa_last_quality = obuf->data[0].quality;
    }
}

static int
alsa_init(void)
{
  struct output_device *device;
  cfg_t *cfg_audio;
  const char *type;

  // Is ALSA enabled in config?
  cfg_audio = cfg_getsec(cfg, "audio");
  type = cfg_getstr(cfg_audio, "type");
  if (type && (strcasecmp(type, "alsa") != 0))
    return -1;

  CHECK_NULL(L_LAUDIO, device = calloc(1, sizeof(struct output_device)));

  device->id = 0;
  device->name = strdup(cfg_getstr(cfg_audio, "nickname"));
  device->type = OUTPUT_TYPE_ALSA;
  device->type_name = outputs_name(device->type);
  device->has_video = 0;

  DPRINTF(E_INFO, L_LAUDIO, "Adding ALSA device '%s' with name '%s'\n", cfg_getstr(cfg_audio, "card"), device->name);

  player_device_add(device);

  snd_lib_error_set_handler(logger_alsa);

  return 0;
}

static void
alsa_deinit(void)
{
  struct alsa_session *as;

  snd_lib_error_set_handler(NULL);

  for (as = sessions; sessions; as = sessions)
    {
      sessions = as->next;
      alsa_session_free(as);
    }
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
  .device_flush = alsa_device_flush,
  .device_probe = alsa_device_probe,
  .device_volume_set = alsa_device_volume_set,
  .device_cb_set = alsa_device_cb_set,
  .write = alsa_write,
};
