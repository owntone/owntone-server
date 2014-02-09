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
# include <sys/time.h>
# include <sys/event.h>
#endif

#if defined(HAVE_SYS_EVENTFD_H) && defined(HAVE_EVENTFD)
# define USE_EVENTFD
# include <sys/eventfd.h>
#endif

#include <event.h>

#include <gcrypt.h>

#include "db.h"
#include "daap_query.h"
#include "logger.h"
#include "mdns.h"
#include "conffile.h"
#include "misc.h"
#include "rng.h"
#include "transcode.h"
#include "player.h"
#include "raop.h"
#include "laudio.h"


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
    player_status_handler status_handler;
    uint32_t *id_ptr;
    uint64_t *raop_ids;
    enum repeat_mode mode;
    uint32_t id;
    int intval;
  } arg;

  int ret;

  int raop_pending;
};

/* Keep in sync with enum raop_devtype */
static const char *raop_devtype[] =
  {
    "AirPort Express 802.11g",
    "AirPort Express 802.11n",
    "AppleTV",
    "Other",
  };


struct event_base *evbase_player;

#ifdef USE_EVENTFD
static int exit_efd;
#else
static int exit_pipe[2];
#endif
static int cmd_pipe[2];
static int player_exit;
static struct event exitev;
static struct event cmdev;
static pthread_t tid_player;

/* Player status */
static enum play_status player_state;
static enum repeat_mode repeat;
static char shuffle;

/* Status updates (for DACP) */
static player_status_handler update_handler;

/* Playback timer */
static int pb_timer_fd;
static struct event pb_timer_ev;
#if defined(__linux__)
static struct timespec pb_timer_last;
static struct timespec packet_timer_last;
static uint64_t MINIMUM_STREAM_PERIOD;
static struct timespec packet_time = { 0, AIRTUNES_V2_STREAM_PERIOD };
static struct timespec timer_res;
#endif

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


/* Command helpers */
static void
command_async_end(struct player_command *cmd)
{
  cur_cmd = NULL;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

  /* Process commands again */
  event_add(&cmdev, NULL);
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

#if defined(__linux__)
  ret = clock_gettime_with_res(CLOCK_MONOTONIC, ts, &timer_res);
#else
  ret = clock_gettime(CLOCK_MONOTONIC, ts);
#endif
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

#if defined(__linux__)
  ret = clock_gettime_with_res(CLOCK_MONOTONIC, ts, &timer_res);
#else
  ret = clock_gettime(CLOCK_MONOTONIC, ts);
#endif
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

/* Forward */
static void
playback_abort(void);

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
metadata_send(struct player_source *ps, int startup)
{
  uint64_t offset;
  uint64_t rtptime;

  offset = 0;

  /* Determine song boundaries, dependent on context */

  /* Restart after pause/seek */
  if (ps->stream_start)
    {
      offset = ps->output_start - ps->stream_start;
      rtptime = ps->stream_start;
    }
  else if (startup)
    {
      rtptime = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
    }
  /* Generic case */
  else if (cur_streaming && (cur_streaming->end))
    {
      rtptime = cur_streaming->end + 1;
    }
  else
    {
      rtptime = 0;
      DPRINTF(E_LOG, L_PLAYER, "PTOH! Unhandled song boundary case in metadata_send()\n");
    }

  raop_metadata_send(ps->id, rtptime, offset, startup);
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

      ps = (struct player_source *)malloc(sizeof(struct player_source));
      if (!ps)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Out of memory for struct player_source\n");

	  ret = -1;
	  break;
	}

      memset(ps, 0, sizeof(struct player_source));

      ps->id = id;

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
  int plid;
  int id;
  int idx;
  int ret;
  int len;
  char *s;
  char buf[1024];

  id = find_first_song_id(query);
  if (id < 0)
    return -1;

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
      snprintf(buf, sizeof(buf), "f.songartistid = %" PRIi64, mfi->songartistid);
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
	  DPRINTF(E_LOG, L_PLAYER, "Unknown queuefilter: %s\n", queuefilter);

	  return -1;
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

  memset(&qp, 0, sizeof(struct query_params));

  qp.id = plid;
  qp.type = Q_PLITEMS;
  qp.offset = 0;
  qp.limit = 0;
  qp.sort = S_NONE;

  ps = player_queue_make(&qp, NULL);

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

static void
source_free(struct player_source *ps)
{
  if (ps->ctx)
    transcode_cleanup(ps->ctx);

  free(ps);
}

static void
source_stop(struct player_source *ps)
{
  struct player_source *tmp;

  while (ps)
    {
      if (ps->ctx)
	{
	  transcode_cleanup(ps->ctx);
	  ps->ctx = NULL;
	}

      tmp = ps;
      ps = ps->play_next;

      tmp->play_next = NULL;
    }
}

static struct player_source *
source_shuffle(struct player_source *head)
{
  struct player_source *ps;
  struct player_source **ps_array;
  int nitems;
  int i;

  if (!head)
    return NULL;

  ps = head;
  nitems = 0;
  do
    {
      nitems++;
      ps = ps->pl_next;
    }
  while (ps != head);

  ps_array = (struct player_source **)malloc(nitems * sizeof(struct player_source *));
  if (!ps_array)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not allocate memory for shuffle array\n");
      return NULL;
    }

  ps = head;
  i = 0;
  do
    {
      ps_array[i] = ps;

      ps = ps->pl_next;
      i++;
    }
  while (ps != head);

  shuffle_ptr(&shuffle_rng, (void **)ps_array, nitems);

  for (i = 0; i < nitems; i++)
    {
      ps = ps_array[i];

      if (i > 0)
	ps->shuffle_prev = ps_array[i - 1];

      if (i < (nitems - 1))
	ps->shuffle_next = ps_array[i + 1];
    }

  ps_array[0]->shuffle_prev = ps_array[nitems - 1];
  ps_array[nitems - 1]->shuffle_next = ps_array[0];

  ps = ps_array[0];

  free(ps_array);

  return ps;
}

static void
source_reshuffle(void)
{
  struct player_source *ps;

  ps = source_shuffle(source_head);
  if (!ps)
    return;

  if (cur_streaming)
    shuffle_head = cur_streaming;
  else
    shuffle_head = ps;
}

/* Helper */
static int
source_open(struct player_source *ps, int no_md)
{
  struct media_file_info *mfi;

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

  DPRINTF(E_DBG, L_PLAYER, "Opening %s\n", mfi->path);

  ps->ctx = transcode_setup(mfi, NULL, 0);

  free_mfi(mfi, 0);

  if (!ps->ctx)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not open file id %d\n", ps->id);

      return -1;
    }

  if (!no_md)
    metadata_send(ps, (player_state == PLAY_PLAYING) ? 0 : 1);

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

	if (cur_streaming->ctx)
	  {
	    ret = transcode_seek(cur_streaming->ctx, 0);

	    /* source_open() takes care of sending metadata, but we don't
	     * call it when repeating a song as we just seek back to 0
	     * so we have to handle metadata ourselves here
	     */
	    if (ret >= 0)
	      metadata_send(cur_streaming, 0);
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

static int
source_position(struct player_source *ps)
{
  struct player_source *p;
  int ret;

  ret = 0;
  for (p = source_head; p != ps; p = p->pl_next)
    ret++;

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

	  if (ps->ctx)
	    {
	      transcode_cleanup(ps->ctx);
	      ps->ctx = NULL;
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

      db_file_inc_playcount((int)cur_playing->id);

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

      if (ps->ctx)
	{
	  transcode_cleanup(ps->ctx);
	  ps->ctx = NULL;
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

static int
source_read(uint8_t *buf, int len, uint64_t rtptime)
{
  int new;
  int ret;
  int nbytes;

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

	  ret = source_next(0);
	  if (ret < 0)
	    return -1;
	}

      if (EVBUFFER_LENGTH(audio_buf) == 0)
	{
	  ret = transcode(cur_streaming->ctx, audio_buf, len - nbytes);
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
  uint8_t rawbuf[AIRTUNES_V2_PACKET_SAMPLES * 2 * 2];
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

#if defined(__linux__)
static void
player_playback_cb(int fd, short what, void *arg)
{
  struct itimerspec next;
  uint64_t ticks;
  int ret;
  uint32_t packet_send_count = 0;
  struct timespec next_tick;
  struct timespec stream_period  = { 0, MINIMUM_STREAM_PERIOD };

  /* Acknowledge timer */
  read(fd, &ticks, sizeof(ticks));

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

  while(timespec_cmp(packet_timer_last, next_tick) < 0);

  /* Make sure playback is still running */
  if (player_state == PLAY_STOPPED)
    return;

  pb_timer_last.tv_nsec += MINIMUM_STREAM_PERIOD;
  if (pb_timer_last.tv_nsec >= 1000000000)
    {
      pb_timer_last.tv_sec++;
      pb_timer_last.tv_nsec -= 1000000000;
    }

  next.it_interval.tv_sec = 0;
  next.it_interval.tv_nsec = 0;
  next.it_value.tv_sec = pb_timer_last.tv_sec;
  next.it_value.tv_nsec = pb_timer_last.tv_nsec;

  ret = timerfd_settime(pb_timer_fd, TFD_TIMER_ABSTIME, &next, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not set playback timer: %s\n", strerror(errno));

      playback_abort();
      return;
    }

  ret = event_add(&pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not re-add playback timer event\n");

      playback_abort();
      return;
    }
}
#endif /* __linux__ */


#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
static void
player_playback_cb(int fd, short what, void *arg)
{
  struct timespec ts;
  struct kevent kev;
  int ret;

  ts.tv_sec = 0;
  ts.tv_nsec = 0;

  while (kevent(pb_timer_fd, NULL, 0, &kev, 1, &ts) > 0)
    {
      if (kev.filter != EVFILT_TIMER)
        continue;

      playback_write();

      /* Make sure playback is still running */
      if (player_state == PLAY_STOPPED)
	return;
    }

  ret = event_add(&pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not re-add playback timer event\n");

      playback_abort();
      return;
    }
}
#endif /* __FreeBSD__ || __FreeBSD_kernel__ */


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
#if defined(__linux__)
      ret = clock_gettime_with_res(CLOCK_MONOTONIC, &ts, &timer_res);
#else
      ret = clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not get current time: %s\n", strerror(errno));

#if defined(__linux__)
	  /* Fallback to nearest timer expiration time */
	  ts.tv_sec = pb_timer_last.tv_sec;
	  ts.tv_nsec = pb_timer_last.tv_nsec;
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
	  if (cur_cmd->ret != -2)
	    cur_cmd->ret = -1;
	  goto out;
#endif
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

  if (event_initialized(&pb_timer_ev))
    event_del(&pb_timer_ev);

  if (pb_timer_fd != -1)
    close(pb_timer_fd);
  pb_timer_fd = -1;

  if (cur_playing)
    source_stop(cur_playing);
  else
    source_stop(cur_streaming);

  cur_playing = NULL;
  cur_streaming = NULL;

  evbuffer_drain(audio_buf, EVBUFFER_LENGTH(audio_buf));

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
  uint64_t pos;
  int ret;

  status = cmd->arg.status;

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

	status->pos_pl = source_position(cur_streaming);
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

	status->id = ps->id;
	status->pos_pl = source_position(ps);
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
playback_stop(struct player_command *cmd)
{
  if (laudio_status != LAUDIO_CLOSED)
    laudio_close();

  /* We may be restarting very soon, so we don't bring the devices to a
   * full stop just yet; this saves time when restarting, which is nicer
   * for the user.
   */
  cmd->raop_pending = raop_flush(device_command_cb, last_rtptime + AIRTUNES_V2_PACKET_SAMPLES);

  if (event_initialized(&pb_timer_ev))
    event_del(&pb_timer_ev);

  if (pb_timer_fd != -1)
    close(pb_timer_fd);
  pb_timer_fd = -1;

  if (cur_playing)
    source_stop(cur_playing);
  else
    source_stop(cur_streaming);

  cur_playing = NULL;
  cur_streaming = NULL;

  evbuffer_drain(audio_buf, EVBUFFER_LENGTH(audio_buf));

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
#if defined(__linux__)
  struct itimerspec next;
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  struct kevent kev;
#endif
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

#if defined(__linux__)
  ret = clock_gettime_with_res(CLOCK_MONOTONIC, &pb_pos_stamp, &timer_res);
#else
  ret = clock_gettime(CLOCK_MONOTONIC, &pb_pos_stamp);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Couldn't get current clock: %s\n", strerror(errno));

      goto out_fail;
    }

  memset(&pb_timer_ev, 0, sizeof(struct event));

#if defined(__linux__)
  /*
   * initialize the packet timer to the same relative time that we have 
   * for the playback timer.
   */
  packet_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  packet_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  pb_timer_last.tv_sec = pb_pos_stamp.tv_sec;
  pb_timer_last.tv_nsec = pb_pos_stamp.tv_nsec;

  pb_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
  if (pb_timer_fd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create playback timer: %s\n", strerror(errno));

      goto out_fail;
    }

  next.it_interval.tv_sec = 0;
  next.it_interval.tv_nsec = 0;
  next.it_value.tv_sec = pb_timer_last.tv_sec;
  next.it_value.tv_nsec = pb_timer_last.tv_nsec;

  ret = timerfd_settime(pb_timer_fd, TFD_TIMER_ABSTIME, &next, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not set playback timer: %s\n", strerror(errno));

      goto out_fail;
    }
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  pb_timer_fd = kqueue();
  if (pb_timer_fd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create kqueue: %s\n", strerror(errno));

      goto out_fail;
    }

  memset(&kev, 0, sizeof(struct kevent));

  EV_SET(&kev, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE, 0, AIRTUNES_V2_STREAM_PERIOD, 0);

  ret = kevent(pb_timer_fd, &kev, 1, NULL, 0, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not add kevent timer: %s\n", strerror(errno));

      goto out_fail;
    }
#endif

  event_set(&pb_timer_ev, pb_timer_fd, EV_READ, player_playback_cb, NULL);
  event_base_set(evbase_player, &pb_timer_ev);

  ret = event_add(&pb_timer_ev, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not set up playback timer event\n");

      goto out_fail;
    }

  /* Everything OK, start RAOP */
  if (raop_sessions > 0)
    raop_playback_start(last_rtptime + AIRTUNES_V2_PACKET_SAMPLES, &pb_pos_stamp);

  status_update(PLAY_PLAYING);

  return 0;

 out_fail:
  if (pb_timer_fd != -1)
    close(pb_timer_fd);
  pb_timer_fd = -1;
  playback_abort();

  return -1;
}

static int
playback_start(struct player_command *cmd)
{
  struct raop_device *rd;
  uint32_t *idx_id;
  int ret;

  if (!source_head)
    {
      DPRINTF(E_LOG, L_PLAYER, "Nothing to play!\n");

      return -1;
    }

  idx_id = cmd->arg.id_ptr;

  if (player_state == PLAY_PLAYING)
    {
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

  pb_pos = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES - 88200;

  if (idx_id)
    {
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
	cur_streaming = source_head;

      if (*idx_id > 0)
	{
	  cur_streaming = source_head;
	  for (; *idx_id > 0; (*idx_id)--)
	    cur_streaming = cur_streaming->pl_next;

	  if (shuffle)
	    shuffle_head = cur_streaming;
	}

      ret = source_open(cur_streaming, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Couldn't jump to queue position %d\n", *idx_id);

	  playback_abort();
	  return -1;
	}

      *idx_id = cur_streaming->id;
      cur_streaming->stream_start = last_rtptime + AIRTUNES_V2_PACKET_SAMPLES;
      cur_streaming->output_start = cur_streaming->stream_start;
    }
  else if (!cur_streaming)
    {
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
      /* After a pause, the source is still open so source_open() doesn't get
       * called and we have to handle metadata ourselves.
       */
      metadata_send(cur_streaming, 1);
    }

  /* Start local audio if needed */
  if (laudio_selected && (laudio_status == LAUDIO_CLOSED))
    {
      ret = laudio_open();
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not open local audio\n");

	  playback_abort();
	  return -1;
	}
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

  if (cur_playing)
    source_stop(cur_playing);
  else
    source_stop(cur_streaming);

  ret = source_prev();
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
playback_next_bh(struct player_command *cmd)
{
  int ret;

  if (cur_playing)
    source_stop(cur_playing);
  else
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
  ret = transcode_seek(ps->ctx, ms);
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

  ret = transcode_seek(ps->ctx, ms);
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

  if (event_initialized(&pb_timer_ev))
    event_del(&pb_timer_ev);

  if (pb_timer_fd != -1)
    close(pb_timer_fd);
  pb_timer_fd = -1;

  if (ps->play_next)
    source_stop(ps->play_next);

  cur_playing = NULL;
  cur_streaming = ps;
  cur_streaming->play_next = NULL;

  evbuffer_drain(audio_buf, EVBUFFER_LENGTH(audio_buf));

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

static int
queue_add(struct player_command *cmd)
{
  struct player_source *ps;
  struct player_source *ps_shuffle;
  struct player_source *source_tail;
  struct player_source *ps_tail;

  ps = cmd->arg.ps;

  ps_shuffle = source_shuffle(ps);
  if (!ps_shuffle)
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

  if (cur_plid != 0)
    cur_plid = 0;

  return 0;
}

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
  event_add(&cmdev, NULL);
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

int
player_playback_start(uint32_t *idx_id)
{
  struct player_command cmd;
  int ret;

  command_init(&cmd);

  cmd.func = playback_start;
  cmd.func_bh = playback_start_bh;
  cmd.arg.id_ptr = idx_id;

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

struct player_source *
player_queue_get(void)
{
  if (shuffle)
    return shuffle_head;
  else
    return source_head;
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


/* RAOP devices discovery - mDNS callback */
/* Thread: main (mdns) */
static void
raop_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct raop_device *rd;
  cfg_t *airplay;
  const char *p;
  char *at_name;
  char *password;
  uint64_t id;
  char wants_metadata;
  char has_password;
  enum raop_devtype devtype;
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

  password = NULL;
  p = keyval_get(txt, "pw");
  if (!p)
    {
      DPRINTF(E_INFO, L_PLAYER, "AirPlay %s: no pw field in TXT record, assuming no password protection\n", name);

      has_password = 0;
    }
  else if (*p == '\0')
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: pw has no value\n", name);

      goto free_rd;
    }
  else
    {
      has_password = (strcmp(p, "false") != 0);
    }

  if (has_password)
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay device %s is password-protected\n", name);

      airplay = cfg_gettsec(cfg, "airplay", at_name);
      if (airplay)
	password = cfg_getstr(airplay, "password");

      if (!password)
	DPRINTF(E_LOG, L_PLAYER, "No password given in config for AirPlay device %s\n", name);
    }

  devtype = RAOP_DEV_APEX_80211N;

  p = keyval_get(txt, "am");
  if (!p)
    {
      DPRINTF(E_INFO, L_PLAYER, "AirPlay %s: no am field in TXT record, assuming old Airport Express\n", name);

      /* Old AirPort Express */
      devtype = RAOP_DEV_APEX_80211G;

      goto no_am;
    }

  if (*p == '\0')
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: am has no value\n", name);

      goto no_am;
    }

  if (strncmp(p, "AppleTV", strlen("AppleTV")) == 0)
    devtype = RAOP_DEV_APPLETV;
  else if (strncmp(p, "AirPort4", strlen("AirPort4")) != 0)
    devtype = OTHER;

 no_am:
  wants_metadata = 0;
  p = keyval_get(txt, "md");
  if (!p)
    {
      DPRINTF(E_INFO, L_PLAYER, "AirPlay %s: no md field in TXT record.\n", name);

      goto no_md;
    }

  if (*p == '\0')
    {
      DPRINTF(E_LOG, L_PLAYER, "AirPlay %s: md has no value\n", name);

      goto no_md;
    }

  wants_metadata = 1;

 no_md:
  DPRINTF(E_DBG, L_PLAYER, "AirPlay device %s: password: %s, type %s\n", name, (password) ? "yes" : "no", raop_devtype[devtype]);

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

  rd->devtype = devtype;

  rd->wants_metadata = wants_metadata;
  rd->has_password = has_password;
  rd->password = password;

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

  pb_timer_fd = -1;

  source_head = NULL;
  shuffle_head = NULL;
  cur_playing = NULL;
  cur_streaming = NULL;
  cur_plid = 0;

  player_state = PLAY_STOPPED;
  repeat = REPEAT_OFF;
  shuffle = 0;

  update_handler = NULL;

#if defined(__linux__)
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
  MINIMUM_STREAM_PERIOD = MAX(timer_res.tv_nsec, AIRTUNES_V2_STREAM_PERIOD);
#endif

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

      return -1;
    }

  raop_v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");


#ifdef USE_EVENTFD
  exit_efd = eventfd(0, EFD_CLOEXEC);
  if (exit_efd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create eventfd: %s\n", strerror(errno));

      goto exit_fail;
    }
#else
# if defined(__linux__)
  ret = pipe2(exit_pipe, O_CLOEXEC);
# else
  ret = pipe(exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not create pipe: %s\n", strerror(errno));

      goto exit_fail;
    }
#endif /* USE_EVENTFD */

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

#ifdef USE_EVENTFD
  event_set(&exitev, exit_efd, EV_READ, exit_cb, NULL);
#else
  event_set(&exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
#endif
  event_base_set(evbase_player, &exitev);
  event_add(&exitev, NULL);

  event_set(&cmdev, cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_player, &cmdev);
  event_add(&cmdev, NULL);

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
  event_base_free(evbase_player);
 evbase_fail:
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
 cmd_fail:
#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
 exit_fail:
  evbuffer_free(audio_buf);

  return -1;
}

/* Thread: main */
void
player_deinit(void)
{
  int ret;

#ifdef USE_EVENTFD
  ret = eventfd_write(exit_efd, 1);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not send exit event: %s\n", strerror(errno));

      return;
    }
#else
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }
#endif

  ret = pthread_join(tid_player, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not join HTTPd thread: %s\n", strerror(errno));

      return;
    }

  if (source_head)
    queue_clear(NULL);

  evbuffer_free(audio_buf);

  laudio_deinit();
  raop_deinit();

  if (event_initialized(&pb_timer_ev))
    event_del(&pb_timer_ev);

  event_del(&cmdev);

#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
  cmd_pipe[0] = -1;
  cmd_pipe[1] = -1;
  event_base_free(evbase_player);
}
