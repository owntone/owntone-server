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

#ifdef HAVE_LIBEVENT2
# include <event2/event.h>
# include <event2/buffer.h>
#else
# include <event.h>
# define evbuffer_get_length(x) (x)->off
#endif

#include <gcrypt.h>

#include "db.h"
#include "daap_query.h"
#include "logger.h"
#include "mdns.h"
#include "conffile.h"
#include "misc.h"
#include "rng.h"
#include "player.h"
#include "raop.h"
#include "laudio.h"
#include "worker.h"

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

enum range_type
  {
    RANGEARG_NONE,
    RANGEARG_ID,
    RANGEARG_POS,
    RANGEARG_RANGE
  };

/*
 * Identifies an item or a range of items
 *
 * Depending on item_range.type the item(s) are identified by:
 * - item id (type = RANGEARG_ID) given in item_range.id
 * - item position (type = RANGEARG_POS) given in item_range.start_pos
 * - start and end position (type = RANGEARG_RANGE) given in item_range.start_pos to item_range.end_pos
 *
 * The pointer id_ptr may be set to an item id by the called function.
 */
struct item_range
{
  enum range_type type;

  uint32_t id;
  int start_pos;
  int end_pos;

  char shuffle;

  uint32_t *id_ptr;
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
    player_status_handler status_handler;
    uint32_t *id_ptr;
    uint64_t *raop_ids;
    enum repeat_mode mode;
    uint32_t id;
    int intval;
    int ps_pos[2];
    struct item_range item_range;
    struct icy_artwork icy;
  } arg;

  int ret;

  int raop_pending;

  struct player_queue *queue;
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

/* Status updates (for DACP) */
static player_status_handler update_handler;

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
static int dev_autoselect;
static struct raop_device *dev_list;

/* Device status */
static enum laudio_state laudio_status;
static int laudio_selected;
static int laudio_volume;
static int laudio_relvol;
static int raop_sessions;

/* Commands */
static struct player_command *cur_cmd;

/* Last commanded volume */
static int master_volume;

/* Shuffle RNG state */
struct rng_ctx shuffle_rng;

/* Audio source */
static struct player_source *source_head;
static struct player_source *shuffle_head;
static struct player_source *cur_playing;
static struct player_source *cur_streaming;
static uint32_t cur_plid;
static struct evbuffer *audio_buf;

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

  if (update_handler)
    update_handler();

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
      if (player_state == PLAY_STOPPED)
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
      if (player_state == PLAY_STOPPED)
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
queue_clear(struct player_command *cmd);

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
metadata_trigger(struct player_source *ps, int startup)
{
  struct player_metadata pmd;

  memset(&pmd, 0, sizeof(struct player_metadata));

  pmd.id = ps->id;
  pmd.startup = startup;

  /* Determine song boundaries, dependent on context */

  /* Restart after pause/seek */
  if (ps->stream_start)
    {
      pmd.offset = ps->output_start - ps->stream_start;
      pmd.rtptime = ps->stream_start;
    }
  else if (startup)
    {
      /* Will be set later, right before sending */
    }
  /* Generic case */
  else if (cur_streaming && (cur_streaming->end))
    {
      pmd.rtptime = cur_streaming->end + 1;
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

  transcode_metadata(cur_streaming->ctx, &metadata, &changed);
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
  metadata_trigger(cur_streaming, 0);

  /* Only free the struct, the content must be preserved for update_icy_cb */
  free(metadata);

  status_update(player_state);

  return;

 no_update:
  http_icy_metadata_free(metadata, 0);
}

/* Audio sources */
/* Thread: httpd (DACP) */
static struct player_source *
player_queue_make(struct query_params *qp, const char *sort)
{
  struct db_media_file_info dbmfi;
  struct player_source *q_head;
  struct player_source *q_tail;
  struct player_source *ps;
  uint32_t id;
  uint32_t song_length;
  int ret;

  qp->idx_type = I_NONE;

  if (sort)
    {
      if (strcmp(sort, "name") == 0)
	qp->sort = S_NAME;
      else if (strcmp(sort, "album") == 0)
	qp->sort = S_ALBUM;
      else if (strcmp(sort, "artist") == 0)
	qp->sort = S_ARTIST;
    }

  ret = db_query_start(qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not start query\n");

      return NULL;
    }

  DPRINTF(E_DBG, L_PLAYER, "Player queue query returned %d items\n", qp->results);

  q_head = NULL;
  q_tail = NULL;
  while (((ret = db_query_fetch_file(qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = safe_atou32(dbmfi.id, &id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Invalid song id in query result!\n");

	  continue;
	}

      ret = safe_atou32(dbmfi.song_length, &song_length);
      if (ret < 0)
      	{
      	  DPRINTF(E_LOG, L_PLAYER, "Invalid song id in query result!\n");

      	  continue;
      	}

      ps = (struct player_source *)malloc(sizeof(struct player_source));
      if (!ps)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Out of memory for struct player_source\n");

	  ret = -1;
	  break;
	}

      memset(ps, 0, sizeof(struct player_source));

      ps->id = id;
      ps->len_ms = song_length;

      if (!q_head)
	q_head = ps;

      if (q_tail)
	{
	  q_tail->pl_next = ps;
	  ps->pl_prev = q_tail;

	  q_tail->shuffle_next = ps;
	  ps->shuffle_prev = q_tail;
	}

      q_tail = ps;

      DPRINTF(E_DBG, L_PLAYER, "Added song id %d (%s)\n", id, dbmfi.title);
    }

  db_query_end(qp);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Error fetching results\n");

      return NULL;
    }

  if (!q_head)
    return NULL;

  q_head->pl_prev = q_tail;
  q_tail->pl_next = q_head;
  q_head->shuffle_prev = q_tail;
  q_tail->shuffle_next = q_head;

  return q_head;
}

static int
find_first_song_id(const char *query)
{
  struct db_media_file_info dbmfi;
  struct query_params qp;
  int id;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  /* We only want the id of the first song */
  qp.type = Q_ITEMS;
  qp.idx_type = I_FIRST;
  qp.sort = S_NONE;
  qp.offset = 0;
  qp.limit = 1;
  qp.filter = daap_query_parse_sql(query);
  if (!qp.filter)
    {
      DPRINTF(E_LOG, L_PLAYER, "Improper DAAP query!\n");

      return -1;
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not start query\n");

      goto no_query_start;
    }

  if (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = safe_atoi32(dbmfi.id, &id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Invalid song id in query result!\n");

	  goto no_result;
	}

      DPRINTF(E_DBG, L_PLAYER, "Found index song (id %d)\n", id);
      ret = 1;
    }
  else
    {
      DPRINTF(E_LOG, L_PLAYER, "No song matches query (results %d): %s\n", qp.results, qp.filter);

      goto no_result;
    }

 no_result:
  db_query_end(&qp);

 no_query_start:
  if (qp.filter)
    free(qp.filter);

  if (ret == 1)
    return id;
  else
    return -1;
}


/* Thread: httpd (DACP) */
int
player_queue_make_daap(struct player_source **head, const char *query, const char *queuefilter, const char *sort, int quirk)
{
  struct media_file_info *mfi;
  struct query_params qp;
  struct player_source *ps;
  int64_t albumid;
  int64_t artistid;
  int plid;
  int id;
  int idx;
  int ret;
  int len;
  char *s;
  char buf[1024];

  if (query)
    {
      id = find_first_song_id(query);
      if (id < 0)
        return -1;
    }
  else
    id = 0;

  memset(&qp, 0, sizeof(struct query_params));

  qp.offset = 0;
  qp.limit = 0;
  qp.sort = S_NONE;

  if (quirk)
    {
      qp.sort = S_ALBUM;
      qp.type = Q_ITEMS;
      mfi = db_file_fetch_byid(id);
      if (!mfi)
	return -1;
      snprintf(buf, sizeof(buf), "f.songalbumid = %" PRIi64, mfi->songalbumid);
      free_mfi(mfi, 0);
      qp.filter = strdup(buf);
    }
  else if (queuefilter)
    {
      len = strlen(queuefilter);
      if ((len > 6) && (strncmp(queuefilter, "album:", 6) == 0))
	{
	  qp.type = Q_ITEMS;
	  ret = safe_atoi64(strchr(queuefilter, ':') + 1, &albumid);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Invalid album id in queuefilter: %s\n", queuefilter);

	      return -1;
	    }
	  snprintf(buf, sizeof(buf), "f.songalbumid = %" PRIi64, albumid);
	  qp.filter = strdup(buf);
	}
      else if ((len > 7) && (strncmp(queuefilter, "artist:", 7) == 0))
	{
	  qp.type = Q_ITEMS;
	  ret = safe_atoi64(strchr(queuefilter, ':') + 1, &artistid);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Invalid artist id in queuefilter: %s\n", queuefilter);

	      return -1;
	    }
	  snprintf(buf, sizeof(buf), "f.songartistid = %" PRIi64, artistid);
	  qp.filter = strdup(buf);
	}
      else if ((len > 9) && (strncmp(queuefilter, "playlist:", 9) == 0))
	{
	  qp.type = Q_PLITEMS;
	  ret = safe_atoi32(strchr(queuefilter, ':') + 1, &plid);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_PLAYER, "Invalid playlist id in queuefilter: %s\n", queuefilter);

	      return -1;
	    }
	  qp.id = plid;
	  qp.filter = strdup("1 = 1");
	}
      else if ((len > 6) && (strncmp(queuefilter, "genre:", 6) == 0))
	{
	  qp.type = Q_ITEMS;
	  s = db_escape_string(queuefilter + 6);
	  if (!s)
	    return -1;
	  snprintf(buf, sizeof(buf), "f.genre = '%s'", s);
	  qp.filter = strdup(buf);
	}
      else
	{
	  DPRINTF(E_LOG, L_PLAYER, "Unknown queuefilter %s\n", queuefilter);

	  // If the queuefilter is unkown, ignore it and use the query parameter instead to build the sql query
	  id = 0;
	  qp.type = Q_ITEMS;
	  qp.filter = daap_query_parse_sql(query);
	}
    }
  else
    {
      id = 0;
      qp.type = Q_ITEMS;
      qp.filter = daap_query_parse_sql(query);
    }

  ps = player_queue_make(&qp, sort);

  if (qp.filter)
    free(qp.filter);

  if (ps)
    *head = ps;
  else
    return -1;

  idx = 0;
  while (id && ps && ps->pl_next && (ps->id != id) && (ps->pl_next != *head))
    {
      idx++;
      ps = ps->pl_next;
    }

  return idx;
}

struct player_source *
player_queue_make_pl(int plid, uint32_t *id)
{
  struct query_params qp;
  struct player_source *ps;
  struct player_source *p;
  uint32_t i;
  char buf[124];

  memset(&qp, 0, sizeof(struct query_params));

  if (plid)
    {
      qp.id = plid;
      qp.type = Q_PLITEMS;
      qp.offset = 0;
      qp.limit = 0;
      qp.sort = S_NONE;
    }
  else if (*id)
    {
      qp.id = 0;
      qp.type = Q_ITEMS;
      qp.offset = 0;
      qp.limit = 0;
      qp.sort = S_NONE;
      snprintf(buf, sizeof(buf), "f.id = %" PRIu32, *id);
      qp.filter = strdup(buf);
    }
  else
    return NULL;

  ps = player_queue_make(&qp, NULL);

  if (qp.filter)
    free(qp.filter);

  /* Shortcut for shuffled playlist */
  if (*id == 0)
    return ps;

  p = ps;
  i = 0;
  do
    {
      if (p->id == *id)
	{
	  *id = i;
	  break;
	}

      p = p->pl_next;
      i++;
    }
  while (p != ps);

  return ps;
}

struct player_source *
player_queue_make_mpd(char *path, int recursive)
{
  struct query_params qp;
  struct player_source *ps;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_ITEMS;
  qp.idx_type = I_NONE;
  qp.sort = S_ALBUM;

  if (recursive)
    {
      qp.filter = sqlite3_mprintf("f.virtual_path LIKE '/%q%%'", path);
      if (!qp.filter)
	DPRINTF(E_DBG, L_PLAYER, "Out of memory\n");
    }
  else
    {
      qp.filter = sqlite3_mprintf("f.virtual_path LIKE '/%q'", path);
      if (!qp.filter)
	DPRINTF(E_DBG, L_PLAYER, "Out of memory\n");
    }

  ps = player_queue_make(&qp, NULL);

  sqlite3_free(qp.filter);
  return ps;
}

static void
source_free(struct player_source *ps)
{
  switch (ps->type)
    {
      case SOURCE_FILE:
      case SOURCE_HTTP:
	if (ps->ctx)
	  transcode_cleanup(ps->ctx);
	break;

      case SOURCE_SPOTIFY:
#ifdef HAVE_SPOTIFY_H
	spotify_playback_stop();
#endif
	break;

      case SOURCE_PIPE:
	pipe_cleanup();
	break;
    }

  free(ps);
}

static void
source_stop(struct player_source *ps)
{
  struct player_source *tmp;

  while (ps)
    {
      switch (ps->type)
	{
	  case SOURCE_FILE:
	  case SOURCE_HTTP:
	    if (ps->ctx)
	      {
		transcode_cleanup(ps->ctx);
		ps->ctx = NULL;
	      }
	    break;

          case SOURCE_SPOTIFY:
#ifdef HAVE_SPOTIFY_H
	    spotify_playback_stop();
#endif
	    break;

	  case SOURCE_PIPE:
	    pipe_cleanup();
	    break;
        }

      tmp = ps;
      ps = ps->play_next;

      tmp->play_next = NULL;
    }
}

/*
 * Shuffles the items between head and tail (excluding head and tail)
 */
static void
source_shuffle(struct player_source *head, struct player_source *tail)
{
  struct player_source *ps;
  struct player_source **ps_array;
  int nitems;
  int i;

  if (!head)
    return;

  if (!tail)
    return;

  if (!shuffle)
    {
      ps = head;
      do
	{
	  ps->shuffle_next = ps->pl_next;
	  ps->shuffle_prev = ps->pl_prev;
	  ps = ps->pl_next;
	}
      while (ps != head);
    }

  // Count items in queue (excluding head and tail)
  ps = head->shuffle_next;
  if (!cur_streaming)
    nitems = 1;
  else
    nitems = 0;
  while (ps != tail)
    {
      nitems++;
      ps = ps->shuffle_next;
    }

  // Do not reshuffle queue with one item
  if (nitems < 1)
    return;

  // Construct array for number of items in queue
  ps_array = (struct player_source **)malloc(nitems * sizeof(struct player_source *));
  if (!ps_array)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate memory for shuffle array\n");
      return;
    }

  // Fill array with items in queue (excluding head and tail)
  if (cur_streaming)
    ps = head->shuffle_next;
  else
    ps = head;
  i = 0;
  do
    {
      ps_array[i] = ps;

      ps = ps->shuffle_next;
      i++;
    }
  while (ps != tail);

  shuffle_ptr(&shuffle_rng, (void **)ps_array, nitems);

  for (i = 0; i < nitems; i++)
    {
      ps = ps_array[i];

      if (i > 0)
	ps->shuffle_prev = ps_array[i - 1];

      if (i < (nitems - 1))
	ps->shuffle_next = ps_array[i + 1];
    }

  // Insert shuffled items between head and tail
  if (cur_streaming)
    {
      ps_array[0]->shuffle_prev = head;
      ps_array[nitems - 1]->shuffle_next = tail;
      head->shuffle_next = ps_array[0];
      tail->shuffle_prev = ps_array[nitems - 1];
    }
  else
    {
      ps_array[0]->shuffle_prev = ps_array[nitems - 1];
      ps_array[nitems - 1]->shuffle_next = ps_array[0];
      shuffle_head = ps_array[0];
    }

  free(ps_array);

  return;
}

static void
source_reshuffle(void)
{
  struct player_source *head;
  struct player_source *tail;

  if (cur_streaming)
    head = cur_streaming;
  else if (shuffle)
    head = shuffle_head;
  else
    head = source_head;

  if (repeat == REPEAT_ALL)
    tail = head;
  else if (shuffle)
    tail = shuffle_head;
  else
    tail = source_head;

  source_shuffle(head, tail);

  if (repeat == REPEAT_ALL)
    shuffle_head = head;
}

/* Helper */
static int
source_open(struct player_source *ps, int no_md)
{
  struct media_file_info *mfi;
  char *url;
  int ret;

  ps->setup_done = 0;
  ps->stream_start = 0;
  ps->output_start = 0;
  ps->end = 0;
  ps->play_next = NULL;

  mfi = db_file_fetch_byid(ps->id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't fetch file id %d\n", ps->id);

      return -1;
    }

  if (mfi->disabled)
    {
      DPRINTF(E_DBG, L_PLAYER, "File id %d is disabled, skipping\n", ps->id);

      free_mfi(mfi, 0);
      return -1;
    }

  DPRINTF(E_INFO, L_PLAYER, "Opening '%s' (%s)\n", mfi->title, mfi->path);

  // Setup the source type responsible for getting the audio
  switch (mfi->data_kind)
    {
      case DATA_KIND_URL:
	ps->type = SOURCE_HTTP;

	ret = http_stream_setup(&url, mfi->path);
	if (ret < 0)
	  break;

	free(mfi->path);
	mfi->path = url;

	ret = transcode_setup(&ps->ctx, mfi, NULL, 0);
	break;

      case DATA_KIND_SPOTIFY:
	ps->type = SOURCE_SPOTIFY;
#ifdef HAVE_SPOTIFY_H
	ret = spotify_playback_play(mfi);
#else
	ret = -1;
#endif
	break;

      case DATA_KIND_PIPE:
	ps->type = SOURCE_PIPE;
	ret = pipe_setup(mfi);
	break;

      default:
	ps->type = SOURCE_FILE;
	ret = transcode_setup(&ps->ctx, mfi, NULL, 0);
    }

  free_mfi(mfi, 0);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not open file id %d\n", ps->id);

      return -1;
    }

  if (!no_md)
    metadata_trigger(ps, (player_state == PLAY_PLAYING) ? 0 : 1);

  ps->setup_done = 1;

  return 0;
}

static int
source_next(int force)
{
  struct player_source *ps;
  struct player_source *head;
  struct player_source *limit;
  enum repeat_mode r_mode;
  int ret;

  head = (shuffle) ? shuffle_head : source_head;
  limit = head;
  r_mode = repeat;

  /* Force repeat mode at user request */
  if (force && (r_mode == REPEAT_SONG))
    r_mode = REPEAT_ALL;

  /* Playlist has only one file, treat REPEAT_ALL as REPEAT_SONG */
  if ((r_mode == REPEAT_ALL) && (source_head == source_head->pl_next))
    r_mode = REPEAT_SONG;
  /* Playlist has only one file, not a user action, treat as REPEAT_ALL
   * and source_check() will stop playback
   */
  else if (!force && (r_mode == REPEAT_OFF) && (source_head == source_head->pl_next))
    r_mode = REPEAT_SONG;

  if (!cur_streaming)
    ps = head;
  else
    ps = (shuffle) ? cur_streaming->shuffle_next : cur_streaming->pl_next;

  switch (r_mode)
    {
      case REPEAT_SONG:
	if (!cur_streaming)
	  break;

	if ((cur_streaming->type == SOURCE_FILE) && cur_streaming->ctx)
	  {
	    ret = transcode_seek(cur_streaming->ctx, 0);

	    /* source_open() takes care of sending metadata, but we don't
	     * call it when repeating a song as we just seek back to 0
	     * so we have to handle metadata ourselves here
	     */
	    if (ret >= 0)
	      metadata_trigger(cur_streaming, 0);
	  }
	else
	  ret = source_open(cur_streaming, force);

	if (ret < 0)
	  {
	    DPRINTF(E_LOG, L_PLAYER, "Failed to restart song for song repeat\n");

	    return -1;
	  }

	return 0;

      case REPEAT_ALL:
	if (!shuffle)
	  {
	    limit = ps;
	    break;
	  }

	/* Reshuffle before repeating playlist */
	if (cur_streaming && (ps == shuffle_head))
	  {
	    source_reshuffle();
	    ps = shuffle_head;
	  }

	limit = shuffle_head;

	break;

      case REPEAT_OFF:
	limit = head;

	if (force && (ps == limit))
	  {
	    DPRINTF(E_DBG, L_PLAYER, "End of playlist reached and repeat is OFF\n");

	    playback_abort();
	    return 0;
	  }
	break;
    }

  do
    {
      ret = source_open(ps, force);
      if (ret < 0)
	{
	  if (shuffle)
	    ps = ps->shuffle_next;
	  else
	    ps = ps->pl_next;

	  continue;
	}

      break;
    }
  while (ps != limit);

  /* Couldn't open any of the files in our queue */
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Could not open any file in the queue (next)\n");

      return -1;
    }

  if (!force && cur_streaming)
    cur_streaming->play_next = ps;

  cur_streaming = ps;

  return 0;
}

static int
source_prev(void)
{
  struct player_source *ps;
  struct player_source *head;
  struct player_source *limit;
  int ret;

  if (!cur_streaming)
    return -1;

  head = (shuffle) ? shuffle_head : source_head;
  ps = (shuffle) ? cur_streaming->shuffle_prev : cur_streaming->pl_prev;
  limit = ps;

  if ((repeat == REPEAT_OFF) && (cur_streaming == head))
    {
      DPRINTF(E_DBG, L_PLAYER, "Start of playlist reached and repeat is OFF\n");

      playback_abort();
      return 0;
    }

  /* We are not reshuffling on prev calls in the shuffle case - should we? */

  do
    {
      ret = source_open(ps, 1);
      if (ret < 0)
	{
	  if (shuffle)
	    ps = ps->shuffle_prev;
	  else
	    ps = ps->pl_prev;

	  continue;
	}

      break;
    }
  while (ps != limit);

  /* Couldn't open any of the files in our queue */
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_PLAYER, "Could not open any file in the queue (prev)\n");

      return -1;
    }

  cur_streaming = ps;

  return 0;
}

/*
 * Returns the position of the given song (ps) in the playqueue or shufflequeue.
 * First song in the queue has position 0. Depending on the 'shuffle' argument,
 * the position is either determined in the playqueue or shufflequeue.
 *
 * @param ps the song to search in the queue
 * @param shuffle 0 search in the playqueue, 1 search in the shufflequeue
 * @return position 0-based in the queue
 */
static int
source_position(struct player_source *ps, char shuffle)
{
  struct player_source *p;
  int ret;

  ret = 0;
  for (p = (shuffle ? shuffle_head : source_head); p != ps; p = (shuffle ? p->shuffle_next : p->pl_next))
    ret++;

  return ret;
}

static uint32_t
source_count()
{
  struct player_source *ps;
  uint32_t ret;

  ret = 0;

  if (source_head)
    {
      ret++;
      for (ps = source_head->pl_next; ps != source_head; ps = ps->pl_next)
	ret++;
    }

  return ret;
}

static uint64_t
source_check(void)
{
  struct timespec ts;
  struct player_source *ps;
  struct player_source *head;
  uint64_t pos;
  enum repeat_mode r_mode;
  int i;
  int id;
  int ret;

  if (!cur_streaming)
    return 0;

  ret = player_get_current_pos(&pos, &ts, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get current playback position\n");

      return 0;
    }

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

  if ((cur_playing->end == 0) || (pos < cur_playing->end))
    return pos;

  r_mode = repeat;
  /* Playlist has only one file, treat REPEAT_ALL as REPEAT_SONG */
  if ((r_mode == REPEAT_ALL) && (source_head == source_head->pl_next))
    r_mode = REPEAT_SONG;

  if (r_mode == REPEAT_SONG)
    {
      ps = cur_playing;

      /* Check that we haven't gone to the next file already
       * (repeat song toggled in the last 2 seconds of a song)
       */
      if (cur_playing->play_next)
	{
	  cur_playing = cur_playing->play_next;

	  if (ps->setup_done)
	    {
	      if ((ps->type == SOURCE_FILE) && ps->ctx)
		{
	          transcode_cleanup(ps->ctx);
	          ps->ctx = NULL;
		}
	      ps->play_next = NULL;
	    }
        }

      cur_playing->stream_start = ps->end + 1;
      cur_playing->output_start = cur_playing->stream_start;

      /* Do not use cur_playing to reset the end position, it may have changed */
      ps->end = 0;

      status_update(PLAY_PLAYING);

      metadata_prune(pos);

      return pos;
    }

  head = (shuffle) ? shuffle_head : source_head;

  i = 0;
  while (cur_playing && (cur_playing->end != 0) && (pos > cur_playing->end))
    {
      i++;

      id = (int)cur_playing->id;
      worker_execute(playcount_inc_cb, &id, sizeof(int), 5);
#ifdef LASTFM
      lastfm_scrobble(id);
#endif

      /* Stop playback if:
       * - at end of playlist (NULL)
       * - repeat OFF and at end of playlist (wraparound)
       */
      if (!cur_playing->play_next
	  || ((r_mode == REPEAT_OFF) && (cur_playing->play_next == head)))
	{
	  playback_abort();

	  return pos;
        }

      ps = cur_playing;
      cur_playing = cur_playing->play_next;

      cur_playing->stream_start = ps->end + 1;
      cur_playing->output_start = cur_playing->stream_start;

      if (ps->setup_done)
	{
	  if ((ps->type == SOURCE_FILE) && ps->ctx)
	    {
	      transcode_cleanup(ps->ctx);
	      ps->ctx = NULL;
	    }
	  ps->play_next = NULL;
	}
    }

  if (i > 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Playback switched to next song\n");

      status_update(PLAY_PLAYING);

      metadata_prune(pos);
    }

  return pos;
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
history_add(uint32_t id)
{
  unsigned int cur_index;
  unsigned int next_index;
  
  /* Check if the current song is already the last in the history to avoid duplicates */
  cur_index = (history->start_index + history->count - 1) % MAX_HISTORY_COUNT;
  if (id == history->id[cur_index])
    {
      DPRINTF(E_LOG, L_PLAYER, "Current playing/streaming song already in history\n");
      return;
    }

  /* Calculate the next index and update the start-index and count for the id-buffer */
  next_index = (history->start_index + history->count) % MAX_HISTORY_COUNT;
  if (next_index == history->start_index && history->count > 0)
    history->start_index = (history->start_index + 1) % MAX_HISTORY_COUNT;

  history->id[next_index] = id;

  if (history->count < MAX_HISTORY_COUNT)
    history->count++;
}

static int
source_read(uint8_t *buf, int len, uint64_t rtptime)
{
  int new;
  int ret;
  int nbytes;
  int icy_timer;

  if (!cur_streaming)
    return 0;

  nbytes = 0;
  new = 0;
  while (nbytes < len)
    {
      if (new)
	{
	  DPRINTF(E_DBG, L_PLAYER, "New file\n");

	  new = 0;

	  // add song to the played history
	  history_add(cur_streaming->id);

	  ret = source_next(0);
	  if (ret < 0)
	    return -1;
	}

      if (evbuffer_get_length(audio_buf) == 0)
	{
	  switch (cur_streaming->type)
	    {
	      case SOURCE_HTTP:
		ret = transcode(cur_streaming->ctx, audio_buf, len - nbytes, &icy_timer);

		if (icy_timer)
		  metadata_check_icy();
		break;

	      case SOURCE_FILE:
		ret = transcode(cur_streaming->ctx, audio_buf, len - nbytes, &icy_timer);
		break;

#ifdef HAVE_SPOTIFY_H
	      case SOURCE_SPOTIFY:
		ret = spotify_audio_get(audio_buf, len - nbytes);
		break;
#endif

	      case SOURCE_PIPE:
		ret = pipe_audio_get(audio_buf, len - nbytes);
		break;

	      default:
		ret = -1;
	    }
	    
	  if (ret <= 0)
	    {
	      /* EOF or error */
	      cur_streaming->end = rtptime + BTOS(nbytes) - 1;

	      new = 1;
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
	  rd->volume = (master_volume >= 0) ? master_volume : 75;
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

  if (cur_playing)
    source_stop(cur_playing);
  else
    source_stop(cur_streaming);

  queue_clear(NULL);

  cur_playing = NULL;
  cur_streaming = NULL;

  evbuffer_drain(audio_buf, evbuffer_get_length(audio_buf));

  status_update(PLAY_STOPPED);

  metadata_purge();
}

static struct player_source *
next_ps(struct player_source *ps, char shuffle)
{
  if (shuffle)
    return ps->shuffle_next;
  else
    return ps->pl_next;
}

/* Actual commands, executed in the player thread */
static int
get_status(struct player_command *cmd)
{
  struct timespec ts;
  struct player_source *ps;
  struct player_status *status;
  uint64_t pos;
  int ret;

  status = cmd->arg.status;

  memset(status, 0, sizeof(struct player_status));

  status->shuffle = shuffle;
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

	pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - cur_streaming->stream_start;
	status->pos_ms = (pos * 1000) / 44100;
	status->len_ms = cur_streaming->len_ms;

	status->pos_pl = source_position(cur_streaming, 0);

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
	status->pos_pl = source_position(ps, 0);

	ps = next_ps(ps, shuffle);
	status->next_id = ps->id;
	status->next_pos_pl = source_position(ps, 0);

	status->playlistlength = source_count();
	break;
    }

  return 0;
}

static int
now_playing(struct player_command *cmd)
{
  uint32_t *id;

  id = cmd->arg.id_ptr;

  if (cur_playing)
    *id = cur_playing->id;
  else if (cur_streaming)
    *id = cur_streaming->id;
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
  if (!ps->ctx || ps->type != SOURCE_HTTP || ps->id != cmd->arg.icy.id)
    return -1;

  transcode_metadata_artwork_url(ps->ctx, &cmd->arg.icy.artwork_url);

  return 0;
}

static int
playback_stop(struct player_command *cmd)
{
  if (laudio_status != LAUDIO_CLOSED)
    laudio_close();

  /* We may be restarting very soon, so we don't bring the devices to a
   * full stop just yet; this saves time when restarting, which is nicer
   * for the user.
   */
  cmd->raop_pending = raop_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  pb_timer_stop();

  if (cur_playing)
    {
      history_add(cur_playing->id);
      source_stop(cur_playing);
    }
  else if (cur_streaming)
    {
      history_add(cur_streaming->id);
      source_stop(cur_streaming);
    }

  cur_playing = NULL;
  cur_streaming = NULL;

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

static struct player_source *
queue_get_source_byid(uint32_t id)
{
  struct player_source *ps;

  if (!source_head)
    return NULL;

  ps = source_head->pl_next;
  while (ps->id != id && ps != source_head)
    {
      ps = ps->pl_next;
    }

  return ps;
}

static struct player_source *
queue_get_source_bypos(int pos)
{
  struct player_source *ps;
  int i;

  if (!source_head)
    return NULL;

  ps = source_head;
  for (i = pos; i > 0; i--)
    ps = ps->pl_next;

  return ps;
}

static int
playback_start(struct player_command *cmd)
{
  struct raop_device *rd;
  uint32_t *idx_id;
  struct player_source *ps;
  int ret;

  if (!source_head)
    {
      DPRINTF(E_LOG, L_PLAYER, "Nothing to play!\n");

      return -1;
    }

  idx_id = cmd->arg.item_range.id_ptr;

  if (player_state == PLAY_PLAYING)
    {
      /*
       * If player is already playing a song, only return current playing song id
       * and do not change player state (ignores given arguments for playing a
       * specified song by pos or id).
       */
      if (idx_id)
	{
	  if (cur_playing)
	    *idx_id = cur_playing->id;
	  else
	    *idx_id = cur_streaming->id;
	}

      status_update(player_state);

      return 0;
    }

  // Update global playback position
  pb_pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - 88200;

  /*
   * If either an item id or an item position is given, get the corresponding
   * player_source from the queue.
   */
  if (cmd->arg.item_range.type == RANGEARG_ID)
    ps = queue_get_source_byid(cmd->arg.item_range.id);
  else if (cmd->arg.item_range.type == RANGEARG_POS)
    ps = queue_get_source_bypos(cmd->arg.item_range.start_pos);
  else
    ps = NULL;

  /*
   * Update queue and cur_streaming depending on
   * - given player_source to start playing
   * - player state
   */
  if (ps)
    {
      /*
       * A song is specified in the arguments (by id or pos) and the corresponding
       * player_source (ps) from the queue was found.
       *
       * Stop playback (if it was paused) and prepare to start playback on ps.
       */
      if (cur_playing)
	source_stop(cur_playing);
      else if (cur_streaming)
	source_stop(cur_streaming);

      cur_playing = NULL;
      cur_streaming = NULL;

      if (shuffle)
	{
	  source_reshuffle();
	  cur_streaming = shuffle_head;
	}
      else
	cur_streaming = ps;

      ret = source_open(cur_streaming, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Couldn't jump to source %d in queue\n", cur_streaming->id);

	  playback_abort();
	  return -1;
	}

      if (idx_id)
	*idx_id = cur_streaming->id;

      cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
      cur_streaming->output_start = cur_streaming->stream_start;
    }
  else if (!cur_streaming)
    {
      /*
       * Player was stopped, start playing the queue
       */
      if (shuffle)
	source_reshuffle();

      ret = source_next(0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Couldn't find anything to play!\n");

	  playback_abort();
	  return -1;
	}

      cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
      cur_streaming->output_start = cur_streaming->stream_start;
    }
  else
    {
      /*
       * Player was paused, resume playing cur_streaming
       *
       * After a pause, the source is still open so source_open() doesn't get
       * called and we have to handle metadata ourselves.
       */
      metadata_trigger(cur_streaming, 1);
    }

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
playback_prev_bh(struct player_command *cmd)
{
  int ret;
  int pos_sec;

  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      return -1;
    }

  /* Only add to history if playback started. */
  if (cur_streaming->end > cur_streaming->stream_start)
    history_add(cur_streaming->id);

  source_stop(cur_streaming);

  /* Compute the playing time in seconds for the current song. */
  if (cur_streaming->end > cur_streaming->stream_start)
    pos_sec = (cur_streaming->end - cur_streaming->stream_start) / 44100;
  else
    pos_sec = 0;

  /* Only skip to the previous song if the playing time is less than 3 seconds,
   otherwise restart the current song. */
  DPRINTF(E_DBG, L_PLAYER, "Skipping song played %d sec\n", pos_sec);
  if (pos_sec < 3)
    {
      ret = source_prev();
      if (ret < 0)
	{
	  playback_abort();

	  return -1;
	}
    }
  else
    {
      ret = source_open(cur_streaming, 1);
      if (ret < 0)
	{
	  playback_abort();

	  return -1;
	}
    }

  if (player_state == PLAY_STOPPED)
    return -1;

  cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
  cur_streaming->output_start = cur_streaming->stream_start;

  cur_playing = NULL;

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}


static int
playback_next_bh(struct player_command *cmd)
{
  int ret;

  if (!cur_streaming)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not get current stream source\n");
      return -1;
    }

  /* Only add to history if playback started. */
  if (cur_streaming->end > cur_streaming->stream_start)
    history_add(cur_streaming->id);

  source_stop(cur_streaming);

  ret = source_next(1);
  if (ret < 0)
    {
      playback_abort();

      return -1;
    }

  if (player_state == PLAY_STOPPED)
    return -1;

  cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
  cur_streaming->output_start = cur_streaming->stream_start;

  cur_playing = NULL;

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}

static int
playback_seek_bh(struct player_command *cmd)
{
  struct player_source *ps;
  int ms;
  int ret;

  ms = cmd->arg.intval;

  if (cur_playing)
    ps = cur_playing;
  else
    ps = cur_streaming;

  ps->end = 0;

  /* Seek to commanded position */
  switch (ps->type)
    {
      case SOURCE_FILE:
	ret = transcode_seek(ps->ctx, ms);
	break;
#ifdef HAVE_SPOTIFY_H
      case SOURCE_SPOTIFY:
	ret = spotify_playback_seek(ms);
	break;
#endif
      case SOURCE_PIPE:
      case SOURCE_HTTP:
	ret = 1;
	break;

      default:
	ret = -1;
    }

  if (ret < 0)
    {
      playback_abort();

      return -1;
    }

  /* Adjust start_pos for the new position */
  ps->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - ((uint64_t)ret * 44100) / 1000;
  ps->output_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;

  cur_streaming = ps;
  cur_playing = NULL;

  /* Silent status change - playback_start() sends the real status update */
  player_state = PLAY_PAUSED;

  return 0;
}

static int
playback_pause_bh(struct player_command *cmd)
{
  struct player_source *ps;
  uint64_t pos;
  int ms;
  int ret;

  if (cur_playing)
    ps = cur_playing;
  else
    ps = cur_streaming;

  pos = ps->end;
  ps->end = 0;

  /* Seek back to current playback position */
  pos -= ps->stream_start;
  ms = (int)((pos * 1000) / 44100);

  switch (ps->type)
    {
      case SOURCE_FILE:
	ret = transcode_seek(ps->ctx, ms);
	break;
#ifdef HAVE_SPOTIFY_H
      case SOURCE_SPOTIFY:
	ret = spotify_playback_seek(ms);
	break;
#endif
      default:
	ret = -1;
    }

  if (ret < 0)
    {
      playback_abort();

      return -1;
    }

  /* Adjust start_pos to take into account the pause and seek back */
  ps->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - ((uint64_t)ret * 44100) / 1000;
  ps->output_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;

  cur_streaming = ps;
  cur_playing = NULL;

  status_update(PLAY_PAUSED);

  return 0;
}

static int
playback_pause(struct player_command *cmd)
{
  struct player_source *ps;
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

  if (cur_playing)
    ps = cur_playing;
  else
    ps = cur_streaming;

  /* Store pause position */
  ps->end = pos;

  cmd->raop_pending = raop_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  if (laudio_status != LAUDIO_CLOSED)
    laudio_stop();

  pb_timer_stop();

  if (ps->play_next)
    source_stop(ps->play_next);

  cur_playing = NULL;
  cur_streaming = ps;
  cur_streaming->play_next = NULL;

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

  if (cmd->raop_pending > 0)
    return 1; /* async */

  return 0;
}

static int
repeat_set(struct player_command *cmd)
{
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

  return 0;
}

static int
shuffle_set(struct player_command *cmd)
{
  switch (cmd->arg.intval)
    {
      case 1:
	if (!shuffle)
	  source_reshuffle();
	/* FALLTHROUGH*/
      case 0:
	shuffle = cmd->arg.intval;
	break;

      default:
	DPRINTF(E_LOG, L_PLAYER, "Invalid shuffle mode: %d\n", cmd->arg.intval);
	return -1;
    }

  return 0;
}

static unsigned int
queue_count()
{
  struct player_source *ps;
  int count;

  if (!source_head)
    return 0;

  count = 1;
  ps = source_head->pl_next;
  while (ps != source_head)
    {
      count++;
      ps = ps->pl_next;
    }

  return count;
}

static int
queue_get(struct player_command *cmd)
{
  int start_pos;
  int end_pos;
  struct player_queue *queue;
  uint32_t *ids;
  unsigned int qlength;
  unsigned int count;
  struct player_source *ps;
  int i;
  int pos;
  char qshuffle;

  queue = malloc(sizeof(struct player_queue));

  qlength = queue_count();
  qshuffle = cmd->arg.item_range.shuffle;

  start_pos = cmd->arg.item_range.start_pos;
  if (start_pos < 0)
    {
      // Set start_pos to the position of the current item + 1
      ps = cur_playing ? cur_playing : cur_streaming;
      start_pos = ps ? source_position(ps, qshuffle) + 1 : 0;
    }

  end_pos = cmd->arg.item_range.end_pos;
  if (cmd->arg.item_range.start_pos < 0)
    end_pos += start_pos;
  if (end_pos <= 0 || end_pos > qlength)
    end_pos = qlength;

  if (end_pos > start_pos)
    count = end_pos - start_pos;
  else
    count = 0;

  ids = malloc(count * sizeof(uint32_t));

  pos = 0;
  ps = qshuffle ? shuffle_head : source_head;
  for (i = 0; i < end_pos; i++)
    {
      if (i >= start_pos)
	{
	  ids[pos] = ps->id;
	  pos++;
	}

	ps = qshuffle ? ps->shuffle_next : ps->pl_next;
    }

  queue->start_pos = start_pos;
  queue->count = count;
  queue->queue = ids;

  queue->length = qlength;
  queue->playingid = 0;
  if (cur_playing)
    queue->playingid = cur_playing->id;
  else if (cur_streaming)
    queue->playingid = cur_streaming->id;

  cmd->queue = queue;

  return 0;
}

void
queue_free(struct player_queue *queue)
{
  free(queue->queue);
  free(queue);
}

static int
queue_add(struct player_command *cmd)
{
  struct player_source *ps;
  struct player_source *ps_shuffle;
  struct player_source *source_tail;
  struct player_source *ps_tail;

  ps = cmd->arg.ps;
  ps_shuffle = ps;

  if (source_head)
    {
      /* Playlist order */
      source_tail = source_head->pl_prev;
      ps_tail = ps->pl_prev;

      source_tail->pl_next = ps;
      ps_tail->pl_next = source_head;

      source_head->pl_prev = ps_tail;
      ps->pl_prev = source_tail;

      /* Shuffle */
      source_tail = shuffle_head->shuffle_prev;
      ps_tail = ps_shuffle->shuffle_prev;

      source_tail->shuffle_next = ps_shuffle;
      ps_tail->shuffle_next = shuffle_head;

      shuffle_head->shuffle_prev = ps_tail;
      ps_shuffle->shuffle_prev = source_tail;
    }
  else
    {
      source_head = ps;
      shuffle_head = ps_shuffle;
    }

  if (shuffle)
    source_reshuffle();

  if (cur_plid != 0)
    cur_plid = 0;

  return 0;
}

static int
queue_add_next(struct player_command *cmd)
{
  struct player_source *ps;
  struct player_source *ps_shuffle;
  struct player_source *ps_playing;

  ps = cmd->arg.ps;
  ps_shuffle = ps;

  if (source_head && cur_streaming)
  {
    ps_playing = cur_streaming;

    // Insert ps after ps_playing
    ps->pl_prev->pl_next = ps_playing->pl_next;
    ps_playing->pl_next->pl_prev = ps->pl_prev;
    ps->pl_prev = ps_playing;
    ps_playing->pl_next = ps;

    // Insert ps_shuffle after ps_playing
    ps_shuffle->shuffle_prev->shuffle_next = ps_playing->shuffle_next;
    ps_playing->shuffle_next->shuffle_prev = ps_shuffle->shuffle_prev;
    ps_shuffle->shuffle_prev = ps_playing;
    ps_playing->shuffle_next = ps_shuffle;
  }
  else
  {
    source_head = ps;
    shuffle_head = ps_shuffle;
  }

  if (shuffle)
    source_reshuffle();

  if (cur_plid != 0)
    cur_plid = 0;

  return 0;
}

static int
queue_move(struct player_command *cmd)
{
  struct player_source *ps;
  struct player_source *ps_src;
  struct player_source *ps_dst;
  int pos_max;
  int i;

  DPRINTF(E_DBG, L_PLAYER, "Moving song from position %d to be the next song after %d\n", cmd->arg.ps_pos[0],
      cmd->arg.ps_pos[1]);

  ps = cur_playing ? cur_playing : cur_streaming;
  if (!ps)
  {
    DPRINTF(E_LOG, L_PLAYER, "Current playing/streaming song not found\n");
    return -1;
  }

  pos_max = MAX(cmd->arg.ps_pos[0], cmd->arg.ps_pos[1]);
  ps_src = NULL;
  ps_dst = NULL;

  for (i = 0; i <= pos_max; i++)
  {
    if (i == cmd->arg.ps_pos[0])
      ps_src = ps;
    if (i == cmd->arg.ps_pos[1])
      ps_dst = ps;

    ps = shuffle ? ps->shuffle_next : ps->pl_next;
  }

  if (!ps_src || !ps_dst || (ps_src == ps_dst))
  {
    DPRINTF(E_LOG, L_PLAYER, "Invalid source and/or destination for queue_move\n");
    return -1;
  }

  if (shuffle)
  {
    // Remove ps_src from shuffle queue
    ps_src->shuffle_prev->shuffle_next = ps_src->shuffle_next;
    ps_src->shuffle_next->shuffle_prev = ps_src->shuffle_prev;

    // Insert after ps_dst
    ps_src->shuffle_prev = ps_dst;
    ps_src->shuffle_next = ps_dst->shuffle_next;
    ps_dst->shuffle_next->shuffle_prev = ps_src;
    ps_dst->shuffle_next = ps_src;
  }
  else
  {
    // Remove ps_src from queue
    ps_src->pl_prev->pl_next = ps_src->pl_next;
    ps_src->pl_next->pl_prev = ps_src->pl_prev;

    // Insert after ps_dst
    ps_src->pl_prev = ps_dst;
    ps_src->pl_next = ps_dst->pl_next;
    ps_dst->pl_next->pl_prev = ps_src;
    ps_dst->pl_next = ps_src;
  }

  return 0;
}

static int
queue_remove(struct player_command *cmd)
{
  struct player_source *ps;
  uint32_t pos;
  uint32_t id;
  int i;

  ps = cur_playing ? cur_playing : cur_streaming;
  if (!ps)
  {
    DPRINTF(E_LOG, L_PLAYER, "Current playing/streaming item not found\n");
    return -1;
  }

  if (cmd->arg.item_range.type == RANGEARG_ID)
    {
      id = cmd->arg.item_range.id;
      DPRINTF(E_DBG, L_PLAYER, "Removing item with id %d\n", id);

      if (id < 1)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Can't remove item, invalid id %d\n", id);
	  return -1;
	}
      else if (id == ps->id)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Can't remove current playing item, id %d\n", id);
	  return -1;
	}

      ps = source_head->pl_next;
      while (ps->id != id && ps != source_head)
	{
	  ps = ps->pl_next;
	}
    }
  else
    {
      pos = cmd->arg.item_range.start_pos;
      DPRINTF(E_DBG, L_PLAYER, "Removing item from position %d\n", pos);

      if (pos < 1)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Can't remove item, invalid position %d\n", pos);
	  return -1;
	}

      for (i = 0; i < pos; i++)
	{
	  ps = shuffle ? ps->shuffle_next : ps->pl_next;
	}
    }

  ps->shuffle_prev->shuffle_next = ps->shuffle_next;
  ps->shuffle_next->shuffle_prev = ps->shuffle_prev;

  ps->pl_prev->pl_next = ps->pl_next;
  ps->pl_next->pl_prev = ps->pl_prev;

  source_free(ps);

  return 0;
}

/*
 * queue_clear removes all items from the playqueue, playback must be stopped before calling queue_clear
 */
static int
queue_clear(struct player_command *cmd)
{
  struct player_source *ps;

  if (!source_head)
    return 0;

  shuffle_head = NULL;
  source_head->pl_prev->pl_next = NULL;

  for (ps = source_head; ps; ps = source_head)
    {
      source_head = ps->pl_next;

      source_free(ps);
    }

  cur_plid = 0;

  return 0;
}

/*
 * Depending on cmd->arg.intval queue_empty removes all items from the history (arg.intval = 1),
 * or removes all upcoming songs from the playqueue (arg.intval != 1). After calling queue_empty
 * to remove the upcoming songs, the playqueue will only contain the current playing song.
 */
static int
queue_empty(struct player_command *cmd)
{
  int clear_hist;
  struct player_source *ps;

  clear_hist = cmd->arg.intval;
  if (clear_hist)
    {
      memset(history, 0, sizeof(struct player_history));
    }
  else
    {
      if (!source_head || !cur_streaming)
	return 0;

      // Stop playback if playing and streaming song are not the same
      if (!cur_playing || cur_playing != cur_streaming)
	{
	  playback_stop(cmd);
	  queue_clear(cmd);
	  return 0;
	}

      // Set head to the current playing song
      shuffle_head = cur_playing;
      source_head = cur_playing;

      // Free all items in the queue except the current playing song
      for (ps = source_head->pl_next; ps != source_head; ps = ps->pl_next)
	{
	  source_free(ps);
	}

      // Make the queue circular again
      source_head->pl_next = source_head;
      source_head->pl_prev = source_head;
      source_head->shuffle_next = source_head;
      source_head->shuffle_prev = source_head;
    }

  return 0;
}

static int
queue_plid(struct player_command *cmd)
{
  if (!source_head)
    return 0;

  cur_plid = cmd->arg.id;

  return 0;
}

static int
set_update_handler(struct player_command *cmd)
{
  update_handler = cmd->arg.status_handler;

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
 * Depending on the player state, this will either resumes playing the current item (player is paused)
 * or begins playing the queue from the beginning.
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item id.
 *
 * @param *itemid if not NULL, will be set to the playing item id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_start(uint32_t *itemid)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start;
  cmd.func_bh = playback_start_bh;
  cmd.arg.item_range.type = RANGEARG_NONE;
  cmd.arg.item_range.id_ptr = itemid;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Starts playback at item number "pos" of the current queue
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item id.
 *
 * @param *itemid if not NULL, will be set to the playing item id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_startpos(int pos, uint32_t *itemid)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start;
  cmd.func_bh = playback_start_bh;
  cmd.arg.item_range.type = RANGEARG_POS;
  cmd.arg.item_range.start_pos = pos;
  cmd.arg.item_range.id_ptr = itemid;
  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

/*
 * Starts playback at item with "id" of the current queue
 *
 * If shuffle is set, the queue is reshuffled prior to starting playback.
 *
 * If a pointer is given as argument "itemid", its value will be set to the playing item id.
 *
 * @param *itemid if not NULL, will be set to the playing item id
 * @return 0 if successful, -1 if an error occurred
 */
int
player_playback_startid(uint32_t id, uint32_t *itemid)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start;
  cmd.func_bh = playback_start_bh;
  cmd.arg.item_range.type = RANGEARG_ID;
  cmd.arg.item_range.id = id;
  cmd.arg.item_range.id_ptr = itemid;
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
 * Retrieves a list of item ids in the queue from postion 'start_pos' to 'end_pos'
 *
 * If start_pos is -1, the list starts with the item next from the current playing item.
 * If end_pos is -1, this list contains all songs starting from 'start_pos'
 *
 * The 'shuffle' argument determines if the items are taken from the playqueue (shuffle = 0)
 * or the shufflequeue (shuffle = 1).
 *
 * @param start_pos Start the listing from 'start_pos'
 * @param end_pos   End the listing at 'end_pos'
 * @param shuffle   If set to 1 use the shuffle queue, otherwise the playqueue
 * @return List of items (ids) in the queue
 */
struct player_queue *
player_queue_get(int start_pos, int end_pos, char shuffle)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_get;
  cmd.func_bh = NULL;
  cmd.arg.item_range.type = RANGEARG_POS;
  cmd.arg.item_range.start_pos = start_pos;
  cmd.arg.item_range.end_pos = end_pos;
  cmd.arg.item_range.shuffle = shuffle;
  cmd.queue = NULL;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  if (ret != 0)
    return NULL;

  return cmd.queue;
}

int
player_queue_add(struct player_source *ps)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_add;
  cmd.func_bh = NULL;
  cmd.arg.ps = ps;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_queue_add_next(struct player_source *ps)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_add_next;
  cmd.func_bh = NULL;
  cmd.arg.ps = ps;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_queue_move(int ps_pos_from, int ps_pos_to)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_move;
  cmd.func_bh = NULL;
  cmd.arg.ps_pos[0] = ps_pos_from;
  cmd.arg.ps_pos[1] = ps_pos_to;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_queue_remove(int ps_pos_remove)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_remove;
  cmd.func_bh = NULL;
  cmd.arg.item_range.type = RANGEARG_POS;
  cmd.arg.item_range.start_pos = ps_pos_remove;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

int
player_queue_removeid(uint32_t id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = queue_remove;
  cmd.func_bh = NULL;
  cmd.arg.item_range.type = RANGEARG_ID;
  cmd.arg.item_range.id = id;

  ret = sync_command(&cmd);

  command_deinit(&cmd);

  return ret;
}

void
player_queue_clear(void)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = queue_clear;
  cmd.func_bh = NULL;
  cmd.arg.noarg = NULL;

  sync_command(&cmd);

  command_deinit(&cmd);
}

void
player_queue_empty(int clear_hist)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = queue_empty;
  cmd.func_bh = NULL;
  cmd.arg.intval = clear_hist;

  sync_command(&cmd);

  command_deinit(&cmd);
}

void
player_queue_plid(uint32_t plid)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = queue_plid;
  cmd.func_bh = NULL;
  cmd.arg.id = plid;

  sync_command(&cmd);

  command_deinit(&cmd);
}

void
player_set_update_handler(player_status_handler handler)
{
  struct player_command cmd;

  command_init(&cmd);

  cmd.func = set_update_handler;
  cmd.func_bh = NULL;
  cmd.arg.status_handler = handler;

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

  source_head = NULL;
  shuffle_head = NULL;
  cur_playing = NULL;
  cur_streaming = NULL;
  cur_plid = 0;

  player_state = PLAY_STOPPED;
  repeat = REPEAT_OFF;
  shuffle = 0;

  update_handler = NULL;

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

  rng_init(&shuffle_rng);

  ret = db_speaker_get(0, &laudio_selected, &laudio_volume);
  if (ret < 0)
    laudio_volume = 75;
  else if (laudio_selected)
    speaker_select_laudio(); /* Run the select helper */

  audio_buf = evbuffer_new();
  if (!audio_buf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate evbuffer for audio buffer\n");

      goto audio_fail;
    }

  raop_v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");

#if defined(__linux__)
  ret = pipe2(exit_pipe, O_CLOEXEC);
#else
  ret = pipe(exit_pipe);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create pipe: %s\n", strerror(errno));

      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(cmd_pipe);
# endif
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

#ifdef HAVE_LIBEVENT2
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

# if defined(__linux__)
  pb_timer_ev = event_new(evbase_player, pb_timer_fd, EV_READ, player_playback_cb, NULL);
# else
  pb_timer_ev = evsignal_new(evbase_player, SIGALRM, player_playback_cb, NULL);
# endif
  if (!pb_timer_ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer event\n");
      goto evnew_fail;
    }
#else
  exitev = (struct event *)malloc(sizeof(struct event));
  if (!exitev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create exit event\n");
      goto evnew_fail;
    }
  event_set(exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_player, exitev);

  cmdev = (struct event *)malloc(sizeof(struct event));
  if (!cmdev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create cmd event\n");
      goto evnew_fail;
    }
  event_set(cmdev, cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_player, cmdev);

  pb_timer_ev = (struct event *)malloc(sizeof(struct event));
  if (!pb_timer_ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer event\n");
      goto evnew_fail;
    }
# if defined(__linux__)
  event_set(pb_timer_ev, pb_timer_fd, EV_READ, player_playback_cb, NULL);
# else
  signal_set(pb_timer_ev, SIGALRM, player_playback_cb, NULL);
# endif
  event_base_set(evbase_player, pb_timer_ev);
#endif /* HAVE_LIBEVENT2 */

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

  if (source_head)
    queue_clear(NULL);

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
