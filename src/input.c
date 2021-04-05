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
#include "conffile.h"
#include "commands.h"
#include "input.h"

// Disallow further writes to the buffer when its size exceeds this threshold.
// The below gives us room to buffer 2 seconds of 48000/16/2 audio.
#define INPUT_BUFFER_THRESHOLD STOB(96000, 16, 2)
// How long (in nsec) to wait when the input buffer is full before looping
#define INPUT_LOOP_TIMEOUT_NSEC 10000000
// How long (in sec) to keep an input open without the player reading from it
#define INPUT_OPEN_TIMEOUT 600

//#define DEBUG_INPUT 1
// For testing http stream underruns
//#define DEBUG_UNDERRUN 1

extern struct input_definition input_file;
extern struct input_definition input_http;
extern struct input_definition input_pipe;
extern struct input_definition input_timer;
#ifdef SPOTIFY
extern struct input_definition input_spotify;
#endif

// Must be in sync with enum input_types
static struct input_definition *inputs[] = {
    &input_file,
    &input_http,
    &input_pipe,
    &input_timer,
#ifdef SPOTIFY
    &input_spotify,
#endif
    NULL
};

struct marker
{
  // Position of marker measured in bytes
  uint64_t pos;

  // Type of marker
  enum input_flags flag;

  // Data associated with the marker, e.g. quality or metadata struct
  void *data;

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
};

/* --- Globals --- */
// Input thread
static pthread_t tid_input;

// Event base, cmdbase and event we use to iterate in the playback loop
static struct event_base *evbase_input;
static struct commands_base *cmdbase;
static struct event *input_ev;
static bool input_initialized;

// The source we are reading now
static struct input_source input_now_reading;

// Input buffer
static struct input_buffer input_buffer;

// Timeout waiting in playback loop
static struct timespec input_loop_timeout = { 0, INPUT_LOOP_TIMEOUT_NSEC };

// Timeout waiting for player read
static struct timeval input_open_timeout = { INPUT_OPEN_TIMEOUT, 0 };
static struct event *input_open_timeout_ev;

#ifdef DEBUG_INPUT
static size_t debug_elapsed;
#endif
#ifdef DEBUG_UNDERRUN
int debug_underrun_trigger;
#endif


/* ------------------------------- MISC HELPERS ----------------------------- */

static int
map_data_kind(int data_kind)
{
  // Test mode - ignores the actual source and just plays a signal with clicks
  if (cfg_getbool(cfg_getsec(cfg, "general"), "timer_test"))
    return INPUT_TYPE_TIMER;

  switch (data_kind)
    {
      case DATA_KIND_FILE:
	return INPUT_TYPE_FILE;

      case DATA_KIND_HTTP:
	return INPUT_TYPE_HTTP;

      case DATA_KIND_PIPE:
	return INPUT_TYPE_PIPE;

#ifdef SPOTIFY
      case DATA_KIND_SPOTIFY:
	return INPUT_TYPE_SPOTIFY;
#endif

      default:
	return -1;
    }
}

static void
metadata_free(struct input_metadata *metadata, int content_only)
{
  if (!metadata)
    return;

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

static struct input_metadata *
metadata_get(struct input_source *source)
{
  struct input_metadata *metadata;
  int ret;

  if (!inputs[source->type]->metadata_get)
    return NULL;

  metadata = calloc(1, sizeof(struct input_metadata));

  ret = inputs[source->type]->metadata_get(metadata, source);
  if (ret < 0)
    goto out_free_metadata;

  metadata->item_id = source->item_id;

  return metadata;

 out_free_metadata:
  metadata_free(metadata, 0);
  return NULL;
}

static void
marker_free(struct marker *marker)
{
  if (!marker)
    return;

  if (marker->flag == INPUT_FLAG_METADATA && marker->data)
    metadata_free(marker->data, 0);

  if (marker->flag == INPUT_FLAG_QUALITY && marker->data)
    free(marker->data);

  free(marker);
}

static void
marker_add(size_t pos, short flag, void *flagdata)
{
  struct marker *insert;
  struct marker *compare;
  struct marker *marker;

  CHECK_NULL(L_PLAYER, marker = calloc(1, sizeof(struct marker)));

  marker->pos = pos;
  marker->flag = flag;
  marker->data = flagdata;

  // We want the list to be ordered by pos, so we reverse through it and compare
  // each element with pos. Only if the element's pos is less than or equal to
  // pos do we keep reversing. If no reversing is possible then we insert as new
  // tail.
  insert = NULL;
  compare = input_buffer.marker_tail;

  while (compare && compare->pos <= pos)
    {
      insert = compare;
      compare = compare->prev;
    }

  if (insert)
    {
      marker->prev = insert->prev;
      insert->prev = marker;
    }
  else
    {
      marker->prev = input_buffer.marker_tail;
      input_buffer.marker_tail = marker;
    }
}

static void
markers_set(short flags, size_t write_size)
{
  struct media_quality *quality;
  struct input_metadata *metadata;

  if (flags & INPUT_FLAG_QUALITY)
    {
      quality = malloc(sizeof(struct media_quality));
      *quality = input_buffer.cur_write_quality;
      marker_add(input_buffer.bytes_written - write_size, INPUT_FLAG_QUALITY, quality);
    }

  if (flags & (INPUT_FLAG_EOF | INPUT_FLAG_ERROR))
    {
      // This controls when the player will open the next track in the queue
      if (input_buffer.bytes_read + INPUT_BUFFER_THRESHOLD < input_buffer.bytes_written)
	// The player's read is behind, tell it to open when it reaches where
	// we are minus the buffer size
	marker_add(input_buffer.bytes_written - INPUT_BUFFER_THRESHOLD, INPUT_FLAG_START_NEXT, NULL);
      else
	// The player's read is close to our write, so open right away
	marker_add(input_buffer.bytes_read, INPUT_FLAG_START_NEXT, NULL);

      marker_add(input_buffer.bytes_written, flags & (INPUT_FLAG_EOF | INPUT_FLAG_ERROR), NULL);
    }

  if (flags & INPUT_FLAG_METADATA)
    {
      metadata = metadata_get(&input_now_reading);
      if (metadata)
	marker_add(input_buffer.bytes_written, INPUT_FLAG_METADATA, metadata);
    }
}

static inline void
buffer_full_cb(void)
{
  if (!input_buffer.full_cb)
    return;

  input_buffer.full_cb();
  input_buffer.full_cb = NULL;
}


/* ------------------------- INPUT SOURCE HANDLING -------------------------- */

static void
clear(struct input_source *source)
{
  free(source->path);
  memset(source, 0, sizeof(struct input_source));
}

static void
flush(short *flagptr)
{
  struct marker *marker;
  short flags;
  size_t len;

  pthread_mutex_lock(&input_buffer.mutex);

  // We will return an OR of all the unread marker flags
  flags = 0;
  for (marker = input_buffer.marker_tail; marker; marker = input_buffer.marker_tail)
    {
      flags |= marker->flag;
      input_buffer.marker_tail = marker->prev;
      marker_free(marker);
    }

  len = evbuffer_get_length(input_buffer.evbuf);

  evbuffer_drain(input_buffer.evbuf, len);

  memset(&input_buffer.cur_read_quality, 0, sizeof(struct media_quality));
  memset(&input_buffer.cur_write_quality, 0, sizeof(struct media_quality));

  input_buffer.bytes_read = 0;
  input_buffer.bytes_written = 0;

  input_buffer.full_cb = NULL;

  pthread_mutex_unlock(&input_buffer.mutex);

#ifdef DEBUG_INPUT
  DPRINTF(E_DBG, L_PLAYER, "Flushing %zu bytes with flags %d\n", len, flags);
#endif

  if (flagptr)
    *flagptr = flags;
}

static void
stop(void)
{
  int type;

  event_del(input_open_timeout_ev);
  event_del(input_ev);

  type = input_now_reading.type;

  if (inputs[type]->stop && input_now_reading.open)
    inputs[type]->stop(&input_now_reading);

  flush(NULL);

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

  // Avoids memleaks in cases where stop() is not called
  clear(source);

  source->type       = type;
  source->data_kind  = queue_item->data_kind;
  source->media_kind = queue_item->media_kind;
  source->item_id    = queue_item->id;
  source->id         = queue_item->file_id;
  source->len_ms     = queue_item->song_length;
  source->path       = safe_strdup(queue_item->path);
  source->evbase     = evbase_input;

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
  else
    ret = 0;

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
  int ret;

  // If we are asked to start the item that is currently open we can just seek
  if (input_now_reading.open && cmdarg->item_id == input_now_reading.item_id)
    {
      flush(NULL);

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

  event_add(input_open_timeout_ev, &input_open_timeout);
  event_active(input_ev, 0, 0);

  *retval = ret; // Return is the seek result
  return COMMAND_END;

 error:
  input_write(NULL, NULL, INPUT_FLAG_ERROR);
  clear(&input_now_reading);
  *retval = -1;
  return COMMAND_END;
}

// Resume is a no-op if what we are reading now (or just finished reading, hence
// we don't check if input_now_reading.open is true) is the same item as
// requested. We also don't want to flush & seek in this case, since that has
// either already been done, or it is not desired because we just filled the
// buffer after an underrun.
static enum command_state
resume(void *arg, int *retval)
{
  struct input_arg *cmdarg = arg;

  if (cmdarg->item_id == input_now_reading.item_id)
    {
      DPRINTF(E_DBG, L_PLAYER, "Resuming input read loop for item '%s' (item id %" PRIu32 ")\n", input_now_reading.path, input_now_reading.item_id);
      *retval = cmdarg->seek_ms;
      return COMMAND_END;
    }

  return start(arg, retval);
}

static enum command_state
stop_cmd(void *arg, int *retval)
{
  stop();

  *retval = 0;
  return COMMAND_END;
}

static void
timeout_cb(int fd, short what, void *arg)
{
  if (input_buffer.bytes_read > 0)
    return;

  DPRINTF(E_WARN, L_PLAYER, "Timed out after %d sec without any reading from input source\n", INPUT_OPEN_TIMEOUT);

  stop();
}


/* ---------------------- Interface towards input backends ------------------ */
/*                           Thread: input and spotify                        */

// Called by input modules from within the playback loop
int
input_write(struct evbuffer *evbuf, struct media_quality *quality, short flags)
{
  bool read_end;
  size_t len;
  int ret;

  pthread_mutex_lock(&input_buffer.mutex);

  read_end = (flags & (INPUT_FLAG_EOF | INPUT_FLAG_ERROR));
  if (read_end)
    {
      buffer_full_cb();
      input_now_reading.open = false;
    }

  if ((evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD) && evbuf)
    {
      buffer_full_cb();

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
      flags |= INPUT_FLAG_QUALITY;
    }

  ret = 0;
  len = 0;
  if (evbuf)
    {
      len = evbuffer_get_length(evbuf);
#ifdef DEBUG_UNDERRUN
      // Starves the player so it underruns after a few minutes
      debug_underrun_trigger++;
      if (debug_underrun_trigger % 10 == 0)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Underrun debug mode: Dropping audio buffer length %zu\n", len);
	  evbuffer_drain(evbuf, len);
	  len = 0;
	}
#endif
      input_buffer.bytes_written += len;
      ret = evbuffer_add_buffer(input_buffer.evbuf, evbuf);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error adding stream data to input buffer, stopping\n");
	  input_stop();
	  flags |= INPUT_FLAG_ERROR;
	}
    }

  if (flags)
    markers_set(flags, len);

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

  pthread_mutex_unlock(&input_buffer.mutex);
  return 0;
}

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

static int
wait_buffer_ready(void)
{
  struct timespec ts;

  pthread_mutex_lock(&input_buffer.mutex);

  // Is the buffer full? Then wait for a read or for loop_timeout to elapse
  if (evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD)
    {
      buffer_full_cb();

      ts = timespec_reltoabs(input_loop_timeout);
      pthread_cond_timedwait(&input_buffer.cond, &input_buffer.mutex, &ts);

      if (evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD)
	{
	  pthread_mutex_unlock(&input_buffer.mutex);
	  return -1;
	}
    }

  pthread_mutex_unlock(&input_buffer.mutex);
  return 0;
}

static void
play(evutil_socket_t fd, short flags, void *arg)
{
  struct timeval tv = { 0, 0 };
  int ret;

  // Spotify runs in its own thread, so no reading is done by the input thread,
  // thus there is no reason to activate input_ev
  if (!inputs[input_now_reading.type]->play)
    return;

  // If the buffer is full we wait until either the player has consumed enough
  // data or INPUT_LOOP_TIMEOUT has elapsed (so we don't hang the input event
  // thread when the player doesn't consume data quickly). If the return is
  // negative then the buffer is still full, so we loop.
  ret = wait_buffer_ready();
  if (ret < 0)
    {
      event_add(input_ev, &tv);
      return;
    }

  // Return will be negative if there is an error or EOF. Here, we just don't
  // loop any more. input_write() will pass the message to the player.
  ret = inputs[input_now_reading.type]->play(&input_now_reading);
  if (ret < 0)
    {
      input_now_reading.open = false;
      return; // Error or EOF, so don't come back
    }

  event_add(input_ev, &tv);
}


/* ---------------------- Interface towards player thread ------------------- */
/*                                Thread: player                              */

int
input_read(void *data, size_t size, short *flag, void **flagdata)
{
  struct marker *marker;
  int len;

  *flag = 0;

  pthread_mutex_lock(&input_buffer.mutex);

  // First we check if there is a marker in the requested samples. If there is,
  // we only return data up until that marker. That way we don't have to deal
  // with multiple markers, and we don't return data that contains mixed sample
  // rates, bits per sample or an EOF in the middle.
  marker = input_buffer.marker_tail;
  if (marker && marker->pos <= input_buffer.bytes_read + size)
    {
      *flag = marker->flag;
      *flagdata = marker->data;

      size = marker->pos - input_buffer.bytes_read;
      input_buffer.marker_tail = marker->prev;
      free(marker);
    }

  len = evbuffer_remove(input_buffer.evbuf, data, size);
  if (len < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading stream data from input buffer\n");
      *flag = INPUT_FLAG_ERROR;
      goto out_unlock;
    }

  input_buffer.bytes_read += len;

#ifdef DEBUG_INPUT
  // Logs if flags present or each 10 seconds

  if (*flag & INPUT_FLAG_QUALITY)
    input_buffer.cur_read_quality = *((struct media_quality *)(*flagdata));

  size_t one_sec_size = STOB(input_buffer.cur_read_quality.sample_rate, input_buffer.cur_read_quality.bits_per_sample, input_buffer.cur_read_quality.channels);
  debug_elapsed += len;
  if (*flag || (debug_elapsed > 10 * one_sec_size))
    {
      debug_elapsed = 0;
      DPRINTF(E_DBG, L_PLAYER, "READ %zu bytes (%d/%d/%d), WROTE %zu bytes (%d/%d/%d), DIFF %zu, SIZE %zu/%d, FLAGS %04x\n",
        input_buffer.bytes_read,
        input_buffer.cur_read_quality.sample_rate,
        input_buffer.cur_read_quality.bits_per_sample,
        input_buffer.cur_read_quality.channels,
        input_buffer.bytes_written,
        input_buffer.cur_write_quality.sample_rate,
        input_buffer.cur_write_quality.bits_per_sample,
        input_buffer.cur_write_quality.channels,
        input_buffer.bytes_written - input_buffer.bytes_read,
        evbuffer_get_length(input_buffer.evbuf),
        INPUT_BUFFER_THRESHOLD,
        *flag);
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
input_resume(uint32_t item_id, int seek_ms)
{
  struct input_arg *cmdarg;

  CHECK_NULL(L_PLAYER, cmdarg = malloc(sizeof(struct input_arg)));

  cmdarg->item_id = item_id;
  cmdarg->seek_ms = seek_ms;

  commands_exec_async(cmdbase, resume, cmdarg);
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

static void
input_stop_sync(void)
{
  commands_exec_sync(cmdbase, stop_cmd, NULL, NULL);
}

void
input_flush(short *flags)
{
  // Flush should be thread safe
  flush(flags);
}

void
input_metadata_free(struct input_metadata *metadata, int content_only)
{
  metadata_free(metadata, content_only);
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
  CHECK_NULL(L_PLAYER, input_ev = event_new(evbase_input, -1, EV_PERSIST, play, NULL));
  CHECK_NULL(L_PLAYER, input_open_timeout_ev = evtimer_new(evbase_input, timeout_cb, NULL));

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
  event_free(input_open_timeout_ev);
  event_free(input_ev);
  evbuffer_free(input_buffer.evbuf);
  event_base_free(evbase_input);
  return -1;
}

void
input_deinit(void)
{
  int i;
  int ret;

  input_stop_sync();

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

  event_free(input_open_timeout_ev);
  event_free(input_ev);
  evbuffer_free(input_buffer.evbuf);
  event_base_free(evbase_input);
}

