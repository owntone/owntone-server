/*
 * Copyright (C) 2010-2011 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2016-2017 Espen Jürgensen <espenjurgensen@gmail.com>
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

#ifndef MIN
# define MIN(a, b) ((a < b) ? a : b)
#endif

#ifndef MAX
#define MAX(a, b) ((a > b) ? a : b)
#endif

// Default volume (must be from 0 - 100)
#define PLAYER_DEFAULT_VOLUME 50
// For every tick_interval, we will read a packet from the input buffer and
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

struct volume_param {
  int volume;
  uint64_t spk_id;
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
  struct volume_param vol_param;
  void *noarg;
  struct spk_enum *spk_enum;
  struct output_device *device;
  struct player_status *status;
  struct player_source *ps;
  struct metadata_param metadata_param;
  uint32_t *id_ptr;
  struct speaker_set_param speaker_set_param;
  enum repeat_mode mode;
  struct speaker_auth_param auth;
  uint32_t id;
  int intval;
};

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
static struct timespec pb_timer_last;
static struct timespec packet_timer_last;

// How often the playback timer triggers playback_cb()
static struct timespec tick_interval;
// Timer resolution
static struct timespec timer_res;
// Time between two packets
static struct timespec packet_time = { 0, AIRTUNES_V2_STREAM_PERIOD };

// How many writes we owe the output (when the input is underrunning)
static int pb_read_deficit;

// PLAYER_READ_BEHIND_MAX and PLAYER_WRITE_BEHIND_MAX converted to clock ticks
static int pb_read_deficit_max;
static int pb_write_deficit_max;

// True if we are trying to recover from a major playback timer overrun (write problems)
static bool pb_write_recovery;

// Sync values
static struct timespec pb_pos_stamp;
static uint64_t pb_pos;

// Stream position (packets)
static uint64_t last_rtptime;

// Output devices
static struct output_device *dev_list;

// Output status
static int output_sessions;

// Last commanded volume
static int master_volume;

// Audio source
static struct player_source *cur_playing;
static struct player_source *cur_streaming;
static uint32_t cur_plid;
static uint32_t cur_plversion;

// Player buffer (holds one packet)
static uint8_t pb_buffer[STOB(AIRTUNES_V2_PACKET_SAMPLES)];
static size_t pb_buffer_offset;

// Play history
static struct player_history *history;


/* -------------------------------- Forwards -------------------------------- */

static void
playback_abort(void);

static void
playback_suspend(void);


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

  for (device = dev_list; device; device = device->next)
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

  for (device = dev_list; device; device = device->next)
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

#ifdef LASTFM
// Callback from the worker thread (async operation as it may block)
static void
scrobble_cb(void *arg)
{
  int *id = arg;

  lastfm_scrobble(*id);
}
#endif

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

  queue_item = db_queue_fetch_byitemid(metadata->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! Input metadata item_id does not match anything in queue\n");
      goto out_free_metadata;
    }

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
  int ret;

  ret = input_metadata_get(&metadata, cur_streaming, startup, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
  if (ret < 0)
    return;

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
  int seek;

  if (!cur_streaming)
    return;

  if (cur_streaming->media_kind & (MEDIA_KIND_MOVIE | MEDIA_KIND_PODCAST | MEDIA_KIND_AUDIOBOOK | MEDIA_KIND_TVSHOW))
    {
      seek = (cur_streaming->output_start - cur_streaming->stream_start) / 44100 * 1000;
      db_file_seek_update(cur_streaming->id, seek);
    }
}

static void
status_update(enum play_status status)
{
  player_state = status;

  listener_notify(LISTENER_PLAYER);
}


/* ----------- Audio source handling (interfaces with input module) --------- */

static struct player_source *
source_now_playing()
{
  if (cur_playing)
    return cur_playing;

  return cur_streaming;
}

/*
 * Creates a new player source for the given queue item
 */
static struct player_source *
source_new(struct db_queue_item *queue_item)
{
  struct player_source *ps;

  ps = calloc(1, sizeof(struct player_source));
  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory (ps)\n");
      return NULL;
    }

  ps->id = queue_item->file_id;
  ps->item_id = queue_item->id;
  ps->data_kind = queue_item->data_kind;
  ps->media_kind = queue_item->media_kind;
  ps->len_ms = queue_item->song_length;
  ps->play_next = NULL;
  ps->path = strdup(queue_item->path);

  return ps;
}

static void
source_free(struct player_source *ps)
{
  if (ps->path)
    free(ps->path);

  free(ps);
}

/*
 * Stops playback for the current streaming source and frees all
 * player sources (starting from the playing source). Sets current streaming
 * and playing sources to NULL.
 */
static void
source_stop()
{
  struct player_source *ps_playing;
  struct player_source *ps_temp;

  if (cur_streaming)
    input_stop(cur_streaming);

  ps_playing = source_now_playing();

  while (ps_playing)
    {
      ps_temp = ps_playing;
      ps_playing = ps_playing->play_next;

      ps_temp->play_next = NULL;
      source_free(ps_temp);
    }

  cur_playing = NULL;
  cur_streaming = NULL;
}

/*
 * Pauses playback
 *
 * Resets the streaming source to the playing source and adjusts stream-start
 * and output-start values to the playing time. Sets the current streaming
 * source to NULL.
 */
static int
source_pause(uint64_t pos)
{
  struct player_source *ps_playing;
  struct player_source *ps_playnext;
  struct player_source *ps_temp;
  uint64_t seek_frames;
  int seek_ms;
  int ret;

  ps_playing = source_now_playing();
  if (!ps_playing)
    return -1;

  if (cur_streaming && (cur_streaming == ps_playing))
    {
      if (ps_playing != cur_streaming)
	{
	  DPRINTF(E_DBG, L_PLAYER,
	      "Pause called on playing source (id=%d) and streaming source already "
	      "switched to the next item (id=%d)\n", ps_playing->id, cur_streaming->id);
	  ret = input_stop(cur_streaming);
	  if (ret < 0)
	    return -1;
	}
      else
	{
	  ret = input_pause(cur_streaming);
	  if (ret < 0)
	    return -1;
	}
    }

  ps_playnext = ps_playing->play_next;
  while (ps_playnext)
    {
      ps_temp = ps_playnext;
      ps_playnext = ps_playnext->play_next;

      ps_temp->play_next = NULL;
      source_free(ps_temp);
    }
  ps_playing->play_next = NULL;

  cur_playing = NULL;
  cur_streaming = ps_playing;

  if (!cur_streaming->setup_done)
    {
      DPRINTF(E_INFO, L_PLAYER, "Opening '%s'\n", cur_streaming->path);

      ret = input_setup(cur_streaming);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Failed to open '%s'\n", cur_streaming->path);
	  return -1;
	}
    }

  // Seek back to the pause position
  seek_frames = (pos - cur_streaming->stream_start);
  seek_ms = (int)((seek_frames * 1000) / 44100);
  ret = input_seek(cur_streaming, seek_ms);

// TODO what if ret < 0?

  // Adjust start_pos to take into account the pause and seek back
  cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - ((uint64_t)ret * 44100) / 1000;
  cur_streaming->output_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
  cur_streaming->end = 0;

  return 0;
}

/*
 * Seeks the current streaming source to the given postion in milliseconds
 * and adjusts stream-start and output-start values.
 *
 * @param seek_ms Position in milliseconds to seek
 * @return The new position in milliseconds or -1 on error
 */
static int
source_seek(int seek_ms)
{
  int ret;

  ret = input_seek(cur_streaming, seek_ms);
  if (ret < 0)
    return -1;

  // Adjust start_pos to take into account the pause and seek back
  cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - ((uint64_t)ret * 44100) / 1000;
  cur_streaming->output_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;

  return ret;
}

/*
 * Starts or resumes playback
 */
static int
source_play()
{
  int ret;

  ret = input_start(cur_streaming);

  return ret;
}

/*
 * Opens the given player source for playback (but does not start playback)
 *
 * The given source is appended to the current streaming source (if one exists) and
 * becomes the new current streaming source.
 *
 * Stream-start and output-start values are set to the given start position.
 */
static int
source_open(struct player_source *ps, uint64_t start_pos, int seek_ms)
{
  int ret;

  DPRINTF(E_INFO, L_PLAYER, "Opening '%s' (id=%d, item-id=%d)\n", ps->path, ps->id, ps->item_id);

  if (cur_streaming && cur_streaming->end == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Current streaming source not at eof '%s' (id=%d, item-id=%d)\n",
	      cur_streaming->path, cur_streaming->id, cur_streaming->item_id);
      return -1;
    }

  ret = input_setup(ps);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Failed to open '%s' (id=%d, item-id=%d)\n", ps->path, ps->id, ps->item_id);
      return -1;
    }

  // If a streaming source exists, append the new source as play-next and set it
  // as the new streaming source
  if (cur_streaming)
      cur_streaming->play_next = ps;

  cur_streaming = ps;

  cur_streaming->stream_start = start_pos;
  cur_streaming->output_start = cur_streaming->stream_start;
  cur_streaming->end = 0;

  // Seek to the given seek position
  if (seek_ms)
    {
      DPRINTF(E_INFO, L_PLAYER, "Seek to %d ms for '%s' (id=%d, item-id=%d)\n", seek_ms, ps->path, ps->id, ps->item_id);
      source_seek(seek_ms);
    }

  return ret;
}

/*
 * Closes the current streaming source and sets its end-time to the given
 * position
 */
static int
source_close(uint64_t end_pos)
{
  input_stop(cur_streaming);

  cur_streaming->end = end_pos;

  return 0;
}

/*
 * Updates the now playing item (cur_playing) and notifies remotes and raop devices
 * about changes. Also takes care of stopping playback after the last item.
 *
 * @return Returns the current playback position as rtp-time
 */
static uint64_t
source_check(void)
{
  struct timespec ts;
  struct player_source *ps;
  uint64_t pos;
  int i;
  int id;
  int ret;

  ret = player_get_current_pos(&pos, &ts, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get current playback position\n");

      return 0;
    }

  if (player_state == PLAY_STOPPED)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! source_check called but playback has already stopped\n");

      return pos;
    }

  // If cur_playing is NULL, we are still in the first two seconds after starting the stream
  if (!cur_playing)
    {
      if (pos >= cur_streaming->output_start)
	{
	  cur_playing = cur_streaming;
	  status_update(PLAY_PLAYING);

	  // Start of streaming, no metadata to prune yet
	}

      return pos;
    }

  // Check if we are still in the middle of the current playing song
  if ((cur_playing->end == 0) || (pos < cur_playing->end))
    return pos;

  // We have reached the end of the current playing song, update cur_playing to 
  // the next song in the queue and initialize stream_start and output_start values.

  i = 0;
  while (cur_playing && (cur_playing->end != 0) && (pos > cur_playing->end))
    {
      i++;

      id = (int)cur_playing->id;
      worker_execute(playcount_inc_cb, &id, sizeof(int), 5);
#ifdef LASTFM
      worker_execute(scrobble_cb, &id, sizeof(int), 8);
#endif
      history_add(cur_playing->id, cur_playing->item_id);

      if (consume)
	db_queue_delete_byitemid(cur_playing->item_id);

      if (!cur_playing->play_next)
	{
	  playback_abort();
	  return pos;
        }

      ps = cur_playing;
      cur_playing = cur_playing->play_next;

      source_free(ps);
    }

  if (i > 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Playback switched to next song\n");

      status_update(PLAY_PLAYING);

      outputs_metadata_prune(pos);
    }

  return pos;
}

/*
 * Returns the next player source based on the current streaming source and repeat mode
 *
 * If repeat mode is repeat all, shuffle is active and the current streaming source is the
 * last item in the queue, the queue is reshuffled prior to returning the first item of the
 * queue.
 */
static struct player_source *
source_next()
{
  struct player_source *ps = NULL;
  struct db_queue_item *queue_item;

  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "source_next() called with no current streaming source available\n");
      return NULL;
    }

  if (repeat == REPEAT_SONG)
    {
      queue_item = db_queue_fetch_byitemid(cur_streaming->item_id);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error fetching item from queue '%s' (id=%d, item-id=%d)\n", cur_streaming->path, cur_streaming->id, cur_streaming->item_id);
	  return NULL;
	}
    }
  else
    {
      queue_item = db_queue_fetch_next(cur_streaming->item_id, shuffle);
      if (!queue_item && repeat == REPEAT_ALL)
	{
	  if (shuffle)
	    {
	      db_queue_reshuffle(0);
	    }

	  queue_item = db_queue_fetch_bypos(0, shuffle);
	  if (!queue_item)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Error fetching item from queue '%s' (id=%d, item-id=%d)\n", cur_streaming->path, cur_streaming->id, cur_streaming->item_id);
	      return NULL;
	    }
	}
    }

  if (!queue_item)
    {
      DPRINTF(E_DBG, L_PLAYER, "Reached end of queue\n");
      return NULL;
    }

  ps = source_new(queue_item);
  free_queue_item(queue_item, 0);
  return ps;
}

/*
 * Returns the previous player source based on the current streaming source
 */
static struct player_source *
source_prev()
{
  struct player_source *ps = NULL;
  struct db_queue_item *queue_item;

  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "source_prev() called with no current streaming source available\n");
      return NULL;
    }

  queue_item = db_queue_fetch_prev(cur_streaming->item_id, shuffle);
  if (!queue_item)
    return NULL;

  ps = source_new(queue_item);
  free_queue_item(queue_item, 0);

  return ps;
}

static int
source_switch(int nbytes)
{
  struct player_source *ps;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Switching track\n");

  source_close(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES + BTOS(nbytes) - 1);

  while ((ps = source_next()))
    {
      ret = source_open(ps, cur_streaming->end + 1, 0);
      if (ret < 0)
	{
	  db_queue_delete_byitemid(ps->item_id);
	  continue;
	}

      ret = source_play();
      if (ret < 0)
	{
	  db_queue_delete_byitemid(ps->item_id);
	  source_close(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES + BTOS(nbytes) - 1);
	  continue;
	}

      break;
    }

  if (!ps) // End of queue
    {
      cur_streaming = NULL;
      return 0;
    }

  metadata_trigger(0);

  return 0;
}


/* ----------------- Main read, write and playback timer event -------------- */

// Returns -1 on error (caller should abort playback), or bytes read (possibly 0)
static int
source_read(uint8_t *buf, int len)
{
  int nbytes;
  uint32_t item_id;
  int ret;
  short flags;

  // Nothing to read, stream silence until source_check() stops playback
  if (!cur_streaming)
    {
      memset(buf, 0, len);
      return len;
    }

  nbytes = input_read(buf, len, &flags);
  if ((nbytes < 0) || (flags & INPUT_FLAG_ERROR))
    {
      DPRINTF(E_LOG, L_PLAYER, "Error reading source %d\n", cur_streaming->id);

      nbytes = 0;
      item_id = cur_streaming->item_id;
      ret = source_switch(0);
      db_queue_delete_byitemid(item_id);
      if (ret < 0)
	return -1;
    }
  else if (flags & INPUT_FLAG_EOF)
    {
      ret = source_switch(nbytes);
      if (ret < 0)
	return -1;
    }
  else if (flags & INPUT_FLAG_METADATA)
    {
      metadata_trigger(0);
    }

  // We pad the output buffer with silence if we don't have enough data for a
  // full packet and there is no more data coming up (no more tracks in queue)
  if ((nbytes < len) && (!cur_streaming))
    {
      memset(buf + nbytes, 0, len - nbytes);
      nbytes = len;
    }

  return nbytes;
}

static void
playback_write(void)
{
  int want;
  int got;

  source_check();

  // Make sure playback is still running after source_check()
  if (player_state == PLAY_STOPPED)
    return;

  pb_read_deficit++;
  while (pb_read_deficit)
    {
      want = sizeof(pb_buffer) - pb_buffer_offset;
      got = source_read(pb_buffer + pb_buffer_offset, want);
      if (got == want)
	{
	  pb_read_deficit--;
	  last_rtptime += AIRTUNES_V2_PACKET_SAMPLES;
	  outputs_write(pb_buffer, last_rtptime);
	  pb_buffer_offset = 0;
	}
      else if (got < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Error reading from source, aborting playback\n");
	  playback_abort();
	  return;
	}
      else if (pb_read_deficit > pb_read_deficit_max)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Source is not providing sufficient data, temporarily suspending playback (deficit=%d)\n", pb_read_deficit);
	  playback_suspend();
	  return;
	}
      else
	{
	  DPRINTF(E_SPAM, L_PLAYER, "Partial read (offset=%zu, deficit=%d)\n", pb_buffer_offset, pb_read_deficit);
	  pb_buffer_offset += got;
	  return;
	}
    }
}

static void
playback_cb(int fd, short what, void *arg)
{
  struct timespec next_tick;
  uint64_t overrun;
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
	  playback_abort();
	  return;
	}

      DPRINTF(E_LOG, L_PLAYER, "Output delay detected (behind=%" PRIu64 ", max=%d), resetting all outputs\n", overrun, pb_write_deficit_max);
      pb_write_recovery = true;
      playback_suspend();
      return;
    }
  else
    {
      if (overrun > 1) // An overrun of 1 is no big deal
	DPRINTF(E_WARN, L_PLAYER, "Output delay detected: player is %" PRIu64 " ticks behind, catching up\n", overrun);

      pb_write_recovery = false;
    }

  // If there was an overrun, we will try to read/write a corresponding number
  // of times so we catch up. The read from the input is non-blocking, so it
  // should not bring us further behind, even if there is no data.
  next_tick = timespec_add(pb_timer_last, tick_interval);
  for (; overrun > 0; overrun--)
    next_tick = timespec_add(next_tick, tick_interval);

  do
    {
      playback_write();
      packet_timer_last = timespec_add(packet_timer_last, packet_time);
    }
  while ((timespec_cmp(packet_timer_last, next_tick) < 0) && (player_state == PLAY_PLAYING));

  // Make sure playback is still running
  if (player_state == PLAY_STOPPED)
    return;

  pb_timer_last = next_tick;
}


/* ----------------- Output device handling (add/remove etc) ---------------- */

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
      for (device = dev_list; device && device->next; device = device->next)
	{
	  next = device->next;
	  if ( (outputs_priority(device) > outputs_priority(next)) ||
	       (outputs_priority(device) == outputs_priority(next) && strcasecmp(device->name, next->name) > 0) )
	    {
	      if (device == dev_list)
		dev_list = next;
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

static void
device_remove(struct output_device *remove)
{
  struct output_device *device;
  struct output_device *prev;
  int ret;

  prev = NULL;
  for (device = dev_list; device; device = device->next)
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

  // Make sure device isn't selected anymore
  if (remove->selected)
    speaker_deselect_output(remove);

  if (!prev)
    dev_list = remove->next;
  else
    prev->next = remove->next;

  outputs_device_free(remove);
}

static int
device_check(struct output_device *check)
{
  struct output_device *device;

  for (device = dev_list; device; device = device->next)
    {
      if (device == check)
	break;
    }

  return (device) ? 0 : -1;
}

static enum command_state
device_add(void *arg, int *retval)
{
  union player_arg *cmdarg;
  struct output_device *add;
  struct output_device *device;
  char *keep_name;
  int ret;

  cmdarg = arg;
  add = cmdarg->device;

  for (device = dev_list; device; device = device->next)
    {
      if (device->id == add->id)
	break;
    }

  // New device
  if (!device)
    {
      device = add;

      keep_name = strdup(device->name);
      ret = db_speaker_get(device, device->id);
      if (ret < 0)
	{
	  device->selected = 0;
	  device->volume = (master_volume >= 0) ? master_volume : PLAYER_DEFAULT_VOLUME;
	}

      free(device->name);
      device->name = keep_name;

      if (device->selected && (player_state != PLAY_PLAYING))
	speaker_select_output(device);
      else
	device->selected = 0;

      device->next = dev_list;
      dev_list = device;
    }
  // Update to a device already in the list
  else
    {
      device->advertised = 1;

      if (add->v4_address)
	{
	  if (device->v4_address)
	    free(device->v4_address);

	  device->v4_address = add->v4_address;
	  device->v4_port = add->v4_port;

	  // Address is ours now
	  add->v4_address = NULL;
	}

      if (add->v6_address)
	{
	  if (device->v6_address)
	    free(device->v6_address);

	  device->v6_address = add->v6_address;
	  device->v6_port = add->v6_port;

	  // Address is ours now
	  add->v6_address = NULL;
	}

      if (device->name)
	free(device->name);
      device->name = add->name;
      add->name = NULL;

      device->has_password = add->has_password;
      device->password = add->password;

      outputs_device_free(add);
    }

  device_list_sort();

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
device_remove_family(void *arg, int *retval)
{
  union player_arg *cmdarg;
  struct output_device *remove;
  struct output_device *device;

  cmdarg = arg;
  remove = cmdarg->device;

  for (device = dev_list; device; device = device->next)
    {
      if (device->id == remove->id)
        break;
    }

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

      if (!device->session)
	device_remove(device);
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
  union player_arg *cmdarg;
  struct input_metadata *imd;
  struct output_metadata *omd;

  cmdarg = arg;
  imd = cmdarg->metadata_param.input;
  omd = cmdarg->metadata_param.output;

  outputs_metadata_send(omd, imd->rtptime, imd->offset, imd->startup);

  status_update(player_state);

  *retval = 0;
  return COMMAND_END;
}


/* -------- Output device callbacks executed in the player thread ----------- */

static void
device_streaming_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_streaming_cb\n", outputs_name(device->type));

  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Output device disappeared during streaming!\n");

      output_sessions--;
      return;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      DPRINTF(E_LOG, L_PLAYER, "The %s device '%s' FAILED\n", device->type_name, device->name);

      output_sessions--;

      if (player_state == PLAY_PLAYING)
	speaker_deselect_output(device);

      device->session = NULL;

      if (!device->advertised)
	device_remove(device);

      if (output_sessions == 0)
	playback_abort();
    }
  else if (status == OUTPUT_STATE_STOPPED)
    {
      DPRINTF(E_INFO, L_PLAYER, "The %s device '%s' stopped\n", device->type_name, device->name);

      output_sessions--;

      device->session = NULL;

      if (!device->advertised)
	device_remove(device);
    }
  else
    outputs_status_cb(session, device_streaming_cb);
}

static void
device_command_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_command_cb\n", outputs_name(device->type));

  outputs_status_cb(session, device_streaming_cb);

  if (status == OUTPUT_STATE_FAILED)
    device_streaming_cb(device, session, status);

  // Used by playback_suspend - is basically the bottom half
  if (player_flush_pending > 0)
    {
      player_flush_pending--;
      if (player_flush_pending == 0)
	input_buffer_full_cb(player_playback_start);
    }

  commands_exec_end(cmdbase, 0);
}

static void
device_shutdown_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  int retval;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_shutdown_cb\n", outputs_name(device->type));

  if (output_sessions)
    output_sessions--;

  retval = commands_exec_returnvalue(cmdbase);
  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared before shutdown completion!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  device->session = NULL;

  if (!device->advertised)
    device_remove(device);

 out:
  /* cur_cmd->ret already set
   *  - to 0 (or -2 if password issue) in speaker_set()
   *  - to -1 above on error
   */
  commands_exec_end(cmdbase, retval);
}

static void
device_lost_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_lost_cb\n", outputs_name(device->type));

  // We lost that device during startup for some reason, not much we can do here
  if (status == OUTPUT_STATE_FAILED)
    DPRINTF(E_WARN, L_PLAYER, "Failed to stop lost device\n");
  else
    DPRINTF(E_INFO, L_PLAYER, "Lost device stopped properly\n");
}

static void
device_activate_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  struct timespec ts;
  int retval;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_activate_cb\n", outputs_name(device->type));

  retval = commands_exec_returnvalue(cmdbase);
  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during startup!\n");

      outputs_status_cb(session, device_lost_cb);
      outputs_device_stop(session);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (!device->advertised)
	device_remove(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  device->session = session;

  output_sessions++;

  if ((player_state == PLAY_PLAYING) && (output_sessions == 1))
    {
      ret = clock_gettime_with_res(CLOCK_MONOTONIC, &ts, &timer_res);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not get current time: %s\n", strerror(errno));

	  // Fallback to nearest timer expiration time
	  ts.tv_sec = pb_timer_last.tv_sec;
	  ts.tv_nsec = pb_timer_last.tv_nsec;
	}

      outputs_playback_start(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, &ts);
    }

  outputs_status_cb(session, device_streaming_cb);

 out:
  /* cur_cmd->ret already set
   *  - to 0 in speaker_set() (default)
   *  - to -2 above if password issue
   *  - to -1 above on error
   */
  commands_exec_end(cmdbase, retval);
}

static void
device_probe_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  int retval;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_probe_cb\n", outputs_name(device->type));

  retval = commands_exec_returnvalue(cmdbase);
  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during probe!\n");

      if (retval != -2)
	retval = -1;
      goto out;
    }

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (!device->advertised)
	device_remove(device);

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
device_restart_cb(struct output_device *device, struct output_session *session, enum output_device_state status)
{
  int retval;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_restart_cb\n", outputs_name(device->type));

  retval = commands_exec_returnvalue(cmdbase);
  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during restart!\n");

      outputs_status_cb(session, device_lost_cb);
      outputs_device_stop(session);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  if (status == OUTPUT_STATE_PASSWORD)
    {
      status = OUTPUT_STATE_FAILED;
      retval = -2;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (!device->advertised)
	device_remove(device);

      if (retval != -2)
	retval = -1;
      goto out;
    }

  device->session = session;

  output_sessions++;
  outputs_status_cb(session, device_streaming_cb);

 out:
  commands_exec_end(cmdbase, retval);
}


/* ------------------------- Internal playback routines --------------------- */

static int
playback_timer_start(void)
{
  struct itimerspec tick;
  int ret;

  ret = event_add(pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not add playback timer\n");

      return -1;
    }

  tick.it_interval = tick_interval;
  tick.it_value = tick_interval;

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
playback_timer_stop(void)
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

static void
playback_abort(void)
{
  outputs_playback_stop();

  playback_timer_stop();

  source_stop();

  if (!clear_queue_on_stop_disabled)
    db_queue_clear(0);

  status_update(PLAY_STOPPED);

  outputs_metadata_purge();
}

// Temporarily suspends/resets playback, used when input buffer underruns or in
// case of problems writing to the outputs
static void
playback_suspend(void)
{
  player_flush_pending = outputs_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  playback_timer_stop();

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
  union player_arg *cmdarg = arg;
  struct timespec ts;
  struct player_source *ps;
  struct player_status *status;
  uint64_t pos;
  int ret;

  status = cmdarg->status;

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

	status->status = PLAY_STOPPED;
	break;

      case PLAY_PAUSED:
	DPRINTF(E_DBG, L_PLAYER, "Player status: paused\n");

	status->status = PLAY_PAUSED;
	status->id = cur_streaming->id;
	status->item_id = cur_streaming->item_id;

	pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - cur_streaming->stream_start;
	status->pos_ms = (pos * 1000) / 44100;
	status->len_ms = cur_streaming->len_ms;

	break;

      case PLAY_PLAYING:
	if (!cur_playing)
	  {
	    DPRINTF(E_DBG, L_PLAYER, "Player status: playing (buffering)\n");

	    status->status = PLAY_PAUSED;
	    ps = cur_streaming;

	    // Avoid a visible 2-second jump backward for the client
	    pos = ps->output_start - ps->stream_start;
	  }
	else
	  {
	    DPRINTF(E_DBG, L_PLAYER, "Player status: playing\n");

	    status->status = PLAY_PLAYING;
	    ps = cur_playing;

	    ret = player_get_current_pos(&pos, &ts, 0);
	    if (ret < 0)
	      {
		DPRINTF(E_LOG, L_PLAYER, "Could not get current stream position for playstatus\n");

		pos = 0;
	      }

	    if (pos < ps->stream_start)
	      pos = 0;
	    else
	      pos -= ps->stream_start;
	  }

	status->pos_ms = (pos * 1000) / 44100;
	status->len_ms = ps->len_ms;

	status->id = ps->id;
	status->item_id = ps->item_id;

	break;
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
now_playing(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  uint32_t *id;
  struct player_source *ps_playing;

  id = cmdarg->id_ptr;

  ps_playing = source_now_playing();

  if (ps_playing)
    *id = ps_playing->id;
  else
    {
      *retval = -1;
      return COMMAND_END;
    }

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_stop(void *arg, int *retval)
{
  struct player_source *ps_playing;

  // We may be restarting very soon, so we don't bring the devices to a full
  // stop just yet; this saves time when restarting, which is nicer for the user
  *retval = outputs_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  playback_timer_stop();

  ps_playing = source_now_playing();
  if (ps_playing)
    {
      history_add(ps_playing->id, ps_playing->item_id);
    }

  source_stop();

  status_update(PLAY_STOPPED);

  outputs_metadata_purge();

  // We're async if we need to flush devices
  if (*retval > 0)
    return COMMAND_PENDING;

  return COMMAND_END;
}

static enum command_state
playback_start_bh(void *arg, int *retval)
{
  int ret;

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, &pb_pos_stamp, &timer_res);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get current clock: %s\n", strerror(errno));

      goto out_fail;
    }

  playback_timer_stop();

  // initialize the packet timer to the same relative time that we have 
  // for the playback timer.
  packet_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  packet_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  pb_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  pb_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  pb_buffer_offset = 0;
  pb_read_deficit = 0;

  ret = playback_timer_start();
  if (ret < 0)
    goto out_fail;

  // Everything OK, start outputs
  outputs_playback_start(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, &pb_pos_stamp);

  status_update(PLAY_PLAYING);

  *retval = 0;
  return COMMAND_END;

 out_fail:
  playback_abort();

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
  int seek_ms;
  int ret;

  if (player_state == PLAY_PLAYING)
    {
      DPRINTF(E_DBG, L_PLAYER, "Player is already playing, ignoring call to playback start\n");

      status_update(player_state);

      *retval = 1; // Value greater 0 will prevent execution of the bottom half function
      return COMMAND_END;
    }

  // Update global playback position
  pb_pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - 88200;

  if (player_state == PLAY_STOPPED && !queue_item)
    {
      DPRINTF(E_LOG, L_PLAYER, "Failed to start/resume playback, no queue item given\n");

      *retval = -1;
      return COMMAND_END;
    }

  if (!queue_item)
    {
      // Resume playback of current source
      ps = source_now_playing();
      DPRINTF(E_DBG, L_PLAYER, "Resume playback of '%s' (id=%d, item-id=%d)\n", ps->path, ps->id, ps->item_id);
    }
  else
    {
      // Start playback for given queue item
      DPRINTF(E_DBG, L_PLAYER, "Start playback of '%s' (id=%d, item-id=%d)\n", queue_item->path, queue_item->file_id, queue_item->id);
      source_stop();

      ps = source_new(queue_item);
      if (!ps)
	{
	  playback_abort();
	  *retval = -1;
	  return COMMAND_END;
	}

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

      ret = source_open(ps, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, seek_ms);
      if (ret < 0)
	{
	  playback_abort();
	  *retval = -1;
	  return COMMAND_END;
	}
    }

  ret = source_play();
  if (ret < 0)
    {
      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }

  metadata_trigger(1);

  // Start sessions on selected devices
  *retval = 0;

  for (device = dev_list; device; device = device->next)
    {
      if (device->selected && !device->session)
	{
	  ret = outputs_device_start(device, device_restart_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
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
    for (device = dev_list; device; device = device->next)
      {
	if ((outputs_priority(device) == 0) || device->session)
	  continue;

	speaker_select_output(device);
	ret = outputs_device_start(device, device_restart_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
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

      ret = db_queue_add_by_fileid(cmdarg->id, 0, 0);
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
  int ret;
  int pos_sec;
  struct player_source *ps;

  // The upper half is playback_pause, therefor the current playing item is
  // already set as the cur_streaming (cur_playing is NULL).
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      *retval = -1;
      return COMMAND_END;
    }

  // Only add to history if playback started
  if (cur_streaming->output_start > cur_streaming->stream_start)
    history_add(cur_streaming->id, cur_streaming->item_id);

  // Compute the playing time in seconds for the current song
  if (cur_streaming->output_start > cur_streaming->stream_start)
    pos_sec = (cur_streaming->output_start - cur_streaming->stream_start) / 44100;
  else
    pos_sec = 0;

  // Only skip to the previous song if the playing time is less than 3 seconds,
  // otherwise restart the current song.
  DPRINTF(E_DBG, L_PLAYER, "Skipping song played %d sec\n", pos_sec);
  if (pos_sec < 3)
    {
      ps = source_prev();
      if (!ps)
        {
          playback_abort();
          *retval = -1;
          return COMMAND_END;
        }

      source_stop();

      ret = source_open(ps, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, 0);
      if (ret < 0)
	{
	  source_free(ps);
	  playback_abort();

          *retval = -1;
          return COMMAND_END;
	}
    }
  else
    {
      ret = source_seek(0);
      if (ret < 0)
	{
	  playback_abort();

          *retval = -1;
          return COMMAND_END;
	}
    }

  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_next_bh(void *arg, int *retval)
{
  struct player_source *ps;
  int ret;
  uint32_t item_id;

  // The upper half is playback_pause, therefor the current playing item is
  // already set as the cur_streaming (cur_playing is NULL).
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      *retval = -1;
      return COMMAND_END;
    }

  // Only add to history if playback started
  if (cur_streaming->output_start > cur_streaming->stream_start)
    history_add(cur_streaming->id, cur_streaming->item_id);

  item_id = cur_streaming->item_id;

  ps = source_next();
  if (!ps)
    {
      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }

  source_stop();

  ret = source_open(ps, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, 0);
  if (ret < 0)
    {
      source_free(ps);
      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }

  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  if (consume)
    db_queue_delete_byitemid(item_id);

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_seek_bh(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  int ms;
  int ret;

  *retval = -1;

  if (!cur_streaming)
    return COMMAND_END;

  ms = cmdarg->intval;

  ret = source_seek(ms);
  if (ret < 0)
    {
      playback_abort();
      return COMMAND_END;
    }

  // Silent status change - playback_start() sends the real status update
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_pause_bh(void *arg, int *retval)
{
  *retval = -1;

  // outputs_flush() in playback_pause() may have a caused a failure callback
  // from the output, which in streaming_cb() can cause playback_abort() ->
  // cur_streaming is NULL
  if (!cur_streaming)
    return COMMAND_END;

  if (cur_streaming->data_kind == DATA_KIND_HTTP || cur_streaming->data_kind == DATA_KIND_PIPE)
    {
      DPRINTF(E_DBG, L_PLAYER, "Source is not pausable, abort playback\n");

      playback_abort();
      return COMMAND_END;
    }
  status_update(PLAY_PAUSED);

  seek_save();

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_pause(void *arg, int *retval)
{
  uint64_t pos;

  pos = source_check();
  if (pos == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not retrieve current position for pause\n");

      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }

  // Make sure playback is still running after source_check()
  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  *retval = outputs_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  playback_timer_stop();

  source_pause(pos);

  outputs_metadata_purge();

  // We're async if we need to flush devices
  if (*retval > 0)
    return COMMAND_PENDING; // async

  // Otherwise, just run the bottom half
  return COMMAND_END;
}

/*
 * Notify of speaker/device changes (other than activation/deactivation)
 */
void
player_speaker_status_trigger(void)
{
  listener_notify(LISTENER_SPEAKER);
}

static enum command_state
speaker_enumerate(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;
  struct spk_enum *spk_enum;
  struct spk_flags flags;

  spk_enum = cmdarg->spk_enum;

#ifdef DEBUG_RELVOL
  DPRINTF(E_DBG, L_PLAYER, "*** master: %d\n", master_volume);
#endif

  for (device = dev_list; device; device = device->next)
    {
      if (device->advertised || device->selected)
	{
	  flags.selected = device->selected;
	  flags.has_password = device->has_password;
	  flags.has_video = device->has_video;
	  flags.requires_auth = device->requires_auth;
	  flags.needs_auth_key = (device->requires_auth && device->auth_key == NULL);

	  spk_enum->cb(device->id, device->name, device->type_name, device->relvol, device->volume, flags, spk_enum->arg);

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", device->name, device->volume, device->relvol);
#endif
	}
    }

  *retval = 0;
  return COMMAND_END;
}

static int
speaker_activate(struct output_device *device)
{
  int ret;

  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Bug! speaker_activate called with device\n");
      return -1;
    }

  if (player_state == PLAY_PLAYING)
    {
      DPRINTF(E_DBG, L_PLAYER, "Activating %s device '%s'\n", device->type_name, device->name);

      ret = outputs_device_start(device, device_activate_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not start %s device '%s'\n", device->type_name, device->name);
	  return -1;
	}
    }
  else
    {
      DPRINTF(E_DBG, L_PLAYER, "Probing %s device '%s'\n", device->type_name, device->name);

      ret = outputs_device_probe(device, device_probe_cb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not probe %s device '%s'\n", device->type_name, device->name);
	  return -1;
	}
    }

  return 0;
}

static int
speaker_deactivate(struct output_device *device)
{
  DPRINTF(E_DBG, L_PLAYER, "Deactivating %s device '%s'\n", device->type_name, device->name);

  outputs_status_cb(device->session, device_shutdown_cb);
  outputs_device_stop(device->session);

  return 0;
}

static enum command_state
speaker_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  *retval = 0;
  ids = cmdarg->speaker_set_param.device_ids;

  if (ids)
    nspk = ids[0];
  else
    nspk = 0;

  DPRINTF(E_DBG, L_PLAYER, "Speaker set: %d speakers\n", nspk);

  *retval = 0;

  for (device = dev_list; device; device = device->next)
    {
      for (i = 1; i <= nspk; i++)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Set %" PRIu64 " device %" PRIu64 "\n", ids[i], device->id);

	  if (ids[i] == device->id)
	    break;
	}

      if (i <= nspk)
	{
	  if (device->has_password && !device->password)
	    {
	      DPRINTF(E_INFO, L_PLAYER, "The %s device '%s' is password-protected, but we don't have it\n", device->type_name, device->name);

	      cmdarg->speaker_set_param.intval = -2;
	      continue;
	    }

	  DPRINTF(E_DBG, L_PLAYER, "The %s device '%s' is selected\n", device->type_name, device->name);

	  if (!device->selected)
	    speaker_select_output(device);

	  if (!device->session)
	    {
	      ret = speaker_activate(device);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Could not activate %s device '%s'\n", device->type_name, device->name);

		  speaker_deselect_output(device);

		  if (cmdarg->speaker_set_param.intval != -2)
		    cmdarg->speaker_set_param.intval = -1;
		}
	      else
		(*retval)++;
	    }
	}
      else
	{
	  DPRINTF(E_DBG, L_PLAYER, "The %s device '%s' is NOT selected\n", device->type_name, device->name);

	  if (device->selected)
	    speaker_deselect_output(device);

	  if (device->session)
	    {
	      ret = speaker_deactivate(device);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Could not deactivate %s device '%s'\n", device->type_name, device->name);

		  if (cmdarg->speaker_set_param.intval != -2)
		    cmdarg->speaker_set_param.intval = -1;
		}
	      else
		(*retval)++;
	    }
	}
    }

  listener_notify(LISTENER_SPEAKER);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  *retval = cmdarg->speaker_set_param.intval;
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

  for (device = dev_list; device; device = device->next)
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

static enum command_state
volume_setrel_speaker(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;
  uint64_t id;
  int relvol;

  *retval = 0;
  id = cmdarg->vol_param.spk_id;
  relvol = cmdarg->vol_param.volume;

  for (device = dev_list; device; device = device->next)
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
	*retval = outputs_device_volume_set(device, device_command_cb);

      break;
    }

  listener_notify(LISTENER_VOLUME);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
volume_setabs_speaker(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct output_device *device;
  uint64_t id;
  int volume;

  *retval = 0;
  id = cmdarg->vol_param.spk_id;
  volume = cmdarg->vol_param.volume;

  master_volume = volume;

  for (device = dev_list; device; device = device->next)
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
	    *retval = outputs_device_volume_set(device, device_command_cb);//FIXME Does this need to be += ?
	}
    }

  listener_notify(LISTENER_VOLUME);

  if (*retval > 0)
    return COMMAND_PENDING; // async

  return COMMAND_END;
}

static enum command_state
repeat_set(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;

  if (cmdarg->mode == repeat)
    {
      *retval = 0;
      return COMMAND_END;
    }

  switch (cmdarg->mode)
    {
      case REPEAT_OFF:
      case REPEAT_SONG:
      case REPEAT_ALL:
	repeat = cmdarg->mode;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid repeat mode: %d\n", cmdarg->mode);
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
  uint32_t cur_id;

  switch (cmdarg->intval)
    {
      case 1:
	if (!shuffle)
	  {
	    cur_id = cur_streaming ? cur_streaming->item_id : 0;
	    db_queue_reshuffle(cur_id);
	  }
	/* FALLTHROUGH */
      case 0:
	shuffle = cmdarg->intval;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid shuffle mode: %d\n", cmdarg->intval);
	*retval = -1;
	return COMMAND_END;
    }

  listener_notify(LISTENER_OPTIONS);

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
player_get_current_pos(uint64_t *pos, struct timespec *ts, int commit)
{
  uint64_t delta;
  int ret;

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, ts, &timer_res);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get clock: %s\n", strerror(errno));

      return -1;
    }

  delta = (ts->tv_sec - pb_pos_stamp.tv_sec) * 1000000 + (ts->tv_nsec - pb_pos_stamp.tv_nsec) / 1000;

#ifdef DEBUG_SYNC
  DPRINTF(E_DBG, L_PLAYER, "Delta is %" PRIu64 " usec\n", delta);
#endif

  delta = (delta * 44100) / 1000000;

#ifdef DEBUG_SYNC
  DPRINTF(E_DBG, L_PLAYER, "Delta is %" PRIu64 " samples\n", delta);
#endif

  *pos = pb_pos + delta;

  if (commit)
    {
      pb_pos = *pos;

      pb_pos_stamp.tv_sec = ts->tv_sec;
      pb_pos_stamp.tv_nsec = ts->tv_nsec;

#ifdef DEBUG_SYNC
      DPRINTF(E_DBG, L_PLAYER, "Pos: %" PRIu64 " (clock)\n", *pos);
#endif
    }

  return 0;
}

int
player_get_status(struct player_status *status)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.status = status;

  ret = commands_exec_sync(cmdbase, get_status, NULL, &cmdarg);
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
player_now_playing(uint32_t *id)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.id_ptr = id;

  ret = commands_exec_sync(cmdbase, now_playing, NULL, &cmdarg);
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
  union player_arg cmdarg;
  struct spk_enum spk_enum;

  spk_enum.cb = cb;
  spk_enum.arg = arg;

  cmdarg.spk_enum = &spk_enum;

  commands_exec_sync(cmdbase, speaker_enumerate, NULL, &cmdarg);
}

int
player_speaker_set(uint64_t *ids)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.speaker_set_param.device_ids = ids;
  cmdarg.speaker_set_param.intval = 0;

  ret = commands_exec_sync(cmdbase, speaker_set, NULL, &cmdarg);
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
  union player_arg cmdarg;
  int ret;

  if (relvol < 0 || relvol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setrel_speaker is out of range\n", relvol);
      return -1;
    }

  cmdarg.vol_param.spk_id = id;
  cmdarg.vol_param.volume = relvol;

  ret = commands_exec_sync(cmdbase, volume_setrel_speaker, NULL, &cmdarg);
  return ret;
}

int
player_volume_setabs_speaker(uint64_t id, int vol)
{
  union player_arg cmdarg;
  int ret;

  if (vol < 0 || vol > 100)
    {
      DPRINTF(E_LOG, L_PLAYER, "Volume (%d) for player_volume_setabs_speaker is out of range\n", vol);
      return -1;
    }

  cmdarg.vol_param.spk_id = id;
  cmdarg.vol_param.volume = vol;

  ret = commands_exec_sync(cmdbase, volume_setabs_speaker, NULL, &cmdarg);
  return ret;
}

int
player_repeat_set(enum repeat_mode mode)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.mode = mode;

  ret = commands_exec_sync(cmdbase, repeat_set, NULL, &cmdarg);
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
  union player_arg cmdarg;

  cmdarg.metadata_param.input = imd;
  cmdarg.metadata_param.output = omd;

  commands_exec_sync(cmdbase, device_metadata_send, NULL, &cmdarg);
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

  for (device = dev_list; device; device = device->next)
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
  uint32_t rnd;
  int ret;

  player_exit = 0;

  speaker_autoselect = cfg_getbool(cfg_getsec(cfg, "general"), "speaker_autoselect");
  clear_queue_on_stop_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");

  dev_list = NULL;

  master_volume = -1;

  output_sessions = 0;

  cur_playing = NULL;
  cur_streaming = NULL;
  cur_plid = 0;
  cur_plversion = 0;

  player_state = PLAY_STOPPED;
  repeat = REPEAT_OFF;
  shuffle = 0;
  consume = 0;

  history = (struct player_history *)calloc(1, sizeof(struct player_history));

  // Determine if the resolution of the system timer is > or < the size
  // of an audio packet. NOTE: this assumes the system clock resolution
  // is less than one second.
  if (clock_getres(CLOCK_MONOTONIC, &timer_res) < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get the system timer resolution.\n");

      return -1;
    }

  if (!cfg_getbool(cfg_getsec(cfg, "general"), "high_resolution_clock"))
    {
      DPRINTF(E_INFO, L_PLAYER, "High resolution clock not enabled on this system (res is %ld)\n", timer_res.tv_nsec);

      timer_res.tv_nsec = 2 * AIRTUNES_V2_STREAM_PERIOD;
    }

  // Set the tick interval for the playback timer
  interval = MAX(timer_res.tv_nsec, AIRTUNES_V2_STREAM_PERIOD);
  tick_interval.tv_nsec = interval;

  pb_write_deficit_max = (PLAYER_WRITE_BEHIND_MAX * 1000000 / interval);
  pb_read_deficit_max  = (PLAYER_READ_BEHIND_MAX * 1000000 / interval);

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

      return -1;
    }

  // Random RTP time start
  gcry_randomize(&rnd, sizeof(rnd), GCRY_STRONG_RANDOM);
  last_rtptime = ((uint64_t)1 << 32) | rnd;

  evbase_player = event_base_new();
  if (!evbase_player)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create an event base\n");

      goto evbase_fail;
    }

#ifdef HAVE_TIMERFD
  pb_timer_ev = event_new(evbase_player, pb_timer_fd, EV_READ | EV_PERSIST, playback_cb, NULL);
#else
  pb_timer_ev = event_new(evbase_player, SIGALRM, EV_SIGNAL | EV_PERSIST, playback_cb, NULL);
#endif
  if (!pb_timer_ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer event\n");
      goto evnew_fail;
    }

  cmdbase = commands_base_new(evbase_player, NULL);

  ret = outputs_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Output initiation failed\n");
      goto outputs_fail;
    }

  ret = input_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Input initiation failed\n");
      goto input_fail;
    }

  ret = pthread_create(&tid_player, NULL, player, NULL);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Could not spawn player thread: %s\n", strerror(errno));
      goto thread_fail;
    }
#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_player, "player");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_player, "player");
#endif

  return 0;

 thread_fail:
  input_deinit();
 input_fail:
  outputs_deinit();
 outputs_fail:
  commands_base_free(cmdbase);
 evnew_fail:
  event_base_free(evbase_player);
 evbase_fail:
#ifdef HAVE_TIMERFD
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif

  return -1;
}

void
player_deinit(void)
{
  int ret;

  player_playback_stop();

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
