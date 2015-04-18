/*
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

#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "laudio.h"


struct pcm_packet
{
  uint8_t samples[STOB(AIRTUNES_V2_PACKET_SAMPLES)];

  uint64_t rtptime;

  size_t offset;

  struct pcm_packet *next;
};

static uint64_t pcm_pos;
static uint64_t pcm_start_pos;
static int pcm_last_error;
static int pcm_recovery;
static int pcm_buf_threshold;

static struct pcm_packet *pcm_pkt_head;
static struct pcm_packet *pcm_pkt_tail;

static char *card_name;
static char *mixer_name;
static snd_pcm_t *hdl;
static snd_mixer_t *mixer_hdl;
static snd_mixer_elem_t *vol_elem;
static long vol_min;
static long vol_max;

static enum laudio_state pcm_status;
static laudio_status_cb status_cb;


static void
update_status(enum laudio_state status)
{
  pcm_status = status;
  status_cb(status);
}

static int
laudio_alsa_xrun_recover(int err)
{
  int ret;

  if (err != 0)
    pcm_last_error = err;

  /* Buffer underrun */
  if (err == -EPIPE)
    {
      pcm_last_error = 0;

      ret = snd_pcm_prepare(hdl);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_LAUDIO, "Couldn't recover from underrun: %s\n", snd_strerror(ret));
	  return 1;
	}

      return 0;
    }
  /* Device suspended */
  else if (pcm_last_error == -ESTRPIPE)
    {
      ret = snd_pcm_resume(hdl);
      if (ret == -EAGAIN)
	{
	  pcm_recovery++;

	  return 2;
	}
      else if (ret < 0)
	{
	  pcm_recovery = 0;

	  ret = snd_pcm_prepare(hdl);
	  if (ret < 0)
	    {
	      DPRINTF(E_WARN, L_LAUDIO, "Couldn't recover from suspend: %s\n", snd_strerror(ret));
	      return 1;
	    }
	}

      pcm_recovery = 0;
      return 0;
    }

  return err;
}

static int
laudio_alsa_set_start_threshold(snd_pcm_uframes_t threshold)
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

static void
laudio_alsa_write(uint8_t *buf, uint64_t rtptime)
{
  struct pcm_packet *pkt;
  snd_pcm_sframes_t nsamp;
  int ret;

  pkt = (struct pcm_packet *)malloc(sizeof(struct pcm_packet));
  if (!pkt)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Out of memory for PCM pkt\n");

      update_status(LAUDIO_FAILED);
      return;
    }

  memcpy(pkt->samples, buf, sizeof(pkt->samples));

  pkt->rtptime = rtptime;
  pkt->offset = 0;
  pkt->next = NULL;

  if (pcm_pkt_tail)
    {
      pcm_pkt_tail->next = pkt;
      pcm_pkt_tail = pkt;
    }
  else
    {
      pcm_pkt_head = pkt;
      pcm_pkt_tail = pkt;
    }

  if (pcm_pos < pcm_pkt_head->rtptime)
    {
      pcm_pos += AIRTUNES_V2_PACKET_SAMPLES;

      return;
    }
  else if ((pcm_status != LAUDIO_RUNNING) && (pcm_pos + pcm_buf_threshold >= pcm_start_pos))
    {
      /* Kill threshold */
      ret = laudio_alsa_set_start_threshold(0);
      if (ret < 0)
	DPRINTF(E_WARN, L_LAUDIO, "Couldn't set PCM start threshold to 0 for output start\n");

      update_status(LAUDIO_RUNNING);
    }

  pkt = pcm_pkt_head;

  while (pkt)
    {
      if (pcm_recovery)
	{
	  ret = laudio_alsa_xrun_recover(0);
	  if ((ret == 2) && (pcm_recovery < 10))
	    return;
	  else
	    {
	      if (ret == 2)
		DPRINTF(E_LOG, L_LAUDIO, "Couldn't recover PCM device after 10 tries, aborting\n");

	      update_status(LAUDIO_FAILED);
	      return;
	    }
	}

      nsamp = snd_pcm_writei(hdl, pkt->samples + pkt->offset, BTOS(sizeof(pkt->samples) - pkt->offset));
      if ((nsamp == -EPIPE) || (nsamp == -ESTRPIPE))
	{
	  ret = laudio_alsa_xrun_recover(nsamp);
	  if ((ret < 0) || (ret == 1))
	    {
	      if (ret < 0)
		DPRINTF(E_LOG, L_LAUDIO, "PCM write error: %s\n", snd_strerror(ret));

	      update_status(LAUDIO_FAILED);
	      return;
	    }
	  else if (ret != 0)
	    return;

	  continue;
	}
      else if (nsamp < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "PCM write error: %s\n", snd_strerror(nsamp));

	  update_status(LAUDIO_FAILED);
	  return;
	}

      pcm_pos += nsamp;

      pkt->offset += STOB(nsamp);
      if (pkt->offset == sizeof(pkt->samples))
	{
	  pcm_pkt_head = pkt->next;

	  if (pkt == pcm_pkt_tail)
	    pcm_pkt_tail = NULL;

	  free(pkt);

	  pkt = pcm_pkt_head;
	}

      /* Don't let ALSA fill up the buffer too much */
// Disabled - seems to cause buffer underruns
//      if (nsamp == AIRTUNES_V2_PACKET_SAMPLES)
//	return;
    }
}

static uint64_t
laudio_alsa_get_pos(void)
{
  snd_pcm_sframes_t delay;
  int ret;

  if (pcm_pos == 0)
    return 0;

  ret = snd_pcm_delay(hdl, &delay);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_LAUDIO, "Could not obtain PCM delay: %s\n", snd_strerror(ret));

      return pcm_pos;
    }

  return pcm_pos - delay;
}

static void
laudio_alsa_set_volume(int vol)
{
  int pcm_vol;

  if (!mixer_hdl || !vol_elem)
    return;

  snd_mixer_handle_events(mixer_hdl);

  if (!snd_mixer_selem_is_active(vol_elem))
    return;

  switch (vol)
    {
      case 0:
	pcm_vol = vol_min;
	break;

      case 100:
	pcm_vol = vol_max;
	break;

      default:
	pcm_vol = vol_min + (vol * (vol_max - vol_min)) / 100;
	break;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Setting PCM volume to %d (%d)\n", pcm_vol, vol);

  snd_mixer_selem_set_playback_volume_all(vol_elem, pcm_vol);
}

static int
laudio_alsa_start(uint64_t cur_pos, uint64_t next_pkt)
{
  int ret;

  ret = snd_pcm_prepare(hdl);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not prepare PCM device: %s\n", snd_strerror(ret));

      return -1;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Start local audio curpos %" PRIu64 ", next_pkt %" PRIu64 "\n", cur_pos, next_pkt);
  DPRINTF(E_DBG, L_LAUDIO, "PCM will start after %d samples (%d packets)\n", pcm_buf_threshold, pcm_buf_threshold / AIRTUNES_V2_PACKET_SAMPLES);

  /* Make pcm_pos the rtptime of the packet containing cur_pos */
  pcm_pos = next_pkt;
  while (pcm_pos > cur_pos)
    pcm_pos -= AIRTUNES_V2_PACKET_SAMPLES;

  pcm_start_pos = next_pkt + pcm_buf_threshold;

  /* Compensate threshold, as it's taken into account by snd_pcm_delay() */
  //pcm_pos += pcm_buf_threshold;

  DPRINTF(E_DBG, L_LAUDIO, "PCM pos %" PRIu64 ", start pos %" PRIu64 "\n", pcm_pos, pcm_start_pos);

  pcm_pkt_head = NULL;
  pcm_pkt_tail = NULL;

  pcm_last_error = 0;
  pcm_recovery = 0;

  ret = laudio_alsa_set_start_threshold(pcm_buf_threshold);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set PCM start threshold for local audio start\n");

      return -1;
    }

  update_status(LAUDIO_STARTED);

  return 0;
}

static void
laudio_alsa_stop(void)
{
  struct pcm_packet *pkt;

  update_status(LAUDIO_STOPPING);

  snd_pcm_drop(hdl);

  for (pkt = pcm_pkt_head; pcm_pkt_head; pkt = pcm_pkt_head)
    {
      pcm_pkt_head = pkt->next;

      free(pkt);
    }

  pcm_pkt_head = NULL;
  pcm_pkt_tail = NULL;

  update_status(LAUDIO_OPEN);
}

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

  ret = snd_mixer_attach(mixer_hdl, card_name);
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

  /* Grab interesting elements */
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

  /* Get min & max volume */
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
laudio_alsa_open(void)
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

  /* HW params */
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

  ret = snd_pcm_hw_params_set_rate(hdl, hw_params, 44100, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Hardware doesn't support 44.1 kHz: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  ret = snd_pcm_hw_params_get_buffer_size_max(hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not get max buffer size: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Max buffer size is %lu samples\n", bufsize);

  ret = snd_pcm_hw_params_set_buffer_size_max(hdl, hw_params, &bufsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set buffer size to max: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Buffer size is %lu samples\n", bufsize);

  ret = snd_pcm_hw_params(hdl, hw_params);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set hw params: %s\n", snd_strerror(ret));

      goto out_fail;
    }

  snd_pcm_hw_params_free(hw_params);
  hw_params = NULL;

  pcm_pos = 0;
  pcm_last_error = 0;
  pcm_recovery = 0;
  pcm_buf_threshold = (bufsize / AIRTUNES_V2_PACKET_SAMPLES) * AIRTUNES_V2_PACKET_SAMPLES;

  ret = mixer_open();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not open mixer\n");

      goto out_fail;
    }

  update_status(LAUDIO_OPEN);

  return 0;

 out_fail:
  if (hw_params)
    snd_pcm_hw_params_free(hw_params);

  snd_pcm_close(hdl);
  hdl = NULL;

  return -1;
}

static void
laudio_alsa_close(void)
{
  struct pcm_packet *pkt;

  snd_pcm_close(hdl);
  hdl = NULL;

  if (mixer_hdl)
    {
      snd_mixer_detach(mixer_hdl, card_name);
      snd_mixer_close(mixer_hdl);

      mixer_hdl = NULL;
      vol_elem = NULL;
    }

  for (pkt = pcm_pkt_head; pcm_pkt_head; pkt = pcm_pkt_head)
    {
      pcm_pkt_head = pkt->next;

      free(pkt);
    }

  pcm_pkt_head = NULL;
  pcm_pkt_tail = NULL;

  update_status(LAUDIO_CLOSED);
}


static int
laudio_alsa_init(laudio_status_cb cb, cfg_t *cfg_audio)
{
  snd_lib_error_set_handler(logger_alsa);

  status_cb = cb;

  card_name = cfg_getstr(cfg_audio, "card");
  mixer_name = cfg_getstr(cfg_audio, "mixer");

  hdl = NULL;
  mixer_hdl = NULL;
  vol_elem = NULL;

  return 0;
}

static void
laudio_alsa_deinit(void)
{
  snd_lib_error_set_handler(NULL);
}

audio_output audio_alsa = {
    .name = "alsa",
    .init = &laudio_alsa_init,
    .deinit = &laudio_alsa_deinit,
    .start = &laudio_alsa_start,
    .stop = &laudio_alsa_stop,
    .open = &laudio_alsa_open,
    .close = &laudio_alsa_close,
    .pos = &laudio_alsa_get_pos,
    .write = &laudio_alsa_write,
    .volume = &laudio_alsa_set_volume,
    };
