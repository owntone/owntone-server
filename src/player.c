/*
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

#if defined(__linux__)
# include <sys/timerfd.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <signal.h>
#endif

#include <event2/event.h>
#include <event2/buffer.h>

#include <gcrypt.h>

#include "db.h"
#include "logger.h"
#include "mdns.h"
#include "conffile.h"
#include "misc.h"
#include "player.h"
#include "raop.h"
#include "laudio.h"
#include "worker.h"
#include "listener.h"

#ifdef LASTFM
# include "lastfm.h"
#endif

/* These handle getting the media data */
#include "transcode.h"
#include "pipe.h"
#include "http.h"
#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

#ifndef MIN
# define MIN(a, b) ((a < b) ? a : b)
#endif

#ifndef MAX
#define MAX(a, b) ((a > b) ? a : b)
#endif

// Default volume (must be from 0 - 100)
#define PLAYER_DEFAULT_VOLUME 50

struct player_source
{
  /* Id of the file/item in the files database */
  uint32_t id;

  /* Item-Id of the file/item in the queue */
  uint32_t item_id;

  /* Length of the file/item in milliseconds */
  uint32_t len_ms;

  enum data_kind data_kind;
  enum media_kind media_kind;

  /* Start time of the media item as rtp-time
     The stream-start is the rtp-time the media item did or would have
     started playing (after seek or pause), therefor the elapsed time of the
     media item is always:
     elapsed time = current rtptime - stream-start */
  uint64_t stream_start;

  /* Output start time of the media item as rtp-time
     The output start time is the rtp-time of the first audio packet send
     to the audio outputs.
     It differs from stream-start especially after a seek, where the first audio
     packet has the next rtp-time as output start and stream start becomes the
     rtp-time the media item would have been started playing if the seek did
     not happen. */
  uint64_t output_start;

  /* End time of media item as rtp-time
     The end time is set if the reading (source_read) of the media item reached
     end of file, until then it is 0. */
  uint64_t end;

  struct transcode_ctx *xcode;
  int setup_done;

  struct player_source *play_next;
};

enum player_sync_source
  {
    PLAYER_SYNC_CLOCK,
    PLAYER_SYNC_LAUDIO,
  };

struct volume_param {
  int volume;
  uint64_t spk_id;
};

struct player_command;
typedef int (*cmd_func)(struct player_command *cmd);

struct spk_enum
{
  spk_enum_cb cb;
  void *arg;
};

struct playback_start_param
{
  uint32_t id;
  int pos;

  uint32_t *id_ptr;
};

struct playerqueue_get_param
{
  int pos;
  int count;

  struct queue *queue;
};

struct playerqueue_add_param
{
  struct queue_item *items;
  int pos;
};

struct playerqueue_move_param
{
  uint32_t item_id;
  int from_pos;
  int to_pos;
  int count;
};

struct playerqueue_remove_param
{
  int from_pos;
  int count;
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

  struct raop_metadata *rmd;
};

struct player_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;
  cmd_func func_bh;

  int nonblock;

  union {
    struct volume_param vol_param;
    void *noarg;
    struct spk_enum *spk_enum;
    struct raop_device *rd;
    struct player_status *status;
    struct player_source *ps;
    struct player_metadata *pmd;
    uint32_t *id_ptr;
    uint64_t *raop_ids;
    enum repeat_mode mode;
    uint32_t id;
    int intval;
    struct icy_artwork icy;
    struct playback_start_param playback_start_param;
    struct playerqueue_get_param queue_get_param;
    struct playerqueue_add_param queue_add_param;
    struct playerqueue_move_param queue_move_param;
    struct playerqueue_remove_param queue_remove_param;
  } arg;

  int ret;

  int raop_pending;
};

/* Keep in sync with enum raop_devtype */
static const char *raop_devtype[] =
  {
    "AirPort Express 1 - 802.11g",
    "AirPort Express 2 - 802.11n",
    "AirPort Express 3 - 802.11n",
    "AppleTV",
    "Other",
  };


struct event_base *evbase_player;

static int exit_pipe[2];
static int cmd_pipe[2];
static int player_exit;
static struct event *exitev;
static struct event *cmdev;
static pthread_t tid_player;

/* Player status */
static enum play_status player_state;
static enum repeat_mode repeat;
static char shuffle;

/* Playback timer */
#if defined(__linux__)
static int pb_timer_fd;
#else
timer_t pb_timer;
#endif
static struct event *pb_timer_ev;
static struct timespec pb_timer_last;
static struct timespec packet_timer_last;
static uint64_t MINIMUM_STREAM_PERIOD;
static struct timespec packet_time = { 0, AIRTUNES_V2_STREAM_PERIOD };
static struct timespec timer_res;

/* Sync source */
static enum player_sync_source pb_sync_source;

/* Sync values */
static struct timespec pb_pos_stamp;
static uint64_t pb_pos;

/* Stream position (packets) */
static uint64_t last_rtptime;

/* AirPlay devices */
static int dev_autoselect; //TODO [player] Is this still necessary?
static struct raop_device *dev_list;

/* Output status */
static enum laudio_state laudio_status;
static int laudio_selected;
static int laudio_volume;
static int laudio_relvol;
static int raop_sessions;
static int streaming_selected;

static player_streaming_cb streaming_write;

/* Commands */
static struct player_command *cur_cmd;

/* Last commanded volume */
static int master_volume;

/* Audio source */
static struct player_source *cur_playing;
static struct player_source *cur_streaming;
static uint32_t cur_plid;
static uint32_t cur_plversion;
static struct evbuffer *audio_buf;

/* Play queue */
static struct queue *queue;

/* Play history */
static struct player_history *history;

/* Command helpers */
static void
command_async_end(struct player_command *cmd)
{
  cur_cmd = NULL;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

  /* Process commands again */
  event_add(cmdev, NULL);
}

static void
command_init(struct player_command *cmd)
{
  memset(cmd, 0, sizeof(struct player_command));

  pthread_mutex_init(&cmd->lck, NULL);
  pthread_cond_init(&cmd->cond, NULL);
}

static void
command_deinit(struct player_command *cmd)
{
  pthread_cond_destroy(&cmd->cond);
  pthread_mutex_destroy(&cmd->lck);
}


static void
status_update(enum play_status status)
{
  player_state = status;

  listener_notify(LISTENER_PLAYER);

  if (status == PLAY_PLAYING)
    dev_autoselect = 0;
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
  struct raop_device *rd;

  master_volume = newvol;

  if (laudio_selected)
    laudio_relvol = vol_to_rel(laudio_volume);

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->selected)
	rd->relvol = vol_to_rel(rd->volume);
    }
}

static void
volume_master_find(void)
{
  struct raop_device *rd;
  int newmaster;

  newmaster = -1;

  if (laudio_selected)
    newmaster = laudio_volume;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->selected && (rd->volume > newmaster))
	newmaster = rd->volume;
    }

  volume_master_update(newmaster);
}


/* Device select/deselect hooks */
static void
speaker_select_laudio(void)
{
  laudio_selected = 1;

  if (laudio_volume > master_volume)
    {
      if (player_state == PLAY_STOPPED || master_volume == -1)
	volume_master_update(laudio_volume);
      else
	laudio_volume = master_volume;
    }

  laudio_relvol = vol_to_rel(laudio_volume);
}

static void
speaker_select_raop(struct raop_device *rd)
{
  rd->selected = 1;

  if (rd->volume > master_volume)
    {
      if (player_state == PLAY_STOPPED || master_volume == -1)
	volume_master_update(rd->volume);
      else
	rd->volume = master_volume;
    }

  rd->relvol = vol_to_rel(rd->volume);
}

static void
speaker_deselect_laudio(void)
{
  laudio_selected = 0;

  if (laudio_volume == master_volume)
    volume_master_find();
}

static void
speaker_deselect_raop(struct raop_device *rd)
{
  rd->selected = 0;

  if (rd->volume == master_volume)
    volume_master_find();
}


static int
player_get_current_pos_clock(uint64_t *pos, struct timespec *ts, int commit)
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
player_get_current_pos_laudio(uint64_t *pos, struct timespec *ts, int commit)
{
  int ret;

  *pos = laudio_get_pos();

  ret = clock_gettime_with_res(CLOCK_MONOTONIC, ts, &timer_res);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get clock: %s\n", strerror(errno));

      return -1;
    }

  if (commit)
    {
      pb_pos = *pos;

      pb_pos_stamp.tv_sec = ts->tv_sec;
      pb_pos_stamp.tv_nsec = ts->tv_nsec;

#ifdef DEBUG_SYNC
      DPRINTF(E_DBG, L_PLAYER, "Pos: %" PRIu64 " (laudio)\n", *pos);
#endif
    }

  return 0;
}

int
player_get_current_pos(uint64_t *pos, struct timespec *ts, int commit)
{
  switch (pb_sync_source)
    {
      case PLAYER_SYNC_CLOCK:
	return player_get_current_pos_clock(pos, ts, commit);

      case PLAYER_SYNC_LAUDIO:
	return player_get_current_pos_laudio(pos, ts, commit);
    }

  return -1;
}

static int
pb_timer_start(struct timespec *ts)
{
  struct itimerspec next;
  int ret;

  next.it_interval.tv_sec = 0;
  next.it_interval.tv_nsec = 0;
  next.it_value.tv_sec = ts->tv_sec;
  next.it_value.tv_nsec = ts->tv_nsec;

#if defined(__linux__)
  ret = timerfd_settime(pb_timer_fd, TFD_TIMER_ABSTIME, &next, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not arm playback timer: %s\n", strerror(errno));

      return -1;
    }

  ret = event_add(pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not add playback timer event\n");

      return -1;
    }
#else
  ret = timer_settime(pb_timer, TIMER_ABSTIME, &next, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not arm playback timer: %s\n", strerror(errno));

      return -1;
    }
#endif

  return 0;
}

static int
pb_timer_stop(void)
{
  struct itimerspec next;
  int ret;

  memset(&next, 0, sizeof(struct itimerspec));

#if defined(__linux__)
  ret = timerfd_settime(pb_timer_fd, TFD_TIMER_ABSTIME, &next, NULL);

  event_del(pb_timer_ev);
#else
  ret = timer_settime(pb_timer, TIMER_ABSTIME, &next, NULL);
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

static int
playerqueue_clear(struct player_command *cmd);

static void
player_metadata_send(struct player_metadata *pmd);

static void
player_laudio_status_cb(enum laudio_state status)
{
  struct timespec ts;
  uint64_t pos;

  switch (status)
    {
      /* Switch sync to clock sync */
      case LAUDIO_STOPPING:
	DPRINTF(E_DBG, L_PLAYER, "Local audio stopping\n");

	laudio_status = status;

	/* Synchronize pb_pos and pb_pos_stamp before laudio stops entirely */
	player_get_current_pos_laudio(&pos, &ts, 1);

	pb_sync_source = PLAYER_SYNC_CLOCK;
	break;

      /* Switch sync to laudio sync */
      case LAUDIO_RUNNING:
	DPRINTF(E_DBG, L_PLAYER, "Local audio running\n");

	laudio_status = status;

	pb_sync_source = PLAYER_SYNC_LAUDIO;
	break;

      case LAUDIO_FAILED:
	DPRINTF(E_DBG, L_PLAYER, "Local audio failed\n");

	pb_sync_source = PLAYER_SYNC_CLOCK;

	laudio_close();

	if (raop_sessions == 0)
	  playback_abort();

	speaker_deselect_laudio();
	break;

      default:
      	laudio_status = status;
	break;
    }
}

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
 * retrieval may take some time. raop_metadata_prepare() is thread safe. The
 * sending, however, must be done in the player thread.
 */
static void
metadata_prepare_cb(void *arg)
{
  struct player_metadata *pmd = arg;

  pmd->rmd = raop_metadata_prepare(pmd->id);

  if (pmd->rmd)
    player_metadata_send(pmd);
}

/* Callback from the worker thread (async operation as it may block) */
static void
update_icy_cb(void *arg)
{
  struct http_icy_metadata *metadata = arg;

  db_file_update_icy(metadata->id, metadata->artist, metadata->title);

  http_icy_metadata_free(metadata, 1);
}

/* Metadata */
static void
metadata_prune(uint64_t pos)
{
  raop_metadata_prune(pos);
}

static void
metadata_purge(void)
{
  raop_metadata_purge();
}

static void
metadata_trigger(int startup)
{
  struct player_metadata pmd;

  memset(&pmd, 0, sizeof(struct player_metadata));

  pmd.id = cur_streaming->id;
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

  metadata->id = cur_streaming->id;

  /* Defer the database update to the worker thread */
  worker_execute(update_icy_cb, metadata, sizeof(struct http_icy_metadata), 0);

  /* Triggers preparing and sending RAOP metadata */
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
 * Initializes the given player source for playback
 */
static int
stream_setup(struct player_source *ps, struct media_file_info *mfi)
{
  char *url;
  int ret;

  if (!ps || !mfi)
    {
      DPRINTF(E_LOG, L_PLAYER, "No player source and/or media info given to stream_setup\n");
      return -1;
    }

  if (ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source already setup (id = %d)\n", ps->id);
      return -1;
    }

  // Setup depending on data kind
  switch (ps->data_kind)
    {
      case DATA_KIND_FILE:
	ps->xcode = transcode_setup(mfi, XCODE_PCM16_NOHEADER, NULL);
	ret = ps->xcode ? 0 : -1;
	break;

      case DATA_KIND_HTTP:
	ret = http_stream_setup(&url, mfi->path);
	if (ret < 0)
	  break;

	free(mfi->path);
	mfi->path = url;

	ps->xcode = transcode_setup(mfi, XCODE_PCM16_NOHEADER, NULL);
	ret = ps->xcode ? 0 : -1;
	break;

      case DATA_KIND_SPOTIFY:
#ifdef HAVE_SPOTIFY_H
	ret = spotify_playback_setup(mfi);
#else
	DPRINTF(E_LOG, L_PLAYER, "Player source has data kind 'spotify' (%d), but forked-daapd is compiled without spotify support - cannot setup source '%s' (%s)\n",
		    ps->data_kind, mfi->title, mfi->path);
	ret = -1;
#endif
	break;

      case DATA_KIND_PIPE:
	ret = pipe_setup(mfi);
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Unknown data kind (%d) for player source - cannot setup source '%s' (%s)\n",
	    ps->data_kind, mfi->title, mfi->path);
	ret = -1;
    }

  if (ret == 0)
      ps->setup_done = 1;
  else
      DPRINTF(E_LOG, L_PLAYER, "Failed to setup player source (id = %d)\n", ps->id);

  return ret;
}

/*
 * Starts or resumes plaback for the given player source
 */
static int
stream_play(struct player_source *ps)
{
  int ret;

  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream play called with no active streaming player source\n");
      return -1;
    }

  if (!ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup, play not possible\n");
      return -1;
    }

  // Start/resume playback depending on data kind
  switch (ps->data_kind)
    {
      case DATA_KIND_HTTP:
      case DATA_KIND_FILE:
	ret = 0;
	break;

#ifdef HAVE_SPOTIFY_H
      case DATA_KIND_SPOTIFY:
	ret = spotify_playback_play();
	break;
#endif

      case DATA_KIND_PIPE:
	ret = 0;
	break;

      default:
	ret = -1;
    }

  return ret;
}

/*
 * Read up to "len" data from the given player source and returns
 * the actual amount of data read.
 */
static int
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
}

/*
 * Pauses playback of the given player source
 */
static int
stream_pause(struct player_source *ps)
{
  int ret;

  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream pause called with no active streaming player source\n");
      return -1;
    }

  if (!ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup, pause not possible\n");
      return -1;
    }

  // Pause playback depending on data kind
  switch (ps->data_kind)
    {
      case DATA_KIND_HTTP:
	ret = 0;
	break;

      case DATA_KIND_FILE:
	ret = 0;
	break;

#ifdef HAVE_SPOTIFY_H
      case DATA_KIND_SPOTIFY:
	ret = spotify_playback_pause();
	break;
#endif

      case DATA_KIND_PIPE:
	ret = 0;
	break;

      default:
	ret = -1;
    }

  return ret;
}

/*
 * Seeks to the given position in milliseconds of the given player source
 */
static int
stream_seek(struct player_source *ps, int seek_ms)
{
  int ret;

  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream seek called with no active streaming player source\n");
      return -1;
    }

  if (!ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup, seek not possible\n");
      return -1;
    }

  // Seek depending on data kind
  switch (ps->data_kind)
    {
      case DATA_KIND_HTTP:
	ret = 0;
	break;

      case DATA_KIND_FILE:
	ret = transcode_seek(ps->xcode, seek_ms);
	break;

#ifdef HAVE_SPOTIFY_H
      case DATA_KIND_SPOTIFY:
	ret = spotify_playback_seek(seek_ms);
	break;
#endif

      case DATA_KIND_PIPE:
	ret = 0;
	break;

      default:
	ret = -1;
    }

  return ret;
}

/*
 * Stops playback and cleanup for the given player source
 */
static int
stream_stop(struct player_source *ps)
{
  if (!ps)
    {
      DPRINTF(E_LOG, L_PLAYER, "Stream cleanup called with no active streaming player source\n");
      return -1;
    }

  if (!ps->setup_done)
    {
      DPRINTF(E_LOG, L_PLAYER, "Given player source not setup, cleanup not possible\n");
      return -1;
    }

  switch (ps->data_kind)
    {
      case DATA_KIND_FILE:
      case DATA_KIND_HTTP:
	if (ps->xcode)
	  {
	    transcode_cleanup(ps->xcode);
	    ps->xcode = NULL;
	  }
	break;

      case DATA_KIND_SPOTIFY:
#ifdef HAVE_SPOTIFY_H
	spotify_playback_stop();
#endif
	break;

      case DATA_KIND_PIPE:
	pipe_cleanup();
	break;
    }

  ps->setup_done = 0;

  return 0;
}


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
source_new(struct queue_item *item)
{
  struct player_source *ps;

  ps = (struct player_source *)calloc(1, sizeof(struct player_source));

  ps->id = queueitem_id(item);
  ps->item_id = queueitem_item_id(item);
  ps->data_kind = queueitem_data_kind(item);
  ps->media_kind = queueitem_media_kind(item);
  ps->len_ms = queueitem_len(item);
  ps->play_next = NULL;

  return ps;
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
    stream_stop(cur_streaming);

  ps_playing = source_now_playing();

  while (ps_playing)
    {
      ps_temp = ps_playing;
      ps_playing = ps_playing->play_next;

      ps_temp->play_next = NULL;
      free(ps_temp);
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
  struct media_file_info *mfi;
  uint64_t seek_frames;
  int seek_ms;
  int ret;

  ps_playing = source_now_playing();

  if (cur_streaming)
    {
      if (ps_playing != cur_streaming)
	{
	  DPRINTF(E_DBG, L_PLAYER,
	      "Pause called on playing source (id=%d) and streaming source already "
	      "switched to the next item (id=%d)\n", ps_playing->id, cur_streaming->id);
	  ret = stream_stop(cur_streaming);
	  if (ret < 0)
	    return -1;
	}
      else
	{
	  ret = stream_pause(cur_streaming);
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
      free(ps_temp);
    }
  ps_playing->play_next = NULL;

  cur_playing = NULL;
  cur_streaming = ps_playing;

  if (!cur_streaming->setup_done)
    {
      mfi = db_file_fetch_byid(cur_streaming->id);
      if (!mfi)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Couldn't fetch file id %d\n", cur_streaming->id);

	  return -1;
	}

      if (mfi->disabled)
	{
	  DPRINTF(E_DBG, L_PLAYER, "File id %d is disabled, skipping\n", cur_streaming->id);

	  free_mfi(mfi, 0);
	  return -1;
	}

      DPRINTF(E_INFO, L_PLAYER, "Opening '%s' (%s)\n", mfi->title, mfi->path);

      ret = stream_setup(cur_streaming, mfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Failed to open '%s' (%s)\n", mfi->title, mfi->path);
	  free_mfi(mfi, 0);
	  return -1;
	}

      free_mfi(mfi, 0);
    }

  /* Seek back to the pause position */
  seek_frames = (pos - cur_streaming->stream_start);
  seek_ms = (int)((seek_frames * 1000) / 44100);
  ret = stream_seek(cur_streaming, seek_ms);

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

  ret = stream_seek(cur_streaming, seek_ms);
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

  ret = stream_play(cur_streaming);

  return ret;
}

/*
 * Initializes playback of the given queue item (but does not start playback)
 *
 * A new source is created for the given queue item and is set as the current
 * streaming source. If a streaming source already existed (and reached eof)
 * the new source is appended as the play-next item to it.
 *
 * Stream-start and output-start values are set to the given start position.
 */
static int
source_open(struct queue_item *qii, uint64_t start_pos, int seek)
{
  struct player_source *ps;
  struct media_file_info *mfi;
  uint32_t id;
  int ret;

  if (cur_streaming && cur_streaming->end == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Current streaming source not at eof %d\n", cur_streaming->id);
      return -1;
    }

  id = queueitem_id(qii);
  mfi = db_file_fetch_byid(id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't fetch file id %d\n", id);

      return -1;
    }

  if (mfi->disabled)
    {
      DPRINTF(E_DBG, L_PLAYER, "File id %d is disabled, skipping\n", id);

      free_mfi(mfi, 0);
      return -1;
    }

  DPRINTF(E_INFO, L_PLAYER, "Opening '%s' (%s)\n", mfi->title, mfi->path);

  ps = source_new(qii);

  ret = stream_setup(ps, mfi);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Failed to open '%s' (%s)\n", mfi->title, mfi->path);
      free_mfi(mfi, 0);
      return -1;
    }

  /* If a streaming source exists, append the new source as play-next and set it
     as the new streaming source */
  if (cur_streaming)
    {
      cur_streaming->play_next = ps;
    }

  cur_streaming = ps;

  cur_streaming->stream_start = start_pos;
  cur_streaming->output_start = cur_streaming->stream_start;
  cur_streaming->end = 0;

  /* Seek to the saved seek position */
  if (seek && mfi->seek)
    source_seek(mfi->seek);

  free_mfi(mfi, 0);
  return ret;
}

/*
 * Closes the current streaming source and sets its end-time to the given
 * position
 */
static int
source_close(uint64_t end_pos)
{
  stream_stop(cur_streaming);

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

      /* Stop playback */
      if (!cur_playing->play_next)
	{
	  playback_abort();

	  return pos;
        }

      ps = cur_playing;
      cur_playing = cur_playing->play_next;

      free(ps);
    }

  if (i > 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Playback switched to next song\n");

      status_update(PLAY_PLAYING);

      metadata_prune(pos);
    }

  return pos;
}

static int
source_read(uint8_t *buf, int len, uint64_t rtptime)
{
  int ret;
  int nbytes;
  char *silence_buf;
  struct queue_item *item;

  if (!cur_streaming)
    return 0;

  nbytes = 0;
  while (nbytes < len)
    {
      if (evbuffer_get_length(audio_buf) == 0)
	{
	  if (cur_streaming)
	    {
	      ret = stream_read(cur_streaming, len - nbytes);
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

	  if (ret <= 0)
	    {
	      /* EOF or error */
	      source_close(rtptime + BTOS(nbytes) - 1);

	      DPRINTF(E_DBG, L_PLAYER, "New file\n");

	      item = queue_next(queue, cur_streaming->item_id, shuffle, repeat, 1);

	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Error reading source %d\n", cur_streaming->id);
		  queue_remove_byitemid(queue, cur_streaming->item_id);
		}

	      if (item)
		{
		  ret = source_open(item, cur_streaming->end + 1, 0);
		  if (ret < 0)
		    return -1;

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
playback_write(void)
{
  uint8_t rawbuf[STOB(AIRTUNES_V2_PACKET_SAMPLES)];
  int ret;

  source_check();

  /* Make sure playback is still running after source_check() */
  if (player_state == PLAY_STOPPED)
    return;

  last_rtptime += AIRTUNES_V2_PACKET_SAMPLES;

  memset(rawbuf, 0, sizeof(rawbuf));

  ret = source_read(rawbuf, sizeof(rawbuf), last_rtptime);
  if (ret < 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Error reading from source, aborting playback\n");

      playback_abort();
      return;
    }

  if (streaming_selected)
    streaming_write(rawbuf, sizeof(rawbuf));

  if (laudio_status & LAUDIO_F_STARTED)
    laudio_write(rawbuf, last_rtptime);

  if (raop_sessions > 0)
    raop_v2_write(rawbuf, last_rtptime);
}

static void
player_playback_cb(int fd, short what, void *arg)
{
  uint32_t packet_send_count = 0;
  struct timespec next_tick;
  struct timespec stream_period  = { 0, MINIMUM_STREAM_PERIOD };
  int ret;
#if defined(__linux__)
  uint64_t ticks;

  /* Acknowledge timer */
  ret = read(fd, &ticks, sizeof(ticks));
  if (ret <= 0)
    DPRINTF(E_WARN, L_PLAYER, "Error reading timer.\n");
#endif /* __linux__ */

  /* Decide how many packets to send */
  next_tick = timespec_add(pb_timer_last, stream_period);
  do
    {
      playback_write();
      packet_timer_last = timespec_add(packet_timer_last, packet_time);
      packet_send_count++;
      /* not possible to have more than 126 audio packets per second */
      if (packet_send_count > 126)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Timing error detected during playback! Aborting.\n");

	  playback_abort();
	  return;
	}
    }
  while (timespec_cmp(packet_timer_last, next_tick) < 0);

  /* Make sure playback is still running */
  if (player_state == PLAY_STOPPED)
    return;

  pb_timer_last = timespec_add(pb_timer_last, stream_period);

  ret = pb_timer_start(&pb_timer_last);
  if (ret < 0)
    playback_abort();
}

static void
device_free(struct raop_device *dev)
{
  free(dev->name);

  if (dev->v4_address)
    free(dev->v4_address);

  if (dev->v6_address)
    free(dev->v6_address);

  free(dev);
}

/* Helpers */
static void
device_remove(struct raop_device *dev)
{
  struct raop_device *rd;
  struct raop_device *prev;
  int ret;

  prev = NULL;
  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd == dev)
	break;

      prev = rd;
    }

  if (!rd)
    return;

  DPRINTF(E_DBG, L_PLAYER, "Removing AirPlay device %s; stopped advertising\n", dev->name);

  /* Make sure device isn't selected anymore */
  if (dev->selected)
    speaker_deselect_raop(dev);

  /* Save device volume */
  ret = db_speaker_save(dev->id, 0, dev->volume);
  if (ret < 0)
    DPRINTF(E_LOG, L_PLAYER, "Could not save state for speaker %s\n", dev->name);

  if (!prev)
    dev_list = dev->next;
  else
    prev->next = dev->next;

  device_free(dev);
}

static int
device_check(struct raop_device *dev)
{
  struct raop_device *rd;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd == dev)
	break;
    }

  return (rd) ? 0 : -1;
}

static int
device_add(struct player_command *cmd)
{
  struct raop_device *dev;
  struct raop_device *rd;
  int selected;
  int ret;

  dev = cmd->arg.rd;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->id == dev->id)
	break;
    }

  /* New device */
  if (!rd)
    {
      rd = dev;

      ret = db_speaker_get(rd->id, &selected, &rd->volume);
      if (ret < 0)
	{
	  selected = 0;
	  rd->volume = (master_volume >= 0) ? master_volume : PLAYER_DEFAULT_VOLUME;
	}

      if (dev_autoselect && selected)
	speaker_select_raop(rd);

      rd->next = dev_list;
      dev_list = rd;
    }
  else
    {
      rd->advertised = 1;

      if (dev->v4_address)
	{
	  if (rd->v4_address)
	    free(rd->v4_address);

	  rd->v4_address = dev->v4_address;
	  rd->v4_port = dev->v4_port;

	  /* Address is ours now */
	  dev->v4_address = NULL;
	}

      if (dev->v6_address)
	{
	  if (rd->v6_address)
	    free(rd->v6_address);

	  rd->v6_address = dev->v6_address;
	  rd->v6_port = dev->v6_port;

	  /* Address is ours now */
	  dev->v6_address = NULL;
	}

      if (rd->name)
	free(rd->name);
      rd->name = dev->name;
      dev->name = NULL;

      rd->devtype = dev->devtype;

      rd->has_password = dev->has_password;
      rd->password = dev->password;

      device_free(dev);
    }

  return 0;
}

static int
device_remove_family(struct player_command *cmd)
{
  struct raop_device *dev;
  struct raop_device *rd;

  dev = cmd->arg.rd;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->id == dev->id)
        break;
    }

  if (!rd)
    {
      DPRINTF(E_WARN, L_PLAYER, "AirPlay device %s stopped advertising, but not in our list\n", dev->name);

      device_free(dev);
      return 0;
    }

  /* v{4,6}_port non-zero indicates the address family stopped advertising */
  if (dev->v4_port && rd->v4_address)
    {
      free(rd->v4_address);
      rd->v4_address = NULL;
      rd->v4_port = 0;
    }

  if (dev->v6_port && rd->v6_address)
    {
      free(rd->v6_address);
      rd->v6_address = NULL;
      rd->v6_port = 0;
    }

  if (!rd->v4_address && !rd->v6_address)
    {
      rd->advertised = 0;

      if (!rd->session)
	device_remove(rd);
    }

  device_free(dev);

  return 0;
}

static int
metadata_send(struct player_command *cmd)
{
  struct player_metadata *pmd;

  pmd = cmd->arg.pmd;

  /* Do the setting of rtptime which was deferred in metadata_trigger because we
   * wanted to wait until we had the actual last_rtptime
   */
  if ((pmd->rtptime == 0) && (pmd->startup))
    pmd->rtptime = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;

  raop_metadata_send(pmd->rmd, pmd->rtptime, pmd->offset, pmd->startup);

  return 0;
}

/* RAOP callbacks executed in the player thread */
static void
device_streaming_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  int ret;

  if (status == RAOP_FAILED)
    {
      raop_sessions--;

      ret = device_check(dev);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared during streaming!\n");

	  return;
	}

      DPRINTF(E_LOG, L_PLAYER, "AirPlay device %s FAILED\n", dev->name);

      if (player_state == PLAY_PLAYING)
	speaker_deselect_raop(dev);

      dev->session = NULL;

      if (!dev->advertised)
	device_remove(dev);
    }
  else if (status == RAOP_STOPPED)
    {
      raop_sessions--;

      ret = device_check(dev);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared during streaming!\n");

	  return;
	}

      DPRINTF(E_INFO, L_PLAYER, "AirPlay device %s stopped\n", dev->name);

      dev->session = NULL;

      if (!dev->advertised)
	device_remove(dev);
    }
}

static void
device_command_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  cur_cmd->raop_pending--;

  raop_set_status_cb(rs, device_streaming_cb);

  if (status == RAOP_FAILED)
    device_streaming_cb(dev, rs, status);

  if (cur_cmd->raop_pending == 0)
    {
      if (cur_cmd->func_bh)
	cur_cmd->ret = cur_cmd->func_bh(cur_cmd);
      else
	cur_cmd->ret = 0;

      command_async_end(cur_cmd);
    }
}

static void
device_shutdown_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  int ret;

  cur_cmd->raop_pending--;

  if (raop_sessions)
    raop_sessions--;

  ret = device_check(dev);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared before shutdown completion!\n");

      if (cur_cmd->ret != -2)
	cur_cmd->ret = -1;
      goto out;
    }

  dev->session = NULL;

  if (!dev->advertised)
    device_remove(dev);

 out:
  if (cur_cmd->raop_pending == 0)
    {
      /* cur_cmd->ret already set
       *  - to 0 (or -2 if password issue) in speaker_set()
       *  - to -1 above on error
       */
      command_async_end(cur_cmd);
    }
}

static void
device_lost_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  /* We lost that device during startup for some reason, not much we can do here */
  if (status == RAOP_FAILED)
    DPRINTF(E_WARN, L_PLAYER, "Failed to stop lost device\n");
  else
    DPRINTF(E_INFO, L_PLAYER, "Lost device stopped properly\n");
}

static void
device_activate_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  struct timespec ts;
  int ret;

  cur_cmd->raop_pending--;

  ret = device_check(dev);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared during startup!\n");

      raop_set_status_cb(rs, device_lost_cb);
      raop_device_stop(rs);

      if (cur_cmd->ret != -2)
	cur_cmd->ret = -1;
      goto out;
    }

  if (status == RAOP_PASSWORD)
    {
      status = RAOP_FAILED;
      cur_cmd->ret = -2;
    }

  if (status == RAOP_FAILED)
    {
      speaker_deselect_raop(dev);

      if (!dev->advertised)
	device_remove(dev);

      if (cur_cmd->ret != -2)
	cur_cmd->ret = -1;
      goto out;
    }

  dev->session = rs;

  raop_sessions++;

  if ((player_state == PLAY_PLAYING) && (raop_sessions == 1))
    {
      ret = clock_gettime_with_res(CLOCK_MONOTONIC, &ts, &timer_res);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not get current time: %s\n", strerror(errno));

	  /* Fallback to nearest timer expiration time */
	  ts.tv_sec = pb_timer_last.tv_sec;
	  ts.tv_nsec = pb_timer_last.tv_nsec;
	}

      raop_playback_start(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, &ts);
    }

  raop_set_status_cb(rs, device_streaming_cb);

 out:
  if (cur_cmd->raop_pending == 0)
    {
      /* cur_cmd->ret already set
       *  - to 0 in speaker_set() (default)
       *  - to -2 above if password issue
       *  - to -1 above on error
       */
      command_async_end(cur_cmd);
    }
}

static void
device_probe_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  int ret;

  cur_cmd->raop_pending--;

  ret = device_check(dev);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared during probe!\n");

      if (cur_cmd->ret != -2)
	cur_cmd->ret = -1;
      goto out;
    }

  if (status == RAOP_PASSWORD)
    {
      status = RAOP_FAILED;
      cur_cmd->ret = -2;
    }

  if (status == RAOP_FAILED)
    {
      speaker_deselect_raop(dev);

      if (!dev->advertised)
	device_remove(dev);

      if (cur_cmd->ret != -2)
	cur_cmd->ret = -1;
      goto out;
    }

 out:
  if (cur_cmd->raop_pending == 0)
    {
      /* cur_cmd->ret already set
       *  - to 0 in speaker_set() (default)
       *  - to -2 above if password issue
       *  - to -1 above on error
       */
      command_async_end(cur_cmd);
    }
}

static void
device_restart_cb(struct raop_device *dev, struct raop_session *rs, enum raop_session_state status)
{
  int ret;

  cur_cmd->raop_pending--;

  ret = device_check(dev);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "AirPlay device disappeared during restart!\n");

      raop_set_status_cb(rs, device_lost_cb);
      raop_device_stop(rs);

      goto out;
    }

  if (status == RAOP_FAILED)
    {
      speaker_deselect_raop(dev);

      if (!dev->advertised)
	device_remove(dev);

      goto out;
    }

  dev->session = rs;

  raop_sessions++;
  raop_set_status_cb(rs, device_streaming_cb);

 out:
  if (cur_cmd->raop_pending == 0)
    {
      cur_cmd->ret = cur_cmd->func_bh(cur_cmd);

      command_async_end(cur_cmd);
    }
}


/* Internal abort routine */
static void
playback_abort(void)
{
  if (laudio_status != LAUDIO_CLOSED)
    laudio_close();

  if (raop_sessions > 0)
    raop_playback_stop();

  pb_timer_stop();

  source_stop();

  playerqueue_clear(NULL);

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  status_update(PLAY_STOPPED);

  metadata_purge();
}

/* Actual commands, executed in the player thread */
static int
get_status(struct player_command *cmd)
{
  struct timespec ts;
  struct player_source *ps;
  struct player_status *status;
  struct queue_item *item_next;
  uint64_t pos;
  int ret;

  status = cmd->arg.status;

  memset(status, 0, sizeof(struct player_status));

  status->shuffle = shuffle;
  status->repeat = repeat;

  status->volume = master_volume;

  status->plid = cur_plid;
  status->plversion = cur_plversion;

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

	status->pos_pl = queue_index_byitemid(queue, cur_streaming->item_id, 0);

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
	status->pos_pl = queue_index_byitemid(queue, ps->item_id, 0);

	item_next = queue_next(queue, ps->item_id, shuffle, repeat, 0);
	if (item_next)
	  {
	    status->next_id = queueitem_id(item_next);
	    status->next_item_id = queueitem_item_id(item_next);
	    status->next_pos_pl = queue_index_byitemid(queue, status->next_item_id, 0);
	  }
	else
	  {
	    //TODO [queue/mpd] Check how mpd sets the next-id/-pos if the last song is playing
	    status->next_id = 0;
	    status->next_pos_pl = 0;
	  }

	status->playlistlength = queue_count(queue);
	break;
    }

  return 0;
}

static int
now_playing(struct player_command *cmd)
{
  uint32_t *id;
  struct player_source *ps_playing;

  id = cmd->arg.id_ptr;

  ps_playing = source_now_playing();

  if (ps_playing)
    *id = ps_playing->id;
  else
    return -1;

  return 0;
}

static int
artwork_url_get(struct player_command *cmd)
{
  struct player_source *ps;

  cmd->arg.icy.artwork_url = NULL;

  if (cur_playing)
    ps = cur_playing;
  else if (cur_streaming)
    ps = cur_streaming;
  else
    return -1;

  /* Check that we are playing a viable stream, and that it has the requested id */
  if (!ps->xcode || ps->data_kind != DATA_KIND_HTTP || ps->id != cmd->arg.icy.id)
    return -1;

  cmd->arg.icy.artwork_url = transcode_metadata_artwork_url(ps->xcode);

  return 0;
}

static int
playback_stop(struct player_command *cmd)
{
  struct player_source *ps_playing;

  if (laudio_status != LAUDIO_CLOSED)
    laudio_close();

  /* We may be restarting very soon, so we don't bring the devices to a
   * full stop just yet; this saves time when restarting, which is nicer
   * for the user.
   */
  cmd->raop_pending = raop_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

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

  /* We're async if we need to flush RAOP devices */
  if (cmd->raop_pending > 0)
    return 1; /* async */

  return 0;
}

/* Playback startup bottom half */
static int
playback_start_bh(struct player_command *cmd)
{
  int ret;

  if ((laudio_status == LAUDIO_CLOSED) && (raop_sessions == 0))
    {
      DPRINTF(E_LOG, L_PLAYER, "Cannot start playback: no output started\n");

      goto out_fail;
    }

  /* Start laudio first as it can fail, but can be stopped easily if needed */
  if (laudio_status == LAUDIO_OPEN)
    {
      laudio_set_volume(laudio_volume);

      ret = laudio_start(pb_pos, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Local audio failed to start\n");

	  goto out_fail;
	}
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

  ret = pb_timer_start(&pb_timer_last);
  if (ret < 0)
    goto out_fail;

  /* Everything OK, start RAOP */
  if (raop_sessions > 0)
    raop_playback_start(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, &pb_pos_stamp);

  status_update(PLAY_PLAYING);

  return 0;

 out_fail:
  playback_abort();

  return -1;
}

static int
playback_start_item(struct player_command *cmd, struct queue_item *qii)
{
  uint32_t *dbmfi_id;
  struct raop_device *rd;
  struct player_source *ps_playing;
  struct queue_item *item;
  int ret;

  dbmfi_id = cmd->arg.playback_start_param.id_ptr;

  ps_playing = source_now_playing();

  if (player_state == PLAY_PLAYING)
    {
      /*
       * If player is already playing a song, only return current playing song id
       * and do not change player state (ignores given arguments for playing a
       * specified song by pos or id).
       */
      if (dbmfi_id && ps_playing)
	{
	  *dbmfi_id = ps_playing->id;
	}

      status_update(player_state);

      return 0;
    }

  // Update global playback position
  pb_pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - 88200;

  item = NULL;
  if (qii)
    {
      item = qii;
    }
  else if (!cur_streaming)
    {
      if (shuffle)
      	queue_shuffle(queue, 0);
      item = queue_next(queue, 0, shuffle, repeat, 0);
    }

  if (item)
    {
      source_stop();
      ret = source_open(item, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, 1);
      if (ret < 0)
	{
	  playback_abort();
	  return -1;
	}
    }

  ret = source_play();
  if (ret < 0)
    {
      playback_abort();
      return -1;
    }


  if (dbmfi_id)
    *dbmfi_id = cur_streaming->id;

  metadata_trigger(1);


  /* Start local audio if needed */
  if (laudio_selected && (laudio_status == LAUDIO_CLOSED))
    {
      ret = laudio_open();
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Could not open local audio, will try AirPlay\n");
    }

  /* Start RAOP sessions on selected devices if needed */
  cmd->raop_pending = 0;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->selected && !rd->session)
	{
	  ret = raop_device_start(rd, device_restart_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not start selected AirPlay device %s\n", rd->name);
	      continue;
	    }

	  cmd->raop_pending++;
	}
    }

  /* Try to autoselect a non-selected RAOP device if the above failed */
  if ((laudio_status == LAUDIO_CLOSED) && (cmd->raop_pending == 0) && (raop_sessions == 0))
    for (rd = dev_list; rd; rd = rd->next)
      {
        if (!rd->session)
	  {
	    speaker_select_raop(rd);
	    ret = raop_device_start(rd, device_restart_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
	    if (ret < 0)
	      {
		DPRINTF(E_DBG, L_PLAYER, "Could not autoselect AirPlay device %s\n", rd->name);
		speaker_deselect_raop(rd);
		continue;
	      }

	    DPRINTF(E_INFO, L_PLAYER, "Autoselecting AirPlay device %s\n", rd->name);
	    cmd->raop_pending++;
	    break;
	  }
      }

  /* No luck finding valid output */
  if ((laudio_status == LAUDIO_CLOSED) && (cmd->raop_pending == 0) && (raop_sessions == 0))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not start playback: no output selected or couldn't start any output\n");

      playback_abort();
      return -1;
    }

  /* We're async if we need to start RAOP devices */
  if (cmd->raop_pending > 0)
    return 1; /* async */

  /* Otherwise, just run the bottom half */
  return playback_start_bh(cmd);
}

static int
playback_start(struct player_command *cmd)
{
  return playback_start_item(cmd, NULL);
}

static int
playback_start_byitemid(struct player_command *cmd)
{
  int item_id;
  struct queue_item *qii;

  item_id = cmd->arg.playback_start_param.id;

  qii = queue_get_byitemid(queue, item_id);

  return playback_start_item(cmd, qii);
}

static int
playback_start_byindex(struct player_command *cmd)
{
  int pos;
  struct queue_item *qii;

  pos = cmd->arg.playback_start_param.pos;

  qii = queue_get_byindex(queue, pos, 0);

  return playback_start_item(cmd, qii);
}

static int
playback_start_bypos(struct player_command *cmd)
{
  int offset;
  struct player_source *ps_playing;
  struct queue_item *qii;

  offset = cmd->arg.playback_start_param.pos;

  ps_playing = source_now_playing();

  if (ps_playing)
    {
      qii = queue_get_bypos(queue, ps_playing->item_id, offset, shuffle);
    }
  else
    {
      qii = queue_get_byindex(queue, offset, shuffle);
    }

  return playback_start_item(cmd, qii);
}

static int
playback_prev_bh(struct player_command *cmd)
{
  int ret;
  int pos_sec;
  struct queue_item *item;

  /*
   * The upper half is playback_pause, therefor the current playing item is
   * already set as the cur_streaming (cur_playing is NULL).
   */
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      return -1;
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
      item = queue_prev(queue, cur_streaming->item_id, shuffle, repeat);
      if (!item)
        {
          playback_abort();
          return -1;
        }

      source_stop();

      ret = source_open(item, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, 0);
      if (ret < 0)
	{
	  playback_abort();

	  return -1;
	}
    }
  else
    {
      ret = source_seek(0);
      if (ret < 0)
	{
	  playback_abort();

	  return -1;
	}
    }

  if (player_state == PLAY_STOPPED)
    return -1;

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}

/*
 * The bottom half of the next command
 */
static int
playback_next_bh(struct player_command *cmd)
{
  int ret;
  struct queue_item *item;

  /*
   * The upper half is playback_pause, therefor the current playing item is
   * already set as the cur_streaming (cur_playing is NULL).
   */
  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      return -1;
    }

  /* Only add to history if playback started. */
  if (cur_streaming->output_start > cur_streaming->stream_start)
    history_add(cur_streaming->id, cur_streaming->item_id);

  item = queue_next(queue, cur_streaming->item_id, shuffle, repeat, 0);
  if (!item)
    {
      playback_abort();
      return -1;
    }

  source_stop();

  ret = source_open(item, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, 0);
  if (ret < 0)
    {
      playback_abort();
      return -1;
    }

  if (player_state == PLAY_STOPPED)
    return -1;

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}

static int
playback_seek_bh(struct player_command *cmd)
{
  int ms;
  int ret;

  ms = cmd->arg.intval;

  ret = source_seek(ms);

  if (ret < 0)
    {
      playback_abort();

      return -1;
    }

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}

static int
playback_pause_bh(struct player_command *cmd)
{
  int ret;

  if (cur_streaming->data_kind == DATA_KIND_HTTP
      || cur_streaming->data_kind == DATA_KIND_PIPE)
    {
      DPRINTF(E_DBG, L_PLAYER, "Source is not pausable, abort playback\n");

      playback_abort();
      return -1;
    }
  status_update(PLAY_PAUSED);

  if (cur_streaming->media_kind & (MEDIA_KIND_MOVIE | MEDIA_KIND_PODCAST | MEDIA_KIND_AUDIOBOOK | MEDIA_KIND_TVSHOW))
    {
      ret = (cur_streaming->output_start - cur_streaming->stream_start) / 44100 * 1000;
      db_file_save_seek(cur_streaming->id, ret);
    }

  return 0;
}

static int
playback_pause(struct player_command *cmd)
{
  uint64_t pos;

  pos = source_check();
  if (pos == 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not retrieve current position for pause\n");

      playback_abort();
      return -1;
    }

  /* Make sure playback is still running after source_check() */
  if (player_state == PLAY_STOPPED)
    return -1;

  cmd->raop_pending = raop_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  if (laudio_status != LAUDIO_CLOSED)
    laudio_stop();

  pb_timer_stop();

  source_pause(pos);

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  metadata_purge();

  /* We're async if we need to flush RAOP devices */
  if (cmd->raop_pending > 0)
    return 1; /* async */

  /* Otherwise, just run the bottom half */
  return cmd->func_bh(cmd);
}

static int
speaker_enumerate(struct player_command *cmd)
{
  struct raop_device *rd;
  struct spk_enum *spk_enum;
  struct spk_flags flags;
  char *laudio_name;

  spk_enum = cmd->arg.spk_enum;

  laudio_name = cfg_getstr(cfg_getsec(cfg, "audio"), "nickname");

  /* Auto-select local audio if there are no AirPlay devices */
  if (!dev_list && !laudio_selected)
    speaker_select_laudio();

  flags.selected = laudio_selected;
  flags.has_password = 0;
  flags.has_video = 0;

  spk_enum->cb(0, laudio_name, laudio_relvol, flags, spk_enum->arg);

#ifdef DEBUG_RELVOL
  DPRINTF(E_DBG, L_PLAYER, "*** master: %d\n", master_volume);
  DPRINTF(E_DBG, L_PLAYER, "*** laudio: abs %d rel %d\n", laudio_volume, laudio_relvol);
#endif

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (rd->advertised || rd->selected)
	{
	  flags.selected = rd->selected;
	  flags.has_password = rd->has_password;
	  flags.has_video = (rd->devtype == RAOP_DEV_APPLETV);

	  spk_enum->cb(rd->id, rd->name, rd->relvol, flags, spk_enum->arg);

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", rd->name, rd->volume, rd->relvol);
#endif
	}
    }

  return 0;
}

static int
speaker_activate(struct raop_device *rd)
{
  struct timespec ts;
  uint64_t pos;
  int ret;

  if (!rd)
    {
      /* Local */
      DPRINTF(E_DBG, L_PLAYER, "Activating local audio\n");

      if (laudio_status == LAUDIO_CLOSED)
	{
	  ret = laudio_open();
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not open local audio\n");

	      return -1;
	    }
	}

      if (player_state == PLAY_PLAYING)
	{
	  laudio_set_volume(laudio_volume);

	  ret = player_get_current_pos(&pos, &ts, 0);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream position for local audio start\n");

	      laudio_close();
	      return -1;
	    }

	  ret = laudio_start(pos, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Local playback failed to start\n");

	      laudio_close();
	      return -1;
	    }
	}

      return 0;
    }
  else
    {
      /* RAOP */
      if (player_state == PLAY_PLAYING)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Activating RAOP device %s\n", rd->name);

	  ret = raop_device_start(rd, device_activate_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not start device %s\n", rd->name);

	      return -1;
	    }
	}
      else
	{
	  DPRINTF(E_DBG, L_PLAYER, "Probing RAOP device %s\n", rd->name);

	  ret = raop_device_probe(rd, device_probe_cb);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not probe device %s\n", rd->name);

	      return -1;
	    }
	}

      return 1;
    }

  return -1;
}

static int
speaker_deactivate(struct raop_device *rd)
{
  if (!rd)
    {
      /* Local */
      DPRINTF(E_DBG, L_PLAYER, "Deactivating local audio\n");

      if (laudio_status == LAUDIO_CLOSED)
	return 0;

      if (laudio_status & LAUDIO_F_STARTED)
	laudio_stop();

      laudio_close();

      return 0;
    }
  else
    {
      /* RAOP */
      DPRINTF(E_DBG, L_PLAYER, "Deactivating RAOP device %s\n", rd->name);

      raop_set_status_cb(rd->session, device_shutdown_cb);
      raop_device_stop(rd->session);

      return 1;
    }

  return -1;
}

static int
speaker_set(struct player_command *cmd)
{
  struct raop_device *rd;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  ids = cmd->arg.raop_ids;

  if (ids)
    nspk = ids[0];
  else
    nspk = 0;

  DPRINTF(E_DBG, L_PLAYER, "Speaker set: %d speakers\n", nspk);

  cmd->raop_pending = 0;
  cmd->ret = 0;

  /* RAOP devices */
  for (rd = dev_list; rd; rd = rd->next)
    {
      for (i = 1; i <= nspk; i++)
	{
	  DPRINTF(E_DBG, L_PLAYER, "Set %" PRIu64 " device %" PRIu64 "\n", ids[i], rd->id);

	  if (ids[i] == rd->id)
	    break;
	}

      if (i <= nspk)
	{
	  if (rd->has_password && !rd->password)
	    {
	      DPRINTF(E_INFO, L_PLAYER, "RAOP device %s is password-protected, but we don't have it\n", rd->name);

	      cmd->ret = -2;
	      continue;
	    }

	  DPRINTF(E_DBG, L_PLAYER, "RAOP device %s selected\n", rd->name);

	  if (!rd->selected)
	    speaker_select_raop(rd);

	  if (!rd->session)
	    {
	      ret = speaker_activate(rd);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Could not activate RAOP device %s\n", rd->name);

		  speaker_deselect_raop(rd);

		  if (cmd->ret != -2)
		    cmd->ret = -1;
		}

	      /* ret = 1 if RAOP needs to take action */
	      cmd->raop_pending += ret;
	    }
	}
      else
	{
	  DPRINTF(E_DBG, L_PLAYER, "RAOP device %s NOT selected\n", rd->name);

	  if (rd->selected)
	    speaker_deselect_raop(rd);

	  if (rd->session)
	    {
	      ret = speaker_deactivate(rd);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_PLAYER, "Could not deactivate RAOP device %s\n", rd->name);

		  if (cmd->ret != -2)
		    cmd->ret = -1;
		}

	      /* ret = 1 if RAOP needs to take action */
	      cmd->raop_pending += ret;
	    }
	}
    }

  /* Local audio */
  for (i = 1; i <= nspk; i++)
    {
      if (ids[i] == 0)
	break;
    }

  if (i <= nspk)
    {
      DPRINTF(E_DBG, L_PLAYER, "Local audio selected\n");

      if (!laudio_selected)
	speaker_select_laudio();

      if (!(laudio_status & LAUDIO_F_STARTED))
	{
	  ret = speaker_activate(NULL);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not activate local audio output\n");

	      speaker_deselect_laudio();

	      if (cmd->ret != -2)
		cmd->ret = -1;
	    }
	}
    }
  else
    {
      DPRINTF(E_DBG, L_PLAYER, "Local audio NOT selected\n");

      if (laudio_selected)
	speaker_deselect_laudio();

      if (laudio_status != LAUDIO_CLOSED)
	{
	  ret = speaker_deactivate(NULL);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Could not deactivate local audio output\n");

	      if (cmd->ret != -2)
		cmd->ret = -1;
	    }
	}
    }

  listener_notify(LISTENER_SPEAKER);

  if (cmd->raop_pending > 0)
    return 1; /* async */

  return cmd->ret;
}

static int
volume_set(struct player_command *cmd)
{
  struct raop_device *rd;
  int volume;

  volume = cmd->arg.intval;

  if (master_volume == volume)
    return 0;

  master_volume = volume;

  if (laudio_selected)
    {
      laudio_volume = rel_to_vol(laudio_relvol);
      laudio_set_volume(laudio_volume);

#ifdef DEBUG_RELVOL
      DPRINTF(E_DBG, L_PLAYER, "*** laudio: abs %d rel %d\n", laudio_volume, laudio_relvol);
#endif
    }

  cmd->raop_pending = 0;

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (!rd->selected)
	continue;

      rd->volume = rel_to_vol(rd->relvol);

#ifdef DEBUG_RELVOL
      DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", rd->name, rd->volume, rd->relvol);
#endif

      if (rd->session)
	cmd->raop_pending += raop_set_volume_one(rd->session, rd->volume, device_command_cb);
    }

  listener_notify(LISTENER_VOLUME);

  if (cmd->raop_pending > 0)
    return 1; /* async */

  return 0;
}

static int
volume_setrel_speaker(struct player_command *cmd)
{
  struct raop_device *rd;
  uint64_t id;
  int relvol;

  id = cmd->arg.vol_param.spk_id;
  relvol = cmd->arg.vol_param.volume;

  if (id == 0)
    {
      laudio_relvol = relvol;
      laudio_volume = rel_to_vol(relvol);
      laudio_set_volume(laudio_volume);

#ifdef DEBUG_RELVOL
      DPRINTF(E_DBG, L_PLAYER, "*** laudio: abs %d rel %d\n", laudio_volume, laudio_relvol);
#endif
    }
  else
    {
      for (rd = dev_list; rd; rd = rd->next)
        {
	  if (rd->id != id)
	    continue;

	  if (!rd->selected)
	    return 0;

	  rd->relvol = relvol;
	  rd->volume = rel_to_vol(relvol);

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", rd->name, rd->volume, rd->relvol);
#endif

	  if (rd->session)
	    cmd->raop_pending = raop_set_volume_one(rd->session, rd->volume, device_command_cb);

	  break;
        }
    }

  listener_notify(LISTENER_VOLUME);

  if (cmd->raop_pending > 0)
    return 1; /* async */

  return 0;
}

static int
volume_setabs_speaker(struct player_command *cmd)
{
  struct raop_device *rd;
  uint64_t id;
  int volume;

  id = cmd->arg.vol_param.spk_id;
  volume = cmd->arg.vol_param.volume;

  master_volume = volume;

  if (id == 0)
    {
      laudio_relvol = 100;
      laudio_volume = volume;
      laudio_set_volume(laudio_volume);
    }
  else
    laudio_relvol = vol_to_rel(laudio_volume);

#ifdef DEBUG_RELVOL
  DPRINTF(E_DBG, L_PLAYER, "*** laudio: abs %d rel %d\n", laudio_volume, laudio_relvol);
#endif

  for (rd = dev_list; rd; rd = rd->next)
    {
      if (!rd->selected)
	continue;

      if (rd->id != id)
	{
	  rd->relvol = vol_to_rel(rd->volume);

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", rd->name, rd->volume, rd->relvol);
#endif
	  continue;
	}
      else
	{
	  rd->relvol = 100;
	  rd->volume = master_volume;

#ifdef DEBUG_RELVOL
	  DPRINTF(E_DBG, L_PLAYER, "*** %s: abs %d rel %d\n", rd->name, rd->volume, rd->relvol);
#endif

	  if (rd->session)
	    cmd->raop_pending = raop_set_volume_one(rd->session, rd->volume, device_command_cb);
	}
    }

  listener_notify(LISTENER_VOLUME);

  if (cmd->raop_pending > 0)
    return 1; /* async */

  return 0;
}

static int
repeat_set(struct player_command *cmd)
{
  if (cmd->arg.mode == repeat)
    return 0;

  switch (cmd->arg.mode)
    {
      case REPEAT_OFF:
      case REPEAT_SONG:
      case REPEAT_ALL:
	repeat = cmd->arg.mode;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid repeat mode: %d\n", cmd->arg.mode);
	return -1;
    }

  listener_notify(LISTENER_OPTIONS);

  return 0;
}

static int
shuffle_set(struct player_command *cmd)
{
  uint32_t cur_id;

  switch (cmd->arg.intval)
    {
      case 1:
	if (!shuffle)
	  {
	    cur_id = cur_streaming ? cur_streaming->item_id : 0;
	    queue_shuffle(queue, cur_id);
	  }
	/* FALLTHROUGH*/
      case 0:
	shuffle = cmd->arg.intval;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid shuffle mode: %d\n", cmd->arg.intval);
	return -1;
    }

  listener_notify(LISTENER_OPTIONS);

  return 0;
}

static int
playerqueue_get_bypos(struct player_command *cmd)
{
  int count;
  struct queue *qi;
  struct player_source *ps;
  int item_id;

  count = cmd->arg.queue_get_param.count;

  ps = source_now_playing();

  item_id = 0;
  if (ps)
    {
      item_id = ps->item_id;
    }

  qi = queue_new_bypos(queue, item_id, count, shuffle);

  cmd->arg.queue_get_param.queue = qi;

  return 0;
}

static int
playerqueue_get_byindex(struct player_command *cmd)
{
  int pos;
  int count;
  struct queue *qi;

  pos = cmd->arg.queue_get_param.pos;
  count = cmd->arg.queue_get_param.count;

  qi = queue_new_byindex(queue, pos, count, 0);
  cmd->arg.queue_get_param.queue = qi;

  return 0;
}

static int
playerqueue_add(struct player_command *cmd)
{
  struct queue_item *items;
  uint32_t cur_id;

  items = cmd->arg.queue_add_param.items;

  queue_add(queue, items);

  if (shuffle)
    {
      cur_id = cur_streaming ? cur_streaming->item_id : 0;
      queue_shuffle(queue, cur_id);
    }

  //TODO [refactor] Unnecessary if, always set plid to 0 after adding items
  if (cur_plid != 0)
    cur_plid = 0;
  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_add_next(struct player_command *cmd)
{
  struct queue_item *items;
  uint32_t cur_id;

  items = cmd->arg.queue_add_param.items;

  cur_id = cur_streaming ? cur_streaming->item_id : 0;

  queue_add_after(queue, items, cur_id);

  if (shuffle)
    queue_shuffle(queue, cur_id);

  //TODO [refactor] Unnecessary if, always set plid to 0 after adding items
  if (cur_plid != 0)
    cur_plid = 0;
  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_move_bypos(struct player_command *cmd)
{
  struct player_source *ps_playing;

  DPRINTF(E_DBG, L_PLAYER, "Moving song from position %d to be the next song after %d\n",
      cmd->arg.queue_move_param.from_pos, cmd->arg.queue_move_param.to_pos);

  ps_playing = source_now_playing();

  if (!ps_playing)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't move item, no playing item found\n");
      return -1;
    }

  queue_move_bypos(queue, ps_playing->item_id, cmd->arg.queue_move_param.from_pos, cmd->arg.queue_move_param.to_pos, shuffle);

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_move_byitemid(struct player_command *cmd)
{
  DPRINTF(E_DBG, L_PLAYER, "Moving song with item-id %d to be the next song after index %d\n",
      cmd->arg.queue_move_param.item_id, cmd->arg.queue_move_param.to_pos);

  queue_move_byitemid(queue, cmd->arg.queue_move_param.item_id, cmd->arg.queue_move_param.to_pos, 0);

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_remove_bypos(struct player_command *cmd)
{
  int pos;
  struct player_source *ps_playing;

  pos = cmd->arg.intval;
  if (pos < 1)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't remove item, invalid position %d\n", pos);
      return -1;
    }

  ps_playing = source_now_playing();

  if (!ps_playing)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't remove item at pos %d, no playing item found\n", pos);
      return -1;
    }

  DPRINTF(E_DBG, L_PLAYER, "Removing item from position %d\n", pos);
  queue_remove_bypos(queue, ps_playing->item_id, pos, shuffle);

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_remove_byindex(struct player_command *cmd)
{
  int pos;
  int count;
  int i;

  pos = cmd->arg.queue_remove_param.from_pos;
  count = cmd->arg.queue_remove_param.count;

  DPRINTF(E_DBG, L_PLAYER, "Removing %d items starting from position %d\n", count, pos);

  for (i = 0; i < count; i++)
    queue_remove_byindex(queue, pos, 0);

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_remove_byitemid(struct player_command *cmd)
{
  uint32_t id;

  id = cmd->arg.id;
  if (id < 1)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't remove item, invalid id %d\n", id);
      return -1;
    }

  DPRINTF(E_DBG, L_PLAYER, "Removing item with id %d\n", id);
  queue_remove_byitemid(queue, id);

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

/*
 * Removes all media items from the queue
 */
static int
playerqueue_clear(struct player_command *cmd)
{
  queue_clear(queue);

  cur_plid = 0;
  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

/*
 * Removes all items from the history
 */
static int
playerqueue_clear_history(struct player_command *cmd)
{
  memset(history, 0, sizeof(struct player_history));

  cur_plversion++;

  listener_notify(LISTENER_PLAYLIST);

  return 0;
}

static int
playerqueue_plid(struct player_command *cmd)
{
  cur_plid = cmd->arg.id;

  return 0;
}

/* Command processing */
/* Thread: player */
static void
command_cb(int fd, short what, void *arg)
{
  struct player_command *cmd;
  int ret;

  ret = read(cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");

      goto readd;
    }

  if (cmd->nonblock)
    {
      cmd->func(cmd);

      free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  cur_cmd = cmd;

  ret = cmd->func(cmd);

  if (ret <= 0)
    {
      cmd->ret = ret;

      cur_cmd = NULL;

      pthread_cond_signal(&cmd->cond);
      pthread_mutex_unlock(&cmd->lck);
    }
  else
    {
      /* Command is asynchronous, we don't want to process another command
       * before we're done with this one. See command_async_end().
       */

      return;
    }

 readd:
  event_add(cmdev, NULL);
}


/* Thread: httpd (DACP) - mDNS */
static int
send_command(struct player_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_PLAYER, "BUG: cmd->func is NULL!\n");

      return -1;
    }

  ret = write(cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not send command: %s\n", strerror(errno));

      return -1;
    }

  return 0;
}

/* Thread: mDNS */
static int
nonblock_command(struct player_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

/* Thread: httpd (DACP) */
static int
sync_command(struct player_command *cmd)
{
  int ret;

  pthread_mutex_lock(&cmd->lck);

  ret = send_command(cmd);
  if (ret < 0)
    {
      pthread_mutex_unlock(&cmd->lck);

      return -1;
    }

  pthread_cond_wait(&cmd->cond, &cmd->lck);

  pthread_mutex_unlock(&cmd->lck);

  ret = cmd->ret;

  return ret;
}

/* Player API executed in the httpd (DACP) thread */
int
player_get_status(struct player_status *status)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = get_status;
  cmd.func_bh = NULL;
  cmd.arg.status = status;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

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
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = now_playing;
  cmd.func_bh = NULL;
  cmd.arg.id_ptr = id;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

char *
player_get_icy_artwork_url(uint32_t id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = artwork_url_get;
  cmd.func_bh = NULL;
  cmd.arg.icy.id = id;

  if (pthread_self() != tid_player)
    ret = sync_command(&cmd);
  else
    ret = artwork_url_get(&cmd);

  command_deinit(&cmd);

  if (ret < 0)
    return NULL;
  else
    return cmd.arg.icy.artwork_url;
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
player_playback_start(uint32_t *id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start;
  cmd.func_bh = playback_start_bh;
  cmd.arg.playback_start_param.id_ptr = id;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

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
player_playback_start_byindex(int index, uint32_t *id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start_byindex;
  cmd.func_bh = playback_start_bh;
  cmd.arg.playback_start_param.pos = index;
  cmd.arg.playback_start_param.id_ptr = id;
  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Starts playback with the media item at the given position in the UpNext-queue.
 * The UpNext-queue consists of all items of the play-queue (shuffle off) or shuffle-queue
 * (shuffle on) after the current playing item (starting with position 0).
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item dbmfi-id.
 *
 * @param pos the position in the UpNext-queue (zero-based)
 * @param *id if not NULL, will be set to the playing item dbmfi-id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start_bypos(int pos, uint32_t *id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start_bypos;
  cmd.func_bh = playback_start_bh;
  cmd.arg.playback_start_param.pos = pos;
  cmd.arg.playback_start_param.id_ptr = id;
  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Starts playback with the media item with the given (queueitem) item-id in queue
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item dbmfi-id.
 *
 * @param item_id The queue-item-id
 * @param *id if not NULL, will be set to the playing item dbmfi-id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start_byitemid(uint32_t item_id, uint32_t *id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start_byitemid;
  cmd.func_bh = playback_start_bh;
  cmd.arg.playback_start_param.id = item_id;
  cmd.arg.playback_start_param.id_ptr = id;
  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_playback_stop(void)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_stop;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_playback_pause(void)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_pause;
  cmd.func_bh = playback_pause_bh;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_playback_seek(int ms)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_pause;
  cmd.func_bh = playback_seek_bh;
  cmd.arg.intval = ms;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_playback_next(void)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_pause;
  cmd.func_bh = playback_next_bh;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_playback_prev(void)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_pause;
  cmd.func_bh = playback_prev_bh;
  cmd.arg.noarg = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

void
player_streaming_start(player_streaming_cb cb)
{
  streaming_write = cb;
  streaming_selected = 1;
}

void
player_streaming_stop(void)
{
  streaming_selected = 0;
}


void
player_speaker_enumerate(spk_enum_cb cb, void *arg)
{
  struct player_command cmd;
  struct spk_enum spk_enum;

  command_init(&cmd);

  spk_enum.cb = cb;
  spk_enum.arg = arg;

  cmd.func = speaker_enumerate;
  cmd.func_bh = NULL;
  cmd.arg.spk_enum = &spk_enum;

  sync_command(&cmd);

  command_deinit(&cmd);
}

int
player_speaker_set(uint64_t *ids)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = speaker_set;
  cmd.func_bh = NULL;
  cmd.arg.raop_ids = ids;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_volume_set(int vol)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = volume_set;
  cmd.func_bh = NULL;
  cmd.arg.intval = vol;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_volume_setrel_speaker(uint64_t id, int relvol)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = volume_setrel_speaker;
  cmd.func_bh = NULL;
  cmd.arg.vol_param.spk_id = id;
  cmd.arg.vol_param.volume = relvol;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_volume_setabs_speaker(uint64_t id, int vol)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = volume_setabs_speaker;
  cmd.func_bh = NULL;
  cmd.arg.vol_param.spk_id = id;
  cmd.arg.vol_param.volume = vol;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_repeat_set(enum repeat_mode mode)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = repeat_set;
  cmd.func_bh = NULL;
  cmd.arg.mode = mode;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_shuffle_set(int enable)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = shuffle_set;
  cmd.func_bh = NULL;
  cmd.arg.intval = enable;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Returns the queue info for max "count" media items in the UpNext-queue
 *
 * The UpNext-queue consists of all items of the play-queue (shuffle off) or shuffle-queue
 * (shuffle on) after the current playing item (starting with position 0).
 *
 * @param count max number of media items to return
 * @return queue info
 */
struct queue *
player_queue_get_bypos(int count)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_get_bypos;
  cmd.func_bh = NULL;
  cmd.arg.queue_get_param.pos = -1;
  cmd.arg.queue_get_param.count = count;
  cmd.arg.queue_get_param.queue = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  if (ret != 0)
    return NULL;

  return cmd.arg.queue_get_param.queue;
}

/*
 * Returns the queue info for max "count" media items starting with the item at the given
 * index in the play-queue
 *
 * @param index Index of the play-queue for the first item
 * @param count max number of media items to return
 * @return queue info
 */
struct queue *
player_queue_get_byindex(int index, int count)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_get_byindex;
  cmd.func_bh = NULL;
  cmd.arg.queue_get_param.pos = index;
  cmd.arg.queue_get_param.count = count;
  cmd.arg.queue_get_param.queue = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  if (ret != 0)
    return NULL;

  return cmd.arg.queue_get_param.queue;
}

/*
 * Appends the given media items to the queue
 */
int
player_queue_add(struct queue_item *items)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_add;
  cmd.func_bh = NULL;
  cmd.arg.queue_add_param.items = items;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Adds the given media items directly after the current playing/streaming media item
 */
int
player_queue_add_next(struct queue_item *items)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_add_next;
  cmd.func_bh = NULL;
  cmd.arg.queue_add_param.items = items;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Moves the media item at 'pos_from' to 'pos_to' in the UpNext-queue.
 *
 * The UpNext-queue consists of all items of the play-queue (shuffle off) or shuffle-queue
 * (shuffle on) after the current playing item (starting with position 0).
 */
int
player_queue_move_bypos(int pos_from, int pos_to)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_move_bypos;
  cmd.func_bh = NULL;
  cmd.arg.queue_move_param.from_pos = pos_from;
  cmd.arg.queue_move_param.to_pos = pos_to;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_queue_move_byitemid(uint32_t item_id, int pos_to)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_move_byitemid;
  cmd.func_bh = NULL;
  cmd.arg.queue_move_param.item_id = item_id;
  cmd.arg.queue_move_param.to_pos = pos_to;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Removes the media item at the given position from the UpNext-queue
 *
 * The UpNext-queue consists of all items of the play-queue (shuffle off) or shuffle-queue
 * (shuffle on) after the current playing item (starting with position 0).
 *
 * @param pos Position in the UpNext-queue (0-based)
 * @return 0 on success, -1 on failure
 */
int
player_queue_remove_bypos(int pos)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_remove_bypos;
  cmd.func_bh = NULL;
  cmd.arg.intval = pos;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Removes the media item at the given position from the UpNext-queue
 *
 * The UpNext-queue consists of all items of the play-queue (shuffle off) or shuffle-queue
 * (shuffle on) after the current playing item (starting with position 0).
 *
 * @param pos Position in the UpNext-queue (0-based)
 * @return 0 on success, -1 on failure
 */
int
player_queue_remove_byindex(int pos, int count)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_remove_byindex;
  cmd.func_bh = NULL;
  cmd.arg.queue_remove_param.from_pos = pos;
  cmd.arg.queue_remove_param.count = count;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Removes the item with the given (queueitem) item id from the queue
 *
 * @param id Id of the queue item to remove
 * @return 0 on success, -1 on failure
 */
int
player_queue_remove_byitemid(uint32_t id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playerqueue_remove_byitemid;
  cmd.func_bh = NULL;
  cmd.arg.id = id;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

void
player_queue_clear(void)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = playerqueue_clear;
  cmd.func_bh = NULL;
  cmd.arg.noarg = NULL;

  sync_command(&cmd);

  command_deinit(&cmd);
}

void
player_queue_clear_history()
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = playerqueue_clear_history;
  cmd.func_bh = NULL;

  sync_command(&cmd);

  command_deinit(&cmd);
}

void
player_queue_plid(uint32_t plid)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = playerqueue_plid;
  cmd.func_bh = NULL;
  cmd.arg.id = plid;

  sync_command(&cmd);

  command_deinit(&cmd);
}

/* Non-blocking commands used by mDNS */
static void
player_device_add(struct raop_device *rd)
{
  struct player_command *cmd;
  int ret;

  cmd = (struct player_command *)malloc(sizeof(struct player_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");

      device_free(rd);
      return;
    }

  memset(cmd, 0, sizeof(struct player_command));

  cmd->nonblock = 1;

  cmd->func = device_add;
  cmd->arg.rd = rd;

  ret = nonblock_command(cmd);
  if (ret < 0)
    {
      free(cmd);
      device_free(rd);

      return;
    }
}

static void
player_device_remove(struct raop_device *rd)
{
  struct player_command *cmd;
  int ret;

  cmd = (struct player_command *)malloc(sizeof(struct player_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate player_command\n");

      device_free(rd);
      return;
    }

  memset(cmd, 0, sizeof(struct player_command));

  cmd->nonblock = 1;

  cmd->func = device_remove_family;
  cmd->arg.rd = rd;

  ret = nonblock_command(cmd);
  if (ret < 0)
    {
      free(cmd);
      device_free(rd);

      return;
    }
}

/* Thread: worker */
static void
player_metadata_send(struct player_metadata *pmd)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = metadata_send;
  cmd.func_bh = NULL;
  cmd.arg.pmd = pmd;

  sync_command(&cmd);

  command_deinit(&cmd);
}


/* RAOP devices discovery - mDNS callback */
/* Thread: main (mdns) */
/* Examples of txt content:
 * Apple TV 2:
     ["sf=0x4" "am=AppleTV2,1" "vs=130.14" "vn=65537" "tp=UDP" "ss=16" "sr=4 4100" "sv=false" "pw=false" "md=0,1,2" "et=0,3,5" "da=true" "cn=0,1,2,3" "ch=2"]
     ["sf=0x4" "am=AppleTV2,1" "vs=105.5" "md=0,1,2" "tp=TCP,UDP" "vn=65537" "pw=false" "ss=16" "sr=44100" "da=true" "sv=false" "et=0,3" "cn=0,1" "ch=2" "txtvers=1"]
 * Apple TV 3:
     ["vv=2" "vs=200.54" "vn=65537" "tp=UDP" "sf=0x44" "pk=8...f" "am=AppleTV3,1" "md=0,1,2" "ft=0x5A7FFFF7,0xE" "et=0,3,5" "da=true" "cn=0,1,2,3"]
 * Sony STR-DN1040:
     ["fv=s9327.1090.0" "am=STR-DN1040" "vs=141.9" "vn=65537" "tp=UDP" "ss=16" "sr=44100" "sv=false" "pw=false" "md=0,2" "ft=0x44F0A00" "et=0,4" "da=true" "cn=0,1" "ch=2" "txtvers=1"]
 * AirFoil:
     ["rastx=iafs" "sm=false" "raver=3.5.3.0" "ek=1" "md=0,1,2" "ramach=Win32NT.6" "et=0,1" "cn=0,1" "sr=44100" "ss=16" "raAudioFormats=ALAC" "raflakyzeroconf=true" "pw=false" "rast=afs" "vn=3" "sv=false" "txtvers=1" "ch=2" "tp=UDP"]
 * Xbmc 13:
     ["am=Xbmc,1" "md=0,1,2" "vs=130.14" "da=true" "vn=3" "pw=false" "sr=44100" "ss=16" "sm=false" "tp=UDP" "sv=false" "et=0,1" "ek=1" "ch=2" "cn=0,1" "txtvers=1"]
 * Shairport (abrasive/1.0):
     ["pw=false" "txtvers=1" "vn=3" "sr=44100" "ss=16" "ch=2" "cn=0,1" "et=0,1" "ek=1" "sm=false" "tp=UDP"]
 * JB2:
     ["fv=95.8947" "am=JB2 Gen" "vs=103.2" "tp=UDP" "vn=65537" "pw=false" "s s=16" "sr=44100" "da=true" "sv=false" "et=0,4" "cn=0,1" "ch=2" "txtvers=1"]
 * Airport Express 802.11g (Gen 1):
     ["tp=TCP,UDP" "sm=false" "sv=false" "ek=1" "et=0,1" "cn=0,1" "ch=2" "ss=16" "sr=44100" "pw=false" "vn=3" "txtvers=1"]
 * Airport Express 802.11n:
     802.11n Gen 2 model (firmware 7.6.4): "am=Airport4,107", "et=0,1"
     802.11n Gen 3 model (firmware 7.6.4): "am=Airport10,115", "et=0,4"
 */
static void
raop_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct raop_device *rd;
  cfg_t *airplay;
  const char *p;
  char *at_name;
  char *password;
  uint64_t id;
  int ret;

  ret = safe_hextou64(name, &id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not extract AirPlay device ID (%s)\n", name);

      return;
    }

  at_name = strchr(name, '@');
  if (!at_name)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not extract AirPlay device name (%s)\n", name);

      return;
    }
  at_name++;

  DPRINTF(E_DBG, L_PLAYER, "Event for AirPlay device %" PRIx64 "/%s (%d)\n", id, at_name, port);

  rd = (struct raop_device *)malloc(sizeof(struct raop_device));
  if (!rd)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for new AirPlay device\n");

      return;
    }

  memset(rd, 0, sizeof(struct raop_device));

  rd->id = id;
  rd->name = strdup(at_name);

  if (port < 0)
    {
      /* Device stopped advertising */
      switch (family)
	{
	  case AF_INET:
	    rd->v4_port = 1;
	    break;

	  case AF_INET6:
	    rd->v6_port = 1;
	    break;
	}

      player_device_remove(rd);

      return;
    }

  /* Protocol */
  p = keyval_get(txt, "tp");
  if (!p)
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: no tp field in TXT record!\n", name);

      goto free_rd;
    }

  if (*p == '\0')
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: tp has no value\n", name);

      goto free_rd;
    }

  if (!strstr(p, "UDP"))
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: device does not support AirTunes v2 (tp=%s), discarding\n", name, p);

      goto free_rd;
    }

  /* Password protection */
  password = NULL;
  p = keyval_get(txt, "pw");
  if (!p)
    {
      DPRINTF(E_INFO, L_PLAYER, "AirPlay %s: no pw field in TXT record, assuming no password protection\n", name);

      rd->has_password = 0;
    }
  else if (*p == '\0')
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: pw has no value\n", name);

      goto free_rd;
    }
  else
    {
      rd->has_password = (strcmp(p, "false") != 0);
    }

  if (rd->has_password)
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay device %s is password-protected\n", name);

      airplay = cfg_gettsec(cfg, "airplay", at_name);
      if (airplay)
	password = cfg_getstr(airplay, "password");

      if (!password)
	DPRINTF(E_LOG, L_PLAYER, "No password given in config for AirPlay device %s\n", name);
    }

  rd->password = password;

  /* Device type */
  rd->devtype = RAOP_DEV_OTHER;
  p = keyval_get(txt, "am");

  if (!p)
    rd->devtype = RAOP_DEV_APEX1_80211G; // First generation AirPort Express
  else if (strncmp(p, "AirPort4", strlen("AirPort4")) == 0)
    rd->devtype = RAOP_DEV_APEX2_80211N; // Second generation
  else if (strncmp(p, "AirPort", strlen("AirPort")) == 0)
    rd->devtype = RAOP_DEV_APEX3_80211N; // Third generation and newer
  else if (strncmp(p, "AppleTV", strlen("AppleTV")) == 0)
    rd->devtype = RAOP_DEV_APPLETV;
  else if (*p == '\0')
    DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: am has no value\n", name);

  /* Encrypt stream */
  p = keyval_get(txt, "ek");
  if (p && (*p == '1'))
    rd->encrypt = 1;
  else
    rd->encrypt = 0;

  /* Metadata support */
  p = keyval_get(txt, "md");
  if (p && (*p != '\0'))
    rd->wants_metadata = 1;
  else
    rd->wants_metadata = 0;

  DPRINTF(E_INFO, L_PLAYER, "AirPlay device %s: password: %u, encrypt: %u, metadata: %u, type %s\n", 
    name, rd->has_password, rd->encrypt, rd->wants_metadata, raop_devtype[rd->devtype]);

  rd->advertised = 1;

  switch (family)
    {
      case AF_INET:
	rd->v4_address = strdup(address);
	rd->v4_port = port;
	break;

      case AF_INET6:
	rd->v6_address = strdup(address);
	rd->v6_port = port;
	break;
    }

  player_device_add(rd);

  return;

 free_rd:
  device_free(rd);
}

/* Thread: player */
static void *
player(void *arg)
{
  struct raop_device *rd;
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

  ret = db_speaker_save(0, laudio_selected, laudio_volume);
  if (ret < 0)
    DPRINTF(E_LOG, L_PLAYER, "Could not save state for local audio\n");

  for (rd = dev_list; rd; rd = rd->next)
    {
      ret = db_speaker_save(rd->id, rd->selected, rd->volume);
      if (ret < 0)
	DPRINTF(E_LOG, L_PLAYER, "Could not save state for speaker %s\n", rd->name);
    }

  db_perthread_deinit();

  pthread_exit(NULL);
}

/* Thread: player */
static void
exit_cb(int fd, short what, void *arg)
{
  event_base_loopbreak(evbase_player);

  player_exit = 1;
}

/* Thread: main */
int
player_init(void)
{
  uint32_t rnd;
  int raop_v6enabled;
  int mdns_flags;
  int ret;

  player_exit = 0;

  dev_autoselect = 1;
  dev_list = NULL;

  master_volume = -1;

  laudio_selected = 0;
  laudio_status = LAUDIO_CLOSED;
  raop_sessions = 0;

  cur_cmd = NULL;

  cur_playing = NULL;
  cur_streaming = NULL;
  cur_plid = 0;
  cur_plversion = 0;

  player_state = PLAY_STOPPED;
  repeat = REPEAT_OFF;
  shuffle = 0;

  queue = queue_new();
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

  MINIMUM_STREAM_PERIOD = MAX(timer_res.tv_nsec, AIRTUNES_V2_STREAM_PERIOD);

  /* Create a timer */
#if defined(__linux__)
  pb_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  ret = pb_timer_fd;
#else
  ret = timer_create(CLOCK_MONOTONIC, NULL, &pb_timer);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer: %s\n", strerror(errno));

      return -1;
    }

  /* Random RTP time start */
  gcry_randomize(&rnd, sizeof(rnd), GCRY_STRONG_RANDOM);
  last_rtptime = ((uint64_t)1 << 32) | rnd;

  ret = db_speaker_get(0, &laudio_selected, &laudio_volume);
  if (ret < 0)
    laudio_volume = PLAYER_DEFAULT_VOLUME;
  else if (laudio_selected)
    speaker_select_laudio(); /* Run the select helper */

  audio_buf = evbuffer_new();
  if (!audio_buf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate evbuffer for audio buffer\n");

      goto audio_fail;
    }

  raop_v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");

  ret = pipe2(exit_pipe, O_CLOEXEC);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create pipe: %s\n", strerror(errno));

      goto exit_fail;
    }

  ret = pipe2(cmd_pipe, O_CLOEXEC);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create command pipe: %s\n", strerror(errno));

      goto cmd_fail;
    }

  evbase_player = event_base_new();
  if (!evbase_player)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create an event base\n");

      goto evbase_fail;
    }

  exitev = event_new(evbase_player, exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!exitev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create exit event\n");
      goto evnew_fail;
    }

  cmdev = event_new(evbase_player, cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!cmdev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create cmd event\n");
      goto evnew_fail;
    }

#if defined(__linux__)
  pb_timer_ev = event_new(evbase_player, pb_timer_fd, EV_READ, player_playback_cb, NULL);
#else
  pb_timer_ev = evsignal_new(evbase_player, SIGALRM, player_playback_cb, NULL);
#endif
  if (!pb_timer_ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer event\n");
      goto evnew_fail;
    }

  event_add(exitev, NULL);
  event_add(cmdev, NULL);

#ifndef __linux__
  event_add(pb_timer_ev, NULL);
#endif

  ret = laudio_init(player_laudio_status_cb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Local audio init failed\n");

      goto laudio_fail;
    }

  ret = raop_init(&raop_v6enabled);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "RAOP init failed\n");

      goto raop_fail;
    }

  if (raop_v6enabled)
    mdns_flags = MDNS_WANT_V4 | MDNS_WANT_V6 | MDNS_WANT_V6LL;
  else
    mdns_flags = MDNS_WANT_V4;

  ret = mdns_browse("_raop._tcp", mdns_flags, raop_device_cb);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_PLAYER, "Could not add mDNS browser for AirPlay devices\n");

      goto mdns_browse_fail;
    }

  ret = pthread_create(&tid_player, NULL, player, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not spawn player thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;

 thread_fail:
 mdns_browse_fail:
  raop_deinit();
 raop_fail:
  laudio_deinit();
 laudio_fail:
 evnew_fail:
  event_base_free(evbase_player);
 evbase_fail:
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
 cmd_fail:
  close(exit_pipe[0]);
  close(exit_pipe[1]);
 exit_fail:
  evbuffer_free(audio_buf);
 audio_fail:
#if defined(__linux__)
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
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not write to exit pipe: %s\n", strerror(errno));

      return;
    }

  ret = pthread_join(tid_player, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not join HTTPd thread: %s\n", strerror(errno));

      return;
    }

  queue_free(queue);
  free(history);

  pb_timer_stop();
#if defined(__linux__)
  close(pb_timer_fd);
#else
  timer_delete(pb_timer);
#endif

  evbuffer_free(audio_buf);

  laudio_deinit();
  raop_deinit();

  close(exit_pipe[0]);
  close(exit_pipe[1]);
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
  cmd_pipe[0] = -1;
  cmd_pipe[1] = -1;
  event_base_free(evbase_player);
}
