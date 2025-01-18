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
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <inttypes.h>

#include <pthread.h>
#include <event2/event.h>

#include "httpd_internal.h"
#include "httpd_daap.h"
#include "logger.h"
#include "misc.h"
#include "conffile.h"
#include "artwork.h"
#include "dmap_common.h"
#include "library.h"
#include "db.h"
#include "player.h"
#include "listener.h"

#define DACP_VOLUME_STEP 5

struct dacp_update_request {
  struct httpd_request *hreq;
  struct event *updateev;

  struct dacp_update_request *next;
};

typedef void (*dacp_propget)(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
typedef void (*dacp_propset)(const char *value, struct httpd_request *hreq);

struct dacp_prop_map {
  char *desc;
  dacp_propget propget;
  dacp_propset propset;
};


/* ---------------- FORWARD - PROPERTIES GETTERS AND SETTERS ---------------- */

static void
dacp_propget_volume(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_volumecontrollable(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_playerstate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_shufflestate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_availableshufflestates(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_repeatstate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_availablerepeatstates(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_nowplaying(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_playingtime(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);

static void
dacp_propget_fullscreenenabled(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_fullscreen(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_visualizerenabled(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_visualizer(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_itms_songid(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_haschapterdata(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_mediakind(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);
static void
dacp_propget_extendedmediakind(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item);


static void
dacp_propset_volume(const char *value, struct httpd_request *hreq);
static void
dacp_propset_devicevolume(const char *value, struct httpd_request *hreq);
static void
dacp_propset_devicepreventplayback(const char *value, struct httpd_request *hreq);
static void
dacp_propset_devicebusy(const char *value, struct httpd_request *hreq);
static void
dacp_propset_playingtime(const char *value, struct httpd_request *hreq);
static void
dacp_propset_shufflestate(const char *value, struct httpd_request *hreq);
static void
dacp_propset_repeatstate(const char *value, struct httpd_request *hreq);
static void
dacp_propset_userrating(const char *value, struct httpd_request *hreq);


/* gperf static hash, dacp_prop.gperf */
#include "dacp_prop_hash.h"

// Play status update requests
static struct dacp_update_request *update_requests;
static pthread_mutex_t update_request_lck;
// Next revision number the client should call with
static int update_current_rev;

// If an item is removed from the library while in the queue, we replace it with this
static struct media_file_info dummy_mfi;
static struct db_queue_item dummy_queue_item;


/* -------------------------------- HELPERS --------------------------------- */

static void
dacp_send_error(struct httpd_request *hreq, const char *container, const char *errmsg)
{
  if (!hreq)
    return;

  dmap_error_make(hreq->out_body, container, errmsg);

  httpd_send_reply(hreq, HTTP_OK, "OK", HTTPD_SEND_NO_GZIP);
}

static void
dacp_nowplaying(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  uint32_t id;
  int64_t songalbumid;
  int pos_pl;

  if ((status->status == PLAY_STOPPED) || !queue_item)
    return;

  /* Send bogus id's if playing internet radio or pipe, because clients like
   * Remote and Retune will only update metadata (like artwork) if the id's
   * change (which they wouldn't do if we sent the real ones)
   * FIXME: Giving the client invalid ids on purpose is hardly ideal, but the
   * clients don't seem to use these ids for anything other than rating.
   */
  if (queue_item->data_kind == DATA_KIND_HTTP || queue_item->data_kind == DATA_KIND_PIPE)
    {
      // Could also use queue_item->queue_version, but it changes a bit too much
      // leading to Remote reloading too much
      if (queue_item->artwork_url)
	id = djb_hash(queue_item->artwork_url, strlen(queue_item->artwork_url));
      else
	id = djb_hash(queue_item->title, strlen(queue_item->title));

      songalbumid = (int64_t)id;
    }
  else
    {
      id = status->id;
      songalbumid = queue_item->songalbumid;
    }

  pos_pl = db_queue_get_pos(status->item_id, 0);

  dmap_add_container(evbuf, "canp", 16);
  dmap_add_raw_uint32(evbuf, 1); /* Database */
  dmap_add_raw_uint32(evbuf, status->plid);
  dmap_add_raw_uint32(evbuf, pos_pl);
  dmap_add_raw_uint32(evbuf, id);

  dmap_add_string(evbuf, "cann", queue_item->title);
  dmap_add_string(evbuf, "cana", queue_item->artist);
  dmap_add_string(evbuf, "canl", queue_item->album);
  dmap_add_string(evbuf, "cang", queue_item->genre);
  dmap_add_long(evbuf, "asai", songalbumid);

  dmap_add_int(evbuf, "cmmk", 1);
}

static void
dacp_playingtime(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  if ((status->status == PLAY_STOPPED) || !queue_item)
    return;

  if (queue_item->song_length)
    dmap_add_int(evbuf, "cant", queue_item->song_length - status->pos_ms); /* Remaining time in ms */
  else
    dmap_add_int(evbuf, "cant", 0); /* Unknown remaining time */

  dmap_add_int(evbuf, "cast", queue_item->song_length); /* Song length in ms */
}

static int
find_first_song_id(const char *query)
{
  struct db_media_file_info dbmfi;
  struct query_params qp;
  int id;
  int ret;

  id = 0;
  memset(&qp, 0, sizeof(struct query_params));

  /* We only want the id of the first song */
  qp.type = Q_ITEMS;
  qp.idx_type = I_FIRST;
  qp.sort = S_NONE;
  qp.offset = 0;
  qp.limit = 1;
  qp.filter = dmap_query_parse_sql(query);
  if (!qp.filter)
    {
      DPRINTF(E_LOG, L_DACP, "Improper DAAP query!\n");

      return -1;
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not start query\n");

      goto no_query_start;
    }

  if ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      ret = safe_atoi32(dbmfi.id, &id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid song id in query result!\n");

	  goto no_result;
	}

      DPRINTF(E_DBG, L_DACP, "Found index song (id %d)\n", id);
    }
  else
    {
      DPRINTF(E_LOG, L_DACP, "No song matches query (results %d): %s\n", qp.results, qp.filter);

      goto no_result;
    }

 no_result:
  db_query_end(&qp);

 no_query_start:
  if (qp.filter)
    free(qp.filter);

  if (id > 0)
    return id;
  else
    return -1;
}

static int
dacp_queueitem_add(const char *query, const char *queuefilter, const char *sort, int quirk, int mode)
{
  struct media_file_info *mfi;
  struct query_params qp;
  int64_t albumid;
  int64_t artistid;
  int plid;
  int id;
  int ret;
  int len;
  char buf[1024];
  struct player_status status;

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
  qp.idx_type = I_NONE;

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
	      DPRINTF(E_LOG, L_DACP, "Invalid album id in queuefilter: '%s'\n", queuefilter);

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
	      DPRINTF(E_LOG, L_DACP, "Invalid artist id in queuefilter: '%s'\n", queuefilter);

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
	      DPRINTF(E_LOG, L_DACP, "Invalid playlist id in queuefilter: '%s'\n", queuefilter);

	      return -1;
	    }
	  qp.id = plid;
	  qp.filter = strdup("1 = 1");
	}
      else if ((len > 6) && (strncmp(queuefilter, "genre:", 6) == 0))
	{
	  qp.type = Q_ITEMS;
	  ret = snprintf(buf, sizeof(buf), "'daap.song%s'", queuefilter);
	  if (ret < 0 || ret >= sizeof(buf))
	    {
	      DPRINTF(E_LOG, L_DACP, "Invalid genre length in queuefilter: '%s'\n", queuefilter);

	      return -1;
	    }
	  qp.filter = dmap_query_parse_sql(buf);
	}
      else
	{
	  DPRINTF(E_LOG, L_DACP, "Unknown queuefilter '%s', query is '%s'\n", queuefilter, query);

	  // If the queuefilter is unkown, ignore it and use the query parameter instead to build the sql query
	  id = 0;
	  qp.type = Q_ITEMS;
	  qp.filter = dmap_query_parse_sql(query);
	}
    }
  else
    {
      id = 0;
      qp.type = Q_ITEMS;
      qp.filter = dmap_query_parse_sql(query);
    }

  if (sort)
    {
      if (strcmp(sort, "name") == 0)
	qp.sort = S_NAME;
      else if (strcmp(sort, "album") == 0)
	qp.sort = S_ALBUM;
      else if (strcmp(sort, "artist") == 0)
	qp.sort = S_ARTIST;
    }

  player_get_status(&status);

  if (mode == 3)
    ret = db_queue_add_by_queryafteritemid(&qp, status.item_id);
  else
    ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id, -1, NULL, NULL);

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    return -1;

  if (status.shuffle && mode != 1)
    return 0;

  return id;
}

static int
playqueuecontents_add_source(struct evbuffer *songlist, uint32_t source_id, int pos_in_queue, uint32_t plid)
{
  struct evbuffer *song;
  struct media_file_info *mfi;
  int ret;

  CHECK_NULL(L_DACP, song = evbuffer_new());
  CHECK_ERR(L_DACP, evbuffer_expand(song, 256));

  mfi = db_file_fetch_byid(source_id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_DACP, "Could not fetch file id %d\n", source_id);
      mfi = &dummy_mfi;
    }
  dmap_add_container(song, "ceQs", 16);
  dmap_add_raw_uint32(song, 1); /* Database */
  dmap_add_raw_uint32(song, plid);
  dmap_add_raw_uint32(song, 0); /* Should perhaps be playlist index? */
  dmap_add_raw_uint32(song, mfi->id);
  dmap_add_string(song, "ceQn", mfi->title);
  dmap_add_string(song, "ceQr", mfi->artist);
  dmap_add_string(song, "ceQa", mfi->album);
  dmap_add_string(song, "ceQg", mfi->genre);
  dmap_add_long(song, "asai", mfi->songalbumid);
  dmap_add_int(song, "cmmk", mfi->media_kind);
  dmap_add_int(song, "casa", 1); /* Unknown  */
  dmap_add_int(song, "astm", mfi->song_length);
  dmap_add_char(song, "casc", 1); /* Maybe an indication of extra data? */
  dmap_add_char(song, "caks", 6); /* Unknown */
  dmap_add_int(song, "ceQI", pos_in_queue);

  dmap_add_container(songlist, "mlit", evbuffer_get_length(song));

  ret = evbuffer_add_buffer(songlist, song);
  evbuffer_free(song);
  if (mfi != &dummy_mfi)
    free_mfi(mfi, 0);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not add song to songlist for playqueue-contents\n");
      return ret;
    }

  return 0;
}

static int
playqueuecontents_add_queue_item(struct evbuffer *songlist, struct db_queue_item *queue_item, int pos_in_queue, uint32_t plid)
{
  struct evbuffer *song;
  int ret;

  CHECK_NULL(L_DACP, song = evbuffer_new());
  CHECK_ERR(L_DACP, evbuffer_expand(song, 256));

  dmap_add_container(song, "ceQs", 16);
  dmap_add_raw_uint32(song, 1); /* Database */
  dmap_add_raw_uint32(song, plid);
  dmap_add_raw_uint32(song, 0); /* Should perhaps be playlist index? */
  dmap_add_raw_uint32(song, queue_item->file_id);
  dmap_add_string(song, "ceQn", queue_item->title);
  dmap_add_string(song, "ceQr", queue_item->artist);
  dmap_add_string(song, "ceQa", queue_item->album);
  dmap_add_string(song, "ceQg", queue_item->genre);
  dmap_add_long(song, "asai", queue_item->songalbumid);
  dmap_add_int(song, "cmmk", queue_item->media_kind);
  dmap_add_int(song, "casa", 1); /* Unknown  */
  dmap_add_int(song, "astm", queue_item->song_length);
  dmap_add_char(song, "casc", 1); /* Maybe an indication of extra data? */
  dmap_add_char(song, "caks", 6); /* Unknown */
  dmap_add_int(song, "ceQI", pos_in_queue);

  dmap_add_container(songlist, "mlit", evbuffer_get_length(song));

  ret = evbuffer_add_buffer(songlist, song);
  evbuffer_free(song);

  return ret;
}

static void
speaker_enum_cb(struct player_speaker_info *spk, void *arg)
{
  struct evbuffer *evbuf;
  int len;

  evbuf = (struct evbuffer *)arg;

  len = 8 + strlen(spk->name) + 28;
  if (spk->selected)
    len += 9;
  if (spk->has_password)
    len += 9;
  if (spk->has_video)
    len += 9;

  CHECK_ERR(L_DACP, evbuffer_expand(evbuf, 71 + len));

  dmap_add_container(evbuf, "mdcl", len);        /* 8 + len */
  if (spk->selected)
    dmap_add_char(evbuf, "caia", 1);             /* 9 */
  if (spk->has_password)
    dmap_add_char(evbuf, "cahp", 1);             /* 9 */
  if (spk->has_video)
    dmap_add_char(evbuf, "caiv", 1);             /* 9 */
  dmap_add_string(evbuf, "minm", spk->name);   /* 8 + len */
  dmap_add_long(evbuf, "msma", spk->id);       /* 16 */

  dmap_add_int(evbuf, "cmvo", spk->relvol);    /* 12 */
}

static int
speaker_get(struct player_speaker_info *speaker_info, struct httpd_request *hreq, const char *req_name)
{
  const char *remote;
  uint32_t active_remote;
  int ret;

  remote = httpd_header_find(hreq->in_headers, "Active-Remote");

  if (!remote || (safe_atou32(remote, &active_remote) < 0))
    {
      DPRINTF(E_LOG, L_DACP, "'%s' request from '%s' has invalid Active-Remote: '%s'\n", req_name, hreq->peer_address, remote);
      return -1;
    }

  ret = player_speaker_get_byactiveremote(speaker_info, active_remote);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "'%s' request from '%s' has unknown Active-Remote: '%s'\n", req_name, hreq->peer_address, remote);
      return -1;
    }

  return 0;
}

static int
speaker_volume_step(struct player_speaker_info *speaker_info, int step)
{
  int new_volume;

  new_volume = speaker_info->absvol + step;

  // Make sure we are setting a correct value
  new_volume = new_volume > 100 ? 100 : new_volume;
  new_volume = new_volume < 0 ? 0 : new_volume;

  return player_volume_setabs_speaker(speaker_info->id, new_volume);
}

static void
seek_timer_cb(int fd, short what, void *arg)
{
  intptr_t seek_target_packed = (intptr_t)arg;
  int seek_target = seek_target_packed;
  int ret;

  DPRINTF(E_DBG, L_DACP, "Seek timer expired, target %d ms\n", seek_target);

  ret = player_playback_seek(seek_target, PLAYER_SEEK_POSITION);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Player failed to seek to %d ms\n", seek_target);

      return;
    }

  ret = player_playback_start();
  if (ret < 0)
    DPRINTF(E_LOG, L_DACP, "Player returned an error for start after seek\n");
}

static int
dacp_request_authorize(struct httpd_request *hreq)
{
  const char *param;
  int32_t id;
  int ret;

  if (httpd_request_is_trusted(hreq))
    return 0;

  param = httpd_query_value_find(hreq->query, "session-id");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "No session-id specified in request\n");
      goto invalid;
    }

  ret = safe_atoi32(param, &id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Invalid session-id specified in request: '%s'\n", param);
      goto invalid;
    }

  if (!daap_session_is_valid(id))
    {
      DPRINTF(E_LOG, L_DACP, "Session %d does not exist\n", id);
      goto invalid;
    }

  return 0;

 invalid:
  DPRINTF(E_LOG, L_DACP, "Unauthorized request '%s' from '%s' (is peer trusted in your config?)\n", hreq->uri, hreq->peer_address);

  httpd_send_error(hreq, HTTP_FORBIDDEN, "Forbidden");
  return -1;
}


/* ---------------------- UPDATE REQUESTS HANDLERS -------------------------- */

static int
make_playstatusupdate(struct evbuffer *evbuf, int current_rev)
{
  struct player_status status;
  struct db_queue_item *queue_item = NULL;
  struct evbuffer *psu;

  CHECK_NULL(L_DACP, psu = evbuffer_new());
  CHECK_ERR(L_DACP, evbuffer_expand(psu, 256));

  player_get_status(&status);
  if (status.status != PLAY_STOPPED)
    {
      queue_item = db_queue_fetch_byitemid(status.item_id);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_DACP, "Could not fetch item id %d (file id %d)\n", status.item_id, status.id);

	  queue_item = &dummy_queue_item;
	}
    }

  dmap_add_int(psu, "mstt", 200);             /* 12 */

  dmap_add_int(psu, "cmsr", current_rev);     /* 12 */

  dmap_add_char(psu, "caps", status.status);  /*  9 */ /* play status, 2 = stopped, 3 = paused, 4 = playing */
  dmap_add_char(psu, "cash", status.shuffle); /*  9 */ /* shuffle, true/false */
  dmap_add_char(psu, "carp", status.repeat);  /*  9 */ /* repeat, 0 = off, 1 = repeat song, 2 = repeat (playlist) */
  dmap_add_char(psu, "cafs", 0);              /*  9 */ /* dacp.fullscreen */
  dmap_add_char(psu, "cavs", 0);              /*  9 */ /* dacp.visualizer */
  dmap_add_char(psu, "cavc", 1);              /*  9 */ /* volume controllable */
  dmap_add_int(psu, "caas", 2);               /* 12 */ /* available shuffle states */
  dmap_add_int(psu, "caar", 6);               /* 12 */ /* available repeat states */
  dmap_add_char(psu, "cafe", 0);              /*  9 */ /* dacp.fullscreenenabled */
  dmap_add_char(psu, "cave", 0);              /*  9 */ /* dacp.visualizerenabled */

  if (queue_item)
    {
      dacp_nowplaying(psu, &status, queue_item);

      dmap_add_int(psu, "casa", 1);           /* 12 */ /* unknown */
      dmap_add_int(psu, "astm", queue_item->song_length);
      dmap_add_char(psu, "casc", 1);          /* Maybe an indication of extra data? */
      dmap_add_char(psu, "caks", 6);          /* Unknown */

      dacp_playingtime(psu, &status, queue_item);

      if (queue_item != &dummy_queue_item)
	free_queue_item(queue_item, 0);
    }

  dmap_add_char(psu, "casu", 1);              /*  9 */ /* unknown */
  dmap_add_char(psu, "ceQu", 0);              /*  9 */ /* unknown */

  dmap_add_container(evbuf, "cmst", evbuffer_get_length(psu));    /* 8 + len */

  CHECK_ERR(L_DACP, evbuffer_add_buffer(evbuf, psu));

  evbuffer_free(psu);

  DPRINTF(E_DBG, L_DACP, "Replying to playstatusupdate with status %d and current_rev %d\n", status.status, current_rev);

  return 0;
}

static void
playstatusupdate_cb(int fd, short what, void *arg);

static struct dacp_update_request *
update_request_new(struct httpd_request *hreq)
{
  struct dacp_update_request *ur;

  CHECK_NULL(L_DACP, ur = calloc(1, sizeof(struct dacp_update_request)));
  CHECK_NULL(L_DACP, ur->updateev = event_new(hreq->evbase, -1, 0, playstatusupdate_cb, ur));
  ur->hreq = hreq;

  return ur;
}

static void
update_request_free(struct dacp_update_request *ur)
{
  if (!ur)
    return;

  if (ur->updateev)
    event_free(ur->updateev);

  free(ur);
}

static void
update_request_remove(struct dacp_update_request **head, struct dacp_update_request *ur)
{
  struct dacp_update_request *p;

  if (ur == *head)
    *head = ur->next;
  else
    {
      for (p = *head; p && (p->next != ur); p = p->next)
	;

      if (!p)
	{
	  DPRINTF(E_LOG, L_DACP, "WARNING: struct dacp_update_request not found in list; BUG!\n");
	  return;
	}

      p->next = ur->next;
    }

  update_request_free(ur);
}

static void
playstatusupdate_cb(int fd, short what, void *arg)
{
  struct dacp_update_request *ur = arg;
  struct httpd_request *hreq = ur->hreq;
  int ret;

  ret = make_playstatusupdate(hreq->out_body, update_current_rev);
  if (ret < 0)
    goto error;

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  pthread_mutex_lock(&update_request_lck);
  update_request_remove(&update_requests, ur);
  pthread_mutex_unlock(&update_request_lck);

 error:
  return;
}

static void
update_fail_cb(void *arg)
{
  struct dacp_update_request *ur = arg;

  DPRINTF(E_DBG, L_DACP, "Update request: client closed connection\n");

  pthread_mutex_lock(&update_request_lck);
  update_request_remove(&update_requests, ur);
  pthread_mutex_unlock(&update_request_lck);
}

/* Thread: player */
static void
dacp_playstatus_update_handler(short event_mask, void *ctx)
{
  struct dacp_update_request *ur;

  pthread_mutex_lock(&update_request_lck);
  update_current_rev++;
  for (ur = update_requests; ur; ur = ur->next)
    {
      event_active(ur->updateev, 0, 0);
    }
  pthread_mutex_unlock(&update_request_lck);
}


/* --------------------- PROPERTIES GETTERS AND SETTERS --------------------- */

static void
dacp_propget_volume(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_int(evbuf, "cmvo", status->volume);
}

static void
dacp_propget_volumecontrollable(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_char(evbuf, "cavc", 1);
}

static void
dacp_propget_playerstate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_char(evbuf, "caps", status->status);
}

static void
dacp_propget_shufflestate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_char(evbuf, "cash", status->shuffle);
}

static void
dacp_propget_availableshufflestates(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_int(evbuf, "caas", 2);
}

static void
dacp_propget_repeatstate(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_char(evbuf, "carp", status->repeat);
}

static void
dacp_propget_availablerepeatstates(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dmap_add_int(evbuf, "caar", 6);
}

static void
dacp_propget_nowplaying(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dacp_nowplaying(evbuf, status, queue_item);
}

static void
dacp_propget_playingtime(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
  dacp_playingtime(evbuf, status, queue_item);
}

static void
dacp_propget_fullscreenenabled(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_fullscreen(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_visualizerenabled(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_visualizer(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_itms_songid(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_haschapterdata(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_mediakind(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

static void
dacp_propget_extendedmediakind(struct evbuffer *evbuf, struct player_status *status, struct db_queue_item *queue_item)
{
	// TODO
}

/* Properties setters */
static void
dacp_propset_volume(const char *value, struct httpd_request *hreq)
{
  const char *param;
  uint64_t id;
  int volume;
  int ret;

  ret = safe_atoi32(value, &volume);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "dmcp.volume argument doesn't convert to integer: %s\n", value);

      return;
    }

  param = httpd_query_value_find(hreq->query, "speaker-id");
  if (param)
    {
      ret = safe_atou64(param, &id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid speaker ID in dmcp.volume request\n");

	  return;
	}

      player_volume_setrel_speaker(id, volume);
      return;
    }

  param = httpd_query_value_find(hreq->query, "include-speaker-id");
  if (param)
    {
      ret = safe_atou64(param, &id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid speaker ID in dmcp.volume request\n");

	  return;
	}

      player_volume_setabs_speaker(id, volume);
      return;
    }

  player_volume_set(volume);
}

static void
dacp_propset_devicevolume(const char *value, struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;

  if (speaker_get(&speaker_info, hreq, "device-volume") < 0)
    return;

  player_volume_setraw_speaker(speaker_info.id, value);
}

// See player.c:speaker_prevent_playback_set() for comments regarding
// prevent-playback and busy properties
static void
dacp_propset_devicepreventplayback(const char *value, struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;

  if (speaker_get(&speaker_info, hreq, "device-prevent-playback") < 0)
    return;

  if (value[0] == '1')
    player_speaker_prevent_playback_set(speaker_info.id, true);
  else if (value[0] == '0')
    player_speaker_prevent_playback_set(speaker_info.id, false);
  else
    DPRINTF(E_LOG, L_DACP, "Request for setting device-prevent-playback has invalid value: '%s'\n", value);
}

static void
dacp_propset_devicebusy(const char *value, struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;

  if (speaker_get(&speaker_info, hreq, "device-busy") < 0)
    return;

  if (value[0] == '1')
    player_speaker_busy_set(speaker_info.id, true);
  else if (value[0] == '0')
    player_speaker_busy_set(speaker_info.id, false);
  else
    DPRINTF(E_LOG, L_DACP, "Request for setting device-busy has invalid value: '%s'\n", value);
}

static void
dacp_propset_playingtime(const char *value, struct httpd_request *hreq)
{
  struct timeval tv;
  int seek_target;
  intptr_t seek_target_packed;
  int ret;

  ret = safe_atoi32(value, &seek_target);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "dacp.playingtime argument doesn't convert to integer: %s\n", value);

      return;
    }

  seek_target_packed = seek_target;

  evutil_timerclear(&tv);
  tv.tv_usec = 200 * 1000;

  event_base_once(hreq->evbase, -1, EV_TIMEOUT, seek_timer_cb, (void *)seek_target_packed, &tv);
}

static void
dacp_propset_shufflestate(const char *value, struct httpd_request *hreq)
{
  int enable;
  int ret;

  ret = safe_atoi32(value, &enable);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "dacp.shufflestate argument doesn't convert to integer: %s\n", value);

      return;
    }

  player_shuffle_set(enable);
}

static void
dacp_propset_repeatstate(const char *value, struct httpd_request *hreq)
{
  int mode;
  int ret;

  ret = safe_atoi32(value, &mode);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "dacp.repeatstate argument doesn't convert to integer: %s\n", value);

      return;
    }

  player_repeat_set(mode);
}

static void
dacp_propset_userrating(const char *value, struct httpd_request *hreq)
{
  const char *param;
  uint32_t itemid;
  uint32_t rating;
  int ret;

  ret = safe_atou32(value, &rating);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "dacp.userrating argument doesn't convert to integer: %s\n", value);

      return;
    }

  param = httpd_query_value_find(hreq->query, "item-spec"); // Remote
  if (!param)
    param = httpd_query_value_find(hreq->query, "song-spec"); // Retune

  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Missing item-spec/song-spec parameter in dacp.userrating query\n");

      return;
    }

  param = strchr(param, ':');
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Malformed item-spec/song-spec parameter in dacp.userrating query\n");

      return;
    }

  param++;

  if (strncmp(param, "0x", 2) == 0)
    ret = safe_hextou32(param, &itemid);
  else
    ret = safe_atou32(param, &itemid);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Couldn't convert item-spec/song-spec to an integer in dacp.userrating (%s)\n", param);

      return;
    }

  library_item_attrib_save(itemid, LIBRARY_ATTRIB_RATING, rating);
}


/* --------------------------- REPLY HANDLERS ------------------------------- */

static int
dacp_reply_ctrlint(struct httpd_request *hreq)
{
  /* /ctrl-int */
  CHECK_ERR(L_DACP, evbuffer_expand(hreq->out_body, 256));

  /* If tags are added or removed container sizes should be adjusted too */
  dmap_add_container(hreq->out_body, "caci", 194); /*  8, unknown dacp container - size of content */
  dmap_add_int(hreq->out_body, "mstt", 200);       /* 12, dmap.status */
  dmap_add_char(hreq->out_body, "muty", 0);        /*  9, dmap.updatetype */
  dmap_add_int(hreq->out_body, "mtco", 1);         /* 12, dmap.specifiedtotalcount */
  dmap_add_int(hreq->out_body, "mrco", 1);         /* 12, dmap.returnedcount */
  dmap_add_container(hreq->out_body, "mlcl", 141); /*  8, dmap.listing - size of content */
  dmap_add_container(hreq->out_body, "mlit", 133); /*  8, dmap.listingitem - size of content */
  dmap_add_int(hreq->out_body, "miid", 1);         /* 12, dmap.itemid - database ID */
  dmap_add_char(hreq->out_body, "cmik", 1);        /*  9, unknown */

  dmap_add_int(hreq->out_body, "cmpr", (2 << 16 | 2)); /* 12, dmcp.protocolversion */
  dmap_add_int(hreq->out_body, "capr", (2 << 16 | 5)); /* 12, dacp.protocolversion */

  dmap_add_char(hreq->out_body, "cmsp", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "aeFR", 0x64);     /*  9, unknown */
  dmap_add_char(hreq->out_body, "cmsv", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "cass", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "caov", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "casu", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "ceSG", 1);        /*  9, unknown */
  dmap_add_char(hreq->out_body, "cmrl", 1);        /*  9, unknown */
  dmap_add_long(hreq->out_body, "ceSX", (1 << 1 | 1));  /* 16, unknown dacp - lowest bit announces support for playqueue-contents/-edit */

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;
}

static int
dacp_reply_cue_play(struct httpd_request *hreq)
{
  struct player_status status;
  const char *sort;
  const char *cuequery;
  const char *param;
  uint32_t pos;
  int clear;
  struct db_queue_item *queue_item = NULL;
  struct player_history *history;
  int ret;

  /* /cue?command=play&query=...&sort=...&index=N */

  param = httpd_query_value_find(hreq->query, "clear-first");
  if (param)
    {
      ret = safe_atoi32(param, &clear);
      if (ret < 0)
	DPRINTF(E_LOG, L_DACP, "Invalid clear-first value in cue request\n");
      else if (clear)
	{
	  player_playback_stop();

	  db_queue_clear(0);
	}
    }

  player_get_status(&status);

  cuequery = httpd_query_value_find(hreq->query, "query");
  if (cuequery)
    {
      sort = httpd_query_value_find(hreq->query, "sort");

      ret = dacp_queueitem_add(cuequery, NULL, sort, 0, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Could not build song queue\n");

	  dacp_send_error(hreq, "cacr", "Could not build song queue");
	  return -1;
	}
    }
  else if (status.status != PLAY_STOPPED)
    {
      player_playback_stop();
    }

  param = httpd_query_value_find(hreq->query, "dacp.shufflestate");
  if (param)
    dacp_propset_shufflestate(param, NULL);

  pos = 0;
  param = httpd_query_value_find(hreq->query, "index");
  if (param)
    {
      ret = safe_atou32(param, &pos);
      if (ret < 0)
	DPRINTF(E_LOG, L_DACP, "Invalid index (%s) in cue request\n", param);
    }

  /* If selection was from Up Next queue or history queue (command will be playnow), then index is relative */
  if ((param = httpd_query_value_find(hreq->query, "command")) && (strcmp(param, "playnow") == 0))
    {
      /* If mode parameter is -1 or 1, the index is relative to the history queue, otherwise to the Up Next queue */
      param = httpd_query_value_find(hreq->query, "mode");
      if (param && ((strcmp(param, "-1") == 0) || (strcmp(param, "1") == 0)))
	{
	  /* Play from history queue */
	  history = player_history_get();
	  if (history->count > pos)
	    {
	      pos = (history->start_index + history->count - pos - 1) % MAX_HISTORY_COUNT;

	      queue_item = db_queue_fetch_byitemid(history->item_id[pos]);
	      if (!queue_item)
		{
		  DPRINTF(E_LOG, L_DACP, "Could not start playback from history\n");

		  dacp_send_error(hreq, "cacr", "Playback failed to start");
		  return -1;
		}
	    }
	  else
	    {
	      DPRINTF(E_LOG, L_DACP, "Could not start playback from history\n");

	      dacp_send_error(hreq, "cacr", "Playback failed to start");
	      return -1;
	    }
	}
      else
	{
	  /* Play from Up Next queue */
	  if (status.status == PLAY_STOPPED)
            queue_item = db_queue_fetch_bypos(pos, status.shuffle);
          else
            queue_item = db_queue_fetch_byposrelativetoitem(pos, status.item_id, status.shuffle);
	  if (!queue_item)
	    {
	      DPRINTF(E_LOG, L_DACP, "Could not fetch item from queue: pos=%d, now playing=%d\n", pos, status.item_id);

	      dacp_send_error(hreq, "cacr", "Playback failed to start");
	      return -1;
	    }
	}
    }
  else
    {
      queue_item = db_queue_fetch_bypos(pos, status.shuffle);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_DACP, "Could not fetch item from queue: pos=%d\n", pos);

	  dacp_send_error(hreq, "cacr", "Playback failed to start");
	  return -1;
	}
    }

  ret = player_playback_start_byitem(queue_item);
  free_queue_item(queue_item, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not start playback\n");

      dacp_send_error(hreq, "cacr", "Playback failed to start");
      return -1;
    }

  player_get_status(&status);

  CHECK_ERR(L_DACP, evbuffer_expand(hreq->out_body, 64));

  dmap_add_container(hreq->out_body, "cacr", 24); /* 8 + len */
  dmap_add_int(hreq->out_body, "mstt", 200);      /* 12 */
  dmap_add_int(hreq->out_body, "miid", status.id);/* 12 */

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;
}

static int
dacp_reply_cue_clear(struct httpd_request *hreq)
{
  /* /cue?command=clear */

  player_playback_stop();

  db_queue_clear(0);

  CHECK_ERR(L_DACP, evbuffer_expand(hreq->out_body, 64));

  dmap_add_container(hreq->out_body, "cacr", 24); /* 8 + len */
  dmap_add_int(hreq->out_body, "mstt", 200);      /* 12 */
  dmap_add_int(hreq->out_body, "miid", 0);        /* 12 */

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;
}

static int
dacp_reply_cue(struct httpd_request *hreq)
{
  const char *param;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "command");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "No command in cue request\n");

      dacp_send_error(hreq, "cacr", "No command in cue request");
      return -1;
    }

  if (strcmp(param, "clear") == 0)
    return dacp_reply_cue_clear(hreq);
  else if (strcmp(param, "play") == 0)
    return dacp_reply_cue_play(hreq);
  else
    {
      DPRINTF(E_LOG, L_DACP, "Unknown cue command %s\n", param);

      dacp_send_error(hreq, "cacr", "Unknown command in cue request");
      return -1;
    }
}

static int
dacp_reply_play(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  ret = player_playback_start();
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playspec(struct httpd_request *hreq)
{
  struct player_status status;
  const char *param;
  const char *shuffle;
  uint32_t plid;
  uint32_t id;
  struct query_params qp = { 0 };
  struct db_queue_item *queue_item = NULL;
  int ret;

  /* /ctrl-int/1/playspec?database-spec='dmap.persistentid:0x1'&container-spec='dmap.persistentid:0x5'&container-item-spec='dmap.containeritemid:0x9'
   * or (Apple Remote when playing a Podcast)
   * /ctrl-int/1/playspec?database-spec='dmap.persistentid:0x1'&container-spec='dmap.persistentid:0x5'&item-spec='dmap.itemid:0x9'
   * With our DAAP implementation, container-spec is the playlist ID and container-item-spec/item-spec is the song ID
   */

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  /* Check for shuffle */
  shuffle = httpd_query_value_find(hreq->query, "dacp.shufflestate");

  /* Playlist ID */
  param = httpd_query_value_find(hreq->query, "container-spec");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "No container-spec in playspec request\n");
      goto out_fail;
    }

  param = strchr(param, ':');
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Malformed container-spec parameter in playspec request\n");
      goto out_fail;
    }
  param++;

  if (strncmp(param, "0x", 2) == 0)
    ret = safe_hextou32(param, &plid);
  else
    ret = safe_atou32(param, &plid);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Couldn't convert container-spec to an integer in playspec (%s)\n", param);
      goto out_fail;
    }

  if (!shuffle)
    {
      /* Start song ID */
      if ((param = httpd_query_value_find(hreq->query, "item-spec")))
	plid = 0; // This is a podcast/audiobook - just play a single item, not a playlist
      else if (!(param = httpd_query_value_find(hreq->query, "container-item-spec")))
	{
	  DPRINTF(E_LOG, L_DACP, "No container-item-spec/item-spec in playspec request\n");
	  goto out_fail;
	}

      param = strchr(param, ':');
      if (!param)
	{
	  DPRINTF(E_LOG, L_DACP, "Malformed container-item-spec/item-spec parameter in playspec request\n");
	  goto out_fail;
	}
      param++;

      if (strncmp(param, "0x", 2) == 0)
	ret = safe_hextou32(param, &id);
      else
	ret = safe_atou32(param, &id);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Couldn't convert container-item-spec/item-spec to an integer in playspec (%s)\n", param);
	  goto out_fail;
	}
    }
  else
    id = 0;

  DPRINTF(E_DBG, L_DACP, "Playspec request for playlist %d, start song id %d%s\n", plid, id, (shuffle) ? ", shuffle" : "");

  player_get_status(&status);

  if (status.status != PLAY_STOPPED)
    player_playback_stop();

  db_queue_clear(0);

  qp.type = (plid > 0) ? Q_PLITEMS : Q_ITEMS;
  qp.id = (plid > 0) ? plid : id;

  ret = db_queue_add_by_query(&qp, status.shuffle, status.item_id, -1, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not build song queue from playlist %d\n", plid);
      goto out_fail;
    }

  player_queue_plid(plid);

  if (shuffle)
    dacp_propset_shufflestate(shuffle, NULL);

  if (id > 0)
    queue_item = db_queue_fetch_byfileid(id);

  if (queue_item)
    {
      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    ret = player_playback_start();

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not start playback\n");
      goto out_fail;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);
  return 0;

 out_fail:
  httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");

  return -1;
}

static int
dacp_reply_stop(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  player_playback_stop();

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_pause(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  player_playback_pause();

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playpause(struct httpd_request *hreq)
{
  struct player_status status;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  player_get_status(&status);
  if (status.status == PLAY_PLAYING)
    {
      player_playback_pause();
    }
  else
    {
      ret = player_playback_start();
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Player returned an error for start after pause\n");

	  httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
	  return -1;
        }
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_nextitem(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  ret = player_playback_next();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Player returned an error for nextitem\n");

      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Player returned an error for start after nextitem\n");

      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_previtem(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  ret = player_playback_prev();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Player returned an error for previtem\n");

      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  ret = player_playback_start();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Player returned an error for start after previtem\n");

      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_beginff(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  /* TODO */

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_beginrew(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  /* TODO */

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playresume(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  /* TODO */

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playqueuecontents(struct httpd_request *hreq)
{
  struct evbuffer *songlist;
  struct evbuffer *playlists;
  struct player_status status;
  struct player_history *history;
  const char *param;
  size_t songlist_length;
  size_t playlist_length;
  int span;
  int count;
  int ret;
  int start_index;
  struct query_params qp;
  struct db_queue_item queue_item;

  /* /ctrl-int/1/playqueue-contents?span=50&session-id=... */

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  DPRINTF(E_DBG, L_DACP, "Fetching playqueue contents\n");

  span = 50; /* Default */
  param = httpd_query_value_find(hreq->query, "span");
  if (param)
    {
      ret = safe_atoi32(param, &span);
      if (ret < 0)
	DPRINTF(E_LOG, L_DACP, "Invalid span value in playqueue-contents request\n");
    }

  CHECK_NULL(L_DACP, songlist = evbuffer_new());
  CHECK_ERR(L_DACP, evbuffer_expand(hreq->out_body, 128));

  player_get_status(&status);

  count = 0; // count of songs in songlist

  /*
   * If the span parameter is negativ make song list for Previously Played,
   * otherwise make song list for Up Next and begin with first song after playlist position.
   */
  if (span < 0)
    {
      history = player_history_get();
      if (abs(span) > history->count)
	{
	  start_index = history->start_index;
	}
      else
	{
	  start_index = (history->start_index + history->count - abs(span)) % MAX_HISTORY_COUNT;
	}

      while ((count < history->count) && (count < abs(span)))
	{
	  ret = playqueuecontents_add_source(songlist, history->id[(start_index + count) % MAX_HISTORY_COUNT], (count + 1), status.plid);
	  if (ret < 0)
	    goto error;

	  count++;
	}
    }
  else
    {
      memset(&qp, 0, sizeof(struct query_params));

      if (status.shuffle)
	qp.sort = S_SHUFFLE_POS;

      ret = db_queue_enum_start(&qp);
      if (ret < 0)
	goto error;

      while ((db_queue_enum_fetch(&qp, &queue_item) == 0) && (queue_item.id > 0) && (count < span))
	{
	  if (status.item_id == 0 || status.item_id == queue_item.id)
	    {
	      count = 1;
	    }
	  else if (count > 0)
	    {
	      ret = playqueuecontents_add_queue_item(songlist, &queue_item, count, status.plid);
	      if (ret < 0)
		{
		  db_queue_enum_end(&qp);
		  goto error;
		}

	      count++;
	    }
	}

      db_queue_enum_end(&qp);
    }

  /* Playlists are hist, curr and main. */
  CHECK_NULL(L_DACP, playlists = evbuffer_new());
  CHECK_ERR(L_DACP, evbuffer_expand(playlists, 256));

  dmap_add_container(playlists, "mlit", 61);               /*  8 */
  dmap_add_string(playlists, "ceQk", "hist");              /* 12 */
  dmap_add_int(playlists, "ceQi", -200);                   /* 12 */
  dmap_add_int(playlists, "ceQm", 200);                    /* 12 */
  dmap_add_string(playlists, "ceQl", "Previously Played"); /* 25 = 8 + 17 */

  if (count > 0)
    {
      dmap_add_container(playlists, "mlit", 36);             /*  8 */
      dmap_add_string(playlists, "ceQk", "curr");            /* 12 */
      dmap_add_int(playlists, "ceQi", 0);                    /* 12 */
      dmap_add_int(playlists, "ceQm", 1);                    /* 12 */

      dmap_add_container(playlists, "mlit", 69);
      dmap_add_string(playlists, "ceQk", "main");            /* 12 */
      dmap_add_int(playlists, "ceQi", 1);                    /* 12 */
      dmap_add_int(playlists, "ceQm", count);                /* 12 */
      dmap_add_string(playlists, "ceQl", "Up Next");         /* 15 = 8 + 7 */
      dmap_add_string(playlists, "ceQh", "from Music");      /* 18 = 8 + 10 */

      songlist_length = evbuffer_get_length(songlist);
    }
  else
    songlist_length = 0;

  /* Final construction of reply */
  playlist_length = evbuffer_get_length(playlists);
  dmap_add_container(hreq->out_body, "ceQR", 79 + playlist_length + songlist_length); /* size of entire container */
  dmap_add_int(hreq->out_body, "mstt", 200);                                          /* 12, dmap.status */
  dmap_add_int(hreq->out_body, "mtco", abs(span));                                    /* 12 */
  dmap_add_int(hreq->out_body, "mrco", count);                                        /* 12 */
  dmap_add_char(hreq->out_body, "ceQu", 0);                                           /*  9 */
  dmap_add_container(hreq->out_body, "mlcl", 8 + playlist_length + songlist_length);  /*  8 */
  dmap_add_container(hreq->out_body, "ceQS", playlist_length);                        /*  8 */

  CHECK_ERR(L_DACP, evbuffer_add_buffer(hreq->out_body, playlists));
  CHECK_ERR(L_DACP, evbuffer_add_buffer(hreq->out_body, songlist));

  evbuffer_free(playlists);
  evbuffer_free(songlist);

  dmap_add_char(hreq->out_body, "apsm", status.shuffle); /*  9, daap.playlistshufflemode - not part of mlcl container */
  dmap_add_char(hreq->out_body, "aprm", status.repeat);  /*  9, daap.playlistrepeatmode  - not part of mlcl container */

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;

 error:
  DPRINTF(E_LOG, L_DACP, "Database error in dacp_reply_playqueuecontents\n");

  evbuffer_free(songlist);
  dacp_send_error(hreq, "ceQR", "Database error");

  return -1;
}

static int
dacp_reply_playqueueedit_clear(struct httpd_request *hreq)
{
  const char *param;
  struct player_status status;

  param = httpd_query_value_find(hreq->query, "mode");

  /*
   * The mode parameter contains the playlist to be cleared.
   * If mode=0x68697374 (hex representation of the ascii string "hist") clear the history,
   * otherwise the current playlist.
   */
  if (strcmp(param,"0x68697374") == 0)
    player_queue_clear_history();
  else
    {
      player_get_status(&status);
      db_queue_clear(status.item_id);
    }

  dmap_add_container(hreq->out_body, "cacr", 24); /* 8 + len */
  dmap_add_int(hreq->out_body, "mstt", 200);      /* 12 */
  dmap_add_int(hreq->out_body, "miid", 0);        /* 12 */

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;
}

static int
dacp_reply_playqueueedit_add(struct httpd_request *hreq)
{
  //?command=add&query='dmap.itemid:156'&sort=album&mode=3&session-id=100
  // -> mode=3: add to playqueue position 0 (play next)
  //?command=add&query='dmap.itemid:158'&sort=album&mode=0&session-id=100
  // -> mode=0: add to end of playqueue
  //?command=add&query='dmap.itemid:306'&queuefilter=album:6525753023700533274&sort=album&mode=1&session-id=100
  // -> mode 1: stop playblack, clear playqueue, add songs to playqueue
  //?command=add&query='dmap.itemid:2'&query-modifier=containers&sort=name&mode=2&session-id=100
  // -> mode 2: stop playblack, clear playqueue, add shuffled songs from playlist=itemid to playqueue

  const char *editquery;
  const char *queuefilter;
  const char *querymodifier;
  const char *sort;
  const char *param;
  char modifiedquery[32];
  int mode;
  int plid;
  int ret;
  int quirkyquery;
  struct db_queue_item *queue_item;
  struct player_status status;

  mode = 1;

  param = httpd_query_value_find(hreq->query, "mode");
  if (param)
    {
      ret = safe_atoi32(param, &mode);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid mode value in playqueue-edit request\n");

	  dacp_send_error(hreq, "cacr", "Invalid request");
	  return -1;
	}
    }

  if ((mode == 1) || (mode == 2))
    {
      player_playback_stop();
      db_queue_clear(0);
    }

  if (mode == 2)
    player_shuffle_set(1);

  editquery = httpd_query_value_find(hreq->query, "query");
  if (!editquery)
    {
      DPRINTF(E_LOG, L_DACP, "Could not add song queue, DACP query missing\n");

      dacp_send_error(hreq, "cacr", "Invalid request");
      return -1;
    }

  sort = httpd_query_value_find(hreq->query, "sort");

  // if sort param is missing and an album or artist is added to the queue, set sort to "album"
  if (!sort && (strstr(editquery, "daap.songalbumid:") || strstr(editquery, "daap.songartistid:")))
    {
      sort = "album";
    }

  // only use queryfilter if mode is not equal 0 (add to up next), 3 (play next) or 5 (add to up next)
  queuefilter = (mode == 0 || mode == 3 || mode == 5) ? NULL : httpd_query_value_find(hreq->query, "queuefilter");

  querymodifier = httpd_query_value_find(hreq->query, "query-modifier");
  if (!querymodifier || (strcmp(querymodifier, "containers") != 0))
    {
      quirkyquery = (mode == 1) && strstr(editquery, "dmap.itemid:") && ((!queuefilter) || strstr(queuefilter, "(null)"));
      ret = dacp_queueitem_add(editquery, queuefilter, sort, quirkyquery, mode);
    }
  else
    {
      // Modify the query: Take the id from the editquery and use it as a queuefilter playlist id
      ret = safe_atoi32(strchr(editquery, ':') + 1, &plid);
      if (ret < 0)
        {
	  DPRINTF(E_LOG, L_DACP, "Invalid playlist id in request: %s\n", editquery);

	  dacp_send_error(hreq, "cacr", "Invalid request");
	  return -1;
	}

      snprintf(modifiedquery, sizeof(modifiedquery), "playlist:%d", plid);
      ret = dacp_queueitem_add(NULL, modifiedquery, sort, 0, mode);
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not build song queue\n");

      dacp_send_error(hreq, "cacr", "Invalid request");
      return -1;
    }

  if (ret > 0)
    queue_item = db_queue_fetch_byfileid(ret);
  else
    queue_item = NULL;

  if (queue_item)
    {
      player_get_status(&status);

      if (status.shuffle)
	{
	  DPRINTF(E_DBG, L_DACP, "Start shuffle queue with item %d\n", queue_item->id);
	  db_queue_move_byitemid(queue_item->id, 0, status.shuffle);
	}

      DPRINTF(E_DBG, L_DACP, "Song queue built, starting playback at index %d\n", queue_item->pos);
      ret = player_playback_start_byitem(queue_item);
      free_queue_item(queue_item, 0);
    }
  else
    {
      DPRINTF(E_DBG, L_DACP, "Song queue built, starting playback\n");
      ret = player_playback_start();
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not start playback\n");

      dacp_send_error(hreq, "cacr", "Playback failed to start");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playqueueedit_move(struct httpd_request *hreq)
{
  /*
   * Handles the move command.
   * Exampe request:
   * playqueue-edit?command=move&edit-params='edit-param.move-pair:3,0'&session-id=100
   *
   * The 'edit-param.move-pair' param contains the index of the song in the playqueue to be moved (index 3 in the example)
   * and the index of the song after which it should be inserted (index 0 in the exampe, the now playing song).
   */
  struct player_status status;
  int ret;
  const char *param;
  int src;
  int dst;

  param = httpd_query_value_find(hreq->query, "edit-params");
  if (param)
  {
    ret = safe_atoi32(strchr(param, ':') + 1, &src);
    if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Invalid edit-params move-from value in playqueue-edit request\n");

      dacp_send_error(hreq, "cacr", "Invalid request");
      return -1;
    }

    ret = safe_atoi32(strchr(param, ',') + 1, &dst);
    if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Invalid edit-params move-to value in playqueue-edit request\n");

      dacp_send_error(hreq, "cacr", "Invalid request");
      return -1;
    }

    player_get_status(&status);
    db_queue_move_byposrelativetoitem(src, dst, status.item_id, status.shuffle);
  }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playqueueedit_remove(struct httpd_request *hreq)
{
  /*
   * Handles the remove command.
   * Exampe request (removes song at position 1 in the playqueue):
   * ?command=remove&items=1&session-id=100
   */
  struct player_status status;
  const char *param;
  int item_index;
  int ret;

  param = httpd_query_value_find(hreq->query, "items");
  if (param)
  {
    ret = safe_atoi32(param, &item_index);
    if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Invalid edit-params remove item value in playqueue-edit request\n");

      dacp_send_error(hreq, "cacr", "Invalid request");
      return -1;
    }

    player_get_status(&status);

    db_queue_delete_byposrelativetoitem(item_index, status.item_id, status.shuffle);
  }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_playqueueedit(struct httpd_request *hreq)
{
  const char *param;
  int ret;

  /*  Variations of /ctrl-int/1/playqueue-edit and expected behaviour
      User selected play (album or artist tab):
	?command=add&query='...'&sort=album&mode=1&session-id=...
	-> clear queue, play query results
      User selected play (playlist):
	?command=add&query='dmap.itemid:...'&query-modifier=containers&mode=1&session-id=...
	-> clear queue, play playlist with the id specified by itemid
      User selected track (album tab):
	?command=add&query='dmap.itemid:...'&queuefilter=album:...&sort=album&mode=1&session-id=...
	-> clear queue, play itemid and the rest of album
      User selected track (artist tab):
	?command=add&query='dmap.itemid:...'&queuefilter=artist:...&sort=album&mode=1&session-id=...
	-> clear queue, play itemid and the rest of artist tracks
      User selected track (song tab):
	?command=add&query='dmap.itemid:...'&queuefilter=playlist:...&sort=name&mode=1&session-id=...
	-> clear queue, play itemid and the rest of playlist
      User selected track (playlist tab):
	?command=add&query='dmap.containeritemid:...'&queuefilter=playlist:...&sort=physical&mode=1&session-id=...
	-> clear queue, play containeritemid and the rest of playlist
      User selected shuffle (artist tab):
	?command=add&query='...'&sort=album&mode=2&session-id=...
	-> clear queue, play shuffled query results
      User selected add item to queue:
	?command=add&query='...'&sort=album&mode=0&session-id=...
	-> add query results to queue
      User selected play next song (album tab)
	?command=add&query='daap.songalbumid:...'&sort=album&mode=3&session-id=...
	-> replace queue from after current song with query results
      User selected track in queue:
	?command=playnow&index=...&session-id=...
	-> play index

      And the quirky query from Remote - no sort and no queuefilter
      User selected track (Audiobooks):
	?command=add&query='dmap.itemid:...'&mode=1&session-id=...
	-> clear queue, play itemid and the rest of album tracks

	?command=move&edit-params='edit-param.move-pair:3,0'&session-id=100
	-> move song from playqueue position 3 to be played after song at position 0
	?command=remove&items=1&session-id=100
  -> remove song on position 1 from the playqueue
   */

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "command");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "No command in playqueue-edit request\n");

      dacp_send_error(hreq, "cmst", "Invalid request");
      return -1;
    }

  if (strcmp(param, "clear") == 0)
    return dacp_reply_playqueueedit_clear(hreq);
  else if (strcmp(param, "playnow") == 0)
    return dacp_reply_cue_play(hreq);
  else if (strcmp(param, "add") == 0)
    return dacp_reply_playqueueedit_add(hreq);
  else if (strcmp(param, "move") == 0)
    return dacp_reply_playqueueedit_move(hreq);
  else if (strcmp(param, "remove") == 0)
    return dacp_reply_playqueueedit_remove(hreq);
  else
    {
      DPRINTF(E_LOG, L_DACP, "Unknown playqueue-edit command %s\n", param);

      dacp_send_error(hreq, "cmst", "Invalid request");
      return -1;
    }
}

static int
dacp_reply_playstatusupdate(struct httpd_request *hreq)
{
  struct dacp_update_request *ur;
  const char *param;
  int reqd_rev;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "revision-number");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Missing revision-number in update request\n");

      dacp_send_error(hreq, "cmst", "Invalid request");
      return -1;
    }

  ret = safe_atoi32(param, &reqd_rev);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Parameter revision-number not an integer\n");

      dacp_send_error(hreq, "cmst", "Invalid request");
      return -1;
    }

  // Caller didn't use current revision number. It was probably his first
  // request so we will give him status immediately, incl. which revision number
  // to use when he calls again.
  if (reqd_rev != update_current_rev)
    {
      ret = make_playstatusupdate(hreq->out_body, update_current_rev);
      if (ret < 0)
	httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      else
	httpd_send_reply(hreq, HTTP_OK, "OK", 0);

      return ret;
    }

  // Else, just let the request hang until we have changes to push back
  ur = update_request_new(hreq);
  if (!ur)
    {
      DPRINTF(E_LOG, L_DACP, "Out of memory for update request\n");

      dacp_send_error(hreq, "cmst", "Out of memory");
      return -1;
    }

  pthread_mutex_lock(&update_request_lck);
  ur->next = update_requests;
  update_requests = ur;
  pthread_mutex_unlock(&update_request_lck);

  // If the connection fails before we have an update to push out to the client,
  // we need to know.
  httpd_request_close_cb_set(hreq, update_fail_cb, ur);

  return 0;
}

static int
dacp_reply_nowplayingartwork(struct httpd_request *hreq)
{
  char clen[32];
  const char *param;
  char *ctype;
  size_t len;
  uint32_t id;
  int max_w;
  int max_h;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "mw");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Request for artwork without mw parameter\n");
      goto error;
    }

  ret = safe_atoi32(param, &max_w);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not convert mw parameter to integer\n");
      goto error;
    }

  param = httpd_query_value_find(hreq->query, "mh");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Request for artwork without mh parameter\n");
      goto error;
    }

  ret = safe_atoi32(param, &max_h);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not convert mh parameter to integer\n");
      goto error;
    }

  ret = player_playing_now(&id);
  if (ret < 0)
    goto no_artwork;

  ret = artwork_get_item(hreq->out_body, id, max_w, max_h, 0);
  len = evbuffer_get_length(hreq->out_body);

  switch (ret)
    {
      case ART_FMT_PNG:
	ctype = "image/png";
	break;

      case ART_FMT_JPEG:
	ctype = "image/jpeg";
	break;

      default:
	if (len > 0)
	  evbuffer_drain(hreq->out_body, len);

	goto no_artwork;
    }

  httpd_header_remove(hreq->out_headers, "Content-Type");
  httpd_header_add(hreq->out_headers, "Content-Type", ctype);
  snprintf(clen, sizeof(clen), "%ld", (long)len);
  httpd_header_add(hreq->out_headers, "Content-Length", clen);

  httpd_send_reply(hreq, HTTP_OK, "OK", HTTPD_SEND_NO_GZIP);
  return 0;

 no_artwork:
  httpd_send_error(hreq, HTTP_NOTFOUND, "Not Found");
  return 0;

 error:
  httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
  return -1;
}

static int
dacp_reply_getproperty(struct httpd_request *hreq)
{
  struct player_status status;
  const struct dacp_prop_map *dpm;
  struct db_queue_item *queue_item = NULL;
  struct evbuffer *proplist;
  const char *param;
  char *ptr;
  char *prop;
  char *propstr;
  size_t len;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "properties");
  if (!param)
    {
      DPRINTF(E_WARN, L_DACP, "Invalid DACP getproperty request, no properties\n");

      dacp_send_error(hreq, "cmgt", "Invalid request");
      return -1;
    }

  propstr = strdup(param);
  if (!propstr)
    {
      DPRINTF(E_LOG, L_DACP, "Could not duplicate properties parameter; out of memory\n");

      dacp_send_error(hreq, "cmgt", "Out of memory");
      return -1;
    }

  proplist = evbuffer_new();
  if (!proplist)
    {
      DPRINTF(E_LOG, L_DACP, "Could not allocate evbuffer for properties list\n");

      dacp_send_error(hreq, "cmgt", "Out of memory");
      goto out_free_propstr;
    }

  player_get_status(&status);

  if (status.status != PLAY_STOPPED)
    {
      queue_item = db_queue_fetch_byitemid(status.item_id);
      if (!queue_item)
	{
	  DPRINTF(E_LOG, L_DACP, "Could not fetch queue_item for item-id %d\n", status.item_id);

	  dacp_send_error(hreq, "cmgt", "Server error");
	  goto out_free_proplist;
	}
    }

  prop = strtok_r(propstr, ",", &ptr);
  while (prop)
    {
      dpm = dacp_find_prop(prop, strlen(prop));
      if (dpm)
	{
	  if (dpm->propget)
	    dpm->propget(proplist, &status, queue_item);
	  else
	    DPRINTF(E_WARN, L_DACP, "No getter method for DACP property %s\n", prop);
	}
      else
	DPRINTF(E_LOG, L_DACP, "Could not find requested property '%s'\n", prop);

      prop = strtok_r(NULL, ",", &ptr);
    }

  free(propstr);

  if (queue_item)
    free_queue_item(queue_item, 0);

  len = evbuffer_get_length(proplist);
  dmap_add_container(hreq->out_body, "cmgt", 12 + len);
  dmap_add_int(hreq->out_body, "mstt", 200);      /* 12 */

  CHECK_ERR(L_DACP, evbuffer_add_buffer(hreq->out_body, proplist));

  evbuffer_free(proplist);

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;

 out_free_proplist:
  evbuffer_free(proplist);

 out_free_propstr:
  free(propstr);

  return -1;
}

static void
setproperty_cb(const char *key, const char *val, void *arg)
{
  struct httpd_request *hreq = arg;
  const struct dacp_prop_map *dpm = dacp_find_prop(key, strlen(key));

  if (!dpm)
    {
      DPRINTF(E_SPAM, L_DACP, "Unknown DACP property %s\n", key);
      return;
    }

  if (dpm->propset)
    dpm->propset(val, hreq);
  else
    DPRINTF(E_WARN, L_DACP, "No setter method for DACP property %s\n", dpm->desc);
}

static int
dacp_reply_setproperty(struct httpd_request *hreq)
{
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  /* Known properties (see dacp_prop.gperf):
   * dacp.shufflestate                0/1
   * dacp.repeatstate                 0/1/2
   * dacp.playingtime                 seek to time in ms
   * dmcp.volume                      0-100, float
   * dmcp.device-volume               -144-0, float (raop volume)
   * dmcp.device-prevent-playback     0/1
   * dmcp.device-busy                 0/1
   */

  /* /ctrl-int/1/setproperty?dacp.shufflestate=1&session-id=100 */

  httpd_query_iterate(hreq->query, setproperty_cb, hreq);

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_getspeakers(struct httpd_request *hreq)
{
  struct evbuffer *spklist;
  size_t len;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  CHECK_NULL(L_DACP, spklist = evbuffer_new());

  player_speaker_enumerate(speaker_enum_cb, spklist);

  len = evbuffer_get_length(spklist);
  dmap_add_container(hreq->out_body, "casp", 12 + len);
  dmap_add_int(hreq->out_body, "mstt", 200); /* 12 */

  evbuffer_add_buffer(hreq->out_body, spklist);

  evbuffer_free(spklist);

  httpd_send_reply(hreq, HTTP_OK, "OK", 0);

  return 0;
}

static int
dacp_reply_setspeakers(struct httpd_request *hreq)
{
  const char *param;
  const char *ptr;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  ret = dacp_request_authorize(hreq);
  if (ret < 0)
    return -1;

  param = httpd_query_value_find(hreq->query, "speaker-id");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Missing speaker-id parameter in DACP setspeakers request\n");

      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  if (strlen(param) == 0)
    {
      ids = NULL;
      goto fastpath;
    }

  nspk = 1;
  ptr = param;
  while ((ptr = strchr(ptr + 1, ',')))
    nspk++;

  CHECK_NULL(L_DACP, ids = calloc((nspk + 1), sizeof(uint64_t)));

  param--;
  i = 1;
  do
    {
      param++;

      // Some like Remote will give us hex, others will give us decimal (e.g. Hyperfine)
      if (strncmp(param, "0x", 2) == 0)
	ret = safe_hextou64(param, &ids[i]);
      else
	ret = safe_atou64(param, &ids[i]);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid speaker id in request: %s\n", param);

	  nspk--;
	  continue;
	}
      else
	{
	  DPRINTF(E_DBG, L_DACP, "Speaker id converted with ret %d, param %s, dec val %" PRIu64 ".\n", ret, param, ids[i]);
	}
      i++;
    }
  while ((param = strchr(param + 1, ',')));

  ids[0] = nspk;

 fastpath:
  ret = player_speaker_set(ids);

  if (ids)
    free(ids);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Speakers de/activation failed!\n");

      /* Password problem */
      if (ret == -2)
	httpd_send_error(hreq, 902, "");
      else
	httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");

      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_volumeup(struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;
  int ret;

  ret = speaker_get(&speaker_info, hreq, "volumeup");
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  ret = speaker_volume_step(&speaker_info, DACP_VOLUME_STEP);
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_volumedown(struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;
  int ret;

  ret = speaker_get(&speaker_info, hreq, "volumedown");
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  ret = speaker_volume_step(&speaker_info, -DACP_VOLUME_STEP);
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static int
dacp_reply_mutetoggle(struct httpd_request *hreq)
{
  struct player_speaker_info speaker_info;
  int ret;

  ret = speaker_get(&speaker_info, hreq, "mutetoggle");
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  // We don't actually mute, because the player doesn't currently support unmuting
  ret = speaker_info.selected ? player_speaker_disable(speaker_info.id) : player_speaker_enable(speaker_info.id);
  if (ret < 0)
    {
      httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
      return -1;
    }

  /* 204 No Content is the canonical reply */
  httpd_send_reply(hreq, HTTP_NOCONTENT, "No Content", HTTPD_SEND_NO_GZIP);

  return 0;
}

static struct httpd_uri_map dacp_handlers[] =
  {
    {
      .regexp = "^/ctrl-int$",
      .handler = dacp_reply_ctrlint
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/cue$",
      .handler = dacp_reply_cue
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/play$",
      .handler = dacp_reply_play
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playspec$",
      .handler = dacp_reply_playspec
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/stop$",
      .handler = dacp_reply_stop
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/pause$",
      .handler = dacp_reply_pause
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/discrete-pause$",
      .handler = dacp_reply_playpause
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playpause$",
      .handler = dacp_reply_playpause
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/nextitem$",
      .handler = dacp_reply_nextitem
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/previtem$",
      .handler = dacp_reply_previtem
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/beginff$",
      .handler = dacp_reply_beginff
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/beginrew$",
      .handler = dacp_reply_beginrew
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playresume$",
      .handler = dacp_reply_playresume
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playstatusupdate$",
      .handler = dacp_reply_playstatusupdate,
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playqueue-contents$",
      .handler = dacp_reply_playqueuecontents
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playqueue-edit$",
      .handler = dacp_reply_playqueueedit
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/nowplayingartwork$",
      .handler = dacp_reply_nowplayingartwork
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/getproperty$",
      .handler = dacp_reply_getproperty
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/setproperty$",
      .handler = dacp_reply_setproperty
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/getspeakers$",
      .handler = dacp_reply_getspeakers
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/setspeakers$",
      .handler = dacp_reply_setspeakers
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/volumeup$",
      .handler = dacp_reply_volumeup
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/volumedown$",
      .handler = dacp_reply_volumedown
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/mutetoggle$",
      .handler = dacp_reply_mutetoggle
    },
    {
      .regexp = NULL,
      .handler = NULL
    }
  };


/* ------------------------------- DACP API --------------------------------- */

static void
dacp_request(struct httpd_request *hreq)
{
  if (!hreq->handler)
    {
      DPRINTF(E_LOG, L_DACP, "Unrecognized path in DACP request: '%s'\n", hreq->uri);

      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  httpd_header_add(hreq->out_headers, "DAAP-Server", PACKAGE_NAME "/" VERSION);
  /* Content-Type for all DACP replies; can be overriden as needed */
  httpd_header_add(hreq->out_headers, "Content-Type", "application/x-dmap-tagged");

  hreq->handler(hreq);
}

static int
dacp_init(void)
{
  dummy_mfi.id = DB_MEDIA_FILE_NON_PERSISTENT_ID;
  dummy_mfi.title = CFG_NAME_UNKNOWN_TITLE;
  dummy_mfi.artist = CFG_NAME_UNKNOWN_ARTIST;
  dummy_mfi.album = CFG_NAME_UNKNOWN_ALBUM;
  dummy_mfi.genre = CFG_NAME_UNKNOWN_GENRE;

  dummy_queue_item.file_id = DB_MEDIA_FILE_NON_PERSISTENT_ID;
  dummy_queue_item.title = CFG_NAME_UNKNOWN_TITLE;
  dummy_queue_item.artist = CFG_NAME_UNKNOWN_ARTIST;
  dummy_queue_item.album = CFG_NAME_UNKNOWN_ALBUM;
  dummy_queue_item.genre = CFG_NAME_UNKNOWN_GENRE;

  CHECK_ERR(L_DACP, mutex_init(&update_request_lck));
  update_current_rev = 2;
  listener_add(dacp_playstatus_update_handler, LISTENER_PLAYER | LISTENER_VOLUME | LISTENER_QUEUE, NULL);

  return 0;
}

static void
dacp_deinit(void)
{
  struct dacp_update_request *ur;

  listener_remove(dacp_playstatus_update_handler);

  for (ur = update_requests; update_requests; ur = update_requests)
    {
      update_requests = ur->next;

      httpd_send_error(ur->hreq, HTTP_SERVUNAVAIL, "Service Unavailable");
      update_request_free(ur);
    }
}

struct httpd_module httpd_dacp =
{
  .name = "DACP",
  .type = MODULE_DACP,
  .logdomain = L_DACP,
  .subpaths = { "/ctrl-int/", NULL },
  .fullpaths = { "/ctrl-int", NULL },
  .handlers = dacp_handlers,
  .init = dacp_init,
  .deinit = dacp_deinit,
  .request = dacp_request,
};
