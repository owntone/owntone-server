/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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

#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "laudio.h"

#ifdef ALSA
extern audio_output audio_alsa;
#endif
#ifdef OSS4
extern audio_output audio_oss4;
#endif

extern audio_output audio_dummy;

static audio_output *outputs[] = {
#ifdef ALSA
    &audio_alsa,
#endif
#ifdef OSS4
    &audio_oss4,
#endif
    &audio_dummy,
    NULL
};

static audio_output *output;

struct pcm_packet
{
  uint8_t samples[STOB(AIRTUNES_V2_PACKET_SAMPLES)];

  uint64_t rtptime;

  size_t offset;

  struct pcm_packet *next;
};



void
laudio_write(uint8_t *buf, uint64_t rtptime)
{
  output->write(buf, rtptime);
}

uint64_t
laudio_get_pos(void)
{
  return output->pos();
}

void
laudio_set_volume(int vol)
{
  output->volume(vol);
}

int
laudio_start(uint64_t cur_pos, uint64_t next_pkt)
{
  return output->start(cur_pos, next_pkt);
}

void
laudio_stop(void)
{
  output->stop();
}


int
laudio_open(void)
{
  return output->open();
}

void
laudio_close(void)
{
  output->close();
}


int
laudio_init(laudio_status_cb cb)
{
  cfg_t *cfg_audio;
  char *type;
  int i;

  cfg_audio = cfg_getsec(cfg, "audio");
  type = cfg_getstr(cfg_audio, "type");

  output = NULL;
  if (type)
    {
      DPRINTF(E_DBG, L_LAUDIO, "Searching for local audio output: '%s'\n", type);
      for (i = 0; outputs[i]; i++)
	{
	  if (0 == strcmp(type, outputs[i]->name))
	    {
	      output = outputs[i];
	    }
	}

      if (!output)
	DPRINTF(E_WARN, L_LAUDIO, "No local audio output '%s' available, falling back to default output\n", type);
    }

  if (!output)
    {
      output = outputs[0];
    }

  DPRINTF(E_INFO, L_LAUDIO, "Local audio output: '%s'\n", output->name);

  return output->init(cb, cfg_audio);
}

void
laudio_deinit(void)
{
  output->deinit();
}
