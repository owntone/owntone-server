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
 * - output device handling (outsourced to outputs.c)
 * - notify about playback status changes
 * - maintain the playback queue
 * 
 * The player thread should never be making operations that may block, since
 * that could block callers requesting status (effectively making the server
 * unresponsive) and it could also starve the outputs. In practice this rule is
 * not always obeyed, for instance some outputs do their setup in ways that
 * could block.
 *
 * Listener events
 * ---------------
 * Events will be signaled via listener_notify(). The following rules apply to
 * how this must be done in the code:
 * - always use status_update() to make sure the callbacks that the listener
 *   makes do not block the player thread, and to avoid any risk of deadlocks
 * - if the event is a result of an external command then trigger it when the
 *   command is completed, so generally in a bottom half
 *
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

#ifdef HAVE_TIMERFD
# include <sys/timerfd.h>
#else
# include <signal.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>

#include <gcrypt.h>

#include "db.h"
#include "logger.h"
#include "conffile.h"
#include "settings.h"
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
#include "listenbrainz.h"

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

// If a speaker fails during playback we try to bring it back by reconnecting
// after this number of seconds. When this feature was added, we had an issue
// with Homepods and ATV4's dropping connections, so it is also a workaround.
#define PLAYER_SPEAKER_RESURRECT_TIME 5

// Shorthand condition for outputs_start and outputs_device_start, both need to
// know if they should only probe the device, or fully start it.
#define PLAYER_ONLY_PROBE (player_state != PLAY_PLAYING)

// Name of settings used by player
#define PLAYER_SETTINGS_MODE_REPEAT "player_mode_repeat"
#define PLAYER_SETTINGS_MODE_SHUFFLE "player_mode_shuffle"
#define PLAYER_SETTINGS_MODE_CONSUME "player_mode_consume"

//#define DEBUG_PLAYER 1

struct spk_enum
{
  spk_enum_cb cb;
  void *arg;
};

struct speaker_set_param
{
  uint64_t *device_ids;
};

struct speaker_attr_param
{
  uint64_t spk_id;

  int volume;
  const char *volstr;

  bool prevent_playback;
  bool busy;

  struct media_quality quality;
  enum media_format format;

  int audio_fd;
  int metadata_fd;

  const char *pin;
};

struct speaker_get_param
{
  uint64_t spk_id;
  uint32_t active_remote;
  uint32_t index;
  const char *address;
  struct player_speaker_info *spk_info;
};

struct speaker_auth_param
{
  enum output_types type;
  char pin[5];
};

struct player_seek_param
{
  int ms;
  enum player_seek_mode mode;
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
  // Id of the file/item in the files database
  uint32_t id;

  // Item-Id of the file/item in the queue
  uint32_t item_id;

  // Length of the file/item in milliseconds, 0 for endless (unless the input
  // has given us a track length)
  uint32_t len_ms;

  // Set when opening the item based on initial track length (so is not changed
  // by later input track length metadata)
  bool is_seekable;

  // Quality of the source (sample rate etc.)
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

  // When we receive a metadata update from the input it shouldn't be pushed to
  // clients until the speakers have reached the position that matches the
  // position the player was reading at when it got INPUT_FLAG_METADATA.
  uint64_t metadata_update;

  // The number of milliseconds into the media that we started
  uint32_t seek_ms;

  // This should at any time match the millisecond position of the media that is
  // coming out of your device. Will be 0 during initial buffering.
  uint32_t pos_ms;

  // How many samples the outputs buffer before playing (=delay)
  int output_buffer_samples;

  // Linked list, where next is the next item to play
  struct player_source *prev;
  struct player_source *next;
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

  // The player sources also have a quality property, but in some situations
  // they may get cleared. So we also save it here.
  struct media_quality quality;

  // We try to read a fixed number of bytes from the source each clock tick,
  // but if it gives us less we increase this correspondingly
  size_t read_deficit;
  size_t read_deficit_max;

  // We send metadata when we start a session, everytime we end a track and if
  // the input gives us a new metadata event. This value tracks if we have sent
  // the starting metadata.
  bool metadata_sent;

  // Pointer to the head of the two-way linked list of items from the queue that
  // we are either playing or going to play. When an item has been played it is
  // removed from the list. The playing_now and reading_now pointers will point
  // to items inside this list (or to NULL).
  struct player_source *source_list;

  // The item from the queue being played right now. It should only be NULL when
  // there is no playback.
  struct player_source *playing_now;

  // The item from the queue which the player is currently doing input_read()
  // for. So "reading" means reading from the input buffer - the source itself
  // may already be closed by the input module.
  struct player_source *reading_now;
};

static struct player_session pb_session;

struct event_base *evbase_player;

static int player_exit;
static pthread_t tid_player;
static struct commands_base *cmdbase;

// Keep track of how many outputs need to call back when flushing internally
// from the player thread (where we can't use player_playback_pause)
static int player_flush_pending;

// Config values and player settings category
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

// Time between ticks, i.e. time between when playback_cb() is invoked
static struct timespec player_tick_interval;
// Timer resolution
static struct timespec player_timer_res;

// PLAYER_WRITE_BEHIND_MAX converted to clock ticks
static int pb_write_deficit_max;

// True if we are trying to recover from a major playback timer overrun (write problems)
static bool pb_write_recovery;

// Audio source
static uint32_t cur_plid;
static uint32_t cur_plversion;

// Play history
static struct player_history *history;

// When we receive track metadata from the input we have to wait until playback
// has reached the position before using it. We use this to record the update.
struct metadata_pending_register
{
  uint64_t pos;
  struct input_metadata *metadata;
} metadata_pending[16];


/* -------------------------------- Forwards -------------------------------- */

static void
pb_abort(void);

static int
pb_suspend(void);


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

// Callback from the worker thread (async operation as it may block)
static void
scrobble_cb(void *arg)
{
  int *id = arg;

#ifdef LASTFM
  lastfm_scrobble(*id);
#endif
  listenbrainz_scrobble(*id);
}

// This is just to be able to log the caller in a simple way
#define status_update(x, y) status_update_impl((x), (y), __func__)
static void
status_update_impl(enum play_status status, short listener_events, const char *caller)
{
  DPRINTF(E_DBG, L_PLAYER, "Status update - status: %d, events: %d, caller: %s\n", status, listener_events, caller);

  player_state = status;

  listener_notify(listener_events);
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
        goto error;
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
	    goto error;
	}
    }

  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Reached end of queue\n");
      return NULL;
    }

  return queue_item;

 error:
  DPRINTF(E_LOG, L_PLAYER, "Error fetching next item from queue (item-id=%" PRIu32 ", repeat=%d)\n", item_id, repeat);
  return NULL;
}

static struct db_queue_item *
queue_item_prev(uint32_t item_id)
{
  return db_queue_fetch_prev(item_id, shuffle);
}


/* ------ All this is for dealing with metadata received from the input ----- */

static int
metadata_pending_add(struct input_metadata *metadata, uint64_t pos)
{
  int i;

  if (pos == 0)
    return -1; // Invalid position

  for (i = 0; i < ARRAY_SIZE(metadata_pending); i++)
    {
      if (metadata_pending[i].metadata == NULL)
	break;
    }

  if (i == ARRAY_SIZE(metadata_pending))
    {
      DPRINTF(E_LOG, L_PLAYER, "Error, too many pending metadata updates\n");
      return -1;
    }

  metadata_pending[i].pos = pos;
  metadata_pending[i].metadata = metadata;
  return 0;
}

static uint64_t
metadata_pending_next_pos(void)
{
  uint64_t next_pos;
  int i;

  for (i = 0, next_pos = 0; i < ARRAY_SIZE(metadata_pending); i++)
    {
      if (metadata_pending[i].metadata && (metadata_pending[i].pos < next_pos || !next_pos))
	next_pos = metadata_pending[i].pos;
    }

  return next_pos;
}

static int
metadata_finalize_cb(struct output_metadata *metadata)
{
  if (!pb_session.playing_now)
    {
      DPRINTF(E_WARN, L_PLAYER, "Aborting metadata_send(), playback stopped during metadata preparation\n");
      return -1;
    }
  else if (metadata->item_id != pb_session.playing_now->item_id)
    {
      DPRINTF(E_WARN, L_PLAYER, "Aborting metadata_send(), item_id changed during metadata preparation (%" PRIu32 " -> %" PRIu32 ")\n",
	metadata->item_id, pb_session.playing_now->item_id);
      return -1;
    }

  if (!metadata->pos_ms)
    metadata->pos_ms = pb_session.playing_now->pos_ms;
  if (!metadata->len_ms)
    metadata->len_ms = pb_session.playing_now->len_ms;
  if (!metadata->pts.tv_sec)
    metadata->pts = pb_session.pts;

  return 0;
}

static enum command_state
metadata_finalize(void *arg, int *retval)
{
  if (!pb_session.playing_now)
    return COMMAND_END; // Playback ended while we doing the metadata update

  outputs_metadata_send(pb_session.playing_now->item_id, false, metadata_finalize_cb);

  status_update(player_state, LISTENER_PLAYER);

  return COMMAND_END;
}

// Done in worker thread because we avoid blocking db updates in the player
static void
metadata_update_queue_cb(void *arg)
{
  struct input_metadata *metadata = *(struct input_metadata **)arg;
  struct db_queue_item *queue_item;
  int ret;

  queue_item = db_queue_fetch_byitemid(metadata->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Could not update queue metadata, the item_id is unknown (%u)\n", metadata->item_id);
      input_metadata_free(metadata, 0);
      return;
    }

  // Update queue item if metadata changed
  if (metadata->artist || metadata->title || metadata->album || metadata->genre || metadata->artwork_url || metadata->len_ms)
    {
      // Since we won't be using the metadata struct values for anything else
      // than this we just swap pointers
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
      if (metadata->len_ms)
	queue_item->song_length = metadata->len_ms;

      ret = db_queue_item_update(queue_item);
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Database error while updating queue with new metadata\n");
    }

  free_queue_item(queue_item, 0);
  input_metadata_free(metadata, 0);

  // Now return to the player thread and run metadata_finalize
  commands_exec_async(cmdbase, metadata_finalize, NULL);
}


/* ----------- Audio source handling (interfaces with input module) --------- */

static void
source_free(struct player_source **ps)
{
  if (!(*ps))
    return;

  free((*ps)->path);
  free(*ps);

  *ps = NULL;
}

/*
 * Creates a new player source for the given queue item
 */
static struct player_source *
source_create(struct db_queue_item *queue_item, uint32_t seek_ms)
{
  struct player_source *ps;

  CHECK_NULL(L_PLAYER, ps = calloc(1, sizeof(struct player_source)));

  ps->id = queue_item->file_id;
  ps->item_id = queue_item->id;
  ps->data_kind = queue_item->data_kind;
  ps->media_kind = queue_item->media_kind;
  ps->len_ms = queue_item->song_length;
  ps->is_seekable = (queue_item->song_length > 0);
  ps->path = strdup(queue_item->path);
  ps->seek_ms = seek_ms;

  return ps;
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
    return NULL;

  ps = source_create(queue_item, 0);

  free_queue_item(queue_item, 0);

  return ps;
}

static void
source_stop(void)
{
  input_stop();
}

static int
source_start(struct player_source *ps)
{
  if (!ps)
    return 0;

  DPRINTF(E_DBG, L_PLAYER, "Opening track: '%s' (id=%d, seek=%d)\n", ps->path, ps->item_id, ps->seek_ms);

  input_flush(NULL);

  return input_seek(ps->item_id, (int)ps->seek_ms);
}

static void
source_next(struct player_source *ps)
{
  if (!ps)
    return;

  DPRINTF(E_DBG, L_PLAYER, "Opening next track: '%s' (id=%d)\n", ps->path, ps->item_id);

  input_start(ps->item_id);
}

static int
source_restart(struct player_source *ps)
{
  DPRINTF(E_DBG, L_PLAYER, "Restarting track: '%s' (id=%d, pos=%d)\n", ps->path, ps->item_id, ps->pos_ms);

  // Must be non-blocking, because otherwise we get a deadlock via the input
  // thread making a sync call to player_playback_start() -> pb_resume() ->
  // source_restart() -> input_resume()
  input_resume(ps->item_id, ps->pos_ms);

  return 0;
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
      pos += snprintf(line + pos, linesize - pos, "%s.read_start=%" PRIu64 "; ", name, ps->read_start);
      pos += snprintf(line + pos, linesize - pos, "%s.play_start=%" PRIu64 "; ", name, ps->play_start);
      pos += snprintf(line + pos, linesize - pos, "%s.read_end=%" PRIu64 "; ", name, ps->read_end);
      pos += snprintf(line + pos, linesize - pos, "%s.play_end=%" PRIu64 "; ", name, ps->play_end);
      pos += snprintf(line + pos, linesize - pos, "%s.metadata_update=%" PRIu64 "; ", name, ps->metadata_update);
      pos += snprintf(line + pos, linesize - pos, "%s.pos_ms=%u; ", name, ps->pos_ms);
      pos += snprintf(line + pos, linesize - pos, "%s.seek_ms=%u; ", name, ps->seek_ms);
    }
  else
    pos += snprintf(line + pos, linesize - pos, "%s=(null); ", name);

  return pos;
}

static void
session_dump(bool use_counter)
{
  struct player_source *ps;
  char line[4096];
  char label[32];
  int n;
  int pos = 0;

  if (use_counter)
    {
      debug_dump_counter++;
      if (debug_dump_counter % 100 != 0)
	return;
    }

  for (ps = pb_session.source_list, n = 0; ps; ps = ps->prev, n--)
    {
      pos = snprintf(line, sizeof(line), "pos=%d; ", pb_session.pos);
      if (ps == pb_session.playing_now && ps == pb_session.reading_now)
	snprintf(label, sizeof(label), "[%d][rp]", n);
      else if (ps == pb_session.playing_now)
	snprintf(label, sizeof(label), "[%d][p]", n);
      else if (ps == pb_session.reading_now)
	snprintf(label, sizeof(label), "[%d][r]", n);
      else
	snprintf(label, sizeof(label), "[%d][n]", n);

      pos += source_print(line + pos, sizeof(line) - pos, ps, label);

      DPRINTF(E_DBG, L_PLAYER, "%s\n", line);
    }
}
#endif

static void
session_update_play_eof(void)
{
  struct player_source *ps = pb_session.playing_now;

  pb_session.playing_now = pb_session.playing_now->next;

  // Remove the item we completed playing from source_list
  if (pb_session.playing_now)
    pb_session.playing_now->prev = NULL;
  else
    pb_session.source_list = NULL;

  // Free the removed item
  source_free(&ps);
}

static void
session_update_play_start(void)
{
  // Nothing to update right now
}

static void
session_update_read_next(struct player_source *ps)
{
  // Attach to linked source list
  ps->prev = pb_session.source_list;
  pb_session.source_list->next = ps;

  pb_session.source_list = ps;
}

static void
session_update_read_eof(void)
{
  pb_session.reading_now->read_end = pb_session.pos;
  pb_session.reading_now->play_end = pb_session.pos + pb_session.reading_now->output_buffer_samples;

  pb_session.reading_now = pb_session.reading_now->next;

  // There is nothing else to play
  if (!pb_session.reading_now)
    return;

  // We inherit this because the input will only notify on quality changes, not
  // if it is the same as the previous track
  pb_session.reading_now->quality = pb_session.reading_now->prev->quality;
  pb_session.reading_now->output_buffer_samples = pb_session.reading_now->prev->output_buffer_samples;

  pb_session.reading_now->read_start = pb_session.pos;
  pb_session.reading_now->play_start = pb_session.pos + pb_session.reading_now->output_buffer_samples;
}

static void
session_update_read_start(uint32_t seek_ms)
{
  pb_session.reading_now = pb_session.source_list;

  // There is nothing to play
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
  uint32_t step_ms;

  // Did we just complete our first read? Then set the start timestamp
  if (pb_session.start_ts.tv_sec == 0)
    {
      clock_gettime_with_res(CLOCK_MONOTONIC, &pb_session.start_ts, &player_timer_res);
      pb_session.pts = pb_session.start_ts;
    }

  // Advance position
  pb_session.pos += nsamples;

  // Need to know sample rate to calculate pos_ms step
  if (!pb_session.playing_now->quality.sample_rate)
    return;

  step_ms = (1000 * nsamples) / pb_session.playing_now->quality.sample_rate;

  // After we have started playing we also must calculate new pos_ms
  if (pb_session.pos > pb_session.playing_now->play_start)
    pb_session.playing_now->pos_ms += step_ms;
}

static void
session_update_read_quality(struct media_quality *quality)
{
  int samples_per_read;

  if (quality_is_equal(quality, &pb_session.quality))
    goto out;

  pb_session.quality = *quality;
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

  // Maybe we should actually adjust play_start and play_end of all items in the
  // source list when the quality changes?
  pb_session.reading_now->play_start = pb_session.reading_now->read_start + pb_session.reading_now->output_buffer_samples;
  if (pb_session.reading_now->prev)
    pb_session.reading_now->prev->play_end = pb_session.reading_now->play_start;

 out:
  free(quality);
}

static void
session_update_read_metadata(void)
{
  if (!pb_session.reading_now)
    return;

  // Sets when to trigger the next event_play_metadata()
  pb_session.reading_now->metadata_update = metadata_pending_next_pos();
}

static void
session_update_play_metadata(struct input_metadata *metadata)
{
  if (metadata->pos_is_updated)
    pb_session.playing_now->pos_ms = metadata->pos_ms;
  if (metadata->len_ms)
    pb_session.playing_now->len_ms = metadata->len_ms;
}

static void
session_restart(void)
{
  pb_session.start_ts.tv_sec = 0;
  pb_session.start_ts.tv_nsec = 0;
  pb_session.pts.tv_sec = 0;
  pb_session.pts.tv_nsec = 0;
  pb_session.read_deficit = 0;
  pb_session.metadata_sent = 0;
}

static void
session_stop(void)
{
  struct player_source *ps;

  free(pb_session.buffer);
  pb_session.buffer = NULL;

  for (ps = pb_session.source_list; pb_session.source_list; ps = pb_session.source_list)
    {
      pb_session.source_list = ps->prev;
      source_free(&ps);
    }

  memset(&pb_session, 0, sizeof(struct player_session));
}

static void
session_start(struct player_source *ps)
{
  session_stop();

  pb_session.source_list = ps;
}


/* ------------------------- Playback event handlers ------------------------ */

static void
event_read_quality(struct media_quality *quality)
{
  DPRINTF(E_DBG, L_PLAYER, "event_read_quality()\n");

  session_update_read_quality(quality);
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

  struct player_source *ps = source_next_create(pb_session.source_list);
  if (!ps)
    return;

  // Attaches next item to pb_session.source_list
  session_update_read_next(ps);

  // Starts the input read loop
  source_next(pb_session.source_list);
}

static void
event_read_metadata(struct input_metadata *metadata)
{
  uint64_t delay;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "event_read_metadata()\n");

  // Add the metadata to the register of pending events with a trigger position
  // that corresponds to OUTPUTS_BUFFER_DURATION into the future. If we have
  // received a negative position we assume the metadata needs to be delayed
  // until the position is 0.
  if (metadata->pos_is_updated && metadata->pos_ms < 0)
    {
      delay = pb_session.reading_now->output_buffer_samples + (-metadata->pos_ms) * (uint64_t)pb_session.reading_now->quality.sample_rate / 1000;
      metadata->pos_ms = 0;
    }
  else
    delay = pb_session.reading_now->output_buffer_samples;

  ret = metadata_pending_add(metadata, pb_session.pos + delay);
  if (ret < 0)
    {
      input_metadata_free(metadata, 0);
      return;
    }

  session_update_read_metadata();
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
      worker_execute(scrobble_cb, &id, sizeof(int), 8);
      history_add(pb_session.playing_now->id, pb_session.playing_now->item_id);
    }

  if (consume)
    db_queue_delete_byitemid(pb_session.playing_now->item_id);

  if (pb_session.playing_now->next)
    outputs_metadata_send(pb_session.playing_now->next->item_id, false, metadata_finalize_cb);

  session_update_play_eof();
}

static void
event_play_start()
{
  DPRINTF(E_DBG, L_PLAYER, "event_play_start()\n");

  if (!pb_session.metadata_sent)
    {
      outputs_metadata_send(pb_session.playing_now->item_id, true, metadata_finalize_cb);
      pb_session.metadata_sent = 1;
    }

  session_update_play_start();

  status_update(PLAY_PLAYING, LISTENER_PLAYER);
}

static void
event_play_metadata()
{
  int i;

  DPRINTF(E_DBG, L_PLAYER, "event_play_metadata()\n");

  for (i = 0; i < ARRAY_SIZE(metadata_pending); i++)
    {
      // Proces all events with position from metadata_update (included) to
      // current read position (excluded)
      if (!(metadata_pending[i].pos >= pb_session.playing_now->metadata_update && metadata_pending[i].pos < pb_session.pos))
	continue;

      // Just in case
      if (!metadata_pending[i].metadata)
	continue;

      session_update_play_metadata(metadata_pending[i].metadata);

      // Triggers an async chain of metadata update, first worker will do an
      // update of the db, then the player will update outputs, where the worker
      // may be called by the output, and then player sends status_update
      worker_execute(metadata_update_queue_cb, &(metadata_pending[i].metadata), sizeof(metadata_pending[i].metadata), 0);

      memset(&metadata_pending[i], 0, sizeof(struct metadata_pending_register));
    }

  // Set trigger (playing_now->metadata_update) to next pending metadata
  session_update_read_metadata();
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

  session_update_read(nsamples);

  // Check if the playback position passed the play_start position
  if (pb_session.pos > pb_session.playing_now->play_start && pb_session.pos <= pb_session.playing_now->play_start + nsamples)
    event_play_start();

  if (pb_session.playing_now->metadata_update == 0)
    return;

  // Check if the playback position passed an input metadata update. The event
  // must process all metadata updates in the read interval.
  if (pb_session.pos > pb_session.playing_now->metadata_update && pb_session.pos <= pb_session.playing_now->metadata_update + nsamples)
    event_play_metadata();
}


/* ---- Main playback stuff: Start, read, write and playback timer event ---- */

// Returns -1 on error or bytes read (possibly 0)
static inline int
source_read(int *nbytes, int *nsamples, uint8_t *buf, int len)
{
  short flag;
  void *flagdata;

  // We can get into this condition if a) we finished reading, but are still
  // playing (playing_now is non-null), or b) the calling loop tries to catch up
  // with an overrun or a deficit, but playback ended in the first iteration (in
  // which case playing_now is null). Note that a) also can occur if the input
  // starts with input_write(NULL, NULL, INPUT_FLAG_ERROR).
  if (!pb_session.reading_now)
    {
      // This is only for case a). If we are in case b) the session was zeroed,
      // which means nsamples will become zero. Quality can be zero if the input
      // failed immediately, i.e. never passed INPUT_FLAG_QUALITY.
      *nbytes = len;
      *nsamples = (len != 0) ? BTOS(*nbytes, pb_session.quality.bits_per_sample, pb_session.quality.channels) : 0;

      // In case a) this advances playback position and possibly ends playback,
      // i.e. sets playing_now to null
      event_read(*nsamples);
      if (!pb_session.playing_now)
	{
	  *nbytes = 0;
	  *nsamples = 0;
	  return 0;
	}

      // Stream silence if playback didn't end yet
      memset(buf, 0, len);
      return 0;
    }

  *nsamples = 0;
  *nbytes = input_read(buf, len, &flag, &flagdata);
  if ((*nbytes < 0) || (flag == INPUT_FLAG_ERROR))
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading source '%s' (id=%d)\n", pb_session.reading_now->path, pb_session.reading_now->id);
      event_read_error();
      return -1;
    }
  else if (flag == INPUT_FLAG_START_NEXT)
    {
      event_read_start_next();
    }
  else if (flag == INPUT_FLAG_EOF)
    {
      event_read_eof();
    }
  else if (flag == INPUT_FLAG_METADATA)
    {
      event_read_metadata((struct input_metadata *)flagdata);
    }
  else if (flag == INPUT_FLAG_QUALITY)
    {
      event_read_quality((struct media_quality *)flagdata);
    }

  if (*nbytes == 0 || pb_session.quality.channels == 0)
    {
      event_read(0); // This will set start_ts even if source isn't open yet
      return 0;
    }

  *nsamples = BTOS(*nbytes, pb_session.quality.bits_per_sample, pb_session.quality.channels);

  event_read(*nsamples);

  return 0;
}

static void
playback_cb(int fd, short what, void *arg)
{
  struct timespec ts;
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
      player_flush_pending = pb_suspend();
      // No devices to wait for, just set the restart cb right away. Otherwise
      // the trigger will be set by device_flush_cb.
      if (player_flush_pending == 0)
	input_buffer_full_cb(player_playback_start);

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
      ret = source_read(&nbytes, &nsamples, pb_session.buffer, pb_session.bufsize);
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

      outputs_write(pb_session.buffer, nbytes, nsamples, &pb_session.quality, &pb_session.pts);

      if (nbytes < pb_session.bufsize)
	{
	  // How much the number of samples we got corresponds to in time (nanoseconds)
	  ts.tv_sec = 0;
	  ts.tv_nsec = 1000000000UL * (uint64_t)nsamples / pb_session.quality.sample_rate;

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

      player_flush_pending = pb_suspend();
      // No devices to wait for, just set the restart cb right away. Otherwise
      // the trigger will be set by device_flush_cb.
      if (player_flush_pending == 0)
	input_buffer_full_cb(player_playback_start);
    }
}


/* ----------------- Output device handling (add/remove etc) ---------------- */

static enum command_state
device_add(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device = cmdarg->device;
  bool new_deselect;

  // Never turn on new devices during playback
  new_deselect = (player_state == PLAY_PLAYING);

  device = outputs_device_add(device, new_deselect);
  if (!device)
    {
      *retval = -1;
      return COMMAND_END;
    }

  status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);

  *retval = 0;
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
	outputs_device_remove(device);
    }

  outputs_device_free(remove);

  status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
device_auth_kickoff(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;

  // First find the device requiring verification
  for (device = outputs_list(); device; device = device->next)
    {
      if (device->type == cmdarg->auth.type && device->state == OUTPUT_STATE_PASSWORD)
	break;
    }

  if (!device)
    {
      *retval = -1;
      return COMMAND_END;
    }

  // We're async, so we don't care about return values or callbacks with result
  outputs_device_authorize(device, cmdarg->auth.pin, NULL);

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
    }
  else if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_WARN, L_PLAYER, "The %s device '%s' failed\n", device->type_name, device->name);

      // The device can fail outside of playback, e.g. if it disconnects after a
      // flush command
      if (player_state != PLAY_PLAYING)
	goto out;

      if (outputs_sessions_count() == 0)
	pb_suspend();

      if (!device->resurrect)
	goto out;

      DPRINTF(E_LOG, L_PLAYER, "Attempting reconnection in %d sec to the %s device '%s'\n", PLAYER_SPEAKER_RESURRECT_TIME, device->type_name, device->name);

      // TODO do this internally instead of through the worker
      worker_execute(player_speaker_resurrect, &(device->id), sizeof(device->id), PLAYER_SPEAKER_RESURRECT_TIME);
    }
  else if (status == OUTPUT_STATE_STOPPED)
    {
      DPRINTF(E_INFO, L_PLAYER, "The %s device '%s' stopped\n", device->type_name, device->name);
    }
  else
    {
      DPRINTF(E_DBG, L_PLAYER, "Callback from %s device %s to device_streaming_cb (status %d)\n", device->type_name, device->name, status);
      outputs_device_cb_set(device, device_streaming_cb);
    }

 out:
  // We don't do this in the other cb's because they are triggered by a command
  // and thus the update should be done as part of the command completion (which
  // can better determine which type of listener event to use)
  status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);
}

static void
device_volume_cb(struct output_device *device, enum output_device_state status)
{
  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Output device disappeared before command completion!\n");
      goto out;
    }
  else if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' failed during execution of volume command\n", device->type_name, device->name);
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s device %s to device_volume_cb (status %d)\n", device->type_name, device->name, status);

  outputs_device_cb_set(device, device_streaming_cb);

 out:
  // If a failure occurred when setting the volume, and we also don't have other
  // active sessions, then we suspend playback
  if (outputs_sessions_count() == 0)
    pb_suspend();

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
  else if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' failed during execution of flush command\n", device->type_name, device->name);
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s device %s to device_flush_cb (status %d)\n", device->type_name, device->name, status);

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

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared before shutdown completion!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s device %s to device_shutdown_cb (status %d)\n", device->type_name, device->name, status);

 out:
  commands_exec_end(cmdbase, retval);
}

static void
device_activate_cb(struct output_device *device, enum output_device_state status)
{
  int retval;

  retval = commands_exec_returnvalue(cmdbase);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during activation!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s device %s to device_activate_cb (status %d)\n", device->type_name, device->name, status);

  if (status == OUTPUT_STATE_PASSWORD)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' requires a valid PIN or password\n", device->type_name, device->name);

      outputs_device_deselect(device);

      retval = -2;
      goto out;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' failed to activate\n", device->type_name, device->name);

      outputs_device_deselect(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  // If we were just probing or doing device verification this is a no-op, since
  // there is no session any more
  outputs_device_cb_set(device, device_streaming_cb);

 out:
  commands_exec_end(cmdbase, retval);
}

const char *
player_pmap(void *p)
{
  if (p == device_activate_cb)
    return "device_activate_cb";
  else if (p == device_streaming_cb)
    return "device_streaming_cb";
  else if (p == device_volume_cb)
    return "device_volume_cb";
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
  uint32_t item_id;
  int ret;

  ps = source_create(queue_item, seek_ms);

  // Clears the session and attaches the new source as pb_session.source_list
  session_start(ps);

  // Sets of opening of the new source
  while ( (ret = source_start(ps)) < 0)
    {
      // Couldn't start requested item, skip to next and remove failed item from queue
      item_id = ps->item_id;
      ps = source_next_create(ps);

      // Will free pb_session.source_list so we don't memleak failed sources
      session_start(ps);
      db_queue_delete_byitemid(item_id);
    }

  session_update_read_start((uint32_t)ret);

  if (!pb_session.playing_now)
    return -1;

  return ret;
}

// Stops input source and stops read loop
static void
pb_session_pause(void)
{
  pb_timer_stop();

  seek_save();

  source_stop();
}

// Stops input source and deallocates pb_session content
static void
pb_session_stop(void)
{
  pb_timer_stop();

  seek_save();

  source_stop();

  session_stop();

  status_update(PLAY_STOPPED, LISTENER_PLAYER);
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

// Restarts the input (in case it was closed during the pause), resets session
// start timestamp and deficits, which is necessary after pb_suspend.
static int
pb_resume(void)
{
  struct player_source *ps;
  int ret;

  // Before pb_resume() is called it is important that source_list is set to
  // have just one item, and that both reading_now and playing_now point to it.
  // Otherwise it would mean that the input is currently reading an item that is
  // not being played, which means asking the input to resume playing_now
  // will bring us into a situation where the order of data read by input_read()
  // from the data input_buffer will not the match the order of the source_list.
  ps = pb_session.source_list;
  if (!ps || ps != pb_session.playing_now)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! pb_resume() called, but source list is invalid (%p, %p)\n", ps, pb_session.playing_now);
      pb_abort();
      return -1;
    }

  // In many cases the input will already be open, so this only has effect if
  // we are resuming after pause longer than INPUT_OPEN_TIMEOUT, or if the input
  // lost connection during the pause
  ret = source_restart(ps);
  if (ret < 0)
    {
      pb_abort();
      return -1;
    }

  session_restart();

  return 0;
}

// Temporarily suspends/resets playback, used when input buffer underruns or in
// case of problems writing to the outputs.
static int
pb_suspend(void)
{
  struct db_queue_item *queue_item;
  int flush_pending;
  int ret;

  // If ->next is set then suspend was called during a track change, which is a
  // tricky time. To simplify things, we reset the entire session, which also
  // means resetting the input, but still letting it proceed with the head of
  // source_list. Ideally, we instead want to resume with playing_now, because
  // with the current solution the user will loose a bit of audio. In practice,
  // that causes issues, because sometimes pb_suspend() is called because of an
  // output delay, which was caused by e.g. changing quality of the output
  // during track change. So going back to playing_now would make that repeat.
  if (pb_session.playing_now->next)
    {
      // So we restart the session with the head source, not playing_now
      queue_item = db_queue_fetch_byitemid(pb_session.source_list->item_id);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error suspending playback, could not retrieve queue item currently being played\n");
	  pb_abort();
	  return -1;
	}

      ret = pb_session_start(queue_item, 0);
      free_queue_item(queue_item, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error suspending playback, could not start session\n");
	  pb_abort();
	  return -1;
	}
    }

  flush_pending = outputs_flush(device_flush_cb);

  pb_timer_stop();

  status_update(PLAY_PAUSED, LISTENER_PLAYER);

  seek_save();

  return flush_pending;
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

  status->volume = outputs_volume_get();

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
	if (pb_session.playing_now->play_start == 0 || pb_session.pos < pb_session.playing_now->play_start)
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

  status_update(PLAY_STOPPED, LISTENER_PLAYER);

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

  // We also ask listeners to update speaker/volume state, since it is possible
  // some of the speakers we tried to start responded with failure
  status_update(PLAY_PLAYING, LISTENER_PLAYER | LISTENER_SPEAKER | LISTENER_VOLUME);

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
  uint32_t seek_ms;
  int ret;

  if (player_state == PLAY_PLAYING)
    {
      DPRINTF(E_DBG, L_PLAYER, "Player is already playing, ignoring call to playback start\n");

      status_update(player_state, LISTENER_PLAYER);

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
      ret = pb_resume();
      if (ret < 0)
	{
	  *retval = -1;
	  return COMMAND_END;
	}
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

  // Start sessions on selected devices. We shouldn't see any callbacks to
  // device_shutdown_cb, since the unselected devices shouldn't have sessions.
  *retval = outputs_start(device_activate_cb, device_shutdown_cb, false);
  if (*retval < 0)
    DPRINTF(E_WARN, L_PLAYER, "All selected speakers failed to start\n");

  // autoselect also applies in non-error cases (if no devices were selected).
  // Note that if session count is 0 then retval should never be positive,
  // because that would mean we are initiating a session. Just to be safe, we
  // check anyway.
  if (speaker_autoselect && outputs_sessions_count() == 0 && *retval <= 0)
    {
      for (device = outputs_list(); device; device = device->next)
	{
	  if (device->selected || outputs_priority(device) == 0 || device->session)
	    continue;

	  *retval = outputs_device_start(device, device_activate_cb, false);
	  if (*retval < 0)
	    continue;

	  DPRINTF(E_LOG, L_PLAYER, "Autoselected %s device '%s'\n", device->type_name, device->name);
	  outputs_device_select(device, -1);
	  break;
	}
    }

  // We're async if we need to wait for devices starting
  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise, just run the bottom half
  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_start_id(void *arg, int *retval)
{
  struct query_params qp = { .type = Q_ITEMS };
  struct db_queue_item *queue_item = NULL;
  union player_arg *cmdarg = arg;
  enum command_state cmd_state;
  int new_item_id;
  int ret;

  *retval = -1;

  if (player_state == PLAY_STOPPED)
    {
      db_queue_clear(0);

      qp.id = cmdarg->id;

      ret = db_queue_add_by_query(&qp, 0, 0, -1, NULL, &new_item_id);
      if (ret < 0)
	return COMMAND_END;

      queue_item = db_queue_fetch_byitemid(new_item_id);
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
  struct db_queue_item *queue_item = NULL;
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

  // Only skip to the previous song if the playing time is less than 3 seconds
  if (pb_session.playing_now->pos_ms < 3000)
    queue_item = queue_item_prev(pb_session.playing_now->item_id);
  // If there is no previous item in the queue or playing time is greater than 3 seconds, restart the current item
  if (!queue_item)
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

  queue_item = queue_item_next(pb_session.playing_now->item_id);

  if (consume)
    db_queue_delete_byitemid(pb_session.playing_now->item_id);

  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error finding next source, end of queue reached or queue item has disappeared\n");
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

/**
 * Based on the given seek parameters "seek_param" and the current playing track, the queue item and the absolute
 * position in milliseconds are calculated.
 *
 * @param queue_item out: queue item to play after the seek
 * @param position_ms out: absolute position in milliseconds the queue_item shoud start playing after the seek
 * @param seek_param in: seek parameters
 * @return 0 on success, -1 on error
 */
static int
seek_calc_position_ms(struct db_queue_item **queue_item, int *position_ms, struct player_seek_param *seek_param)
{
  struct db_queue_item *seek_queue_item = NULL;
  int seek_ms = 0;

  // Initialize out parameters
  *queue_item = NULL;
  *position_ms = 0;

  // Calculate seek position
  if (seek_param->mode == PLAYER_SEEK_POSITION)
    seek_ms = seek_param->ms;
  else
    seek_ms = pb_session.playing_now->pos_ms + seek_param->ms;

  // Check if we need to switch to a previous track, this will be done if we are in the first 3 seconds
  // of a track and we have a seek request for more than 3 seconds
  if (seek_ms < 0)
    {
      if (pb_session.playing_now->pos_ms < 3000)
	{
	  // We are in the first 3 seconds of the track, switch to the previous track and recalculate the absolute seek position
	  seek_queue_item = queue_item_prev(pb_session.playing_now->item_id);

	  if (seek_queue_item)
	    {
	      seek_ms = seek_queue_item->song_length + seek_ms;
	      // Make sure to not try to seek behind the previous track (this is also the case if song_length is zero)
	      seek_ms = (seek_ms < 0) ? 0 : seek_ms;
	    }
	  else
	    {
	      // There is no previous queue item, seek to the start of the current item
	      seek_ms = 0;
	    }
	}
      else
	{
	  // We are more than 3 seconds into the playing track, seek to beginning of current track
	  seek_ms = 0;
	}
    }
  else if (seek_ms > 0 && seek_ms > pb_session.playing_now->len_ms)
    {
      // We are seeking beyond the current track, play the next track from the beginning
      seek_queue_item = queue_item_next(pb_session.playing_now->item_id);
      if (seek_queue_item)
	{
	  seek_ms = 0;
	}
      else
	{
	  // There is no next queue item, we will seek beyond the length of the current track which will result in stopping playback
	  DPRINTF(E_DBG, L_PLAYER, "Seeking beyond the last queue item (seek_ms=%d, seek_mode=%d)\n", seek_param->ms, seek_param->mode);
	}
    }

  if (!seek_queue_item)
    {
      // Seeking in the current queue item
      seek_queue_item = db_queue_fetch_byitemid(pb_session.playing_now->item_id);

      if (!seek_queue_item)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error fetching queue item for seek command (seek_ms=%d, seek_mode=%d)\n", seek_param->ms, seek_param->mode);
	  return -1;
	}
    }


  DPRINTF(E_DBG, L_PLAYER, "Seek position for seek command (seek_ms=%d, seek_mode=%d) is: seek_ms=%d, queue item id=%d\n",
	  seek_param->ms, seek_param->mode, seek_ms, seek_queue_item->id);

  *queue_item = seek_queue_item;
  *position_ms = seek_ms;
  return 0;
}

static enum command_state
playback_seek_bh(void *arg, int *retval)
{
  struct player_seek_param *seek_param = arg;
  struct db_queue_item *queue_item;
  int position_ms;
  int ret;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause pb_abort()
  if (player_state == PLAY_STOPPED)
    {
      goto error;
    }

  ret = seek_calc_position_ms(&queue_item, &position_ms, seek_param);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error calculating new seek position\n");
      goto error;
    }

  ret = pb_session_start(queue_item, position_ms);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error seeking to %d, aborting playback\n", position_ms);
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

  status_update(PLAY_PAUSED, LISTENER_PLAYER);

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

  pb_session_pause();

  *retval = outputs_flush(device_flush_cb);
  outputs_metadata_purge();

  // We're async if we need to flush devices
  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise, just run the bottom half
  return COMMAND_END;
}

static enum command_state
playback_flush(void *arg, int *retval)
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

  input_flush(NULL);

  *retval = outputs_flush(device_flush_cb);
  outputs_metadata_purge();

  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise we are done
  return COMMAND_END;
}

static enum command_state
playback_seek(void *arg, int *retval)
{
  // Only check if the current playing track is seekable, other checks will be done in playback_pause()
  if (pb_session.playing_now && !pb_session.playing_now->is_seekable)
    {
      DPRINTF(E_WARN, L_PLAYER, "Failed to seek, track is not seekable\n");

      *retval = -1;
      return COMMAND_END;
    }

  return playback_pause(arg, retval);
}

static void
device_to_speaker_info(struct player_speaker_info *spk, struct output_device *device, uint32_t index)
{
  memset(spk, 0, sizeof(struct player_speaker_info));
  spk->id = device->id;
  spk->index = index;
  spk->active_remote = (uint32_t)device->id;
  strncpy(spk->name, device->name, sizeof(spk->name));
  spk->name[sizeof(spk->name) - 1] = '\0';
  strncpy(spk->output_type, device->type_name, sizeof(spk->output_type));
  spk->output_type[sizeof(spk->output_type) - 1] = '\0';
  spk->relvol = device->relvol;
  spk->absvol = device->volume;

  spk->supported_formats = device->supported_formats;
  // Devices supporting more than one format should at least have default_format set
  if (device->selected_format != MEDIA_FORMAT_UNKNOWN)
    spk->format = device->selected_format;
  else if (device->default_format != MEDIA_FORMAT_UNKNOWN)
    spk->format = device->default_format;
  else
    spk->format = device->supported_formats;

  spk->selected = OUTPUTS_DEVICE_DISPLAY_SELECTED(device);

  spk->has_password = device->has_password;
  spk->has_video = device->has_video;
  spk->requires_auth = device->requires_auth;
  spk->needs_auth_key = (device->requires_auth && device->auth_key == NULL);
  spk->prevent_playback = device->prevent_playback;
  spk->busy = device->busy;
}

static enum command_state
speaker_enumerate(void *arg, int *retval)
{
  struct spk_enum *spk_enum = arg;
  struct output_device *device;
  struct player_speaker_info spk;
  int i;

  for (device = outputs_list(), i = 0; device; device = device->next, i++)
    {
      device_to_speaker_info(&spk, device, i);
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
  int i;

  for (device = outputs_list(), i = 0; device; device = device->next, i++)
    {
      if ((device->advertised || device->selected)
	  && device->id == spk_param->spk_id)
	{
	  device_to_speaker_info(spk_param->spk_info, device, i);
	  *retval = 0;
	  return COMMAND_END;
	}
    }

  // No output device found with matching id
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
speaker_get_byactiveremote(void *arg, int *retval)
{
  struct speaker_get_param *spk_param = arg;
  struct output_device *device;
  int i;

  for (device = outputs_list(), i = 0; device; device = device->next, i++)
    {
      if ((uint32_t)device->id == spk_param->active_remote)
	{
	  device_to_speaker_info(spk_param->spk_info, device, i);
	  *retval = 0;
	  return COMMAND_END;
	}
    }

  // No output device found with matching id
  *retval = -1;
  return COMMAND_END;
}

static enum command_state
speaker_get_byaddress(void *arg, int *retval)
{
  struct speaker_get_param *spk_param = arg;
  struct output_device *device;
  bool match_v4;
  bool match_v6;
  int i;

  for (device = outputs_list(), i = 0; device; device = device->next, i++)
    {
      match_v4 = device->v4_address && (strcmp(spk_param->address, device->v4_address) == 0);
      match_v6 = device->v6_address && (strcmp(spk_param->address, device->v6_address) == 0);
      if (match_v4 || match_v6)
	{
	  device_to_speaker_info(spk_param->spk_info, device, i);
	  *retval = 0;
	  return COMMAND_END;
	}
    }

  *retval = -1;
  return COMMAND_END;
}

static enum command_state
speaker_get_byindex(void *arg, int *retval)
{
  struct speaker_get_param *spk_param = arg;
  struct output_device *device;
  int i;

  for (device = outputs_list(), i = 0; device; device = device->next, i++)
    {
      if (i == spk_param->index)
	{
	  device_to_speaker_info(spk_param->spk_info, device, i);
	  *retval = 0;
	  return COMMAND_END;
	}
    }

  *retval = -1;
  return COMMAND_END;
}

static enum command_state
speaker_set(void *arg, int *retval)
{
  struct speaker_set_param *speaker_set_param = arg;
  struct output_device *device;
  uint64_t *ids;
  int max_volume;
  int nspk;
  int i;

  *retval = -1;

  ids = speaker_set_param->device_ids;

  if (ids)
    nspk = ids[0];
  else
    nspk = 0;

  DPRINTF(E_DBG, L_PLAYER, "Speaker set: %d speakers\n", nspk);

  // Save the current master volume before we start selecting/unselecting, as
  // that will affect master volume. See comment in outputs_device_select() for
  // why we want to provide a max_volume.
  max_volume = (player_state != PLAY_STOPPED) ? outputs_volume_get() : -1;

  // Log a warning via outputs_device_get() if a speaker doesn't exist
  for (i = 1; i <= nspk; i++)
    outputs_device_get(ids[i]);

  for (device = outputs_list(); device; device = device->next)
    {
      for (i = 1; i <= nspk; i++)
	{
	  if (ids[i] == device->id)
	    break;
	}

      if (i <= nspk)
	outputs_device_select(device, max_volume);
      else
	outputs_device_deselect(device);
    }

  *retval = outputs_start(device_activate_cb, device_shutdown_cb, PLAYER_ONLY_PROBE);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_enable(void *arg, int *retval)
{
  uint64_t *id = arg;
  struct output_device *device;
  int max_volume;

  *retval = -1;

  device = outputs_device_get(*id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker enable: '%s' (id=%" PRIu64 ")\n", device->name, *id);

  max_volume = (player_state != PLAY_STOPPED) ? outputs_volume_get() : -1;

  outputs_device_select(device, max_volume);

  *retval = outputs_device_start(device, device_activate_cb, PLAYER_ONLY_PROBE);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_disable(void *arg, int *retval)
{
  uint64_t *id = arg;
  struct output_device *device;

  *retval = -1;

  device = outputs_device_get(*id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker disable: '%s' (id=%" PRIu64 ")\n", device->name, *id);

  outputs_device_deselect(device);

  *retval = outputs_device_stop(device, device_shutdown_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_generic_bh(void *arg, int *retval)
{
  status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);
  return COMMAND_END;
}

/*
 * Airplay speakers can via DACP set the "busy" + "prevent-playback" properties,
 * which we handle below. We try to do this like iTunes, except we need to
 * unselect devices, since our clients don't understand the "grayed out" state:
 *
 *                           | Playing to 1 device                            | Playing to 2 devices
 * device-prevent-playback=1 | Playback stops, device selected but grayed out | Playback stops on device, continues on other device, device selected but grayed out
 * device-prevent-playback=0 | Playback does not resume, device not grayed    | Playback resumes on device, device not grayed
 * (device-busy does the same)
 *
 * device-prevent-playback=1 | (same)                                         | (same)
 * device-busy=1             | (no change)                                    | (no change)
 * device-prevent-playback=0 | Playback does not resume, device still grayed  | Playback does not resume, device still grayed
 * device-busy=0             | Playback does not resume, device not grayed    | Playback resumes on device, device not grayed
 * (same if vice versa, ie busy=1 first)
 *
 * Special cases:
 *  a) Denon Home 150: User deselects speaker, we send TEARDOWN, speaker then
 *     sends prevent-playback=1 and then shortly after prevent-play-playback=0.
 *     In this case we never want to resume playback.
 */
static enum command_state
speaker_prevent_playback_set(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device *device;

  *retval = -1;

  device = outputs_device_get(param->spk_id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker prevent playback change %u -> %u: '%s' (id=%" PRIu64 ")\n",
    device->prevent_playback, param->prevent_playback, device->name, device->id);

  if (param->prevent_playback && device->state == OUTPUT_STATE_STOPPED)
    *retval = -1; // Means we won't set device->prevent_playback below, so that special case a) will work
  else if (param->prevent_playback)
    *retval = outputs_device_stop(device, device_shutdown_cb);
  else if (!device->busy && device->prevent_playback) // Only start if previously prevented
    *retval = outputs_device_start(device, device_activate_cb, PLAYER_ONLY_PROBE);
  else
    *retval = 0;

  if (*retval >= 0)
    device->prevent_playback = param->prevent_playback;

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_prevent_playback_set_bh(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;

  if (player_state == PLAY_PLAYING && outputs_sessions_count() == 0)
    {
      DPRINTF(E_INFO, L_PLAYER, "Suspending playback, speaker (id=%" PRIu64 ") set 'busy' or 'prevent-playback' flag\n", param->spk_id);
      pb_suspend(); // Don't want to use pb_abort here, since that may clear the queue
    }
  else
    status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
speaker_busy_set(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device *device;

  *retval = -1;

  device = outputs_device_get(param->spk_id);
  if (!device)
    return COMMAND_END;

  DPRINTF(E_DBG, L_PLAYER, "Speaker busy change %u -> %u: '%s' (id=%" PRIu64 ")\n",
    device->busy, param->busy, device->name, device->id);

  if (param->busy && device->state == OUTPUT_STATE_STOPPED)
    *retval = -1; // Means we won't set device->busy below, so that special case a) will work
  else if (param->busy)
    *retval = outputs_device_stop(device, device_shutdown_cb);
  else if (!device->prevent_playback && device->busy) // Only start if previously busy
    *retval = outputs_device_start(device, device_activate_cb, PLAYER_ONLY_PROBE);
  else
    *retval = 0;

  if (*retval >= 0)
    device->busy = param->busy;

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_format_set(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device *device;

  if (param->format == MEDIA_FORMAT_UNKNOWN)
    goto error;

  device = outputs_device_get(param->spk_id);
  if (!device)
    goto error;

  if (!(param->format & device->supported_formats))
    goto error;

  device->selected_format = param->format;

  *retval = 0;
  return COMMAND_END;

 error:
  DPRINTF(E_LOG, L_PLAYER, "Error setting format '%s', device unknown or format unsupported\n", media_format_to_string(param->format));
  *retval = -1;
  return COMMAND_END;
}

// Attempts to reactivate a speaker that has failed. That includes restarting
// playback if it was stopped.
static enum command_state
speaker_resurrect(void *arg, int *retval)
{
  struct speaker_set_param *param = arg;
  struct output_device *device;

  *retval = -1;

  device = outputs_device_get(*param->device_ids);
  if (!device)
    goto out;

  DPRINTF(E_DBG, L_PLAYER, "Speaker resurrect: '%s' (id=%" PRIu64 ")\n", device->name, device->id);

  if (device->busy || device->prevent_playback)
    goto out;

  if (player_state == PLAY_PAUSED)
    {
      // Playback was suspended by device_streaming_cb() because the speaker was
      // the only one playing. In that case we need to first resume the source,
      // then wait for the speaker to reactivate, and then run bottom half.
      *retval = pb_resume();
      if (*retval < 0)
	goto out;
    }
  else if (player_state == PLAY_STOPPED)
    {
      // If PLAY_STOPPED there is nothing to do, we can't resurrect since the
      // source is gone
      goto out;
    }

  *retval = outputs_device_start(device, device_activate_cb, false);

  if (*retval > 0)
    return COMMAND_PENDING; // Wait for speaker

 out:
  return COMMAND_END;
}

static enum command_state
speaker_resurrect_bh(void *arg, int *retval)
{
  // Playback was suspended by device_streaming_cb. We resumed the input in the
  // top half, now we have to start the playback timer and update status
  if (player_state == PLAY_PAUSED)
    return playback_start_bh(arg, retval);

  status_update(player_state, LISTENER_SPEAKER | LISTENER_VOLUME);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
speaker_authorize(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device *device;

  device = outputs_device_get(param->spk_id);
  if (!device)
    return COMMAND_END;

  *retval = outputs_device_authorize(device, param->pin, device_activate_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
speaker_start_all(void *arg, int *retval)
{
  outputs_stop_delayed_cancel();

  *retval = outputs_start(device_activate_cb, device_shutdown_cb, false);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise we are done
  return COMMAND_END;
}

// This is borderline misuse of the outputs_device interface, but the purpose is
// to register streaming session info with outputs/streaming.c via the player
// thread. It must be the player thread because session setup requires that
// outputs_quality_subscribe() is called, and by design it isn't thread safe.
static enum command_state
streaming_register(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device device =
  {
    .type = OUTPUT_TYPE_STREAMING,
    .type_name = "streaming",
    .name = "streaming",
    .quality = param->quality,
    .selected_format = param->format,
  };

  *retval = outputs_device_start(&device, NULL, false);

  param->spk_id = device.id;
  param->audio_fd = device.audio_fd;
  param->metadata_fd = device.metadata_fd;
  return COMMAND_END;
}

static enum command_state
streaming_deregister(void *arg, int *retval)
{
  struct speaker_attr_param *param = arg;
  struct output_device device =
  {
    .type = OUTPUT_TYPE_STREAMING,
    .type_name = "streaming",
    .name = "streaming",
    .id = param->spk_id,
    .session = "dummy", // to pass check in outputs_device_stop()
  };

  *retval = outputs_device_stop(&device, NULL);
  return COMMAND_END;
}

static enum command_state
volume_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  int volume;

  volume = cmdarg->intval;

  *retval = outputs_volume_set(volume, device_volume_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_setrel_speaker(void *arg, int *retval)
{
  struct speaker_attr_param *vol_param = arg;
  struct output_device *device;

  device = outputs_device_get(vol_param->spk_id);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Could not set volume for speaker id %" PRIu64 ", speaker disappeared\n", vol_param->spk_id);
      *retval = -1;
      return COMMAND_END;
    }

  outputs_device_volume_register(device, -1, vol_param->volume);

  *retval = outputs_device_volume_set(device, device_volume_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_setabs_speaker(void *arg, int *retval)
{
  struct speaker_attr_param *vol_param = arg;
  struct output_device *device;

  device = outputs_device_get(vol_param->spk_id);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Could not set volume for speaker id %" PRIu64 ", speaker disappeared\n", vol_param->spk_id);
      *retval = -1;
      return COMMAND_END;
    }

  outputs_device_volume_register(device, vol_param->volume, -1);

  *retval = outputs_device_volume_set(device, device_volume_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_setraw_speaker(void *arg, int *retval)
{
  struct speaker_attr_param *vol_param = arg;
  struct output_device *device;
  int volume;

  device = outputs_device_get(vol_param->spk_id);
  if (!device)
    {
      DPRINTF(E_WARN, L_PLAYER, "Could not set volume for speaker id %" PRIu64 ", speaker disappeared\n", vol_param->spk_id);
      *retval = -1;
      return COMMAND_END;
    }

  volume = outputs_device_volume_to_pct(device, vol_param->volstr); // Only converts
  if (volume < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not parse volume '%s' in update_volume() for speaker '%s'\n", vol_param->volstr, device->name);
      *retval = -1;
      return COMMAND_END;
    }

  outputs_device_volume_register(device, volume, -1);

  *retval = outputs_device_volume_set(device, device_volume_cb);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_generic_bh(void *arg, int *retval)
{
  status_update(player_state, LISTENER_VOLUME);
  return COMMAND_END;
}

static enum command_state
consume_set(void *arg, int *retval);

static enum command_state
repeat_set(void *arg, int *retval)
{
  enum repeat_mode *mode = arg;
  union player_arg consume_arg;

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

  // Persist
  SETTINGS_SETINT("player", PLAYER_SETTINGS_MODE_REPEAT, repeat);

  if (repeat == REPEAT_ALL || repeat == REPEAT_SONG)
    {
      // Activating repeat requires repeat consume mode to be off
      consume_arg.intval = 0;
      consume_set(&consume_arg, retval);
    }

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

  // Update shuffle mode
  shuffle = new_shuffle;

  // Persist
  SETTINGS_SETBOOL("player", PLAYER_SETTINGS_MODE_SHUFFLE, shuffle);

 out:
  *retval = 0;
  return COMMAND_END;
}

static enum command_state
consume_set(void *arg, int *retval)
{
  enum repeat_mode repeat_mode;
  union player_arg *cmdarg = arg;

  consume = cmdarg->intval;

  // Persist
  SETTINGS_SETBOOL("player", PLAYER_SETTINGS_MODE_CONSUME, consume);

  if (consume)
    {
      // Activating cosume mode requires repeat mode to be off
      repeat_mode = REPEAT_OFF;
      repeat_set(&repeat_mode, retval);
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
options_generic_bh(void *arg, int *retval)
{
  status_update(player_state, LISTENER_OPTIONS);
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

static enum command_state
playerqueue_generic_bh(void *arg, int *retval)
{
  status_update(player_state, LISTENER_QUEUE);
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


/* ------------------------------ Thread: httpd ----------------------------- */

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

/**
 * Flushes outputs and input buffer, but does not stop the input read loop. Used
 * by the pipe input when a track change is registered. Flushing outputs will
 * stop them, so the command is two-step, i.e. it starts them again.
 *
 * @return Returns 0 on success and a negative value on error
 */

int
player_playback_flush(void)
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_flush, NULL, NULL);
  if (ret < 0)
    return ret;

  ret = commands_exec_sync(cmdbase, speaker_start_all, NULL, NULL);
  return ret;
}

/**
 * Seeks to the position "seek_ms", depending on the given "seek_mode" seek_ms is
 * either the new position in the current track (seek_mode == PLAYER_SEEK_POSITION)
 * or a relative amount of milliseconds from the current playing position
 * (seek_mode == PLAYER_SEEK_RELATIVE).
 *
 * Relative seeking switches tracks, if:
 * - seeking behind the the current track and current playing position is not more than 3 seconds
 * - seeking beyond the current track
 *
 * @param seek_ms Position or relative amount of milliseconds to seek to
 * @param seek_mode If PLAYER_SEEK_POSITION seek_ms is a position in milliseconds,
 * 		if PLAYER_SEEK_RELATIVE seek_ms is the relative amount of milliseconds
 * @return Returns 0 on success and a negative value on error
 */
int
player_playback_seek(int seek_ms, enum player_seek_mode seek_mode)
{
  struct player_seek_param seek_param;
  int ret;

  seek_param.ms = seek_ms;
  seek_param.mode = seek_mode;

  ret = commands_exec_sync(cmdbase, playback_seek, playback_seek_bh, &seek_param);
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

  ret = commands_exec_sync(cmdbase, speaker_set, speaker_generic_bh, &speaker_set_param);

  return ret;
}

int
player_speaker_get_byid(struct player_speaker_info *spk, uint64_t id)
{
  struct speaker_get_param param;
  int ret;

  param.spk_id = id;
  param.spk_info = spk;

  ret = commands_exec_sync(cmdbase, speaker_get_byid, NULL, &param);
  return ret;
}

int
player_speaker_get_byactiveremote(struct player_speaker_info *spk, uint32_t active_remote)
{
  struct speaker_get_param param;
  int ret;

  param.active_remote = active_remote;
  param.spk_info = spk;

  ret = commands_exec_sync(cmdbase, speaker_get_byactiveremote, NULL, &param);
  return ret;
}

int
player_speaker_get_byaddress(struct player_speaker_info *spk, const char *address)
{
  struct speaker_get_param param;
  int ret;

  param.address = address;
  param.spk_info = spk;

  ret = commands_exec_sync(cmdbase, speaker_get_byaddress, NULL, &param);
  return ret;
}

int
player_speaker_get_byindex(struct player_speaker_info *spk, uint32_t index)
{
  struct speaker_get_param param;
  int ret;

  param.index = index;
  param.spk_info = spk;

  ret = commands_exec_sync(cmdbase, speaker_get_byindex, NULL, &param);
  return ret;
}

int
player_speaker_enable(uint64_t id)
{
  int ret;

  ret = commands_exec_sync(cmdbase, speaker_enable, speaker_generic_bh, &id);

  return ret;
}

int
player_speaker_disable(uint64_t id)
{
  int ret;

  ret = commands_exec_sync(cmdbase, speaker_disable, speaker_generic_bh, &id);

  return ret;
}

int
player_speaker_prevent_playback_set(uint64_t id, bool prevent_playback)
{
  struct speaker_attr_param param;
  int ret;

  param.spk_id = id;
  param.prevent_playback = prevent_playback;

  ret = commands_exec_sync(cmdbase, speaker_prevent_playback_set, speaker_prevent_playback_set_bh, &param);

  return ret;
}

int
player_speaker_busy_set(uint64_t id, bool busy)
{
  struct speaker_attr_param param;
  int ret;

  param.spk_id = id;
  param.busy = busy;

  ret = commands_exec_sync(cmdbase, speaker_busy_set, speaker_prevent_playback_set_bh, &param);

  return ret;
}

void
player_speaker_resurrect(void *arg)
{
  struct speaker_set_param param;

  param.device_ids = (uint64_t *)arg;

  commands_exec_sync(cmdbase, speaker_resurrect, speaker_resurrect_bh, &param);
}

int
player_speaker_authorize(uint64_t id, const char *pin)
{
  struct speaker_attr_param param;

  param.spk_id = id;
  param.pin = pin;

  return commands_exec_sync(cmdbase, speaker_authorize, speaker_generic_bh, &param);
}

int
player_speaker_format_set(uint64_t id, enum media_format format)
{
  struct speaker_attr_param param;

  param.spk_id = id;
  param.format = format;

  return commands_exec_sync(cmdbase, speaker_format_set, speaker_generic_bh, &param);
}

int
player_streaming_register(int *audio_fd, int *metadata_fd, enum media_format format, struct media_quality quality)
{
  struct speaker_attr_param param;
  int ret;

  param.format = format;
  param.quality = quality;

  ret = commands_exec_sync(cmdbase, streaming_register, NULL, &param);
  if (ret < 0)
    return ret;

  *audio_fd = param.audio_fd;
  *metadata_fd = param.metadata_fd;
  return param.spk_id;
}

int
player_streaming_deregister(int id)
{
  struct speaker_attr_param param;
  int ret;

  param.spk_id = id;

  ret = commands_exec_sync(cmdbase, streaming_deregister, NULL, &param);

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

  ret = commands_exec_sync(cmdbase, volume_set, volume_generic_bh, &cmdarg);

  return ret;
}

int
player_volume_setrel_speaker(uint64_t id, int relvol)
{
  struct speaker_attr_param vol_param;
  int ret;

  if (relvol < 0 || relvol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setrel_speaker is out of range\n", relvol);
      return -1;
    }

  vol_param.spk_id = id;
  vol_param.volume = relvol;

  ret = commands_exec_sync(cmdbase, volume_setrel_speaker, volume_generic_bh, &vol_param);

  return ret;
}

int
player_volume_setabs_speaker(uint64_t id, int vol)
{
  struct speaker_attr_param vol_param;
  int ret;

  if (vol < 0 || vol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setabs_speaker is out of range\n", vol);
      return -1;
    }

  vol_param.spk_id = id;
  vol_param.volume = vol;

  ret = commands_exec_sync(cmdbase, volume_setabs_speaker, volume_generic_bh, &vol_param);

  return ret;
}

int
player_volume_setraw_speaker(uint64_t id, const char *volstr)
{
  struct speaker_attr_param vol_param;
  int ret;

  vol_param.spk_id = id;
  vol_param.volstr  = volstr;

  ret = commands_exec_sync(cmdbase, volume_setraw_speaker, volume_generic_bh, &vol_param);

  return ret;
}

int
player_repeat_set(enum repeat_mode mode)
{
  int ret;

  ret = commands_exec_sync(cmdbase, repeat_set, options_generic_bh, &mode);

  return ret;
}

int
player_shuffle_set(int enable)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.intval = enable;

  ret = commands_exec_sync(cmdbase, shuffle_set, options_generic_bh, &cmdarg);

  return ret;
}

int
player_consume_set(int enable)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.intval = enable;

  ret = commands_exec_sync(cmdbase, consume_set, options_generic_bh, &cmdarg);

  return ret;
}

void
player_queue_clear_history()
{
  commands_exec_sync(cmdbase, playerqueue_clear_history, playerqueue_generic_bh, NULL);
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


/* ----------------------- Thread: filescanner/httpd ------------------------ */

void
player_raop_verification_kickoff(char **arglist)
{
  union player_arg *cmdarg;

  cmdarg = calloc(1, sizeof(union player_arg));
  if (!cmdarg)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");
      return;
    }

  cmdarg->auth.type = OUTPUT_TYPE_RAOP;
  memcpy(cmdarg->auth.pin, arglist[0], 4);

  commands_exec_async(cmdbase, device_auth_kickoff, cmdarg);

}


/* ---------------------------- Thread: player ------------------------------ */

static void *
player(void *arg)
{
  struct output_device *device;
  int ret;

  thread_setname("player");

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_player);

  if (!player_exit)
    DPRINTF(E_LOG, L_PLAYER, "Player event loop terminated ahead of time!\n");

  for (device = outputs_list(); device; device = device->next)
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
  clear_queue_on_stop_disabled = cfg_getbool(cfg_getsec(cfg, "library"), "clear_queue_on_stop_disable");

  /* Handle deprecated config options, note that this is also in library.c */
  if (0 < cfg_opt_size(cfg_getopt(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable")))
    {
      clear_queue_on_stop_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");
    }

  ret = SETTINGS_GETINT("player", PLAYER_SETTINGS_MODE_REPEAT);
  repeat = (ret > 0) ? ret : REPEAT_OFF;
  shuffle = SETTINGS_GETBOOL("player", PLAYER_SETTINGS_MODE_SHUFFLE);
  consume = SETTINGS_GETBOOL("player", PLAYER_SETTINGS_MODE_CONSUME);

  player_state = PLAY_STOPPED;

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

  return 0;

 error_input_deinit:
  input_deinit();
 error_outputs_deinit:
  outputs_deinit();
 error_evbase_free:
  commands_base_free(cmdbase);
  event_free(pb_timer_ev);
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

  event_free(pb_timer_ev);
  event_base_free(evbase_player);
}
