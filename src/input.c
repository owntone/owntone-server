/*
 * Copyright (C) 2017 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <event2/buffer.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif

#include "misc.h"
#include "logger.h"
#include "commands.h"
#include "input.h"

// Disallow further writes to the buffer when its size exceeds this threshold.
// The below gives us room to buffer 2 seconds of 48000/16/2 audio.
#define INPUT_BUFFER_THRESHOLD STOB(96000, 16, 2)
// How long (in sec) to wait for player read before looping in playback thread
#define INPUT_LOOP_TIMEOUT 1

#define DEBUG 1 //TODO disable

extern struct input_definition input_file;
extern struct input_definition input_http;
extern struct input_definition input_pipe;
#ifdef HAVE_SPOTIFY_H
extern struct input_definition input_spotify;
#endif

// Must be in sync with enum input_types
static struct input_definition *inputs[] = {
    &input_file,
    &input_http,
    &input_pipe,
#ifdef HAVE_SPOTIFY_H
    &input_spotify,
#endif
    NULL
};

struct marker
{
  uint64_t pos; // Position of marker measured in bytes
  struct media_quality quality;
  enum input_flags flags;

  // Reverse linked list, yay!
  struct marker *prev;
};

struct input_buffer
{
  // Raw pcm stream data
  struct evbuffer *evbuf;

  // If an input makes a write with a flag or a changed sample rate etc, we add
  // a marker to head, and when we read we check from the tail to see if there
  // are updates to the player.
  struct marker *marker_tail;

  // Optional callback to player if buffer is full
  input_cb full_cb;

  // Quality of write/read data
  struct media_quality cur_write_quality;
  struct media_quality cur_read_quality;

  size_t bytes_written;
  size_t bytes_read;

  // Locks for sharing the buffer between input and player thread
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

struct input_arg
{
  uint32_t item_id;
  int seek_ms;
  struct input_metadata *metadata;
};

/* --- Globals --- */
// Input thread
static pthread_t tid_input;

// Event base, cmdbase and event we use to iterate in the playback loop
static struct event_base *evbase_input;
static struct commands_base *cmdbase;
static struct event *inputev;
static bool input_initialized;

// The source we are reading now
static struct input_source input_now_reading;

// Input buffer
static struct input_buffer input_buffer;

// Timeout waiting in playback loop
static struct timespec input_loop_timeout = { INPUT_LOOP_TIMEOUT, 0 };

#ifdef DEBUG
static size_t debug_elapsed;
#endif


/* ------------------------------- MISC HELPERS ----------------------------- */

static int
map_data_kind(int data_kind)
{
  switch (data_kind)
    {
      case DATA_KIND_FILE:
	return INPUT_TYPE_FILE;

      case DATA_KIND_HTTP:
	return INPUT_TYPE_HTTP;

      case DATA_KIND_PIPE:
	return INPUT_TYPE_PIPE;

#ifdef HAVE_SPOTIFY_H
      case DATA_KIND_SPOTIFY:
	return INPUT_TYPE_SPOTIFY;
#endif

      default:
	return -1;
    }
}

static void
marker_add(size_t pos, short flags)
{
  struct marker *head;
  struct marker *marker;

  CHECK_NULL(L_PLAYER, marker = calloc(1, sizeof(struct marker)));

  marker->pos = pos;
  marker->quality = input_buffer.cur_write_quality;
  marker->flags = flags;

  for (head = input_buffer.marker_tail; head && head->prev; head = head->prev)
    ; // Fast forward to the head

  if (!head)
    input_buffer.marker_tail = marker;
  else
    head->prev = marker;
}


/* ------------------------- INPUT SOURCE HANDLING -------------------------- */

static void
clear(struct input_source *source)
{
  free(source->path);
  memset(source, 0, sizeof(struct input_source));
}

static void
flush(short *flags)
{
  struct marker *marker;
  size_t len;

  pthread_mutex_lock(&input_buffer.mutex);

  // We will return an OR of all the unread marker flags
  *flags = 0;
  for (marker = input_buffer.marker_tail; marker; marker = input_buffer.marker_tail)
    {
      *flags |= marker->flags;
      input_buffer.marker_tail = marker->prev;
      free(marker);
    }

  len = evbuffer_get_length(input_buffer.evbuf);

  evbuffer_drain(input_buffer.evbuf, len);

  memset(&input_buffer.cur_read_quality, 0, sizeof(struct media_quality));
  memset(&input_buffer.cur_write_quality, 0, sizeof(struct media_quality));

  input_buffer.bytes_read = 0;
  input_buffer.bytes_written = 0;

  input_buffer.full_cb = NULL;

  pthread_mutex_unlock(&input_buffer.mutex);

#ifdef DEBUG
  DPRINTF(E_DBG, L_PLAYER, "Flushing %zu bytes with flags %d\n", len, *flags);
#endif
}

static void
stop(void)
{
  short flags;
  int type;

  event_del(inputev);

  type = input_now_reading.type;

  if (inputs[type]->stop && input_now_reading.open)
    inputs[type]->stop(&input_now_reading);

  flush(&flags);

  clear(&input_now_reading);
}

static int
seek(struct input_source *source, int seek_ms)
{
  if (inputs[source->type]->seek)
    return inputs[source->type]->seek(source, seek_ms);
  else
    return 0;
}

// On error returns -1, on success + seek given + seekable returns the position
// that the seek gave us, otherwise returns 0.
static int
setup(struct input_source *source, struct db_queue_item *queue_item, int seek_ms)
{
  int type;
  int ret;

  type = map_data_kind(queue_item->data_kind);
  if ((type < 0) || (inputs[type]->disabled))
    goto setup_error;

  source->type       = type;
  source->data_kind  = queue_item->data_kind;
  source->media_kind = queue_item->media_kind;
  source->item_id    = queue_item->id;
  source->id         = queue_item->file_id;
  source->len_ms     = queue_item->song_length;
  source->path       = safe_strdup(queue_item->path);

  DPRINTF(E_DBG, L_PLAYER, "Setting up input item '%s' (item id %" PRIu32 ")\n", source->path, source->item_id);

  if (inputs[type]->setup)
    {
      ret = inputs[type]->setup(source);
      if (ret < 0)
	goto setup_error;
    }

  source->open = true;

  if (seek_ms > 0)
    {
      ret = seek(source, seek_ms);
      if (ret < 0)
	goto seek_error;
    }

  return ret;

 seek_error:
  stop();
 setup_error:
  clear(source);
  return -1;
}

static enum command_state
start(void *arg, int *retval)
{
  struct input_arg *cmdarg = arg;
  struct db_queue_item *queue_item;
  short flags;
  int ret;

  // If we are asked to start the item that is currently open we can just seek
  if (input_now_reading.open && cmdarg->item_id == input_now_reading.item_id)
    {
      flush(&flags);

      ret = seek(&input_now_reading, cmdarg->seek_ms);
      if (ret < 0)
	DPRINTF(E_WARN, L_PLAYER, "Ignoring failed seek to %d ms in '%s'\n", cmdarg->seek_ms, input_now_reading.path);
    }
  else
    {
      if (input_now_reading.open)
	stop();

      // Get the queue_item from the db
      queue_item = db_queue_fetch_byitemid(cmdarg->item_id);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Input start was called with an item id that has disappeared (id=%d)\n", cmdarg->item_id);
	  goto error;
	}

      ret = setup(&input_now_reading, queue_item, cmdarg->seek_ms);
      free_queue_item(queue_item, 0);
      if (ret < 0)
	goto error;
    }

  DPRINTF(E_DBG, L_PLAYER, "Starting input read loop for item '%s' (item id %" PRIu32 "), seek %d\n",
    input_now_reading.path, input_now_reading.item_id, cmdarg->seek_ms);

  event_active(inputev, 0, 0);

  *retval = ret; // Return is the seek result
  return COMMAND_END;

 error:
  input_write(NULL, NULL, INPUT_FLAG_ERROR);
  clear(&input_now_reading);
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
stop_cmd(void *arg, int *retval)
{
  stop();

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
metadata_get(void *arg, int *retval)
{
  struct input_arg *cmdarg = arg;
  int type;

  if (!input_now_reading.open)
    {
      DPRINTF(E_WARN, L_PLAYER, "Source is no longer available for input_metadata_get()\n");
      goto error;
    }

  type = input_now_reading.type;
  if ((type < 0) || (inputs[type]->disabled))
    goto error;

  if (inputs[type]->metadata_get)
    *retval = inputs[type]->metadata_get(cmdarg->metadata, &input_now_reading);
  else
    *retval = 0;

  return COMMAND_END;

 error:
  *retval = -1;
  return COMMAND_END;
}


/* ---------------------- Interface towards input backends ------------------ */
/*                           Thread: input and spotify                        */

// Called by input modules from within the playback loop
int
input_write(struct evbuffer *evbuf, struct media_quality *quality, short flags)
{
  bool read_end;
  int ret;

  pthread_mutex_lock(&input_buffer.mutex);

  read_end = (flags & (INPUT_FLAG_EOF | INPUT_FLAG_ERROR));

  if ((evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD) && evbuf)
    {
      if (input_buffer.full_cb)
	{
	  input_buffer.full_cb();
	  input_buffer.full_cb = NULL;
	}

      // In case of EOF or error the input is always allowed to write, even if the
      // buffer is full. There is no point in holding back the input in that case.
      if (!read_end)
	{
	  pthread_mutex_unlock(&input_buffer.mutex);
	  return EAGAIN;
	}
    }

  if (quality && !quality_is_equal(quality, &input_buffer.cur_write_quality))
    {
      input_buffer.cur_write_quality = *quality;
      marker_add(input_buffer.bytes_written, INPUT_FLAG_QUALITY);
    }

  ret = 0;
  if (evbuf)
    {
      input_buffer.bytes_written += evbuffer_get_length(evbuf);
      ret = evbuffer_add_buffer(input_buffer.evbuf, evbuf);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error adding stream data to input buffer, stopping\n");
	  input_stop();
	  flags |= INPUT_FLAG_ERROR;
	}
    }

  if (flags)
    {
      if (read_end)
	{
	  input_now_reading.open = false;
	  // This controls when the player will open the next track in the queue
	  if (input_buffer.bytes_read + INPUT_BUFFER_THRESHOLD < input_buffer.bytes_written)
	    // The player's read is behind, tell it to open when it reaches where
	    // we are minus the buffer size
	    marker_add(input_buffer.bytes_written - INPUT_BUFFER_THRESHOLD, INPUT_FLAG_START_NEXT);
	  else
	    // The player's read is close to our write, so open right away
	    marker_add(input_buffer.bytes_read, INPUT_FLAG_START_NEXT);
	}

      // Note this marker is added at the post-write position, since EOF, error
      // and metadata belong there.
      marker_add(input_buffer.bytes_written, flags);
    }

  pthread_mutex_unlock(&input_buffer.mutex);

  return ret;
}

int
input_wait(void)
{
  struct timespec ts;

  pthread_mutex_lock(&input_buffer.mutex);

  ts = timespec_reltoabs(input_loop_timeout);
  pthread_cond_timedwait(&input_buffer.cond, &input_buffer.mutex, &ts);

  // Is the buffer full?
  if (evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD)
    {
      if (input_buffer.full_cb)
	{
	  input_buffer.full_cb();
	  input_buffer.full_cb = NULL;
	}

      pthread_mutex_unlock(&input_buffer.mutex);
      return -1;
    }

  pthread_mutex_unlock(&input_buffer.mutex);
  return 0;
}

/*void
input_next(void)
{
  commands_exec_async(cmdbase, next, NULL);
}*/

/* ---------------------------------- MAIN ---------------------------------- */
/*                                Thread: input                               */

static void *
input(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Error: DB init failed (input thread)\n");
      pthread_exit(NULL);
    }

  input_initialized = true;

  event_base_dispatch(evbase_input);

  if (input_initialized)
    {
      DPRINTF(E_LOG, L_MAIN, "Input event loop terminated ahead of time!\n");
      input_initialized = false;
    }

  db_perthread_deinit();

  pthread_exit(NULL);
}

static void
play(evutil_socket_t fd, short flags, void *arg)
{
  struct timeval tv = { 0, 0 };
  int ret;

  // Spotify runs in its own thread, so no reading is done by the input thread,
  // thus there is no reason to activate inputev
  if (!inputs[input_now_reading.type]->play)
    return;

  // Return will be negative if there is an error or EOF. Here, we just don't
  // loop any more. input_write() will pass the message to the player.
  ret = inputs[input_now_reading.type]->play(&input_now_reading);
  if (ret < 0)
    {
      input_now_reading.open = false;
      return; // Error or EOF, so don't come back
    }

  event_add(inputev, &tv);
}


/* ---------------------- Interface towards player thread ------------------- */
/*                                Thread: player                              */

int
input_read(void *data, size_t size, short *flags)
{
  struct marker *marker;
  int len;

  *flags = 0;

  pthread_mutex_lock(&input_buffer.mutex);

  // First we check if there is a marker in the requested samples. If there is,
  // we only return data up until that marker. That way we don't have to deal
  // with multiple markers, and we don't return data that contains mixed sample
  // rates, bits per sample or an EOF in the middle.
  marker = input_buffer.marker_tail;
  if (marker && marker->pos <= input_buffer.bytes_read + size)
    {
      *flags = marker->flags;
      if (*flags & INPUT_FLAG_QUALITY)
	input_buffer.cur_read_quality = marker->quality;

      size = marker->pos - input_buffer.bytes_read;
      input_buffer.marker_tail = marker->prev;
      free(marker);
    }

  len = evbuffer_remove(input_buffer.evbuf, data, size);
  if (len < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading stream data from input buffer\n");
      *flags |= INPUT_FLAG_ERROR;
      goto out_unlock;
    }

  input_buffer.bytes_read += len;

#ifdef DEBUG
  // Logs if flags present or each 10 seconds
  size_t one_sec_size = STOB(input_buffer.cur_read_quality.sample_rate, input_buffer.cur_read_quality.bits_per_sample, input_buffer.cur_read_quality.channels);
  debug_elapsed += len;
  if (*flags || (debug_elapsed > 10 * one_sec_size))
    {
      debug_elapsed = 0;
      DPRINTF(E_SPAM, L_PLAYER, "READ %zu bytes (%d/%d/%d), WROTE %zu bytes (%d/%d/%d), SIZE %zu (=%zu), FLAGS %04x\n",
        input_buffer.bytes_read,
        input_buffer.cur_read_quality.sample_rate,
        input_buffer.cur_read_quality.bits_per_sample,
        input_buffer.cur_read_quality.channels,
        input_buffer.bytes_written,
        input_buffer.cur_write_quality.sample_rate,
        input_buffer.cur_write_quality.bits_per_sample,
        input_buffer.cur_write_quality.channels,
        evbuffer_get_length(input_buffer.evbuf),
        input_buffer.bytes_written - input_buffer.bytes_read,
        *flags);
    }
#endif

 out_unlock:
  pthread_cond_signal(&input_buffer.cond);
  pthread_mutex_unlock(&input_buffer.mutex);

  return len;
}

void
input_buffer_full_cb(input_cb cb)
{
  pthread_mutex_lock(&input_buffer.mutex);
  input_buffer.full_cb = cb;

  pthread_mutex_unlock(&input_buffer.mutex);
}

int
input_seek(uint32_t item_id, int seek_ms)
{
  struct input_arg cmdarg;

  cmdarg.item_id = item_id;
  cmdarg.seek_ms = seek_ms;

  return commands_exec_sync(cmdbase, start, NULL, &cmdarg);
}

void
input_start(uint32_t item_id)
{
  struct input_arg *cmdarg;

  CHECK_NULL(L_PLAYER, cmdarg = malloc(sizeof(struct input_arg)));

  cmdarg->item_id = item_id;
  cmdarg->seek_ms = 0;

  commands_exec_async(cmdbase, start, cmdarg);
}

void
input_stop(void)
{
  commands_exec_async(cmdbase, stop_cmd, NULL);
}

void
input_flush(short *flags)
{
  // Flush should be thread safe
  flush(flags);
}

int
input_quality_get(struct media_quality *quality)
{
  // No mutex, other threads should not be able to affect cur_read_quality
  *quality = input_buffer.cur_read_quality;
  return 0;
}

int
input_metadata_get(struct input_metadata *metadata)
{
  struct input_arg cmdarg;

  cmdarg.metadata = metadata;

  return commands_exec_sync(cmdbase, metadata_get, NULL, &cmdarg);
}

void
input_metadata_free(struct input_metadata *metadata, int content_only)
{
  free(metadata->artist);
  free(metadata->title);
  free(metadata->album);
  free(metadata->genre);
  free(metadata->artwork_url);

  if (!content_only)
    free(metadata);
  else
    memset(metadata, 0, sizeof(struct input_metadata));
}

int
input_init(void)
{
  int no_input;
  int ret;
  int i;

  // Prepare input buffer
  pthread_mutex_init(&input_buffer.mutex, NULL);
  pthread_cond_init(&input_buffer.cond, NULL);

  CHECK_NULL(L_PLAYER, evbase_input = event_base_new());
  CHECK_NULL(L_PLAYER, input_buffer.evbuf = evbuffer_new());
  CHECK_NULL(L_PLAYER, inputev = event_new(evbase_input, -1, EV_PERSIST, play, NULL));

  no_input = 1;
  for (i = 0; inputs[i]; i++)
    {
      if (inputs[i]->type != i)
	{
	  DPRINTF(E_FATAL, L_PLAYER, "BUG! Input definitions are misaligned with input enum\n");
	  goto input_fail;
	}

      if (!inputs[i]->init)
	{
	  no_input = 0;
	  continue;
	}

      ret = inputs[i]->init();
      if (ret < 0)
	inputs[i]->disabled = 1;
      else
	no_input = 0;
    }

  if (no_input)
    goto input_fail;

  cmdbase = commands_base_new(evbase_input, NULL);

  ret = pthread_create(&tid_input, NULL, input, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not spawn input thread: %s\n", strerror(errno));
      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_input, "input");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_input, "input");
#endif

  return 0;

 thread_fail:
  commands_base_free(cmdbase);
 input_fail:
  event_free(inputev);
  evbuffer_free(input_buffer.evbuf);
  event_base_free(evbase_input);
  return -1;
}

void
input_deinit(void)
{
  int i;
  int ret;

// TODO ok to do from here?
  input_stop();

  for (i = 0; inputs[i]; i++)
    {
      if (inputs[i]->disabled)
	continue;

      if (inputs[i]->deinit)
        inputs[i]->deinit();
    }

  input_initialized = false;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_input, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not join input thread: %s\n", strerror(errno));
      return;
    }

  pthread_cond_destroy(&input_buffer.cond);
  pthread_mutex_destroy(&input_buffer.mutex);

  event_free(inputev);
  evbuffer_free(input_buffer.evbuf);
  event_base_free(evbase_input);
}

