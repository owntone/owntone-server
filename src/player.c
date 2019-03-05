/*
 * Copyright (C) 2016-2019 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2010-2011 Julien BLACHE <jb@jblache.org>
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


 * About player.c
 * --------------
 * The main tasks of the player are the following:
 * - handle playback commands, status checks and events from other threads
 * - receive audio from the input thread and to own the playback buffer
 * - feed the outputs at the appropriate rate (controlled by the playback timer)
 * - output device handling (partly outsourced to outputs.c)
 * - notify about playback status changes
 * - maintain the playback queue
 * 
 * The player thread should never be making operations that may block, since
 * that could block callers requesting status (effectively making forked-daapd
 * unresponsive) and it could also starve the outputs. In practice this rule is
 * not always obeyed, for instance some outputs do their setup in ways that
 * could block.
 *
 *
 * About metadata
 * --------------
 * The player gets metadata from library + inputs and passes it to the outputs
 * and other clients (e.g. Remotes).
 *
 *  1. On playback start, metadata from the library is loaded into the queue
 *     items, and these items are then the source of metadata for clients.
 *  2. During playback, the input may signal new metadata by making a
 *     input_write() with the INPUT_FLAG_METADATA flag. When the player read
 *     reaches that data, the player will request the metadata from the input
 *     with input_metadata_get(). This metadata is then saved to the currently
 *     playing queue item, and the clients are told to update metadata.
 *  3. Artwork works differently than textual metadata. The artwork module will
 *     look for artwork in the library, and addition also check the artwork_url
 *     of the queue_item.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif

#ifdef HAVE_TIMERFD
# include <sys/timerfd.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <signal.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>

#include <gcrypt.h>

#include "db.h"
#include "logger.h"
#include "conffile.h"
#include "misc.h"
#include "player.h"
#include "worker.h"
#include "listener.h"
#include "commands.h"

// Audio and metadata outputs
#include "outputs.h"

// Audio and metadata input
#include "input.h"

// Scrobbling
#ifdef LASTFM
# include "lastfm.h"
#endif

// Default volume (must be from 0 - 100)
#define PLAYER_DEFAULT_VOLUME 50

// The interval between each tick of the playback clock in ms. This means that
// we read 10 ms frames from the input and pass to the output, so the clock
// ticks 100 times a second. We use this value because most common sample rates
// are divisible by 100, and because it keeps delay low.
// TODO sample rates of 22050 might cause underruns, since we would be reading
// only 100 x 220 = 22000 samples each second.
#define PLAYER_TICK_INTERVAL 10

// For every tick_interval, we will read a frame from the input buffer and
// write it to the outputs. If the input is empty, we will try to catch up next
// tick. However, at some point we will owe the outputs so much data that we
// have to suspend playback and wait for the input to get its act together.
// (value is in milliseconds and should be low enough to avoid output underrun)
#define PLAYER_READ_BEHIND_MAX 1500

// Generally, an output must not block (for long) when outputs_write() is
// called. If an output does that anyway, the next tick event will be late, and
// by extension playback_cb(). We will try to catch up, but if the delay
// gets above this value, we will suspend playback and reset the output.
// (value is in milliseconds)
#define PLAYER_WRITE_BEHIND_MAX 1500

// When we pause, we keep the input open, but we can't do that forever. We must
// think of the poor streaming servers, for instance. This timeout determines
// how long we stay paused, before we close the inputs.
// (value is in seconds)
#define PLAYER_PAUSE_TIMEOUT 600

//#define DEBUG_PLAYER 1

struct volume_param {
  int volume;
  uint64_t spk_id;
};

struct activeremote_param {
  uint32_t activeremote;
  const char *value;
};

struct spk_enum
{
  spk_enum_cb cb;
  void *arg;
};

struct speaker_set_param
{
  uint64_t *device_ids;
  int intval;
};

struct speaker_get_param
{
  uint64_t spk_id;
  struct player_speaker_info *spk_info;
};

struct metadata_param
{
  struct input_metadata *input;
  struct output_metadata *output;
};

struct speaker_auth_param
{
  enum output_types type;
  char pin[5];
};

union player_arg
{
  struct output_device *device;
  struct speaker_auth_param auth;
  uint32_t id;
  int intval;
};

struct player_source
{
  /* Id of the file/item in the files database */
  uint32_t id;

  /* Item-Id of the file/item in the queue */
  uint32_t item_id;

  /* Length of the file/item in milliseconds */
  uint32_t len_ms;

  /* Quality of the source (sample rate etc.) */
  struct media_quality quality;

  enum data_kind data_kind;
  enum media_kind media_kind;
  char *path;

  // This is the position (measured in samples) the session was at when we
  // started reading from the input source.
  uint64_t read_start;

  // This is the position (measured in samples) the session was at when we got
  // a EOF or error from the input.
  uint64_t read_end;

  // Same as the above, but added with samples equivalent to
  // OUTPUTS_BUFFER_DURATION. So when the session position reaches play_start it
  // means the media should actually be playing on your device.
  uint64_t play_start;
  uint64_t play_end;

  // The number of milliseconds into the media that we started
  uint32_t seek_ms;
  // This should at any time match the millisecond position of the media that is
  // coming out of your device. Will be 0 during initial buffering.
  uint32_t pos_ms;

  // How many samples the outputs buffer before playing (=delay)
  int output_buffer_samples;
};

struct player_session
{
  uint8_t *buffer;
  size_t bufsize;

  // The time the playback session started
  struct timespec start_ts;

  // The time the first sample in the buffer should be played by the output,
  // without taking output buffer time (OUTPUTS_BUFFER_DURATION) into account.
  // It will be equal to:
  // pts = start_ts + ticks_elapsed * player_tick_interval
  struct timespec pts;

  // Equals current number of samples written to outputs
  uint32_t pos;

  // We try to read a fixed number of bytes from the source each clock tick,
  // but if it gives us less we increase this correspondingly
  size_t read_deficit;
  size_t read_deficit_max;

  // The item from the queue being read by the input now, previously and next
  struct player_source *reading_now;
  struct player_source *reading_next;
  struct player_source *reading_prev;

  // The item from the queue being played right now. This will normally point at
  // reading_now or reading_prev. It should only be NULL if no playback.
  struct player_source *playing_now;
};

static struct player_session pb_session;

struct event_base *evbase_player;

static int player_exit;
static pthread_t tid_player;
static struct commands_base *cmdbase;

// Keep track of how many outputs need to call back when flushing internally
// from the player thread (where we can't use player_playback_pause)
static int player_flush_pending;

// Config values
static int speaker_autoselect;
static int clear_queue_on_stop_disabled;

// Player status
static enum play_status player_state;
static enum repeat_mode repeat;
static char shuffle;
static char consume;

// Playback timer
#ifdef HAVE_TIMERFD
static int pb_timer_fd;
#else
timer_t pb_timer;
#endif
static struct event *pb_timer_ev;
static struct event *player_pause_timeout_ev;

// Time between ticks, i.e. time between when playback_cb() is invoked
static struct timespec player_tick_interval;
// Timer resolution
static struct timespec player_timer_res;

static struct timeval player_pause_timeout = { PLAYER_PAUSE_TIMEOUT, 0 };

// PLAYER_WRITE_BEHIND_MAX converted to clock ticks
static int pb_write_deficit_max;

// True if we are trying to recover from a major playback timer overrun (write problems)
static bool pb_write_recovery;

// Output status
static int output_sessions;

// Last commanded volume
static int master_volume;

// Audio source
static uint32_t cur_plid;
static uint32_t cur_plversion;

// Play history
static struct player_history *history;


/* -------------------------------- Forwards -------------------------------- */

static void
pb_abort(void);

static void
pb_suspend(void);


/* ----------------------------- Volume helpers ----------------------------- */

static int
rel_to_vol(int relvol)
{
  float vol;

  if (relvol == 100)
    return master_volume;

  vol = ((float)relvol * (float)master_volume) / 100.0;

  return (int)vol;
}

static int
vol_to_rel(int volume)
{
  float rel;

  if (volume == master_volume)
    return 100;

  rel = ((float)volume / (float)master_volume) * 100.0;

  return (int)rel;
}

// Master volume helpers
static void
volume_master_update(int newvol)
{
  struct output_device *device;

  master_volume = newvol;

  for (device = output_device_list; device; device = device->next)
    {
      if (device->selected)
	device->relvol = vol_to_rel(device->volume);
    }
}

static void
volume_master_find(void)
{
  struct output_device *device;
  int newmaster;

  newmaster = -1;

  for (device = output_device_list; device; device = device->next)
    {
      if (device->selected && (device->volume > newmaster))
	newmaster = device->volume;
    }

  volume_master_update(newmaster);
}


/* ---------------------- Device select/deselect hooks ---------------------- */

static void
speaker_select_output(struct output_device *device)
{
  device->selected = 1;

  if (device->volume > master_volume)
    {
      if (player_state == PLAY_STOPPED || master_volume == -1)
	volume_master_update(device->volume);
      else
	device->volume = master_volume;
    }

  device->relvol = vol_to_rel(device->volume);
}

static void
speaker_deselect_output(struct output_device *device)
{
  device->selected = 0;

  if (device->volume == master_volume)
    volume_master_find();
}


/* ----------------------- Misc helpers and callbacks ----------------------- */

// Callback from the worker thread (async operation as it may block)
static void
playcount_inc_cb(void *arg)
{
  int *id = arg;

  db_file_inc_playcount(*id);
}

static void
skipcount_inc_cb(void *arg)
{
  int *id = arg;

  db_file_inc_skipcount(*id);
}

#ifdef LASTFM
// Callback from the worker thread (async operation as it may block)
static void
scrobble_cb(void *arg)
{
  int *id = arg;

  lastfm_scrobble(*id);
}
#endif

static void
pause_timer_cb(int fd, short what, void *arg)
{
  pb_abort();
}

// Callback from the worker thread. Here the heavy lifting is done: updating the
// db_queue_item, retrieving artwork (through outputs_metadata_prepare) and
// when done, telling the player to send the metadata to the clients
static void
metadata_update_cb(void *arg)
{
  struct input_metadata *metadata = arg;
  struct output_metadata *o_metadata;
  struct db_queue_item *queue_item;
  int ret;

  ret = input_metadata_get(metadata);
  if (ret < 0)
    {
      goto out_free_metadata;
    }

  queue_item = db_queue_fetch_byitemid(metadata->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Input metadata item_id does not match anything in queue\n");
      goto out_free_metadata;
    }

  // Update queue item if metadata changed
  if (metadata->artist || metadata->title || metadata->album || metadata->genre || metadata->artwork_url || metadata->song_length)
    {
      // Since we won't be using the metadata struct values for anything else than
      // this we just swap pointers
      if (metadata->artist)
	swap_pointers(&queue_item->artist, &metadata->artist);
      if (metadata->title)
	swap_pointers(&queue_item->title, &metadata->title);
      if (metadata->album)
	swap_pointers(&queue_item->album, &metadata->album);
      if (metadata->genre)
	swap_pointers(&queue_item->genre, &metadata->genre);
      if (metadata->artwork_url)
	swap_pointers(&queue_item->artwork_url, &metadata->artwork_url);
      if (metadata->song_length)
	queue_item->song_length = metadata->song_length;

      ret = db_queue_update_item(queue_item);
      if (ret < 0)
        {
	  DPRINTF(E_LOG, L_PLAYER, "Database error while updating queue with new metadata\n");
	  goto out_free_queueitem;
	}
    }

  o_metadata = outputs_metadata_prepare(metadata->item_id);

  // Actual sending must be done by player, since the worker does not own the outputs
  player_metadata_send(metadata, o_metadata);

  outputs_metadata_free(o_metadata);

 out_free_queueitem:
  free_queue_item(queue_item, 0);

 out_free_metadata:
  input_metadata_free(metadata, 1);
}

// Gets the metadata, but since the actual update requires db writes and
// possibly retrieving artwork we let the worker do the next step
static void
metadata_trigger(int startup)
{
  struct input_metadata metadata;

  memset(&metadata, 0, sizeof(struct input_metadata));

  metadata.item_id = pb_session.playing_now->item_id;

  metadata.startup = startup;
  metadata.start   = pb_session.playing_now->read_start;
  metadata.offset  = pb_session.playing_now->play_start - pb_session.playing_now->read_start;
  metadata.rtptime = pb_session.pos;

  worker_execute(metadata_update_cb, &metadata, sizeof(metadata), 0);
}

/*
 * Add the song with the given id to the list of previously played songs
 */
static void
history_add(uint32_t id, uint32_t item_id)
{
  unsigned int cur_index;
  unsigned int next_index;

  // Check if the current song is already the last in the history to avoid duplicates
  cur_index = (history->start_index + history->count - 1) % MAX_HISTORY_COUNT;
  if (id == history->id[cur_index])
    {
      DPRINTF(E_DBG, L_PLAYER, "Current playing/streaming song already in history\n");
      return;
    }

  // Calculate the next index and update the start-index and count for the id-buffer
  next_index = (history->start_index + history->count) % MAX_HISTORY_COUNT;
  if (next_index == history->start_index && history->count > 0)
    history->start_index = (history->start_index + 1) % MAX_HISTORY_COUNT;

  history->id[next_index] = id;
  history->item_id[next_index] = item_id;

  if (history->count < MAX_HISTORY_COUNT)
    history->count++;
}

static void
seek_save(void)
{
  struct player_source *ps = pb_session.playing_now;

  if (ps && (ps->media_kind & (MEDIA_KIND_MOVIE | MEDIA_KIND_PODCAST | MEDIA_KIND_AUDIOBOOK | MEDIA_KIND_TVSHOW)))
    db_file_seek_update(ps->id, ps->pos_ms);
}

/*
 * Returns the next queue item based on the current streaming source and repeat mode
 *
 * If repeat mode is repeat all, shuffle is active and the current streaming source is the
 * last item in the queue, the queue is reshuffled prior to returning the first item of the
 * queue.
 */
static struct db_queue_item *
queue_item_next(uint32_t item_id)
{
  struct db_queue_item *queue_item;

  if (repeat == REPEAT_SONG)
    {
      queue_item = db_queue_fetch_byitemid(item_id);
      if (!queue_item)
        return NULL;
    }
  else
    {
      queue_item = db_queue_fetch_next(item_id, shuffle);
      if (!queue_item && repeat == REPEAT_ALL)
	{
	  if (shuffle)
	    db_queue_reshuffle(0);

	  queue_item = db_queue_fetch_bypos(0, shuffle);
	  if (!queue_item)
	    return NULL;
	}
    }

  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Reached end of queue\n");
      return NULL;
    }

  return queue_item;
}

static struct db_queue_item *
queue_item_prev(uint32_t item_id)
{
  return db_queue_fetch_prev(item_id, shuffle);
}

static void
status_update(enum play_status status)
{
  player_state = status;

  listener_notify(LISTENER_PLAYER);
}


/* ----------- Audio source handling (interfaces with input module) --------- */

/*
 * Creates a new player source for the given queue item
 */
static struct player_source *
source_new(struct db_queue_item *queue_item)
{
  struct player_source *ps;

  CHECK_NULL(L_PLAYER, ps = calloc(1, sizeof(struct player_source)));

  ps->id = queue_item->file_id;
  ps->item_id = queue_item->id;
  ps->data_kind = queue_item->data_kind;
  ps->media_kind = queue_item->media_kind;
  ps->len_ms = queue_item->song_length;
  ps->path = strdup(queue_item->path);

  return ps;
}

static void
source_free(struct player_source **ps)
{
  if (!(*ps))
    return;

  free((*ps)->path);
  free(*ps);

  *ps = NULL;
}

static void
source_stop(void)
{
  input_stop();
}

static struct player_source *
source_next_create(struct player_source *current)
{
  struct player_source *ps;
  struct db_queue_item *queue_item;

  if (!current)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! source_next_create called without a current source\n");
      return NULL;
    }

  queue_item = queue_item_next(current->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error fetching next item from queue (item-id=%d, repeat=%d)\n", current->item_id, repeat);
      return NULL;
    }

  ps = source_new(queue_item);

  free_queue_item(queue_item, 0);

  return ps;
}

static void
source_next(void)
{
  if (!pb_session.reading_next)
    return;

  DPRINTF(E_DBG, L_PLAYER, "Opening next track: '%s' (id=%d)\n", pb_session.reading_next->path, pb_session.reading_next->item_id);

  input_start(pb_session.reading_next->item_id);
}

static int
source_start(void)
{
  short flags;

  if (!pb_session.reading_next)
    return 0;

  DPRINTF(E_DBG, L_PLAYER, "(Re)opening track: '%s' (id=%d, seek=%d)\n", pb_session.reading_next->path, pb_session.reading_next->item_id, pb_session.reading_next->seek_ms);

  input_flush(&flags);

  return input_seek(pb_session.reading_next->item_id, (int)pb_session.reading_next->seek_ms);
}


/* ------------------------ Playback session upkeep ------------------------- */

// The below update the playback session so it is always in mint condition. That
// is all they do, they should not do anything else. If you are looking for a
// place to add some non session actions, look further down at the events.

#ifdef DEBUG_PLAYER
static int debug_dump_counter = -1;

static int
source_print(char *line, size_t linesize, struct player_source *ps, const char *name)
{
  int pos = 0;

  if (ps)
    {
      pos += snprintf(line + pos, linesize - pos, "%s.path=%s; ", name, ps->path);
      pos += snprintf(line + pos, linesize - pos, "%s.quality=%d; ", name, ps->quality.sample_rate);
      pos += snprintf(line + pos, linesize - pos, "%s.item_id=%u; ", name, ps->item_id);
      pos += snprintf(line + pos, linesize - pos, "%s.read_start=%lu; ", name, ps->read_start);
      pos += snprintf(line + pos, linesize - pos, "%s.play_start=%lu; ", name, ps->play_start);
      pos += snprintf(line + pos, linesize - pos, "%s.read_end=%lu; ", name, ps->read_end);
      pos += snprintf(line + pos, linesize - pos, "%s.play_end=%lu; ", name, ps->play_end);
      pos += snprintf(line + pos, linesize - pos, "%s.pos_ms=%d; ", name, ps->pos_ms);
      pos += snprintf(line + pos, linesize - pos, "%s.seek_ms=%d; ", name, ps->seek_ms);
    }
  else
    pos += snprintf(line + pos, linesize - pos, "%s=(null); ", name);

  return pos;
}

static void
session_dump(bool use_counter)
{
  char line[4096];
  int pos = 0;

  if (use_counter)
    {
      debug_dump_counter++;
      if (debug_dump_counter % 100 != 0)
	return;
    }

  pos += snprintf(line + pos, sizeof(line) - pos, "pos=%d; ", pb_session.pos);
  pos += source_print(line + pos, sizeof(line) - pos, pb_session.reading_now, "reading_now");

  DPRINTF(E_DBG, L_PLAYER, "%s\n", line);

  pos = 0;
  pos += snprintf(line + pos, sizeof(line) - pos, "pos=%d; ", pb_session.pos);
  pos += source_print(line + pos, sizeof(line) - pos, pb_session.playing_now, "playing_now");

  DPRINTF(E_DBG, L_PLAYER, "%s\n", line);

  pos = 0;
  pos += snprintf(line + pos, sizeof(line) - pos, "pos=%d; ", pb_session.pos);
  pos += source_print(line + pos, sizeof(line) - pos, pb_session.reading_prev, "reading_prev");

  DPRINTF(E_DBG, L_PLAYER, "%s\n", line);

  pos = 0;
  pos += snprintf(line + pos, sizeof(line) - pos, "pos=%d; ", pb_session.pos);
  pos += source_print(line + pos, sizeof(line) - pos, pb_session.reading_next, "reading_next");

  DPRINTF(E_DBG, L_PLAYER, "%s\n", line);
}
#endif

static void
session_update_play_eof(void)
{
  pb_session.playing_now = pb_session.reading_now;
}

static void
session_update_play_start(void)
{
  pb_session.playing_now = pb_session.reading_now;
}

static void
session_update_read_next(void)
{
  struct player_source *ps;

  ps = source_next_create(pb_session.reading_now);
  source_free(&pb_session.reading_next);
  pb_session.reading_next = ps;
}

static void
session_update_read_eof(void)
{
  pb_session.reading_now->read_end = pb_session.pos;
  pb_session.reading_now->play_end = pb_session.pos + pb_session.reading_now->output_buffer_samples;

  source_free(&pb_session.reading_prev);
  pb_session.reading_prev = pb_session.reading_now;
  pb_session.reading_now  = pb_session.reading_next;
  pb_session.reading_next = NULL;

  // There is nothing else to play
  if (!pb_session.reading_now)
    return;

  // We inherit this because the input will only notify on quality changes, not
  // if it is the same as the previous track
  pb_session.reading_now->quality = pb_session.reading_prev->quality;
  pb_session.reading_now->output_buffer_samples = pb_session.reading_prev->output_buffer_samples;

  pb_session.reading_now->read_start = pb_session.pos;
  pb_session.reading_now->play_start = pb_session.pos + pb_session.reading_now->output_buffer_samples;
}

static void
session_update_read_start(uint32_t seek_ms)
{
  source_free(&pb_session.reading_prev);
  pb_session.reading_prev = pb_session.reading_now;
  pb_session.reading_now  = pb_session.reading_next;
  pb_session.reading_next = NULL;

  // There is nothing else to play
  if (!pb_session.reading_now)
    return;

  pb_session.reading_now->pos_ms     = seek_ms;
  pb_session.reading_now->seek_ms    = seek_ms;
  pb_session.reading_now->read_start = pb_session.pos;

  pb_session.playing_now = pb_session.reading_now;
}

static inline void
session_update_read(int nsamples)
{
  // Did we just complete our first read? Then set the start timestamp
  if (pb_session.start_ts.tv_sec == 0)
    {
      clock_gettime_with_res(CLOCK_MONOTONIC, &pb_session.start_ts, &player_timer_res);
      pb_session.pts = pb_session.start_ts;
    }

  // Advance position
  pb_session.pos += nsamples;

  // After we have started playing we also must calculate new pos_ms
  if (pb_session.playing_now->quality.sample_rate && pb_session.pos > pb_session.playing_now->play_start)
    pb_session.playing_now->pos_ms = pb_session.playing_now->seek_ms + 1000UL * (pb_session.pos - pb_session.playing_now->play_start) / pb_session.playing_now->quality.sample_rate;
}

static void
session_update_read_quality(struct media_quality *quality)
{
  int samples_per_read;

  if (quality_is_equal(quality, &pb_session.reading_now->quality))
    return;

  pb_session.reading_now->quality = *quality;
  samples_per_read = ((uint64_t)quality->sample_rate * (player_tick_interval.tv_nsec / 1000000)) / 1000;
  pb_session.reading_now->output_buffer_samples = OUTPUTS_BUFFER_DURATION * quality->sample_rate;

  pb_session.bufsize = STOB(samples_per_read, quality->bits_per_sample, quality->channels);
  pb_session.read_deficit_max = STOB(((uint64_t)quality->sample_rate * PLAYER_READ_BEHIND_MAX) / 1000, quality->bits_per_sample, quality->channels);

  DPRINTF(E_DBG, L_PLAYER, "New session values (q=%d/%d/%d, spr=%d, bufsize=%zu)\n",
    quality->sample_rate, quality->bits_per_sample, quality->channels, samples_per_read, pb_session.bufsize);

  if (pb_session.buffer)
    pb_session.buffer = realloc(pb_session.buffer, pb_session.bufsize);
  else
    pb_session.buffer = malloc(pb_session.bufsize);

  CHECK_NULL(L_PLAYER, pb_session.buffer);

  pb_session.reading_now->play_start = pb_session.reading_now->read_start + pb_session.reading_now->output_buffer_samples;
}

static void
session_stop(void)
{
  free(pb_session.buffer);
  pb_session.buffer = NULL;

  source_free(&pb_session.reading_prev);
  source_free(&pb_session.reading_now);
  source_free(&pb_session.reading_next);

  memset(&pb_session, 0, sizeof(struct player_session));
}

static void
session_start(struct player_source *ps, uint32_t seek_ms)
{
  session_stop();

  // Add the item to play as reading_next
  pb_session.reading_next = ps;
  pb_session.reading_next->seek_ms = seek_ms;
}


/* ------------------------- Playback event handlers ------------------------ */

static void
event_read_quality()
{
  DPRINTF(E_DBG, L_PLAYER, "event_read_quality()\n");

  struct media_quality quality;

  input_quality_get(&quality);

  session_update_read_quality(&quality);
}

// Stuff to do when read of current track ends
static void
event_read_eof()
{
  DPRINTF(E_DBG, L_PLAYER, "event_read_eof()\n");

  session_update_read_eof();
}

static void
event_read_error()
{
  DPRINTF(E_DBG, L_PLAYER, "event_read_error()\n");

  db_queue_delete_byitemid(pb_session.reading_now->item_id);

  event_read_eof();
}

// Kicks of input reading of next source (async)
static void
event_read_start_next()
{
  DPRINTF(E_DBG, L_PLAYER, "event_read_start_next()\n");

  // Attaches next item to session as reading_next
  session_update_read_next();

  source_next();
}

static void
event_metadata_new()
{
  DPRINTF(E_DBG, L_PLAYER, "event_metadata_new()\n");

  metadata_trigger(0);
}

static void
event_play_end()
{
  DPRINTF(E_DBG, L_PLAYER, "event_play_end()\n");

  pb_abort();
}

// Stuff to do when playback of current track ends
static void
event_play_eof()
{
  DPRINTF(E_DBG, L_PLAYER, "event_play_eof()\n");

  int id = (int)pb_session.playing_now->id;

  if (id != DB_MEDIA_FILE_NON_PERSISTENT_ID)
    {
      worker_execute(playcount_inc_cb, &id, sizeof(int), 5);
#ifdef LASTFM
      worker_execute(scrobble_cb, &id, sizeof(int), 8);
#endif
      history_add(pb_session.playing_now->id, pb_session.playing_now->item_id);
    }

  if (consume)
    db_queue_delete_byitemid(pb_session.playing_now->item_id);

  outputs_metadata_prune(pb_session.pos);

  session_update_play_eof();
}

static void
event_play_start()
{
  DPRINTF(E_DBG, L_PLAYER, "event_play_start()\n");

  event_metadata_new();

  status_update(PLAY_PLAYING);

  session_update_play_start();
}

// Checks if the new playback position requires change of play status, plus
// calls session_update_read that updates playback position
static inline void
event_read(int nsamples)
{
  // Shouldn't happen, playing_now must be set during playback, but check anyway
  if (!pb_session.playing_now)
    return;

  if (pb_session.playing_now->play_end != 0 && pb_session.pos + nsamples >= pb_session.playing_now->play_end)
    {
      event_play_eof();

      if (!pb_session.playing_now)
	{
	  event_play_end();
	  return;
	}
    }

  // Check if the playback position will be passing the play_start position
  if (pb_session.pos < pb_session.playing_now->play_start && pb_session.pos + nsamples >= pb_session.playing_now->play_start)
    event_play_start();

  session_update_read(nsamples);
}


/* ---- Main playback stuff: Start, read, write and playback timer event ---- */

// Returns -1 on error or bytes read (possibly 0)
static inline int
source_read(int *nbytes, int *nsamples, struct media_quality *quality, uint8_t *buf, int len)
{
  short flags;

  // Nothing to read
  if (!pb_session.reading_now)
    {
      // This can happen if the loop tries to catch up with an overrun or a
      // deficit, but the playback ends in the first iteration
      if (!pb_session.playing_now)
	{
	  *nbytes = 0;
	  *nsamples = 0;
	  return 0;
	}

      // Stream silence until event_read() stops playback
      memset(buf, 0, len);
      *quality = pb_session.playing_now->quality;
      *nbytes = len;
      *nsamples = BTOS(*nbytes, quality->bits_per_sample, quality->channels);
      event_read(*nsamples);
      return 0;
    }

  *quality = pb_session.reading_now->quality;
  *nsamples = 0;
  *nbytes = input_read(buf, len, &flags);
  if ((*nbytes < 0) || (flags & INPUT_FLAG_ERROR))
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading source '%s' (id=%d)\n", pb_session.reading_now->path, pb_session.reading_now->id);
      event_read_error();
      return -1;
    }
  else if (flags & INPUT_FLAG_START_NEXT)
    {
      event_read_start_next();
    }
  else if (flags & INPUT_FLAG_EOF)
    {
      event_read_eof();
    }
  else if (flags & INPUT_FLAG_METADATA)
    {
      event_metadata_new();
    }
  else if (flags & INPUT_FLAG_QUALITY || quality->channels == 0)
    {
      event_read_quality();
    }

  if (*nbytes == 0 || quality->channels == 0)
    {
      event_read(0); // This will set start_ts even if source isn't open yet
      return 0;
    }

  *nsamples = BTOS(*nbytes, quality->bits_per_sample, quality->channels);

  event_read(*nsamples);

  return 0;
}

static void
playback_cb(int fd, short what, void *arg)
{
  struct timespec ts;
  struct media_quality quality;
  uint64_t overrun;
  int nbytes;
  int nsamples;
  int i;
  int ret;

  // Check if we missed any timer expirations
  overrun = 0;
#ifdef HAVE_TIMERFD
  ret = read(fd, &overrun, sizeof(overrun));
  if (ret <= 0)
    DPRINTF(E_LOG, L_PLAYER, "Error reading timer\n");
  else if (overrun > 0)
    overrun--;
#else
  ret = timer_getoverrun(pb_timer);
  if (ret < 0)
    DPRINTF(E_LOG, L_PLAYER, "Error getting timer overrun\n");
  else
    overrun = ret;
#endif /* HAVE_TIMERFD */

  // We are too delayed, probably some output blocked: reset if first overrun or abort if second overrun
  if (overrun > pb_write_deficit_max)
    {
      if (pb_write_recovery)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Permanent output delay detected (behind=%" PRIu64 ", max=%d), aborting\n", overrun, pb_write_deficit_max);
	  pb_abort();
	  return;
	}

      DPRINTF(E_LOG, L_PLAYER, "Output delay detected (behind=%" PRIu64 ", max=%d), resetting all outputs\n", overrun, pb_write_deficit_max);
      pb_write_recovery = true;
      pb_suspend();
      return;
    }
  else
    {
      if (overrun > 1) // An overrun of 1 is no big deal
	DPRINTF(E_WARN, L_PLAYER, "Output delay detected: player is %" PRIu64 " ticks behind, catching up\n", overrun);

      pb_write_recovery = false;
    }

#ifdef DEBUG_PLAYER
  session_dump(true);
#endif

  // The pessimistic approach: Assume you won't get anything, then anything that
  // comes your way is a positive surprise.
  pb_session.read_deficit += (1 + overrun) * pb_session.bufsize;

  // If there was an overrun, we will try to read/write a corresponding number
  // of times so we catch up. The read from the input is non-blocking, so it
  // should not bring us further behind, even if there is no data.
  for (i = 1 + overrun; i > 0; i--)
    {
      ret = source_read(&nbytes, &nsamples, &quality, pb_session.buffer, pb_session.bufsize);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error reading from source\n");
	  pb_session.read_deficit -= pb_session.bufsize;
	  break;
	}
      if (nbytes == 0)
	{
	  break;
	}

      pb_session.read_deficit -= nbytes;

      outputs_write(pb_session.buffer, nbytes, nsamples, &quality, &pb_session.pts);

      if (nbytes < pb_session.bufsize)
	{
	  // How much the number of samples we got corresponds to in time (nanoseconds)
	  ts.tv_sec = 0;
	  ts.tv_nsec = 1000000000UL * (uint64_t)nsamples / quality.sample_rate;

	  DPRINTF(E_DBG, L_PLAYER, "Incomplete read, wanted %zu, got %d (samples=%d/time=%lu), deficit %zu\n", pb_session.bufsize, nbytes, nsamples, ts.tv_nsec, pb_session.read_deficit);

	  pb_session.pts = timespec_add(pb_session.pts, ts);
	}
      else
	{
	  // We got a full frame, so that means we can also advance the presentation timestamp by a full tick
	  pb_session.pts = timespec_add(pb_session.pts, player_tick_interval);

	  // It is going well, lets take another round to repay our debt
	  if (i == 1 && pb_session.read_deficit > pb_session.bufsize)
	    i = 2;
	}
    }

  if (pb_session.read_deficit_max && pb_session.read_deficit > pb_session.read_deficit_max)
    {
      DPRINTF(E_LOG, L_PLAYER, "Source is not providing sufficient data, temporarily suspending playback (deficit=%zu/%zu bytes)\n",
	pb_session.read_deficit, pb_session.read_deficit_max);

      pb_suspend();
    }
}


/* ----------------- Output device handling (add/remove etc) ---------------- */

static enum command_state
device_add(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device = cmdarg->device;
  bool new_deselect;
  int default_volume;

  // Default volume for new devices
  default_volume = (master_volume >= 0) ? master_volume : PLAYER_DEFAULT_VOLUME;

  // Never turn on new devices during playback
  new_deselect = (player_state == PLAY_PLAYING);

  device = outputs_device_add(device, new_deselect, default_volume);
  *retval = device ? 0 : -1;

  if (device && device->selected)
    speaker_select_output(device);

  return COMMAND_END;
}

static enum command_state
device_remove_family(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *remove;
  struct output_device *device;

  remove = cmdarg->device;

  device = outputs_device_get(remove->id);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "The %s device '%s' stopped advertising, but not in our list\n", remove->type_name, remove->name);

      outputs_device_free(remove);
      *retval = 0;
      return COMMAND_END;
    }

  // v{4,6}_port non-zero indicates the address family stopped advertising
  if (remove->v4_port && device->v4_address)
    {
      free(device->v4_address);
      device->v4_address = NULL;
      device->v4_port = 0;
    }

  if (remove->v6_port && device->v6_address)
    {
      free(device->v6_address);
      device->v6_address = NULL;
      device->v6_port = 0;
    }

  if (!device->v4_address && !device->v6_address)
    {
      device->advertised = 0;

      // If there is a session we will keep the device in the list until the
      // backend gives us a callback with a failure. Then outputs.c will remove
      // the device. If the output backend never gives a callback (can that
      // happen?) then the device will never be removed.
      if (!device->session)
	{
	  if (device->selected)
	    speaker_deselect_output(device);

	  outputs_device_remove(device);
	}
    }

  outputs_device_free(remove);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
device_auth_kickoff(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;

  outputs_authorize(cmdarg->auth.type, cmdarg->auth.pin);

  *retval = 0;
  return COMMAND_END;
}


static enum command_state
device_metadata_send(void *arg, int *retval)
{
  struct metadata_param *metadata_param = arg;
  struct input_metadata *imd;
  struct output_metadata *omd;

  imd = metadata_param->input;
  omd = metadata_param->output;

  outputs_metadata_send(omd, imd->rtptime, imd->offset, imd->startup);

  status_update(player_state);

  *retval = 0;
  return COMMAND_END;
}


/* -------- Output device callbacks executed in the player thread ----------- */

static void
device_streaming_cb(struct output_device *device, enum output_device_state status)
{
  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Output device disappeared during streaming!\n");

      output_sessions--;
      return;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_streaming_cb (status %d)\n", outputs_name(device->type), status);

  if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' FAILED\n", device->type_name, device->name);

      output_sessions--;

      if (player_state == PLAY_PLAYING)
	speaker_deselect_output(device);

      if (output_sessions == 0)
	pb_abort();
    }
  else if (status == OUTPUT_STATE_STOPPED)
    {
      DPRINTF(E_INFO, L_PLAYER, "The %s device '%s' stopped\n", device->type_name, device->name);

      output_sessions--;
    }
  else
    outputs_device_cb_set(device, device_streaming_cb);
}

static void
device_command_cb(struct output_device *device, enum output_device_state status)
{
  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Output device disappeared before command completion!\n");
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_command_cb (status %d)\n", outputs_name(device->type), status);

  outputs_device_cb_set(device, device_streaming_cb);

  if (status == OUTPUT_STATE_FAILED)
    device_streaming_cb(device, status);

 out:
  commands_exec_end(cmdbase, 0);
}

static void
device_flush_cb(struct output_device *device, enum output_device_state status)
{
  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Output device disappeared before flush completion!\n");
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_flush_cb (status %d)\n", outputs_name(device->type), status);

  if (status == OUTPUT_STATE_FAILED)
    device_streaming_cb(device, status);

  // Used by pb_suspend - is basically the bottom half
  if (player_flush_pending > 0)
    {
      player_flush_pending--;
      if (player_flush_pending == 0)
	input_buffer_full_cb(player_playback_start);
    }

  outputs_device_stop_delayed(device, device_streaming_cb);

 out:
  commands_exec_end(cmdbase, 0);
}

static void
device_shutdown_cb(struct output_device *device, enum output_device_state status)
{
  int retval;

  if (output_sessions)
    output_sessions--;

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared before shutdown completion!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_shutdown_cb (status %d)\n", outputs_name(device->type), status);

 out:
  /* cur_cmd->ret already set
   *  - to 0 (or -2 if password issue) in speaker_set()
   *  - to -1 above on error
   */
  commands_exec_end(cmdbase, retval);
}

static void
device_activate_cb(struct output_device *device, enum output_device_state status)
{
  int retval;

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during startup!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_activate_cb (status %d)\n", outputs_name(device->type), status);

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  output_sessions++;

  outputs_device_cb_set(device, device_streaming_cb);

 out:
  /* cur_cmd->ret already set
   *  - to 0 in speaker_set() (default)
   *  - to -2 above if password issue
   *  - to -1 above on error
   */
  commands_exec_end(cmdbase, retval);
}

static void
device_probe_cb(struct output_device *device, enum output_device_state status)
{
  int retval;

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during probe!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_probe_cb (status %d)\n", outputs_name(device->type), status);

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

 out:
  /* cur_cmd->ret already set
   *  - to 0 in speaker_set() (default)
   *  - to -2 above if password issue
   *  - to -1 above on error
   */
  commands_exec_end(cmdbase, retval);
}

static void
device_restart_cb(struct output_device *device, enum output_device_state status)
{
  int retval;

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during restart!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_restart_cb (status %d)\n", outputs_name(device->type), status);

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  output_sessions++;
  outputs_device_cb_set(device, device_streaming_cb);

 out:
  commands_exec_end(cmdbase, retval);
}

const char *
player_pmap(void *p)
{
  if (p == device_restart_cb)
    return "device_restart_cb";
  else if (p == device_probe_cb)
    return "device_probe_cb";
  else if (p == device_activate_cb)
    return "device_activate_cb";
  else if (p == device_streaming_cb)
    return "device_streaming_cb";
  else if (p == device_command_cb)
    return "device_command_cb";
  else if (p == device_flush_cb)
    return "device_flush_cb";
  else if (p == device_shutdown_cb)
    return "device_shutdown_cb";
  else
    return "unknown";
}

/* ------------------------- Internal playback routines --------------------- */

static int
pb_timer_start(void)
{
  struct itimerspec tick;
  int ret;

  // The stop timers will be active if we have recently paused, but now that the
  // playback loop has been kicked off, we deactivate them
  event_del(player_pause_timeout_ev);
  outputs_stop_delayed_cancel();

  ret = event_add(pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not add playback timer\n");

      return -1;
    }

  tick.it_interval = player_tick_interval;
  tick.it_value = player_tick_interval;

#ifdef HAVE_TIMERFD
  ret = timerfd_settime(pb_timer_fd, 0, &tick, NULL);
#else
  ret = timer_settime(pb_timer, 0, &tick, NULL);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not arm playback timer: %s\n", strerror(errno));

      return -1;
    }

  return 0;
}

static int
pb_timer_stop(void)
{
  struct itimerspec tick;
  int ret;

  event_del(pb_timer_ev);

  memset(&tick, 0, sizeof(struct itimerspec));

#ifdef HAVE_TIMERFD
  ret = timerfd_settime(pb_timer_fd, 0, &tick, NULL);
#else
  ret = timer_settime(pb_timer, 0, &tick, NULL);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not disarm playback timer: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

// Initiates the session and starts the input source
static int
pb_session_start(struct db_queue_item *queue_item, uint32_t seek_ms)
{
  struct player_source *ps;
  int ret;

  ps = source_new(queue_item);

  // Clears the session and attaches the new source as reading_next
  session_start(ps, seek_ms);

  // Sets of opening of the new source
  while ( (ret = source_start()) < 0)
    {
      // Couldn't start requested item, remove it from queue and try next in line
      db_queue_delete_byitemid(pb_session.reading_next->item_id);
      session_update_read_next();
    }

  session_update_read_start((uint32_t)ret);

  if (!pb_session.playing_now)
    return -1;

  // The input source is now open and ready, but we might actually be paused. So
  // we activate the below event in case the user never starts us again
  event_add(player_pause_timeout_ev, &player_pause_timeout);

  return ret;
}

// Stops input source and deallocates pb_session content
static void
pb_session_stop(void)
{
  source_stop();

  session_stop();

  pb_timer_stop();

  status_update(PLAY_STOPPED);
}

static void
pb_abort(void)
{
  // Immediate stop of all outputs
  outputs_stop(device_streaming_cb);
  outputs_metadata_purge();

  pb_session_stop();

  if (!clear_queue_on_stop_disabled)
    db_queue_clear(0);
}

// Temporarily suspends/resets playback, used when input buffer underruns or in
// case of problems writing to the outputs
static void
pb_suspend(void)
{
  player_flush_pending = outputs_flush(device_flush_cb);

  pb_timer_stop();

  status_update(PLAY_PAUSED);

  seek_save();

  // No devices to wait for, just set the restart cb right away
  if (player_flush_pending == 0)
    input_buffer_full_cb(player_playback_start);
}


/* --------------- Actual commands, executed in the player thread ----------- */

static enum command_state
get_status(void *arg, int *retval)
{
  struct player_status *status = arg;

  memset(status, 0, sizeof(struct player_status));

  status->shuffle = shuffle;
  status->consume = consume;
  status->repeat = repeat;

  status->volume = master_volume;

  status->plid = cur_plid;

  switch (player_state)
    {
      case PLAY_STOPPED:
	DPRINTF(E_DBG, L_PLAYER, "Player status: stopped\n");

	status->status  = PLAY_STOPPED;
	break;

      case PLAY_PAUSED:
	DPRINTF(E_DBG, L_PLAYER, "Player status: paused\n");

	status->status  = PLAY_PAUSED;
	status->id      = pb_session.playing_now->id;
	status->item_id = pb_session.playing_now->item_id;

	status->pos_ms  = pb_session.playing_now->pos_ms;
	status->len_ms  = pb_session.playing_now->len_ms;

	break;

      case PLAY_PLAYING:
	if (pb_session.playing_now->pos_ms == 0)
	  {
	    DPRINTF(E_DBG, L_PLAYER, "Player status: playing (buffering)\n");

	    status->status = PLAY_PAUSED;
	  }
	else
	  {
	    DPRINTF(E_DBG, L_PLAYER, "Player status: playing\n");

	    status->status = PLAY_PLAYING;
	  }

	status->id      = pb_session.playing_now->id;
	status->item_id = pb_session.playing_now->item_id;

	status->pos_ms  = pb_session.playing_now->pos_ms;
	status->len_ms  = pb_session.playing_now->len_ms;

	break;
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playing_now(void *arg, int *retval)
{
  uint32_t *id = arg;

  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  *id = pb_session.playing_now->id;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_stop(void *arg, int *retval)
{
  if (player_state == PLAY_STOPPED)
    {
      *retval = 0;
      return COMMAND_END;
    }

  if (pb_session.playing_now && pb_session.playing_now->pos_ms > 0)
    history_add(pb_session.playing_now->id, pb_session.playing_now->item_id);

  // We may be restarting very soon, so we don't bring the devices to a full
  // stop just yet; this saves time when restarting, which is nicer for the user
  *retval = outputs_flush(device_flush_cb);
  outputs_metadata_purge();

  // Stops the input
  pb_session_stop();

  status_update(PLAY_STOPPED);

  // We're async if we need to flush devices
  if (*retval > 0)
    return COMMAND_PENDING;

  return COMMAND_END;
}

static enum command_state
playback_abort(void *arg, int *retval)
{
  pb_abort();

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_start_bh(void *arg, int *retval)
{
  int ret;

  ret = pb_timer_start();
  if (ret < 0)
    goto error;

  status_update(PLAY_PLAYING);

  *retval = 0;
  return COMMAND_END;

 error:
  pb_abort();
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
playback_start_item(void *arg, int *retval)
{
  struct db_queue_item *queue_item = arg;
  struct media_file_info *mfi;
  struct output_device *device;
  struct player_source *ps;
  uint32_t seek_ms;
  int ret;

  if (player_state == PLAY_PLAYING)
    {
      DPRINTF(E_DBG, L_PLAYER, "Player is already playing, ignoring call to playback start\n");

      status_update(player_state);

      *retval = 1; // Value greater 0 will prevent execution of the bottom half function
      return COMMAND_END;
    }

  if (player_state == PLAY_STOPPED && !queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Failed to start/resume playback, no queue item given\n");

      *retval = -1;
      return COMMAND_END;
    }

  if (!queue_item)
    {
      ps = pb_session.playing_now;
      if (!ps)
	{
	  DPRINTF(E_WARN, L_PLAYER, "Bug! playing_now is null but playback is not stopped!\n");
	  *retval = -1;
	  return COMMAND_END;
	}

      DPRINTF(E_DBG, L_PLAYER, "Resume playback of '%s' (id=%d, item-id=%d)\n", ps->path, ps->id, ps->item_id);
    }
  else
    {
      // Start playback for given queue item
      DPRINTF(E_DBG, L_PLAYER, "Start playback of '%s' (id=%d, item-id=%d)\n", queue_item->path, queue_item->file_id, queue_item->id);

      // Look up where we should start
      seek_ms = 0;
      if (queue_item->file_id > 0)
	{
	  mfi = db_file_fetch_byid(queue_item->file_id);
	  if (mfi)
	    {
	      seek_ms = mfi->seek;
	      free_mfi(mfi, 0);
	    }
	}

      ret = pb_session_start(queue_item, seek_ms);
      if (ret < 0)
	{
	  *retval = -1;
	  return COMMAND_END;
	}
    }

  metadata_trigger(1);

  // Start sessions on selected devices
  *retval = 0;

  for (device = output_device_list; device; device = device->next)
    {
      if (device->selected && !device->session)
	{
	  ret = outputs_device_start(device, device_restart_cb);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not start selected %s device '%s'\n", device->type_name, device->name);
	      continue;
	    }

	  DPRINTF(E_INFO, L_PLAYER, "Using selected %s device '%s'\n", device->type_name, device->name);
	  (*retval)++;
	}
    }

  // If autoselecting is enabled, try to autoselect a non-selected device if the above failed
  if (speaker_autoselect && (*retval == 0) && (output_sessions == 0))
    for (device = output_device_list; device; device = device->next)
      {
	if ((outputs_priority(device) == 0) || device->session)
	  continue;

	speaker_select_output(device);
	ret = outputs_device_start(device, device_restart_cb);
	if (ret < 0)
	  {
	    DPRINTF(E_DBG, L_PLAYER, "Could not autoselect %s device '%s'\n", device->type_name, device->name);
	    speaker_deselect_output(device);
	    continue;
	  }

	DPRINTF(E_INFO, L_PLAYER, "Autoselecting %s device '%s'\n", device->type_name, device->name);
	(*retval)++;
	break;
      }

  // We're async if we need to start devices
  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise, just run the bottom half
  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_start_id(void *arg, int *retval)
{
  struct db_queue_item *queue_item = NULL;
  union player_arg *cmdarg = arg;
  enum command_state cmd_state;
  int ret;

  *retval = -1;

  if (player_state == PLAY_STOPPED)
    {
      db_queue_clear(0);

      ret = db_queue_add_by_fileid(cmdarg->id, 0, 0, -1, NULL, NULL);
      if (ret < 0)
	return COMMAND_END;

      queue_item = db_queue_fetch_byfileid(cmdarg->id);
      if (!queue_item)
	return COMMAND_END;
    }

  cmd_state = playback_start_item(queue_item, retval);

  free_queue_item(queue_item, 0);

  return cmd_state;
}

static enum command_state
playback_start(void *arg, int *retval)
{
  struct db_queue_item *queue_item = NULL;
  enum command_state cmd_state;

  *retval = -1;

  if (player_state == PLAY_STOPPED)
    {
      // Start playback of first item in queue
      queue_item = db_queue_fetch_bypos(0, shuffle);
      if (!queue_item)
	return COMMAND_END;
    }

  cmd_state = playback_start_item(queue_item, retval);

  free_queue_item(queue_item, 0);

  return cmd_state;
}

static enum command_state
playback_prev_bh(void *arg, int *retval)
{
  struct db_queue_item *queue_item;
  int ret;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause pb_abort()
  if (player_state == PLAY_STOPPED)
    {
      goto error;
    }

  // Only add to history if playback started
  if (pb_session.playing_now->pos_ms > 0)
    history_add(pb_session.playing_now->id, pb_session.playing_now->item_id);

  // Only skip to the previous song if the playing time is less than 3 seconds,
  // otherwise restart the current song.
  if (pb_session.playing_now->pos_ms < 3000)
    queue_item = queue_item_prev(pb_session.playing_now->item_id);
  else
    queue_item = db_queue_fetch_byitemid(pb_session.playing_now->item_id);
  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error finding previous source, queue item has disappeared\n");
      goto error;
    }

  ret = pb_session_start(queue_item, 0);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error skipping to previous item, aborting playback\n");
      goto error;
    }

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;

 error:
  pb_abort();
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
playback_next_bh(void *arg, int *retval)
{
  struct db_queue_item *queue_item;
  int ret;
  int id;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause pb_abort()
  if (player_state == PLAY_STOPPED)
    {
      goto error;
    }

  // Only add to history if playback started
  if (pb_session.playing_now->pos_ms > 0)
    {
      history_add(pb_session.playing_now->id, pb_session.playing_now->item_id);

      id = (int)(pb_session.playing_now->id);
      worker_execute(skipcount_inc_cb, &id, sizeof(int), 5);
    }

  if (consume)
    db_queue_delete_byitemid(pb_session.playing_now->item_id);

  queue_item = queue_item_next(pb_session.playing_now->item_id);
  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error finding next source, queue item has disappeared\n");
      goto error;
    }

  ret = pb_session_start(queue_item, 0);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error skipping to next item, aborting playback\n");
      goto error;
    }

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;

 error:
  pb_abort();
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
playback_seek_bh(void *arg, int *retval)
{
  struct db_queue_item *queue_item;
  union player_arg *cmdarg = arg;
  int ret;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause pb_abort()
  if (player_state == PLAY_STOPPED)
    {
      goto error;
    }

  queue_item = db_queue_fetch_byitemid(pb_session.playing_now->item_id);
  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error seeking in source, queue item has disappeared\n");
      goto error;
    }

  ret = pb_session_start(queue_item, cmdarg->intval);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error seeking to %d, aborting playback\n", cmdarg->intval);
      goto error;
    }

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;

 error:
  pb_abort();
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
playback_pause_bh(void *arg, int *retval)
{
  struct db_queue_item *queue_item;
  int ret;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause pb_abort()
  if (player_state == PLAY_STOPPED)
    {
      goto error;
    }

  queue_item = db_queue_fetch_byitemid(pb_session.playing_now->item_id);
  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error pausing source, queue item has disappeared\n");
      goto error;
    }

  ret = pb_session_start(queue_item, pb_session.playing_now->pos_ms);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error pausing source, aborting playback\n");
      goto error;
    }

  status_update(PLAY_PAUSED);

  seek_save();

  *retval = 0;
  return COMMAND_END;

 error:
  pb_abort();
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
playback_pause(void *arg, int *retval)
{
  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  if (player_state == PLAY_PAUSED)
    {
      *retval = 0;
      return COMMAND_END;
    }

  pb_timer_stop();

  *retval = outputs_flush(device_flush_cb);
  outputs_metadata_purge();

  // We're async if we need to flush devices
  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise, just run the bottom half
  return COMMAND_END;
}

static void
device_to_speaker_info(struct player_speaker_info *spk, struct output_device *device)
{
  memset(spk, 0, sizeof(struct player_speaker_info));
  spk->id = device->id;
  strncpy(spk->name, device->name, sizeof(spk->name));
  spk->name[sizeof(spk->name) - 1] = '\0';
  strncpy(spk->output_type, device->type_name, sizeof(spk->output_type));
  spk->output_type[sizeof(spk->output_type) - 1] = '\0';
  spk->relvol = device->relvol;
  spk->absvol = device->volume;

  spk->selected = device->selected;
  spk->has_password = device->has_password;
  spk->has_video = device->has_video;
  spk->requires_auth = device->requires_auth;
  spk->needs_auth_key = (device->requires_auth && device->auth_key == NULL);
}

static enum command_state
speaker_enumerate(void *arg, int *retval)
{
  struct spk_enum *spk_enum = arg;
  struct output_device *device;
  struct player_speaker_info spk;

  for (device = output_device_list; device; device = device->next)
    {
      device_to_speaker_info(&spk, device);
      spk_enum->cb(&spk, spk_enum->arg);
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
speaker_get_byid(void *arg, int *retval)
{
  struct speaker_get_param *spk_param = arg;
  struct output_device *device;

  for (device = output_device_list; device; device = device->next)
    {
      if ((device->advertised || device->selected)
	  && device->id == spk_param->spk_id)
	{
	  device_to_speaker_info(spk_param->spk_info, device);
	  *retval = 0;
	  return COMMAND_END;
	}
    }

  // No output device found with matching id
  *retval = -1;
  return COMMAND_END;
}

static int
speaker_activate(struct output_device *device)
{
  int ret;

  if (device->has_password && !device->password)
    {
      DPRINTF(E_INFO, L_PLAYER, "The %s device '%s' is password-protected, but we don't have it\n", device->type_name, device->name);

      return -2;
    }

  DPRINTF(E_DBG, L_PLAYER, "The %s device '%s' is selected\n", device->type_name, device->name);

  if (!device->selected)
    speaker_select_output(device);

  if (device->session)
    return 0;

  if (player_state == PLAY_PLAYING)
    {
      DPRINTF(E_DBG, L_PLAYER, "Activating %s device '%s'\n", device->type_name, device->name);

      ret = outputs_device_start(device, device_activate_cb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not start %s device '%s'\n", device->type_name, device->name);
	  goto error;
	}
    }
  else
    {
      DPRINTF(E_DBG, L_PLAYER, "Probing %s device '%s'\n", device->type_name, device->name);

      ret = outputs_device_probe(device, device_probe_cb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not probe %s device '%s'\n", device->type_name, device->name);
	  goto error;
	}
    }

  return 0;

 error:
  DPRINTF(E_LOG, L_PLAYER, "Could not activate %s device '%s'\n", device->type_name, device->name);
  speaker_deselect_output(device);
  return -1;
}

static int
speaker_deactivate(struct output_device *device)
{
  DPRINTF(E_DBG, L_PLAYER, "Deactivating %s device '%s'\n", device->type_name, device->name);

  if (device->selected)
    speaker_deselect_output(device);

  if (!device->session)
    return 0;

  outputs_device_stop(device, device_shutdown_cb);
  return 1;
}

static enum command_state
speaker_set(void *arg, int *retval)
{
  struct speaker_set_param *speaker_set_param = arg;
  struct output_device *device;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  *retval = 0;
  ids = speaker_set_param->device_ids;

  if (ids)
    nspk = ids[0];
  else
    nspk = 0;

  DPRINTF(E_DBG, L_PLAYER, "Speaker set: %d speakers\n", nspk);

  *retval = 0;

  for (device = output_device_list; device; device = device->next)
    {
      for (i = 1; i <= nspk; i++)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Set %" PRIu64 " device %" PRIu64 "\n", ids[i], device->id);

	  if (ids[i] == device->id)
	    break;
	}

      if (i <= nspk)
	{
	  ret = speaker_activate(device);

	  if (ret > 0)
	    (*retval)++;
	  else if (ret < 0 && speaker_set_param->intval != -2)
	    speaker_set_param->intval = ret;
	}
      else
	{
	  ret = speaker_deactivate(device);

	  if (ret > 0)
	    (*retval)++;
	}
    }

  if (*retval > 0)
    return COMMAND_PENDING; // async

  *retval = speaker_set_param->intval;
  return COMMAND_END;
}

static enum command_state
speaker_enable(void *arg, int *retval)
{
  uint64_t *id = arg;
  struct output_device *device;

  device = outputs_device_get(*id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker enable: '%s' (id=%" PRIu64 ")\n", device->name, *id);

  *retval = speaker_activate(device);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_disable(void *arg, int *retval)
{
  uint64_t *id = arg;
  struct output_device *device;

  device = outputs_device_get(*id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker disable: '%s' (id=%" PRIu64 ")\n", device->name, *id);

  *retval = speaker_deactivate(device);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;
  int volume;

  *retval = 0;
  volume = cmdarg->intval;

  if (master_volume == volume)
    return COMMAND_END;

  master_volume = volume;

  for (device = output_device_list; device; device = device->next)
    {
      if (!device->selected)
	continue;

      device->volume = rel_to_vol(device->relvol);

#ifdef DEBUG_RELVOL
      DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif

      if (device->session)
	*retval += outputs_device_volume_set(device, device_command_cb);
    }

  listener_notify(LISTENER_VOLUME);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

#ifdef DEBUG_RELVOL
static void debug_print_speaker()
{
  struct output_device *device;

  DPRINTF(E_DBG, L_PLAYER, "*** Master: %d\n", master_volume);

  for (device = output_device_list; device; device = device->next)
    {
      if (!device->selected)
	continue;

      DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
    }
}
#endif

static enum command_state
volume_setrel_speaker(void *arg, int *retval)
{
  struct volume_param *vol_param = arg;
  struct output_device *device;
  uint64_t id;
  int relvol;

  *retval = 0;
  id = vol_param->spk_id;
  relvol = vol_param->volume;

  for (device = output_device_list; device; device = device->next)
    {
      if (device->id != id)
	continue;

      if (!device->selected)
	{
	  *retval = 0;
	  return COMMAND_END;
	}

      device->relvol = relvol;
      device->volume = rel_to_vol(relvol);

#ifdef DEBUG_RELVOL
      DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif

      if (device->session)
	*retval += outputs_device_volume_set(device, device_command_cb);

      break;
    }

  volume_master_find();

#ifdef DEBUG_RELVOL
  debug_print_speaker();
#endif
  listener_notify(LISTENER_VOLUME);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_setabs_speaker(void *arg, int *retval)
{
  struct volume_param *vol_param = arg;
  struct output_device *device;
  uint64_t id;
  int volume;

  *retval = 0;
  id = vol_param->spk_id;
  volume = vol_param->volume;

  master_volume = volume;

  for (device = output_device_list; device; device = device->next)
    {
      if (!device->selected)
	continue;

      if (device->id != id)
	{
	  device->relvol = vol_to_rel(device->volume);

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif
	  continue;
	}
      else
	{
	  device->relvol = 100;
	  device->volume = master_volume;

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif

	  if (device->session)
	    *retval += outputs_device_volume_set(device, device_command_cb);
	}
    }

  volume_master_find();

#ifdef DEBUG_RELVOL
  debug_print_speaker();
#endif

  listener_notify(LISTENER_VOLUME);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

// Just updates internal volume params (does not make actual requests to the speaker)
static enum command_state
volume_byactiveremote(void *arg, int *retval)
{
  struct activeremote_param *ar_param = arg;
  struct output_device *device;
  uint32_t activeremote;
  int volume;

  *retval = 0;
  activeremote = ar_param->activeremote;

  for (device = output_device_list; device; device = device->next)
    {
      if ((uint32_t)device->id == activeremote)
	break;
    }

  if (!device)
    {
      DPRINTF(E_LOG, L_DACP, "Could not find speaker with Active-Remote id %d\n", activeremote);
      *retval = -1;
      return COMMAND_END;
    }

  volume = outputs_device_volume_to_pct(device, ar_param->value); // Only converts
  if (volume < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not parse volume given by Active-Remote id %d\n", activeremote);
      *retval = -1;
      return COMMAND_END;
    }

  device->volume = volume;

  volume_master_find();

#ifdef DEBUG_RELVOL
  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif

  listener_notify(LISTENER_VOLUME);

  return COMMAND_END;
}

static enum command_state
repeat_set(void *arg, int *retval)
{
  enum repeat_mode *mode = arg;

  if (*mode == repeat)
    {
      *retval = 0;
      return COMMAND_END;
    }

  switch (*mode)
    {
      case REPEAT_OFF:
      case REPEAT_SONG:
      case REPEAT_ALL:
	repeat = *mode;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid repeat mode: %d\n", *mode);
	*retval = -1;
	return COMMAND_END;
    }

  listener_notify(LISTENER_OPTIONS);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
shuffle_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  char new_shuffle;

  new_shuffle = (cmdarg->intval == 0) ? 0 : 1;

  // Ignore unchanged shuffle mode requests
  if (new_shuffle == shuffle)
    goto out;

  // Update queue and notify listeners
  if (new_shuffle)
    {
      if (pb_session.playing_now)
	db_queue_reshuffle(pb_session.playing_now->item_id);
      else
	db_queue_reshuffle(0);
    }
  else
    {
      db_queue_inc_version();
    }

  // Update shuffle mode and notify listeners
  shuffle = new_shuffle;
  listener_notify(LISTENER_OPTIONS);

 out:
  *retval = 0;
  return COMMAND_END;
}

static enum command_state
consume_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;

  consume = cmdarg->intval;

  listener_notify(LISTENER_OPTIONS);

  *retval = 0;
  return COMMAND_END;
}

/*
 * Removes all items from the history
 */
static enum command_state
playerqueue_clear_history(void *arg, int *retval)
{
  memset(history, 0, sizeof(struct player_history));

  cur_plversion++; // TODO [db_queue] need to update db queue version

  listener_notify(LISTENER_QUEUE);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playerqueue_plid(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  cur_plid = cmdarg->id;

  *retval = 0;
  return COMMAND_END;
}


/* ------------------------------- Player API ------------------------------- */

int
player_get_status(struct player_status *status)
{
  int ret;

  ret = commands_exec_sync(cmdbase, get_status, NULL, status);
  return ret;
}


/* --------------------------- Thread: httpd (DACP) ------------------------- */

/*
 * Stores the now playing media item dbmfi-id in the given id pointer.
 *
 * @param id Pointer will hold the playing item (dbmfi) id if the function returns 0
 * @return 0 on success, -1 on failure (e. g. no playing item found)
 */
int
player_playing_now(uint32_t *id)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playing_now, NULL, id);
  return ret;
}

/*
 * Starts/resumes playback
 *
 * Depending on the player state, this will either resume playing the current
 * item (player is paused) or begin playing the queue from the beginning.
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_start, playback_start_bh, NULL);
  return ret;
}

/*
 * Starts/resumes playback of the given queue_item
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item id.
 *
 * @param queue_item to start playing
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start_byitem(struct db_queue_item *queue_item)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_start_item, playback_start_bh, queue_item);
  return ret;
}

int
player_playback_start_byid(uint32_t id)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.id = id;

  ret = commands_exec_sync(cmdbase, playback_start_id, playback_start_bh, &cmdarg);
  return ret;
}

int
player_playback_stop(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_stop, NULL, NULL);
  return ret;
}

int
player_playback_abort(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_abort, NULL, NULL);
  return ret;
}

int
player_playback_pause(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_pause, playback_pause_bh, NULL);
  return ret;
}

int
player_playback_seek(int ms)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.intval = ms;

  ret = commands_exec_sync(cmdbase, playback_pause, playback_seek_bh, &cmdarg);
  return ret;
}

int
player_playback_next(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_pause, playback_next_bh, NULL);
  return ret;
}

int
player_playback_prev(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_pause, playback_prev_bh, NULL);
  return ret;
}

void
player_speaker_enumerate(spk_enum_cb cb, void *arg)
{
  struct spk_enum spk_enum;

  spk_enum.cb = cb;
  spk_enum.arg = arg;

  commands_exec_sync(cmdbase, speaker_enumerate, NULL, &spk_enum);
}

int
player_speaker_set(uint64_t *ids)
{
  struct speaker_set_param speaker_set_param;
  int ret;

  speaker_set_param.device_ids = ids;
  speaker_set_param.intval = 0;

  ret = commands_exec_sync(cmdbase, speaker_set, NULL, &speaker_set_param);

  listener_notify(LISTENER_SPEAKER);

  return ret;
}

int
player_speaker_get_byid(uint64_t id, struct player_speaker_info *spk)
{
  struct speaker_get_param param;
  int ret;

  param.spk_id = id;
  param.spk_info = spk;

  ret = commands_exec_sync(cmdbase, speaker_get_byid, NULL, &param);
  return ret;
}

int
player_speaker_enable(uint64_t id)
{
  int ret;

  ret = commands_exec_sync(cmdbase, speaker_enable, NULL, &id);

  listener_notify(LISTENER_SPEAKER);

  return ret;
}

int
player_speaker_disable(uint64_t id)
{
  int ret;

  ret = commands_exec_sync(cmdbase, speaker_disable, NULL, &id);

  listener_notify(LISTENER_SPEAKER);

  return ret;
}

int
player_volume_set(int vol)
{
  union player_arg cmdarg;
  int ret;

  if (vol < 0 || vol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_set is out of range\n", vol);
      return -1;
    }

  cmdarg.intval = vol;

  ret = commands_exec_sync(cmdbase, volume_set, NULL, &cmdarg);
  return ret;
}

int
player_volume_setrel_speaker(uint64_t id, int relvol)
{
  struct volume_param vol_param;
  int ret;

  if (relvol < 0 || relvol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setrel_speaker is out of range\n", relvol);
      return -1;
    }

  vol_param.spk_id = id;
  vol_param.volume = relvol;

  ret = commands_exec_sync(cmdbase, volume_setrel_speaker, NULL, &vol_param);
  return ret;
}

int
player_volume_setabs_speaker(uint64_t id, int vol)
{
  struct volume_param vol_param;
  int ret;

  if (vol < 0 || vol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setabs_speaker is out of range\n", vol);
      return -1;
    }

  vol_param.spk_id = id;
  vol_param.volume = vol;

  ret = commands_exec_sync(cmdbase, volume_setabs_speaker, NULL, &vol_param);
  return ret;
}

int
player_volume_byactiveremote(uint32_t activeremote, const char *value)
{
  struct activeremote_param ar_param;
  int ret;

  ar_param.activeremote = activeremote;
  ar_param.value = value;

  ret = commands_exec_sync(cmdbase, volume_byactiveremote, NULL, &ar_param);
  return ret;
}

int
player_repeat_set(enum repeat_mode mode)
{
  int ret;

  ret = commands_exec_sync(cmdbase, repeat_set, NULL, &mode);
  return ret;
}

int
player_shuffle_set(int enable)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.intval = enable;

  ret = commands_exec_sync(cmdbase, shuffle_set, NULL, &cmdarg);
  return ret;
}

int
player_consume_set(int enable)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.intval = enable;

  ret = commands_exec_sync(cmdbase, consume_set, NULL, &cmdarg);
  return ret;
}

void
player_queue_clear_history()
{
  commands_exec_sync(cmdbase, playerqueue_clear_history, NULL, NULL);
}

void
player_queue_plid(uint32_t plid)
{
  union player_arg cmdarg;

  cmdarg.id = plid;

  commands_exec_sync(cmdbase, playerqueue_plid, NULL, &cmdarg);
}

struct player_history *
player_history_get(void)
{
  return history;
}


/* ------------------- Non-blocking commands used by mDNS ------------------- */

int
player_device_add(void *device)
{
  union player_arg *cmdarg;
  int ret;

  cmdarg = calloc(1, sizeof(union player_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");
      return -1;
    }

  cmdarg->device = device;

  ret = commands_exec_async(cmdbase, device_add, cmdarg);
  return ret;
}

int
player_device_remove(void *device)
{
  union player_arg *cmdarg;
  int ret;

  cmdarg = calloc(1, sizeof(union player_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");
      return -1;
    }

  cmdarg->device = device;

  ret = commands_exec_async(cmdbase, device_remove_family, cmdarg);
  return ret;
}

static void
player_device_auth_kickoff(enum output_types type, char **arglist)
{
  union player_arg *cmdarg;

  cmdarg = calloc(1, sizeof(union player_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");
      return;
    }

  cmdarg->auth.type = type;
  memcpy(cmdarg->auth.pin, arglist[0], 4);

  commands_exec_async(cmdbase, device_auth_kickoff, cmdarg);
}


/* --------------------------- Thread: filescanner -------------------------- */

void
player_raop_verification_kickoff(char **arglist)
{
  player_device_auth_kickoff(OUTPUT_TYPE_RAOP, arglist);
}


/* ---------------------------- Thread: worker ------------------------------ */

void
player_metadata_send(void *imd, void *omd)
{
  struct metadata_param metadata_param;

  metadata_param.input = imd;
  metadata_param.output = omd;

  commands_exec_sync(cmdbase, device_metadata_send, NULL, &metadata_param);
}


/* ---------------------------- Thread: player ------------------------------ */

static void *
player(void *arg)
{
  struct output_device *device;
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_player);

  if (!player_exit)
    DPRINTF(E_LOG, L_PLAYER, "Player event loop terminated ahead of time!\n");

  db_speaker_clear_all();

  for (device = output_device_list; device; device = device->next)
    {
      ret = db_speaker_save(device);
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Could not save state for %s device '%s'\n", device->type_name, device->name);
    }

  db_perthread_deinit();

  pthread_exit(NULL);
}


/* ----------------------------- Thread: main ------------------------------- */

int
player_init(void)
{
  uint64_t interval;
  int ret;

  speaker_autoselect = cfg_getbool(cfg_getsec(cfg, "general"), "speaker_autoselect");
  clear_queue_on_stop_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");

  master_volume = -1;

  player_state = PLAY_STOPPED;
  repeat = REPEAT_OFF;

  CHECK_NULL(L_PLAYER, history = calloc(1, sizeof(struct player_history)));

  // Determine if the resolution of the system timer is > or < the size
  // of an audio packet. NOTE: this assumes the system clock resolution
  // is less than one second.
  if (clock_getres(CLOCK_MONOTONIC, &player_timer_res) < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get the system timer resolution.\n");
      goto error_history_free;
    }

  if (!cfg_getbool(cfg_getsec(cfg, "general"), "high_resolution_clock"))
    {
      DPRINTF(E_INFO, L_PLAYER, "High resolution clock not enabled on this system (res is %ld)\n", player_timer_res.tv_nsec);
      player_timer_res.tv_nsec = 10 * PLAYER_TICK_INTERVAL * 1000000;
    }

  // Set the tick interval for the playback timer
  interval = MAX(player_timer_res.tv_nsec, PLAYER_TICK_INTERVAL * 1000000);
  player_tick_interval.tv_nsec = interval;

  pb_write_deficit_max = (PLAYER_WRITE_BEHIND_MAX * 1000000 / interval);

  // Create the playback timer
#ifdef HAVE_TIMERFD
  pb_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  ret = pb_timer_fd;
#else
  ret = timer_create(CLOCK_MONOTONIC, NULL, &pb_timer);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer: %s\n", strerror(errno));
      goto error_history_free;
    }

  CHECK_NULL(L_PLAYER, evbase_player = event_base_new());
  CHECK_NULL(L_PLAYER, player_pause_timeout_ev = evtimer_new(evbase_player, pause_timer_cb, NULL));
#ifdef HAVE_TIMERFD
  CHECK_NULL(L_PLAYER, pb_timer_ev = event_new(evbase_player, pb_timer_fd, EV_READ | EV_PERSIST, playback_cb, NULL));
#else
  CHECK_NULL(L_PLAYER, pb_timer_ev = event_new(evbase_player, SIGALRM, EV_SIGNAL | EV_PERSIST, playback_cb, NULL));
#endif
  CHECK_NULL(L_PLAYER, cmdbase = commands_base_new(evbase_player, NULL));

  ret = outputs_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Output initiation failed\n");
      goto error_evbase_free;
    }

  ret = input_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Input initiation failed\n");
      goto error_outputs_deinit;
    }

  ret = pthread_create(&tid_player, NULL, player, NULL);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Could not spawn player thread: %s\n", strerror(errno));
      goto error_input_deinit;
    }
#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_player, "player");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_player, "player");
#endif

  return 0;

 error_input_deinit:
  input_deinit();
 error_outputs_deinit:
  outputs_deinit();
 error_evbase_free:
  commands_base_free(cmdbase);
  event_base_free(evbase_player);
#ifdef HAVE_TIMERFD
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif
 error_history_free:
  free(history);

  return -1;
}

void
player_deinit(void)
{
  int ret;

  player_playback_abort();

#ifdef HAVE_TIMERFD
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif

  input_deinit();

  outputs_deinit();

  player_exit = 1;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_player, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not join player thread: %s\n", strerror(errno));

      return;
    }

  free(history);

  event_base_free(evbase_player);
}
