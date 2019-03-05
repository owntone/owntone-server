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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include <event2/event.h>

#include "logger.h"
#include "misc.h"
#include "transcode.h"
#include "listener.h"
#include "db.h"
#include "player.h" //TODO remove me when player_pmap is removed again
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

/* From player.c */
extern struct event_base *evbase_player;

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

// When we stop, we keep the outputs open for a while, just in case we are
// actually just restarting. This timeout determines how long we wait before
// full stop.
// (value is in seconds)
#define OUTPUTS_STOP_TIMEOUT 10

#define OUTPUTS_MAX_CALLBACKS 64

struct outputs_callback_register
{
  output_status_cb cb;
  struct output_device *device;

  // We have received the callback with the result from the backend
  bool ready;

  // We store a device_id to avoid the risk of dangling device pointer
  uint64_t device_id;
  enum output_device_state state;
};

struct output_quality_subscription
{
  int count;
  struct media_quality quality;
  struct encode_ctx *encode_ctx;
};

static struct outputs_callback_register outputs_cb_register[OUTPUTS_MAX_CALLBACKS];
static struct event *outputs_deferredev;
static struct timeval outputs_stop_timeout = { OUTPUTS_STOP_TIMEOUT, 0 };

// Last element is a zero terminator
static struct output_quality_subscription output_quality_subscriptions[OUTPUTS_MAX_QUALITY_SUBSCRIPTIONS + 1];
static bool outputs_got_new_subscription;


/* ------------------------------- MISC HELPERS ----------------------------- */

static output_status_cb
callback_get(struct output_device *device)
{
  int callback_id;

  for (callback_id = 0; callback_id < ARRAY_SIZE(outputs_cb_register); callback_id++)
    {
      if (outputs_cb_register[callback_id].device == device)
	return outputs_cb_register[callback_id].cb;
    }

  return NULL;
}

static void
callback_remove(struct output_device *device)
{
  int callback_id;

  if (!device)
    return;

  for (callback_id = 0; callback_id < ARRAY_SIZE(outputs_cb_register); callback_id++)
    {
      if (outputs_cb_register[callback_id].device == device)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Removing callback to %s, id %d\n", player_pmap(outputs_cb_register[callback_id].cb), callback_id);
	  memset(&outputs_cb_register[callback_id], 0, sizeof(struct outputs_callback_register));
	}
    }
}

static int
callback_add(struct output_device *device, output_status_cb cb)
{
  int callback_id;

  if (!cb)
    return -1;

  // We will replace any previously registered callbacks, since that's what the
  // player expects
  callback_remove(device);

  // Find a free slot in the queue
  for (callback_id = 0; callback_id < ARRAY_SIZE(outputs_cb_register); callback_id++)
    {
      if (outputs_cb_register[callback_id].cb == NULL)
	break;
    }

  if (callback_id == ARRAY_SIZE(outputs_cb_register))
    {
      DPRINTF(E_LOG, L_PLAYER, "Output callback queue is full! (size is %d)\n", OUTPUTS_MAX_CALLBACKS);
      return -1;
    }

  outputs_cb_register[callback_id].cb = cb;
  outputs_cb_register[callback_id].device = device; // Don't dereference this later, it might become invalid!

  DPRINTF(E_DBG, L_PLAYER, "Registered callback to %s with id %d (device %p, %s)\n", player_pmap(cb), callback_id, device, device->name);

  int active = 0;
  for (int i = 0; i < ARRAY_SIZE(outputs_cb_register); i++)
    if (outputs_cb_register[i].cb)
      active++;

  DPRINTF(E_DBG, L_PLAYER, "Number of active callbacks: %d\n", active);

  return callback_id;
};

static void
deferred_cb(int fd, short what, void *arg)
{
  struct output_device *device;
  output_status_cb cb;
  enum output_device_state state;
  int callback_id;

  for (callback_id = 0; callback_id < ARRAY_SIZE(outputs_cb_register); callback_id++)
    {
      if (outputs_cb_register[callback_id].ready)
	{
	  // Must copy before making callback, since you never know what the
	  // callback might result in (could call back in)
	  cb = outputs_cb_register[callback_id].cb;
	  state = outputs_cb_register[callback_id].state;

	  // Will be NULL if the device has disappeared
	  device = outputs_device_get(outputs_cb_register[callback_id].device_id);

	  memset(&outputs_cb_register[callback_id], 0, sizeof(struct outputs_callback_register));

	  // The device has left the building (stopped/failed), and the backend
	  // is not using it any more
	  if (!device->advertised && !device->session)
	    {
	      outputs_device_remove(device);
	      device = NULL;
	    }

	  DPRINTF(E_DBG, L_PLAYER, "Making deferred callback to %s, id was %d\n", player_pmap(cb), callback_id);

	  cb(device, state);
	}
    }

  for (int i = 0; i < ARRAY_SIZE(outputs_cb_register); i++)
    {
      if (outputs_cb_register[i].cb)
	DPRINTF(E_DBG, L_PLAYER, "%d. Active callback: %s\n", i, player_pmap(outputs_cb_register[i].cb));
    }
}

static void
stop_timer_cb(int fd, short what, void *arg)
{
  struct output_device *device = arg;
  output_status_cb cb = callback_get(device);

  outputs_device_stop(device, cb);
}

static void
device_stop_cb(struct output_device *device, enum output_device_state status)
{
  if (status == OUTPUT_STATE_FAILED)
    DPRINTF(E_WARN, L_PLAYER, "Failed to stop device\n");
  else
    DPRINTF(E_INFO, L_PLAYER, "Device stopped properly\n");
}

static enum transcode_profile
quality_to_xcode(struct media_quality *quality)
{
  if (quality->sample_rate == 44100 && quality->bits_per_sample == 16)
    return XCODE_PCM16_44100;
  if (quality->sample_rate == 44100 && quality->bits_per_sample == 24)
    return XCODE_PCM24_44100;
  if (quality->sample_rate == 44100 && quality->bits_per_sample == 32)
    return XCODE_PCM32_44100;
  if (quality->sample_rate == 48000 && quality->bits_per_sample == 16)
    return XCODE_PCM16_48000;
  if (quality->sample_rate == 48000 && quality->bits_per_sample == 24)
    return XCODE_PCM24_48000;
  if (quality->sample_rate == 48000 && quality->bits_per_sample == 32)
    return XCODE_PCM32_48000;
  if (quality->sample_rate == 96000 && quality->bits_per_sample == 16)
    return XCODE_PCM16_96000;
  if (quality->sample_rate == 96000 && quality->bits_per_sample == 24)
    return XCODE_PCM24_96000;
  if (quality->sample_rate == 96000 && quality->bits_per_sample == 32)
    return XCODE_PCM32_96000;

  return XCODE_UNKNOWN;
}

static int
encoding_reset(struct media_quality *quality)
{
  struct output_quality_subscription *subscription;
  struct decode_ctx *decode_ctx;
  enum transcode_profile profile;
  int i;

  profile = quality_to_xcode(quality);
  if  (profile == XCODE_UNKNOWN)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create subscription decoding context, invalid quality (%d/%d/%d)\n",
	quality->sample_rate, quality->bits_per_sample, quality->channels);
      return -1;
    }

  decode_ctx = transcode_decode_setup_raw(profile);
  if (!decode_ctx)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create subscription decoding context (profile %d)\n", profile);
      return -1;
    }

  for (i = 0; output_quality_subscriptions[i].count > 0; i++)
    {
      subscription = &output_quality_subscriptions[i]; // Just for short-hand

      transcode_encode_cleanup(&subscription->encode_ctx); // Will also point the ctx to NULL

      if (quality_is_equal(quality, &subscription->quality))
	continue; // No resampling required

      profile = quality_to_xcode(&subscription->quality);
      if (profile != XCODE_UNKNOWN)
	subscription->encode_ctx = transcode_encode_setup(profile, decode_ctx, NULL, 0, 0);
      else
	DPRINTF(E_LOG, L_PLAYER, "Could not setup resampling to %d/%d/%d for output\n",
	  subscription->quality.sample_rate, subscription->quality.bits_per_sample, subscription->quality.channels);
    }

  transcode_decode_cleanup(&decode_ctx);

  return 0;
}

static void
buffer_fill(struct output_buffer *obuf, void *buf, size_t bufsize, struct media_quality *quality, int nsamples, struct timespec *pts)
{
  transcode_frame *frame;
  int ret;
  int i;
  int n;

  obuf->write_counter++;
  obuf->pts = *pts;

  // The resampling/encoding (transcode) contexts work for a given input quality,
  // so if the quality changes we need to reset the contexts. We also do that if
  // we have received a subscription for a new quality.
  if (!quality_is_equal(quality, &obuf->data[0].quality) || outputs_got_new_subscription)
    {
      encoding_reset(quality);
      outputs_got_new_subscription = false;
    }

  // The first element of the output_buffer is always just the raw input data
  // TODO can we avoid the copy below? we can't use evbuffer_add_buffer_reference,
  // because then the outputs can't use it and we would need to copy there instead
  evbuffer_add(obuf->data[0].evbuf, buf, bufsize);
  obuf->data[0].buffer = buf;
  obuf->data[0].bufsize = bufsize;
  obuf->data[0].quality = *quality;
  obuf->data[0].samples = nsamples;

  for (i = 0, n = 1; output_quality_subscriptions[i].count > 0; i++)
    {
      if (quality_is_equal(&output_quality_subscriptions[i].quality, quality))
	continue; // Skip, no resampling required and we have the data in element 0

      if (!output_quality_subscriptions[i].encode_ctx)
	continue;

      frame = transcode_frame_new(buf, bufsize, nsamples, quality->sample_rate, quality->bits_per_sample);
      if (!frame)
	continue;

      ret = transcode_encode(obuf->data[n].evbuf, output_quality_subscriptions[i].encode_ctx, frame, 0);
      transcode_frame_free(frame);
      if (ret < 0)
	continue;

      obuf->data[n].buffer  = evbuffer_pullup(obuf->data[n].evbuf, -1);
      obuf->data[n].bufsize = evbuffer_get_length(obuf->data[n].evbuf);
      obuf->data[n].quality = output_quality_subscriptions[i].quality;
      obuf->data[n].samples = BTOS(obuf->data[n].bufsize, obuf->data[n].quality.bits_per_sample, obuf->data[n].quality.channels);
      n++;
    }
}

static void
buffer_drain(struct output_buffer *obuf)
{
  int i;

  for (i = 0; obuf->data[i].buffer; i++)
    {
      evbuffer_drain(obuf->data[i].evbuf, obuf->data[i].bufsize);
      obuf->data[i].buffer  = NULL;
      obuf->data[i].bufsize = 0;
      // We don't reset quality and samples, would be a waste of time
    }
}

static void
device_list_sort(void)
{
  struct output_device *device;
  struct output_device *next;
  struct output_device *prev;
  int swaps;

  // Swap sorting since even the most inefficient sorting should do fine here
  do
    {
      swaps = 0;
      prev = NULL;
      for (device = output_device_list; device && device->next; device = device->next)
	{
	  next = device->next;
	  if ( (outputs_priority(device) > outputs_priority(next)) ||
	       (outputs_priority(device) == outputs_priority(next) && strcasecmp(device->name, next->name) > 0) )
	    {
	      if (device == output_device_list)
		output_device_list = next;
	      if (prev)
		prev->next = next;

	      device->next = next->next;
	      next->next = device;
	      swaps++;
	    }
	  prev = device;
	}
    }
  while (swaps > 0);
}


/* ----------------------------------- API ---------------------------------- */

struct output_device *
outputs_device_get(uint64_t device_id)
{
  struct output_device *device;

  for (device = output_device_list; device; device = device->next)
    {
      if (device_id == device->id)
	return device;
    }

  DPRINTF(E_LOG, L_PLAYER, "Output device with id %" PRIu64 " has disappeared from our list\n", device_id);
  return NULL;
}

/* ----------------------- Called by backend modules ------------------------ */

// Sessions free their sessions themselves, but should not touch the device,
// since they can't know for sure that it is still valid in memory
int
outputs_device_session_add(uint64_t device_id, void *session)
{
  struct output_device *device;

  device = outputs_device_get(device_id);
  if (!device)
    return -1;

  device->session = session;
  return 0;
}

void
outputs_device_session_remove(uint64_t device_id)
{
  struct output_device *device;

  device = outputs_device_get(device_id);
  if (device)
    device->session = NULL;

  return;
}

int
outputs_quality_subscribe(struct media_quality *quality)
{
  int i;

  // If someone else is already subscribing to this quality we just increase the
  // reference count.
  for (i = 0; output_quality_subscriptions[i].count > 0; i++)
    {
      if (!quality_is_equal(quality, &output_quality_subscriptions[i].quality))
	continue;

      output_quality_subscriptions[i].count++;

      DPRINTF(E_DBG, L_PLAYER, "Subscription request for quality %d/%d/%d (now %d subscribers)\n",
	quality->sample_rate, quality->bits_per_sample, quality->channels, output_quality_subscriptions[i].count);

      return 0;
    }

  if (i >= (ARRAY_SIZE(output_quality_subscriptions) - 1))
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! The number of different quality levels requested by outputs is too high\n");
      return -1;
    }

  output_quality_subscriptions[i].quality = *quality;
  output_quality_subscriptions[i].count++;

  DPRINTF(E_DBG, L_PLAYER, "Subscription request for quality %d/%d/%d (now %d subscribers)\n",
    quality->sample_rate, quality->bits_per_sample, quality->channels, output_quality_subscriptions[i].count);

  // Better way of signaling this?
  outputs_got_new_subscription = true;

  return 0;
}

void
outputs_quality_unsubscribe(struct media_quality *quality)
{
  int i;

  // Find subscription
  for (i = 0; output_quality_subscriptions[i].count > 0; i++)
    {
      if (quality_is_equal(quality, &output_quality_subscriptions[i].quality))
	break;
    }

  if (output_quality_subscriptions[i].count == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Unsubscription request for a quality level that there is no subscription for\n");
      return;
    }

  output_quality_subscriptions[i].count--;

  DPRINTF(E_DBG, L_PLAYER, "Unsubscription request for quality %d/%d/%d (now %d subscribers)\n",
    quality->sample_rate, quality->bits_per_sample, quality->channels, output_quality_subscriptions[i].count);

  if (output_quality_subscriptions[i].count > 0)
    return;

  transcode_encode_cleanup(&output_quality_subscriptions[i].encode_ctx);

  // Shift elements
  for (; i < ARRAY_SIZE(output_quality_subscriptions) - 1; i++)
    output_quality_subscriptions[i] = output_quality_subscriptions[i + 1];
}

// Output backends call back through the below wrapper to make sure that:
// 1. Callbacks are always deferred
// 2. The callback never has a dangling pointer to a device (a device that has been removed from our list)
void
outputs_cb(int callback_id, uint64_t device_id, enum output_device_state state)
{
  if (callback_id < 0)
    return;

  if (!(callback_id < ARRAY_SIZE(outputs_cb_register)) || !outputs_cb_register[callback_id].cb)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Output backend called us with an illegal callback id (%d)\n", callback_id);
      return;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback request received, id is %i\n", callback_id);

  outputs_cb_register[callback_id].ready = true;
  outputs_cb_register[callback_id].device_id = device_id;
  outputs_cb_register[callback_id].state = state;
  event_active(outputs_deferredev, 0, 0);
}

// Maybe not so great, seems it would be better if integrated into the callback
// mechanism so that the notifications where at least deferred
void
outputs_listener_notify(void)
{
  listener_notify(LISTENER_SPEAKER);
}


/* ---------------------------- Called by player ---------------------------- */

struct output_device *
outputs_device_add(struct output_device *add, bool new_deselect, int default_volume)
{
  struct output_device *device;
  char *keep_name;
  int ret;

  for (device = output_device_list; device; device = device->next)
    {
      if (device->id == add->id)
	break;
    }

  // New device
  if (!device)
    {
      device = add;

      device->stop_timer = evtimer_new(evbase_player, stop_timer_cb, device);

      keep_name = strdup(device->name);
      ret = db_speaker_get(device, device->id);
      if (ret < 0)
	{
	  device->selected = 0;
	  device->volume = default_volume;
	}

      free(device->name);
      device->name = keep_name;

      if (new_deselect)
	device->selected = 0;

      device->next = output_device_list;
      output_device_list = device;
    }
  // Update to a device already in the list
  else
    {
      if (add->v4_address)
	{
	  free(device->v4_address);

	  device->v4_address = add->v4_address;
	  device->v4_port = add->v4_port;

	  // Address is ours now
	  add->v4_address = NULL;
	}

      if (add->v6_address)
	{
	  free(device->v6_address);

	  device->v6_address = add->v6_address;
	  device->v6_port = add->v6_port;

	  // Address is ours now
	  add->v6_address = NULL;
	}

      free(device->name);
      device->name = add->name;
      add->name = NULL;

      device->has_password = add->has_password;
      device->password = add->password;

      outputs_device_free(add);
    }

  device_list_sort();

  device->advertised = 1;

  listener_notify(LISTENER_SPEAKER);

  return device;
}

void
outputs_device_remove(struct output_device *remove)
{
  struct output_device *device;
  struct output_device *prev;
  int ret;

  // Device stop should be able to handle that we invalidate the device, even
  // if it is an async stop. It might call outputs_device_session_remove(), but
  // that just won't do anything since the id will be unknown.
  if (remove->session)
    outputs_device_stop(remove, device_stop_cb);

  prev = NULL;
  for (device = output_device_list; device; device = device->next)
    {
      if (device == remove)
	break;

      prev = device;
    }

  if (!device)
    return;

  // Save device volume
  ret = db_speaker_save(remove);
  if (ret < 0)
    DPRINTF(E_LOG, L_PLAYER, "Could not save state for %s device '%s'\n", remove->type_name, remove->name);

  DPRINTF(E_INFO, L_PLAYER, "Removing %s device '%s'; stopped advertising\n", remove->type_name, remove->name);

  if (!prev)
    output_device_list = remove->next;
  else
    prev->next = remove->next;

  outputs_device_free(remove);

  listener_notify(LISTENER_SPEAKER);
}

int
outputs_device_start(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_start)
    return -1;

  if (device->session)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! outputs_device_start() called for a device that already has a session\n");
      return -1;
    }

  return outputs[device->type]->device_start(device, callback_add(device, cb));
}

int
outputs_device_stop(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_stop)
    return -1;

  if (!device->session)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! outputs_device_stop() called for a device that has no session\n");
      return -1;
    }

  return outputs[device->type]->device_stop(device, callback_add(device, cb));
}

int
outputs_device_stop_delayed(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_stop)
    return -1;

  outputs[device->type]->device_cb_set(device, callback_add(device, cb));

  event_add(device->stop_timer, &outputs_stop_timeout);

  return 0;
}

int
outputs_device_flush(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_flush)
    return -1;

  if (!device->session)
    return -1;

  return outputs[device->type]->device_flush(device, callback_add(device, cb));
}

int
outputs_device_probe(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_probe)
    return -1;

  if (device->session)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! outputs_device_probe() called for a device that already has a session\n");
      return -1;
    }

  return outputs[device->type]->device_probe(device, callback_add(device, cb));
}

int
outputs_device_volume_set(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_volume_set)
    return -1;

  return outputs[device->type]->device_volume_set(device, callback_add(device, cb));
}

int
outputs_device_volume_to_pct(struct output_device *device, const char *volume)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_volume_to_pct)
    return -1;

  return outputs[device->type]->device_volume_to_pct(device, volume);
}

int
outputs_device_quality_set(struct output_device *device, struct media_quality *quality, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_quality_set)
    return -1;

  return outputs[device->type]->device_quality_set(device, quality, callback_add(device, cb));
}

void
outputs_device_cb_set(struct output_device *device, output_status_cb cb)
{
  if (outputs[device->type]->disabled || !outputs[device->type]->device_cb_set)
    return;

  if (!device->session)
    return;

  outputs[device->type]->device_cb_set(device, callback_add(device, cb));
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

  if (device->stop_timer)
    event_free(device->stop_timer);

  free(device->name);
  free(device->auth_key);
  free(device->v4_address);
  free(device->v6_address);

  free(device);
}

int
outputs_flush(output_status_cb cb)
{
  struct output_device *device;
  int count = 0;
  int ret;

  for (device = output_device_list; device; device = device->next)
    {
      ret = outputs_device_flush(device, cb);
      if (ret < 0)
	continue;

      count++;
    }

  return count;
}

int
outputs_stop(output_status_cb cb)
{
  struct output_device *device;
  int count = 0;
  int ret;

  for (device = output_device_list; device; device = device->next)
    {
      if (!device->session)
	continue;

      ret = outputs_device_stop(device, cb);
      if (ret < 0)
	continue;

      count++;
    }

  return count;
}

int
outputs_stop_delayed_cancel(void)
{
  struct output_device *device;

  for (device = output_device_list; device; device = device->next)
    event_del(device->stop_timer);

  return 0;
}

void
outputs_write(void *buf, size_t bufsize, int nsamples, struct media_quality *quality, struct timespec *pts)
{
  int i;

  buffer_fill(&output_buffer, buf, bufsize, quality, nsamples, pts);

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->write)
	outputs[i]->write(&output_buffer);
    }

  buffer_drain(&output_buffer);
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

  CHECK_NULL(L_PLAYER, outputs_deferredev = evtimer_new(evbase_player, deferred_cb, NULL));

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

  for (i = 0; i < ARRAY_SIZE(output_buffer.data); i++)
    output_buffer.data[i].evbuf = evbuffer_new();

  return 0;
}

void
outputs_deinit(void)
{
  int i;

  evtimer_del(outputs_deferredev);

  for (i = 0; outputs[i]; i++)
    {
      if (outputs[i]->disabled)
	continue;

      if (outputs[i]->deinit)
        outputs[i]->deinit();
    }

  // In case some outputs forgot to unsubscribe
  for (i = 0; i < ARRAY_SIZE(output_quality_subscriptions); i++)
    if (output_quality_subscriptions[i].count > 0)
      {
	transcode_encode_cleanup(&output_quality_subscriptions[i].encode_ctx);
	memset(&output_quality_subscriptions[i], 0, sizeof(struct output_quality_subscription));
      }

  for (i = 0; i < ARRAY_SIZE(output_buffer.data); i++)
    evbuffer_free(output_buffer.data[i].evbuf);
}

