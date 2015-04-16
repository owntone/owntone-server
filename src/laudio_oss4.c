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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <sys/soundcard.h>

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
static int pcm_buf_threshold;
static int pcm_retry;

static struct pcm_packet *pcm_pkt_head;
static struct pcm_packet *pcm_pkt_tail;

static char *card_name;
static int oss_fd;

static enum laudio_state pcm_status;
static laudio_status_cb status_cb;


static void
update_status(enum laudio_state status)
{
  pcm_status = status;
  status_cb(status);
}

static void
laudio_oss4_write(uint8_t *buf, uint64_t rtptime)
{
  struct pcm_packet *pkt;
  int scratch;
  int nsamp;
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
  else if ((pcm_status != LAUDIO_RUNNING) && (pcm_pos >= pcm_start_pos))
    {
      /* Start audio output */
      scratch = PCM_ENABLE_OUTPUT;
      ret = ioctl(oss_fd, SNDCTL_DSP_SETTRIGGER, &scratch);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LAUDIO, "Could not enable output: %s\n", strerror(errno));

	  update_status(LAUDIO_FAILED);
	  return;
	}

      update_status(LAUDIO_RUNNING);
    }

  pkt = pcm_pkt_head;

  while (pkt)
    {
      nsamp = write(oss_fd, pkt->samples + pkt->offset, sizeof(pkt->samples) - pkt->offset);
      if (nsamp < 0)
	{
	  if (errno == EAGAIN)
	    {
	      pcm_retry++;

	      if (pcm_retry < 10)
		return;
	    }

	  DPRINTF(E_LOG, L_LAUDIO, "Write error: %s\n", strerror(errno));

	  update_status(LAUDIO_FAILED);
	  return;
	}

      pcm_retry = 0;

      pkt->offset += nsamp;

      nsamp = BTOS(nsamp);
      pcm_pos += nsamp;

      if (pkt->offset == sizeof(pkt->samples))
	{
	  pcm_pkt_head = pkt->next;

	  if (pkt == pcm_pkt_tail)
	    pcm_pkt_tail = NULL;

	  free(pkt);

	  pkt = pcm_pkt_head;
	}

      /* Don't let the buffer fill up too much */
      if (nsamp == AIRTUNES_V2_PACKET_SAMPLES)
	break;
    }
}

static uint64_t
laudio_oss4_get_pos(void)
{
  int delay;
  int ret;

  ret = ioctl(oss_fd, SNDCTL_DSP_GETODELAY, &delay);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not obtain output delay: %s\n", strerror(errno));

      return pcm_pos;
    }

  return pcm_pos - BTOS(delay);
}

static void
laudio_oss4_set_volume(int vol)
{
  int oss_vol;
  int ret;

  vol = vol & 0xff;
  oss_vol = vol | (vol << 8);

  ret = ioctl(oss_fd, SNDCTL_DSP_SETPLAYVOL, &oss_vol);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set volume: %s\n", strerror(errno));

      return;
    }

  DPRINTF(E_DBG, L_LAUDIO, "Setting PCM volume to %d (real: %d)\n", vol, (oss_vol & 0xff));
}

static int
laudio_oss4_start(uint64_t cur_pos, uint64_t next_pkt)
{
  int scratch;
  int ret;

  DPRINTF(E_DBG, L_LAUDIO, "PCM will start after %d samples (%d packets)\n", pcm_buf_threshold, pcm_buf_threshold / AIRTUNES_V2_PACKET_SAMPLES);

  /* Make pcm_pos the rtptime of the packet containing cur_pos */
  pcm_pos = next_pkt;
  while (pcm_pos > cur_pos)
    pcm_pos -= AIRTUNES_V2_PACKET_SAMPLES;

  pcm_start_pos = next_pkt + pcm_buf_threshold;

  /* FIXME check for OSS - Compensate threshold, as it's taken into account by snd_pcm_delay() */
  pcm_pos += pcm_buf_threshold;

  DPRINTF(E_DBG, L_LAUDIO, "PCM pos %" PRIu64 ", start pos %" PRIu64 "\n", pcm_pos, pcm_start_pos);

  pcm_pkt_head = NULL;
  pcm_pkt_tail = NULL;

  pcm_retry = 0;

  scratch = 0;
  ret = ioctl(oss_fd, SNDCTL_DSP_SETTRIGGER, &scratch);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set trigger: %s\n", strerror(errno));

      return -1;
    }

  update_status(LAUDIO_STARTED);

  return 0;
}

static void
laudio_oss4_stop(void)
{
  struct pcm_packet *pkt;
  int ret;

  update_status(LAUDIO_STOPPING);

  ret = ioctl(oss_fd, SNDCTL_DSP_HALT_OUTPUT, NULL);
  if (ret < 0)
    DPRINTF(E_LOG, L_LAUDIO, "Failed to halt output: %s\n", strerror(errno));

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
laudio_oss4_open(void)
{
  audio_buf_info bi;
  oss_sysinfo si;
  int scratch;
  int ret;

  oss_fd = open(card_name, O_RDWR | O_NONBLOCK);
  if (oss_fd < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not open sound device: %s\n", strerror(errno));

      return -1;
    }

  ret = ioctl(oss_fd, SNDCTL_SYSINFO, &si);
  if ((ret < 0) || (si.versionnum < 0x040000))
    {
      DPRINTF(E_LOG, L_LAUDIO, "Your OSS version (%s) is unavailable or too old; version 4.0.0+ is required\n", si.version);

      goto out_fail;
    }

  scratch = 0;
  ret = ioctl(oss_fd, SNDCTL_DSP_SETTRIGGER, &scratch);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Could not set trigger: %s\n", strerror(errno));

      goto out_fail;
    }

  scratch = AFMT_S16_LE;
  errno = 0;
  ret = ioctl(oss_fd, SNDCTL_DSP_SETFMT, &scratch);
  if ((ret < 0) || (scratch != AFMT_S16_LE))
    {
      if (errno)
	DPRINTF(E_LOG, L_LAUDIO, "Could not set sample format (S16 LE): %s\n", strerror(errno));
      else
	DPRINTF(E_LOG, L_LAUDIO, "Sample format S16 LE not supported\n");

      goto out_fail;
    }

  scratch = 2;
  errno = 0;
  ret = ioctl(oss_fd, SNDCTL_DSP_CHANNELS, &scratch);
  if ((ret < 0) || (scratch != 2))
    {
      if (errno)
	DPRINTF(E_LOG, L_LAUDIO, "Could not set stereo: %s\n", strerror(errno));
      else
	DPRINTF(E_LOG, L_LAUDIO, "Stereo not supported\n");

      goto out_fail;
    }

  scratch = 44100;
  errno = 0;
  ret = ioctl(oss_fd, SNDCTL_DSP_SPEED, &scratch);
  if ((ret < 0) || (scratch != 44100))
    {
      if (errno)
	DPRINTF(E_LOG, L_LAUDIO, "Could not set speed (44100): %s\n", strerror(errno));
      else
	DPRINTF(E_LOG, L_LAUDIO, "Sample rate 44100 not supported\n");

      goto out_fail;
    }

  ret = ioctl(oss_fd, SNDCTL_DSP_GETOSPACE, &bi);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LAUDIO, "Couldn't get output buffer status: %s\n", strerror(errno));

      goto out_fail;
    }

  pcm_buf_threshold = (BTOS(bi.bytes) / AIRTUNES_V2_PACKET_SAMPLES) * AIRTUNES_V2_PACKET_SAMPLES;

  update_status(LAUDIO_OPEN);

  return 0;

 out_fail:
  close(oss_fd);
  oss_fd = -1;

  return -1;
}

static void
laudio_oss4_close(void)
{
  struct pcm_packet *pkt;
  int ret;

  ret = ioctl(oss_fd, SNDCTL_DSP_HALT_OUTPUT, NULL);
  if (ret < 0)
    DPRINTF(E_LOG, L_LAUDIO, "Failed to halt output: %s\n", strerror(errno));

  close(oss_fd);
  oss_fd = -1;

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
laudio_oss4_init(laudio_status_cb cb, cfg_t *cfg_audio)
{
  status_cb = cb;

  card_name = cfg_getstr(cfg_audio, "card");

  return 0;
}

static void
laudio_oss4_deinit(void)
{
  /* EMPTY */
}

audio_output audio_oss4 = {
    .name = "oss4",
    .init = &laudio_oss4_init,
    .deinit = &laudio_oss4_deinit,
    .start = &laudio_oss4_start,
    .stop = &laudio_oss4_stop,
    .open = &laudio_oss4_open,
    .close = &laudio_oss4_close,
    .pos = &laudio_oss4_get_pos,
    .write = &laudio_oss4_write,
    .volume = &laudio_oss4_set_volume,
    };

