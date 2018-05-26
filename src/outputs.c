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

#include "logger.h"
#include "outputs.h"

extern struct output_definition output_raop;
extern struct output_definition output_streaming;
extern struct output_definition output_dummy;
extern struct output_definition output_fifo;
#ifdef HAVE_ALSA
extern struct output_definition output_alsa;
#endif
#ifdef HAVE_LIBPULSE
extern struct output_definition output_pulse;
#endif
#ifdef CHROMECAST
extern struct output_definition output_cast;
#endif

// Must be in sync with enum output_types
static struct output_definition *outputs[] = {
    &output_raop,
    &output_streaming,
    &output_dummy,
    &output_fifo,
#ifdef HAVE_ALSA
    &output_alsa,
#endif
#ifdef HAVE_LIBPULSE
    &output_pulse,
#endif
#ifdef CHROMECAST
    &output_cast,
#endif
    NULL
};

int
outputs_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  if (outputs[device->type]->disabled)
    return -1;

  if (outputs[device->type]->device_start)
    return outputs[device->type]->device_start(device, cb, rtptime);
  else
    return -1;
}

void
outputs_device_stop(struct output_session *session)
{
  if (outputs[session->type]->disabled)
    return;

  if (outputs[session->type]->device_stop)
    outputs[session->type]->device_stop(session);
}

int
outputs_device_probe(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled)
    return -1;

  if (outputs[device->type]->device_probe)
    return outputs[device->type]->device_probe(device, cb);
  else
    return -1;
}

void
outputs_device_free(struct output_device *device)
{
  if (!device)
    return;

  if (outputs[device->type]->disabled)
    DPRINTF(E_LOG, L_PLAYER, "BUG! Freeing device from a disabled output?\n");

  if (device->session)
    DPRINTF(E_LOG, L_PLAYER, "BUG! Freeing device with active session?\n");

  if (outputs[device->type]->device_free_extra)
    outputs[device->type]->device_free_extra(device);

  free(device->name);
  free(device->auth_key);
  free(device->v4_address);
  free(device->v6_address);

  free(device);
}

int
outputs_device_volume_set(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled)
    return -1;

  if (outputs[device->type]->device_volume_set)
    return outputs[device->type]->device_volume_set(device, cb);
  else
    return -1;
}

int
outputs_device_volume_to_pct(struct output_device *device, const char *volume)
{
  if (outputs[device->type]->disabled)
    return -1;

  if (outputs[device->type]->device_volume_to_pct)
    return outputs[device->type]->device_volume_to_pct(device, volume);
  else
    return -1;
}

void
outputs_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->playback_start)
	outputs[i]->playback_start(next_pkt, ts);
    }
}

void
outputs_playback_stop(void)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->playback_stop)
	outputs[i]->playback_stop();
    }
}

void
outputs_write(uint8_t *buf, uint64_t rtptime)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->write)
	outputs[i]->write(buf, rtptime);
    }
}

int
outputs_flush(output_status_cb cb, uint64_t rtptime)
{
  int ret;
  int i;

  ret = 0;
  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->flush)
	ret += outputs[i]->flush(cb, rtptime);
    }

  return ret;
}

void
outputs_status_cb(struct output_session *session, output_status_cb cb)
{
  if (outputs[session->type]->disabled)
    return;

  if (outputs[session->type]->status_cb)
    outputs[session->type]->status_cb(session, cb);
}

struct output_metadata *
outputs_metadata_prepare(int id)
{
  struct output_metadata *omd;
  struct output_metadata *new;
  void *metadata;
  int i;

  omd = NULL;
  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (!outputs[i]->metadata_prepare)
	continue;

      metadata = outputs[i]->metadata_prepare(id);
      if (!metadata)
	continue;

      new = calloc(1, sizeof(struct output_metadata));
      if (!new)
	return omd;

      if (omd)
	new->next = omd;
      omd = new;
      omd->type = i;
      omd->metadata = metadata;
    }

  return omd;  
}

void
outputs_metadata_send(struct output_metadata *omd, uint64_t rtptime, uint64_t offset, int startup)
{
  struct output_metadata *ptr;
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (!outputs[i]->metadata_send)
	continue;

      // Go through linked list to find appropriate metadata for type
      for (ptr = omd; ptr; ptr = ptr->next)
	if (ptr->type == i)
	  break;

      if (!ptr)
	continue;

      outputs[i]->metadata_send(ptr->metadata, rtptime, offset, startup);
    }
}

void
outputs_metadata_purge(void)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->metadata_purge)
	outputs[i]->metadata_purge();
    }
}

void
outputs_metadata_prune(uint64_t rtptime)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->metadata_prune)
	outputs[i]->metadata_prune(rtptime);
    }
}

void
outputs_metadata_free(struct output_metadata *omd)
{
  struct output_metadata *ptr;

  if (!omd)
    return;

  for (ptr = omd; omd; ptr = omd)
    {
      omd = ptr->next;
      free(ptr);
    }
}

void
outputs_authorize(enum output_types type, const char *pin)
{
  if (outputs[type]->disabled)
    return;

  if (outputs[type]->authorize)
    outputs[type]->authorize(pin);
}

int
outputs_priority(struct output_device *device)
{
  return outputs[device->type]->priority;
}

const char *
outputs_name(enum output_types type)
{
  return outputs[type]->name;
}

int
outputs_init(void)
{
  int no_output;
  int ret;
  int i;

  no_output = 1;
  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->type != i)
	{
	  DPRINTF(E_FATAL, L_PLAYER, "BUG! Output definitions are misaligned with output enum\n");
	  return -1;
	}

      if (!outputs[i]->init)
	{
	  no_output = 0;
	  continue;
	}

      ret = outputs[i]->init();
      if (ret < 0)
	outputs[i]->disabled = 1;
      else
	no_output = 0;
    }

  if (no_output)
    return -1;

  return 0;
}

void
outputs_deinit(void)
{
  int i;

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->deinit)
        outputs[i]->deinit();
    }
}

