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
 * and other clients (e.g. Remotes). Text metadata is handled differently than
 * artwork. Here's how text works:
 *
 *  1. On playback start, the player will TODO
 *  2. During playback, the input may signal new metadata by making a
 *     input_write() with the INPUT_FLAG_METADATA flag. When the player read
 *     reaches that data, the player will request the metadata from the input
 *     with input_metadata_get().
 *  3. If the new metadata is different than the TODO
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
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

/* Audio and metadata outputs */
#include "outputs.h"

/* Audio and metadata input */
#include "input.h"

/* Scrobbling */
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
// Used to keep the player from getting ahead of a rate limited source (see below)
#define PLAYER_TICKS_MAX_OVERRUN 2

struct volume_param {
  int volume;
  uint64_t spk_id;
};

struct spk_enum
{
  spk_enum_cb cb;
  void *arg;
};

struct icy_artwork
{
  uint32_t id;
  char *artwork_url;
};

struct player_metadata
{
  int id;
  uint64_t rtptime;
  uint64_t offset;
  int startup;

  struct output_metadata *omd;
};

struct speaker_set_param
{
  uint64_t *device_ids;
  int intval;
};

union player_arg
{
  struct volume_param vol_param;
  void *noarg;
  struct spk_enum *spk_enum;
  struct output_device *device;
  struct player_status *status;
  struct player_source *ps;
  struct player_metadata *pmd;
  uint32_t *id_ptr;
  struct speaker_set_param speaker_set_param;
  enum repeat_mode mode;
  uint32_t id;
  int intval;
  struct icy_artwork icy;
};

struct event_base *evbase_player;

static int player_exit;
static pthread_t tid_player;
static struct commands_base *cmdbase;

/* Config values */
static int speaker_autoselect;
static int clear_queue_on_stop_disabled;

/* Player status */
static enum play_status player_state;
static enum repeat_mode repeat;
static char shuffle;
static char consume;

/* Playback timer */
#ifdef HAVE_TIMERFD
static int pb_timer_fd;
#else
timer_t pb_timer;
#endif
static struct event *pb_timer_ev;
static struct timespec pb_timer_last;
static struct timespec packet_timer_last;

// How often the playback timer triggers player_playback_cb
static struct timespec tick_interval;
// Timer resolution
static struct timespec timer_res;
// Time between two packets
static struct timespec packet_time = { 0, AIRTUNES_V2_STREAM_PERIOD };
// Will be positive if we need to skip some source reads (see below)
static int ticks_skip;

/* Sync values */
static struct timespec pb_pos_stamp;
static uint64_t pb_pos;

/* Stream position (packets) */
static uint64_t last_rtptime;

/* Output devices */
static struct output_device *dev_list;

/* Output status */
static int output_sessions;

/* Last commanded volume */
static int master_volume;

/* Audio source */
static struct player_source *cur_playing;
static struct player_source *cur_streaming;
static uint32_t cur_plid;
static uint32_t cur_plversion;

static struct evbuffer *audio_buf;
static uint8_t rawbuf[STOB(AIRTUNES_V2_PACKET_SAMPLES)];


/* Play history */
static struct player_history *history;


static void
status_update(enum play_status status)
{
  player_state = status;

  listener_notify(LISTENER_PLAYER);
}


/* Volume helpers */
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

/* Master volume helpers */
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


/* Device select/deselect hooks */
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

static int
pb_timer_start(void)
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

/* Forward */
static void
playback_abort(void);

static void
player_metadata_send(struct player_metadata *pmd);

/* Callback from the worker thread (async operation as it may block) */
static void
playcount_inc_cb(void *arg)
{
  int *id = arg;

  db_file_inc_playcount(*id);
}

#ifdef LASTFM
/* Callback from the worker thread (async operation as it may block) */
static void
scrobble_cb(void *arg)
{
  int *id = arg;

  lastfm_scrobble(*id);
}
#endif

/* Callback from the worker thread
 * This prepares metadata in the worker thread, since especially the artwork
 * retrieval may take some time. outputs_metadata_prepare() must be thread safe.
 * The sending must be done in the player thread.
 */
static void
metadata_prepare_cb(void *arg)
{
  struct player_metadata *pmd = arg;

  pmd->omd = outputs_metadata_prepare(pmd->id);

  if (pmd->omd)
    player_metadata_send(pmd);

  outputs_metadata_free(pmd->omd);
}

/* Callback from the worker thread (async operation as it may block) */
static void
update_icy_cb(void *arg)
{
  struct http_icy_metadata *metadata = arg;

  db_queue_update_icymetadata(metadata->id, metadata->artist, metadata->title);

  http_icy_metadata_free(metadata, 1);
}

/* Metadata */
static void
metadata_prune(uint64_t pos)
{
  outputs_metadata_prune(pos);
}

static void
metadata_purge(void)
{
  outputs_metadata_purge();
}

static void
metadata_trigger(int startup)
{
  struct player_metadata pmd;

  memset(&pmd, 0, sizeof(struct player_metadata));

  pmd.id = cur_streaming->item_id;
  pmd.startup = startup;

  if (cur_streaming->stream_start && cur_streaming->output_start)
    {
      pmd.offset = cur_streaming->output_start - cur_streaming->stream_start;
      pmd.rtptime = cur_streaming->stream_start;
    }
  else
    {
      DPRINTF(E_LOG, L_PLAYER, "PTOH! Unhandled song boundary case in metadata_trigger()\n");
    }

  /* Defer the actual work of preparing the metadata to the worker thread */
  worker_execute(metadata_prepare_cb, &pmd, sizeof(struct player_metadata), 0);
}

/* Checks if there is new HTTP ICY metadata, and if so sends updates to clients */
void
metadata_check_icy(void)
{
  struct http_icy_metadata *metadata;
  int changed;

  metadata = transcode_metadata(cur_streaming->xcode, &changed);
  if (!metadata)
    return;

  if (!changed || !metadata->title)
    goto no_update;

  if (metadata->title[0] == '\0')
    goto no_update;

  metadata->id = cur_streaming->item_id;

  /* Defer the database update to the worker thread */
  worker_execute(update_icy_cb, metadata, sizeof(struct http_icy_metadata), 0);

  /* Triggers preparing and sending output metadata */
  metadata_trigger(0);

  /* Only free the struct, the content must be preserved for update_icy_cb */
  free(metadata);

  status_update(player_state);

  return;

 no_update:
  http_icy_metadata_free(metadata, 0);
}

struct player_history *
player_history_get(void)
{
  return history;
}

/*
 * Add the song with the given id to the list of previously played songs
 */
static void
history_add(uint32_t id, uint32_t item_id)
{
  unsigned int cur_index;
  unsigned int next_index;

  /* Check if the current song is already the last in the history to avoid duplicates */
  cur_index = (history->start_index + history->count - 1) % MAX_HISTORY_COUNT;
  if (id == history->id[cur_index])
    {
      DPRINTF(E_DBG, L_PLAYER, "Current playing/streaming song already in history\n");
      return;
    }

  /* Calculate the next index and update the start-index and count for the id-buffer */
  next_index = (history->start_index + history->count) % MAX_HISTORY_COUNT;
  if (next_index == history->start_index && history->count > 0)
    history->start_index = (history->start_index + 1) % MAX_HISTORY_COUNT;

  history->id[next_index] = id;
  history->item_id[next_index] = item_id;

  if (history->count < MAX_HISTORY_COUNT)
    history->count++;
}


/* Audio sources */

/*
 * Read up to "len" data from the given player source and returns
 * the actual amount of data read.
 */
/*static int
stream_read(struct player_source *ps, int len)
{
  int icy_timer;
  int ret;

  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream read called with no active streaming player source\n");
      return -1;
    }

  if (!ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup for reading data\n");
      return -1;
    }

  // Read up to len data depending on data kind
  switch (ps->data_kind)
    {
      case DATA_KIND_HTTP:
	ret = transcode(audio_buf, len, ps->xcode, &icy_timer);

	if (icy_timer)
	  metadata_check_icy();
	break;

      case DATA_KIND_FILE:
	ret = transcode(audio_buf, len, ps->xcode, &icy_timer);
	break;

#ifdef HAVE_SPOTIFY_H
      case DATA_KIND_SPOTIFY:
	ret = spotify_audio_get(audio_buf, len);
	break;
#endif

      case DATA_KIND_PIPE:
	ret = pipe_audio_get(audio_buf, len);
	break;

      default:
	ret = -1;
    }

  return ret;
}*/

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

  /* Seek back to the pause position */
  seek_frames = (pos - cur_streaming->stream_start);
  seek_ms = (int)((seek_frames * 1000) / 44100);
  ret = input_seek(cur_streaming, seek_ms);

// TODO what if ret < 0?

  /* Adjust start_pos to take into account the pause and seek back */
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

  /* Adjust start_pos to take into account the pause and seek back */
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

  ticks_skip = 0;
  memset(rawbuf, 0, sizeof(rawbuf));

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

  /* If a streaming source exists, append the new source as play-next and set it
     as the new streaming source */
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

  /* If cur_playing is NULL, we are still in the first two seconds after starting the stream */
  if (!cur_playing)
    {
      if (pos >= cur_streaming->output_start)
	{
	  cur_playing = cur_streaming;
	  status_update(PLAY_PLAYING);

	  /* Start of streaming, no metadata to prune yet */
	}

      return pos;
    }

  /* Check if we are still in the middle of the current playing song */
  if ((cur_playing->end == 0) || (pos < cur_playing->end))
    return pos;

  /* We have reached the end of the current playing song, update cur_playing to the next song in the queue
     and initialize stream_start and output_start values. */

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

      /* Stop playback */
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

      metadata_prune(pos);
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
source_read(uint8_t *buf, int len, uint64_t rtptime)
{
  int ret;
  int nbytes;
  short flags;
  char *silence_buf;
  struct player_source *ps;

  if (!cur_streaming)
    return 0;

  nbytes = 0;
  while (nbytes < len)
    {
      if (evbuffer_get_length(audio_buf) == 0)
	{
	  flags = 0;
	  if (cur_streaming)
	    {
	      ret = input_read(audio_buf, len - nbytes, &flags);
	    }
	  else if (cur_playing)
	    {
	      // Reached end of playlist (cur_playing is NULL) send silence and source_check will abort playback if the last item was played
	      DPRINTF(E_SPAM, L_PLAYER, "End of playlist reached, stream silence until playback of last item ends\n");
	      silence_buf = (char *)calloc((len - nbytes), sizeof(char));
	      evbuffer_add(audio_buf, silence_buf, (len - nbytes));
	      free(silence_buf);
	      ret = len - nbytes;
	    }
	  else
	    {
	      // If cur_streaming and cur_playing are NULL, source_read for all queue items failed. Playback will be aborted in the calling function
	      return -1;
	    }

	  if (ret == 0)
	    sleep(1); // TODO Underrun -> proper pause
	  else if ((ret < 0) || (flags & INPUT_FLAG_EOF))
	    {
	      source_close(rtptime + BTOS(nbytes) - 1);

	      DPRINTF(E_DBG, L_PLAYER, "New file\n");

	      ps = source_next();

	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Error reading source %d\n", cur_streaming->id);
		  db_queue_delete_byitemid(cur_streaming->item_id);
		}

	      if (ps)
		{
		  ret = source_open(ps, cur_streaming->end + 1, 0);
		  if (ret < 0)
		    {
		      source_free(ps);
		      return -1;
		    }

		  ret = source_play();
		  if (ret < 0)
		    return -1;

		  metadata_trigger(0);
		}
	      else
		{
		  cur_streaming = NULL;
		}
	      continue;
	    }
	}

      nbytes += evbuffer_remove(audio_buf, buf + nbytes, len - nbytes);
    }

  return nbytes;
}

static void
playback_write(int read_skip)
{
  int ret;

  source_check();

  /* Make sure playback is still running after source_check() */
  if (player_state == PLAY_STOPPED)
    return;

  last_rtptime += AIRTUNES_V2_PACKET_SAMPLES;

  if (!read_skip)
    {
      ret = source_read(rawbuf, sizeof(rawbuf), last_rtptime);
      if (ret < 0)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Error reading from source, aborting playback\n");

	  playback_abort();
	  return;
	}
    }
  else
    DPRINTF(E_SPAM, L_PLAYER, "Skipping read\n");

  outputs_write(rawbuf, last_rtptime);
}

static void
player_playback_cb(int fd, short what, void *arg)
{
  struct timespec next_tick;
  uint64_t overrun;
  int ret;
  int skip;
  int skip_first;

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

  // The reason we get behind the playback timer may be that we are playing a 
  // network stream OR that the source is slow to open OR some interruption.
  // For streams, we might be consuming faster than the stream delivers, so
  // when ffmpeg's buffer empties (might take a few hours) our av_read_frame()
  // in transcode.c will begin to block, because ffmpeg has to wait for new data
  // from the stream server.
  //
  // Our strategy to catch up with the timer depends on the source:
  //   - streams: We will skip reading data every second until we have countered
  //              the overrun by skipping reads for a number of ticks that is
  //              3 times the overrun. That should make the source catch up. To
  //              keep the output happy we resend the previous rawbuf when we
  //              have skipped a read.
  //   - files:   Just read and write like crazy until we have caught up.

  skip_first = 0;
  if (overrun > PLAYER_TICKS_MAX_OVERRUN)
    {
      DPRINTF(E_WARN, L_PLAYER, "Behind the playback timer with %" PRIu64 " ticks\n", overrun);

      if (cur_streaming && (cur_streaming->data_kind == DATA_KIND_HTTP || cur_streaming->data_kind == DATA_KIND_PIPE))
	{
          ticks_skip = 3 * overrun;

	  DPRINTF(E_WARN, L_PLAYER, "Will skip reading for a total of %d ticks to catch up\n", ticks_skip);

	  // We always skip after a timer overrun, since another read will
	  // probably just give another time overrun
	  skip_first = 1;
	}
      else
	ticks_skip = 0;
    }

  // Decide how many packets to send
  next_tick = timespec_add(pb_timer_last, tick_interval);
  for (; overrun > 0; overrun--)
    next_tick = timespec_add(next_tick, tick_interval);

  do
    {
	skip = skip_first || ((ticks_skip > 0) && ((last_rtptime / AIRTUNES_V2_PACKET_SAMPLES) % 126 == 0));

	playback_write(skip);

	skip_first = 0;
	if (skip)
	  ticks_skip--;

      packet_timer_last = timespec_add(packet_timer_last, packet_time);
    }
  while ((timespec_cmp(packet_timer_last, next_tick) < 0) && (player_state == PLAY_PLAYING));

  /* Make sure playback is still running */
  if (player_state == PLAY_STOPPED)
    return;

  pb_timer_last = next_tick;
}

/* Helpers */
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

  /* Save device volume */
  ret = db_speaker_save(remove->id, remove->selected, remove->volume, remove->name);
  if (ret < 0)
    DPRINTF(E_LOG, L_PLAYER, "Could not save state for %s device '%s'\n", remove->type_name, remove->name);

  DPRINTF(E_DBG, L_PLAYER, "Removing %s device '%s'; stopped advertising\n", remove->type_name, remove->name);

  /* Make sure device isn't selected anymore */
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
  int selected;
  int ret;

  cmdarg = arg;
  add = cmdarg->device;

  for (device = dev_list; device; device = device->next)
    {
      if (device->id == add->id)
	break;
    }

  /* New device */
  if (!device)
    {
      device = add;

      ret = db_speaker_get(device->id, &selected, &device->volume);
      if (ret < 0)
	{
	  selected = 0;
	  device->volume = (master_volume >= 0) ? master_volume : PLAYER_DEFAULT_VOLUME;
	}

      if (selected && (player_state != PLAY_PLAYING))
	speaker_select_output(device);

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

	  /* Address is ours now */
	  add->v4_address = NULL;
	}

      if (add->v6_address)
	{
	  if (device->v6_address)
	    free(device->v6_address);

	  device->v6_address = add->v6_address;
	  device->v6_port = add->v6_port;

	  /* Address is ours now */
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

  /* v{4,6}_port non-zero indicates the address family stopped advertising */
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
metadata_send(void *arg, int *retval)
{
  union player_arg *cmdarg;
  struct player_metadata *pmd;

  cmdarg = arg;
  pmd = cmdarg->pmd;

  /* Do the setting of rtptime which was deferred in metadata_trigger because we
   * wanted to wait until we had the actual last_rtptime
   */
  if ((pmd->rtptime == 0) && (pmd->startup))
    pmd->rtptime = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;

  outputs_metadata_send(pmd->omd, pmd->rtptime, pmd->offset, pmd->startup);

  *retval = 0;
  return COMMAND_END;
}

/* Output device callbacks executed in the player thread */
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

  /* We lost that device during startup for some reason, not much we can do here */
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

	  /* Fallback to nearest timer expiration time */
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
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Callback from %s to device_restart_cb\n", outputs_name(device->type));

  ret = device_check(device);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Output device disappeared during restart!\n");

      outputs_status_cb(session, device_lost_cb);
      outputs_device_stop(session);

      goto out;
    }

  if (status == OUTPUT_STATE_FAILED)
    {
      speaker_deselect_output(device);

      if (!device->advertised)
	device_remove(device);

      goto out;
    }

  device->session = session;

  output_sessions++;
  outputs_status_cb(session, device_streaming_cb);

 out:
  commands_exec_end(cmdbase, 0);
}

/* Internal abort routine */
static void
playback_abort(void)
{
  outputs_playback_stop();

  pb_timer_stop();

  source_stop();

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  if (!clear_queue_on_stop_disabled)
    db_queue_clear();

  status_update(PLAY_STOPPED);

  metadata_purge();
}

/* Actual commands, executed in the player thread */
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

	    /* Avoid a visible 2-second jump backward for the client */
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
artwork_url_get(void *arg, int *retval)
{
  union player_arg *cmdarg = arg;
  struct player_source *ps;

  cmdarg->icy.artwork_url = NULL;

  if (cur_playing)
    ps = cur_playing;
  else if (cur_streaming)
    ps = cur_streaming;
  else
    {
      *retval = -1;
      return COMMAND_END;
    }

  /* Check that we are playing a viable stream, and that it has the requested id */
  if (!ps->xcode || ps->data_kind != DATA_KIND_HTTP || ps->id != cmdarg->icy.id)
    {
      *retval = -1;
      return COMMAND_END;
    }

  cmdarg->icy.artwork_url = transcode_metadata_artwork_url(ps->xcode);

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_stop(void *arg, int *retval)
{
  struct player_source *ps_playing;

  /* We may be restarting very soon, so we don't bring the devices to a
   * full stop just yet; this saves time when restarting, which is nicer
   * for the user.
   */
  *retval = outputs_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  pb_timer_stop();

  ps_playing = source_now_playing();
  if (ps_playing)
    {
      history_add(ps_playing->id, ps_playing->item_id);
    }

  source_stop();

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  status_update(PLAY_STOPPED);

  metadata_purge();

  /* We're async if we need to flush devices */
  if (*retval > 0)
    return COMMAND_PENDING; /* async */

  return COMMAND_END;
}

/* Playback startup bottom half */
static enum command_state
playback_start_bh(void *arg, int *retval)
{
  int ret;

  if (output_sessions == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Cannot start playback: no output started\n");

      goto out_fail;
    }

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, &pb_pos_stamp, &timer_res);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get current clock: %s\n", strerror(errno));

      goto out_fail;
    }

  pb_timer_stop();

  /*
   * initialize the packet timer to the same relative time that we have 
   * for the playback timer.
   */
  packet_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  packet_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  pb_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  pb_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  ret = pb_timer_start();
  if (ret < 0)
    goto out_fail;

  /* Everything OK, start outputs */
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
      status_update(player_state);

      *retval = 1; // Value greater 0 will prevent execution of the bottom half function
      return COMMAND_END;
    }

  // Update global playback position
  pb_pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - 88200;

  if (player_state == PLAY_STOPPED && !queue_item)
    {
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

  /* Start sessions on selected devices */
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

  /* If autoselecting is enabled, try to autoselect a non-selected device if the above failed */
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

  /* No luck finding valid output */
  if ((*retval == 0) && (output_sessions == 0))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not start playback: no output selected or couldn't start any output\n");

      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }

  /* We're async if we need to start devices */
  if (*retval > 0)
    return COMMAND_PENDING; /* async */

  /* Otherwise, just run the bottom half */
  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_start(void *arg, int *retval)
{
  struct db_queue_item *queue_item = NULL;
  enum command_state cmd_state;

  if (player_state == PLAY_STOPPED)
{
      // Start playback of first item in queue
      queue_item = db_queue_fetch_bypos(0, shuffle);
      if (!queue_item)
{
	  *retval = -1;
	  return COMMAND_END;
}
    }

  cmd_state = playback_start_item(queue_item, retval);
  return cmd_state;
}

static enum command_state
playback_prev_bh(void *arg, int *retval)
{
  int ret;
  int pos_sec;
  struct player_source *ps;

  /*
   * The upper half is playback_pause, therefor the current playing item is
   * already set as the cur_streaming (cur_playing is NULL).
   */
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      *retval = -1;
      return COMMAND_END;
    }

  /* Only add to history if playback started. */
  if (cur_streaming->output_start > cur_streaming->stream_start)
    history_add(cur_streaming->id, cur_streaming->item_id);

  /* Compute the playing time in seconds for the current song. */
  if (cur_streaming->output_start > cur_streaming->stream_start)
    pos_sec = (cur_streaming->output_start - cur_streaming->stream_start) / 44100;
  else
    pos_sec = 0;

  /* Only skip to the previous song if the playing time is less than 3 seconds,
   otherwise restart the current song. */
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

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

/*
 * The bottom half of the next command
 */
static enum command_state
playback_next_bh(void *arg, int *retval)
{
  struct player_source *ps;
  int ret;
  uint32_t item_id;

  /*
   * The upper half is playback_pause, therefor the current playing item is
   * already set as the cur_streaming (cur_playing is NULL).
   */
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      *retval = -1;
      return COMMAND_END;
    }

  /* Only add to history if playback started. */
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

  /* Silent status change - playback_start() sends the real status update */
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

  ms = cmdarg->intval;

  ret = source_seek(ms);

  if (ret < 0)
    {
      playback_abort();

      *retval = -1;
      return COMMAND_END;
    }

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  *retval = 0;
  return COMMAND_END;
}

static enum command_state
playback_pause_bh(void *arg, int *retval)
{
  int ret;

  if (cur_streaming->data_kind == DATA_KIND_HTTP
      || cur_streaming->data_kind == DATA_KIND_PIPE)
    {
      DPRINTF(E_DBG, L_PLAYER, "Source is not pausable, abort playback\n");

      playback_abort();
      *retval = -1;
      return COMMAND_END;
    }
  status_update(PLAY_PAUSED);

  if (cur_streaming->media_kind & (MEDIA_KIND_MOVIE | MEDIA_KIND_PODCAST | MEDIA_KIND_AUDIOBOOK | MEDIA_KIND_TVSHOW))
    {
      ret = (cur_streaming->output_start - cur_streaming->stream_start) / 44100 * 1000;
      db_file_save_seek(cur_streaming->id, ret);
    }

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

  /* Make sure playback is still running after source_check() */
  if (player_state == PLAY_STOPPED)
    {
      *retval = -1;
      return COMMAND_END;
    }

  *retval = outputs_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  pb_timer_stop();

  source_pause(pos);

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  metadata_purge();

  /* We're async if we need to flush devices */
  if (*retval > 0)
    return COMMAND_PENDING; /* async */

  /* Otherwise, just run the bottom half */
  return COMMAND_END;
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

	  spk_enum->cb(device->id, device->name, device->relvol, device->volume, flags, spk_enum->arg);

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
    return COMMAND_PENDING; /* async */

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
    return COMMAND_PENDING; /* async */

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
    return COMMAND_PENDING; /* async */

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
    return COMMAND_PENDING; /* async */

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
	/* FALLTHROUGH*/
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


/* Player API executed in the httpd (DACP) thread */
int
player_get_status(struct player_status *status)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.status = status;

  ret = commands_exec_sync(cmdbase, get_status, NULL, &cmdarg);
  return ret;
}

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

char *
player_get_icy_artwork_url(uint32_t id)
{
  union player_arg cmdarg;
  int ret;

  cmdarg.icy.id = id;

  if (pthread_self() != tid_player)
    ret = commands_exec_sync(cmdbase, artwork_url_get, NULL, &cmdarg);
  else
    artwork_url_get(&cmdarg, &ret);

  if (ret < 0)
    return NULL;
  else
    return cmdarg.icy.artwork_url;
}

/*
 * Starts/resumes playback
 *
 * Depending on the player state, this will either resume playing the current item (player is paused)
 * or begin playing the queue from the beginning.
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item dbmfi-id.
 *
 * @param *id if not NULL, will be set to the playing item dbmfi-id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start()
{
  int ret;

  ret = commands_exec_sync(cmdbase, playback_start, playback_start_bh, NULL);
  return ret;
}

/*
 * Starts playback with the media item at the given index of the play-queue.
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item id.
 *
 * @param index the index of the item in the play-queue
 * @param *id if not NULL, will be set to the playing item id
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

  cmdarg.intval = vol;

  ret = commands_exec_sync(cmdbase, volume_set, NULL, &cmdarg);
  return ret;
}

int
player_volume_setrel_speaker(uint64_t id, int relvol)
{
  union player_arg cmdarg;
  int ret;

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

/* Non-blocking commands used by mDNS */
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

/* Thread: worker */
static void
player_metadata_send(struct player_metadata *pmd)
{
  union player_arg cmdarg;

  cmdarg.pmd = pmd;

  commands_exec_sync(cmdbase, metadata_send, NULL, &cmdarg);
}

/* Thread: player */
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

  /* Save selected devices */
  db_speaker_clear_all();

  for (device = dev_list; device; device = device->next)
    {
      ret = db_speaker_save(device->id, device->selected, device->volume, device->name);
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Could not save state for %s device '%s'\n", device->type_name, device->name);
    }

  db_perthread_deinit();

  pthread_exit(NULL);
}


/* Thread: main */
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

  /*
   * Determine if the resolution of the system timer is > or < the size
   * of an audio packet. NOTE: this assumes the system clock resolution
   * is less than one second.
   */
  if (clock_getres(CLOCK_MONOTONIC, &timer_res) < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get the system timer resolution.\n");

      return -1;
    }

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  /* FreeBSD will report a resolution of 1, but actually has a resolution
   * larger than an audio packet
   */
  if (timer_res.tv_nsec == 1)
    timer_res.tv_nsec = 2 * AIRTUNES_V2_STREAM_PERIOD;
#endif

  // Set the tick interval for the playback timer
  interval = MAX(timer_res.tv_nsec, AIRTUNES_V2_STREAM_PERIOD);
  tick_interval.tv_nsec = interval;

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

  audio_buf = evbuffer_new();
  if (!audio_buf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate evbuffer for audio buffer\n");

      goto audio_fail;
    }

  evbase_player = event_base_new();
  if (!evbase_player)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create an event base\n");

      goto evbase_fail;
    }

#ifdef HAVE_TIMERFD
  pb_timer_ev = event_new(evbase_player, pb_timer_fd, EV_READ | EV_PERSIST, player_playback_cb, NULL);
#else
  pb_timer_ev = event_new(evbase_player, SIGALRM, EV_SIGNAL | EV_PERSIST, player_playback_cb, NULL);
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
  evbuffer_free(audio_buf);
 audio_fail:
#ifdef HAVE_TIMERFD
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif

  return -1;
}

/* Thread: main */
void
player_deinit(void)
{
  int ret;

  player_exit = 1;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_player, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not join player thread: %s\n", strerror(errno));

      return;
    }

  free(history);

  pb_timer_stop();
#ifdef HAVE_TIMERFD
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif

  evbuffer_free(audio_buf);

  input_deinit();
  outputs_deinit();

  event_base_free(evbase_player);
}
