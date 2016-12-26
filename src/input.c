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
#include "input.h"

// Disallow further writes to the buffer when its size is larger than this threshold
#define INPUT_BUFFER_THRESHOLD STOB(44100)

#define DEBUG 1 //TODO disable

extern struct input_definition input_file;
extern struct input_definition input_http;

// Must be in sync with enum input_types
static struct input_definition *inputs[] = {
    &input_file,
    &input_http,
    NULL
};

struct input_buffer
{
  // Raw pcm stream data
  struct evbuffer *evbuf;

  // If non-zero, remaining length of buffer until EOF
  size_t eof;
  // If non-zero, remaining length of buffer until (possible) new metadata
  size_t metadata;

  // Locks for sharing the buffer between input and player thread
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

/* --- Globals --- */
// Input thread
static pthread_t tid_input;

// Input buffer
static struct input_buffer input_buffer;

#ifdef DEBUG
static size_t debug_elapsed;
#endif


/* ------------------------------ MISC HELPERS ---------------------------- */

static short
flags_set(size_t len)
{
  short flags = 0;

  if (input_buffer.eof)
    {
      if (len >= input_buffer.eof)
	{
	  flags |= INPUT_FLAG_EOF;
	  input_buffer.eof = 0;
	}
      else
	input_buffer.eof -= len;
    }

  if (input_buffer.metadata)
    {
      if (len >= input_buffer.metadata)
	{
	  flags |= INPUT_FLAG_METADATA;
	  input_buffer.metadata = 0;
	}
      else
	input_buffer.metadata -= len;
    }

  return flags;
}

static int
map_data_kind(int data_kind)
{
  switch (data_kind)
    {
      case DATA_KIND_FILE:
	return INPUT_TYPE_FILE;

      case DATA_KIND_HTTP:
	return INPUT_TYPE_HTTP;

      default:
	return -1;
    }
}

static int
source_check_and_map(struct player_source *ps, const char *action, char check_setup)
{
  int type;

#ifdef DEBUG
  DPRINTF(E_DBG, L_PLAYER, "Action is %s\n", action);
#endif

  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream %s called with invalid player source\n", action);
      return -1;
    }

  if (check_setup && !ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup, %s not possible\n", action);
      return -1;
    }

  type = map_data_kind(ps->data_kind);
  if (type < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Unsupported input type, %s not possible\n", action);
      return -1;
    }

  return type;
}

/* ----------------------------- PLAYBACK LOOP ---------------------------- */
/*                               Thread: input                              */

// TODO Thread safety of ps? Do we need the return of the loop?
static void *
playback(void *arg)
{
  struct player_source *ps = arg;
  int type;

  type = source_check_and_map(ps, "start", 1);
  if ((type < 0) || (inputs[type]->disabled))
    goto thread_exit;

  // Loops until input_loop_break is set or no more input, e.g. EOF
  inputs[type]->start(ps);

#ifdef DEBUG
  DPRINTF(E_DBG, L_PLAYER, "Playback loop stopped (break is %d)\n", input_loop_break);
#endif

 thread_exit:
  pthread_exit(NULL);
}

// Called by input modules from within the playback loop
int
input_write(struct evbuffer *evbuf, short flags)
{
  int ret;

  pthread_mutex_lock(&input_buffer.mutex);

  while ( (!input_loop_break) && (evbuffer_get_length(input_buffer.evbuf) > INPUT_BUFFER_THRESHOLD) )
    {
      if (flags & INPUT_FLAG_NONBLOCK)
	{
	  pthread_mutex_unlock(&input_buffer.mutex);
	  return EAGAIN;
	}

      pthread_cond_wait(&input_buffer.cond, &input_buffer.mutex);

      // TODO protect against infinite looping and waiting?
    }

  if (!input_loop_break)
    {
      ret = evbuffer_add_buffer(input_buffer.evbuf, evbuf);
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Error adding stream data to input buffer\n");

      if (!input_buffer.eof && (flags & INPUT_FLAG_EOF))
	input_buffer.eof = evbuffer_get_length(input_buffer.evbuf);
      if (!input_buffer.metadata && (flags & INPUT_FLAG_METADATA))
	input_buffer.metadata = evbuffer_get_length(input_buffer.evbuf);
    }
  else
    ret = 0;

  pthread_mutex_unlock(&input_buffer.mutex);

  return ret;
}


/* -------------------- Interface towards player thread ------------------- */
/*                               Thread: player                             */

int
input_read(struct evbuffer *evbuf, size_t want, short *flags)
{
  int len;

  *flags = 0;

  pthread_mutex_lock(&input_buffer.mutex);

#ifdef DEBUG
  debug_elapsed += want;
  if (debug_elapsed > STOB(441000)) // 10 sec
    {
      DPRINTF(E_DBG, L_PLAYER, "Input buffer has %zu bytes\n", evbuffer_get_length(input_buffer.evbuf));
      debug_elapsed = 0;
    }
#endif

  len = evbuffer_remove_buffer(input_buffer.evbuf, evbuf, want);
  if (len < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading stream data from input buffer\n");
      goto out_unlock;
    }

  *flags = flags_set(len);

 out_unlock:
  pthread_cond_signal(&input_buffer.cond);
  pthread_mutex_unlock(&input_buffer.mutex);

  return len;
}

int
input_setup(struct player_source *ps)
{
  int type;

  type = source_check_and_map(ps, "setup", 0);
  if ((type < 0) || (inputs[type]->disabled))
    return -1;

  if (!inputs[type]->setup)
    return 0;

  return inputs[type]->setup(ps);
}

int
input_start(struct player_source *ps)
{
  int ret;

  if (tid_input)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Input start called, but playback already running\n");
      input_pause(ps);
    }

  input_loop_break = 0;

  ret = pthread_create(&tid_input, NULL, playback, ps);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not spawn input thread: %s\n", strerror(errno));
      return -1;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_input, "input");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_input, "input");
#endif

  return 0;
}

int
input_pause(struct player_source *ps)
{
  short flags;
  int ret;

#ifdef DEBUG
  DPRINTF(E_DBG, L_PLAYER, "Pause called, stopping playback loop\n");
#endif

  if (!tid_input)
    return -1;

  pthread_mutex_lock(&input_buffer.mutex);

  input_loop_break = 1;

  pthread_cond_signal(&input_buffer.cond);
  pthread_mutex_unlock(&input_buffer.mutex);

  ret = pthread_join(tid_input, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not join input thread: %s\n", strerror(errno));
      return -1;
    }

  tid_input = 0;

  input_flush(&flags);

  return 0;
}

int
input_stop(struct player_source *ps)
{
  int type;

  if (tid_input)
    input_pause(ps);

  if (!ps)
    return 0;

  type = source_check_and_map(ps, "stop", 1);
  if ((type < 0) || (inputs[type]->disabled))
    return -1;

  if (!inputs[type]->stop)
    return 0;

  return inputs[type]->stop(ps);
}

int
input_seek(struct player_source *ps, int seek_ms)
{
  int type;

  type = source_check_and_map(ps, "seek", 1);
  if ((type < 0) || (inputs[type]->disabled))
    return -1;

  if (!inputs[type]->seek)
    return 0;

  if (tid_input)
    input_pause(ps);

  return inputs[type]->seek(ps, seek_ms);
}

void
input_flush(short *flags)
{
  size_t len;

  pthread_mutex_lock(&input_buffer.mutex);

  len = evbuffer_get_length(input_buffer.evbuf);

  evbuffer_drain(input_buffer.evbuf, len);

  *flags = flags_set(len);

  input_buffer.eof = 0;
  input_buffer.metadata = 0;

  pthread_mutex_unlock(&input_buffer.mutex);

#ifdef DEBUG
  DPRINTF(E_DBG, L_PLAYER, "Flush with flags %d\n", *flags);
#endif
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

  input_buffer.evbuf = evbuffer_new();
  if (!input_buffer.evbuf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for input buffer\n");
      return -1;
    }

  no_input = 1;
  for (i = 0; inputs[i]; i++)
    {
      if (inputs[i]->type != i)
	{
	  DPRINTF(E_FATAL, L_PLAYER, "BUG! Input definitions are misaligned with input enum\n");
	  return -1;
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
    return -1;

  return 0;
}

void
input_deinit(void)
{
  int i;

  input_stop(NULL);

  for (i = 0; inputs[i]; i++)
    {
      if (inputs[i]->disabled)
	continue;

      if (inputs[i]->deinit)
        inputs[i]->deinit();
    }

  evbuffer_free(input_buffer.evbuf);
}

