/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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

#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <uninorm.h>
#include <unistr.h>

#include <event2/event.h>

#include "library.h"
#include "cache.h"
#include "commands.h"
#include "conffile.h"
#include "db.h"
#include "logger.h"
#include "misc.h"
#include "listener.h"
#include "player.h"

struct playlist_add_param
{
  const char *vp_playlist;
  const char *vp_item;
};

static struct commands_base *cmdbase;
static pthread_t tid_library;

struct event_base *evbase_lib;

extern struct library_source filescanner;
#ifdef HAVE_SPOTIFY_H
extern struct library_source spotifyscanner;
#endif

static struct library_source *sources[] = {
    &filescanner,
#ifdef HAVE_SPOTIFY_H
    &spotifyscanner,
#endif
    NULL
};

/* Flag for aborting scan on exit */
static bool scan_exit;

/* Flag for scan in progress */
static bool scanning;

// After being told by db that the library was updated through
// library_update_trigger(), wait 5 seconds before notifying listeners
// of LISTENER_DATABASE. This is to catch bulk updates like automated
// tag editing, music file imports/renames.  This way multiple updates
// are collected for a single update notification (useful to avoid
// repeated library reads from clients).
//
// Note: this update delay does not apply to library scans.  The scans
// use the flag `scanning` for deferring update notifcations.
static struct timeval library_update_wait = { 5, 0 };
static struct event *updateev;

// Counts the number of changes made to the database between to DATABASE
// event notifications
static unsigned int deferred_update_notifications;
static short deferred_update_events;

static bool
handle_deferred_update_notifications(void)
{
  bool ret = (deferred_update_notifications > 0);

  if (ret)
    {
      DPRINTF(E_DBG, L_LIB, "Database changed (%d changes)\n", deferred_update_notifications);

      deferred_update_notifications = 0;
      db_admin_setint64(DB_ADMIN_DB_UPDATE, (int64_t) time(NULL));
    }

  return ret;
}

static void
sort_tag_create(char **sort_tag, char *src_tag)
{
  const uint8_t *i_ptr;
  const uint8_t *n_ptr;
  const uint8_t *number;
  uint8_t out[1024];
  uint8_t *o_ptr;
  int append_number;
  ucs4_t puc;
  int numlen;
  size_t len;
  int charlen;

  /* Note: include terminating NUL in string length for u8_normalize */

  if (*sort_tag)
    {
      DPRINTF(E_DBG, L_LIB, "Existing sort tag will be normalized: %s\n", *sort_tag);
      o_ptr = u8_normalize(UNINORM_NFD, (uint8_t *)*sort_tag, strlen(*sort_tag) + 1, NULL, &len);
      free(*sort_tag);
      *sort_tag = (char *)o_ptr;
      return;
    }

  if (!src_tag || ((len = strlen(src_tag)) == 0))
    {
      *sort_tag = NULL;
      return;
    }

  // Set input pointer past article if present
  if ((strncasecmp(src_tag, "a ", 2) == 0) && (len > 2))
    i_ptr = (uint8_t *)(src_tag + 2);
  else if ((strncasecmp(src_tag, "an ", 3) == 0) && (len > 3))
    i_ptr = (uint8_t *)(src_tag + 3);
  else if ((strncasecmp(src_tag, "the ", 4) == 0) && (len > 4))
    i_ptr = (uint8_t *)(src_tag + 4);
  else
    i_ptr = (uint8_t *)src_tag;

  // Poor man's natural sort. Makes sure we sort like this: a1, a2, a10, a11, a21, a111
  // We do this by padding zeroes to (short) numbers. As an alternative we could have
  // made a proper natural sort algorithm in sqlext.c, but we don't, since we don't
  // want any risk of hurting response times
  memset(&out, 0, sizeof(out));
  o_ptr = (uint8_t *)&out;
  number = NULL;
  append_number = 0;

  do
    {
      n_ptr = u8_next(&puc, i_ptr);

      if (uc_is_digit(puc))
	{
	  if (!number) // We have encountered the beginning of a number
	    number = i_ptr;
	  append_number = (n_ptr == NULL); // If last char in string append number now
	}
      else
	{
	  if (number)
	    append_number = 1; // A number has ended so time to append it
	  else
	    {
              charlen = u8_strmblen(i_ptr);
              if (charlen >= 0)
	    	o_ptr = u8_stpncpy(o_ptr, i_ptr, charlen); // No numbers in sight, just append char
	    }
	}

      // Break if less than 100 bytes remain (prevent buffer overflow)
      if (sizeof(out) - u8_strlen(out) < 100)
	break;

      // Break if number is very large (prevent buffer overflow)
      if (number && (i_ptr - number > 50))
	break;

      if (append_number)
	{
	  numlen = i_ptr - number;
	  if (numlen < 5) // Max pad width
	    {
	      u8_strcpy(o_ptr, (uint8_t *)"00000");
	      o_ptr += (5 - numlen);
	    }
	  o_ptr = u8_stpncpy(o_ptr, number, numlen + u8_strmblen(i_ptr));

	  number = NULL;
	  append_number = 0;
	}

      i_ptr = n_ptr;
    }
  while (n_ptr);

  *sort_tag = (char *)u8_normalize(UNINORM_NFD, (uint8_t *)&out, u8_strlen(out) + 1, NULL, &len);
}

static void
fixup_tags(struct media_file_info *mfi)
{
  cfg_t *lib;
  size_t len;
  char *tag;
  char *sep = " - ";
  char *ca;

  if (mfi->genre && (strlen(mfi->genre) == 0))
    {
      free(mfi->genre);
      mfi->genre = NULL;
    }

  if (mfi->artist && (strlen(mfi->artist) == 0))
    {
      free(mfi->artist);
      mfi->artist = NULL;
    }

  if (mfi->title && (strlen(mfi->title) == 0))
    {
      free(mfi->title);
      mfi->title = NULL;
    }

  /*
   * Default to mpeg4 video/audio for unknown file types
   * in an attempt to allow streaming of DRM-afflicted files
   */
  if (mfi->codectype && strcmp(mfi->codectype, "unkn") == 0)
    {
      if (mfi->has_video)
	{
	  strcpy(mfi->codectype, "mp4v");
	  strcpy(mfi->type, "m4v");
	}
      else
	{
	  strcpy(mfi->codectype, "mp4a");
	  strcpy(mfi->type, "m4a");
	}
    }

  if (!mfi->artist)
    {
      if (mfi->orchestra && mfi->conductor)
	{
	  len = strlen(mfi->orchestra) + strlen(sep) + strlen(mfi->conductor);
	  tag = (char *)malloc(len + 1);
	  if (tag)
	    {
	      sprintf(tag,"%s%s%s", mfi->orchestra, sep, mfi->conductor);
	      mfi->artist = tag;
            }
        }
      else if (mfi->orchestra)
	{
	  mfi->artist = strdup(mfi->orchestra);
        }
      else if (mfi->conductor)
	{
	  mfi->artist = strdup(mfi->conductor);
        }
    }

  /* Handle TV shows, try to present prettier metadata */
  if (mfi->tv_series_name && strlen(mfi->tv_series_name) != 0)
    {
      mfi->media_kind = MEDIA_KIND_TVSHOW;  /* tv show */

      /* Default to artist = series_name */
      if (mfi->artist && strlen(mfi->artist) == 0)
	{
	  free(mfi->artist);
	  mfi->artist = NULL;
	}

      if (!mfi->artist)
	mfi->artist = strdup(mfi->tv_series_name);

      /* Default to album = "<series_name>, Season <season_num>" */
      if (mfi->album && strlen(mfi->album) == 0)
	{
	  free(mfi->album);
	  mfi->album = NULL;
	}

      if (!mfi->album)
	{
	  len = snprintf(NULL, 0, "%s, Season %u", mfi->tv_series_name, mfi->tv_season_num);

	  mfi->album = (char *)malloc(len + 1);
	  if (mfi->album)
	    sprintf(mfi->album, "%s, Season %u", mfi->tv_series_name, mfi->tv_season_num);
	}
    }

  /* Check the 4 top-tags are filled */
  if (!mfi->artist)
    mfi->artist = strdup("Unknown artist");
  if (!mfi->album)
    mfi->album = strdup("Unknown album");
  if (!mfi->genre)
    mfi->genre = strdup("Unknown genre");
  if (!mfi->title)
    {
      /* fname is left untouched by unicode_fixup_mfi() for
       * obvious reasons, so ensure it is proper UTF-8
       */
      mfi->title = unicode_fixup_string(mfi->fname, "ascii");
      if (mfi->title == mfi->fname)
	mfi->title = strdup(mfi->fname);
    }

  /* Ensure sort tags are filled, manipulated and normalized */
  sort_tag_create(&mfi->artist_sort, mfi->artist);
  sort_tag_create(&mfi->album_sort, mfi->album);
  sort_tag_create(&mfi->title_sort, mfi->title);

  /* We need to set album_artist according to media type and config */
  if (mfi->compilation)          /* Compilation */
    {
      lib = cfg_getsec(cfg, "library");
      ca = cfg_getstr(lib, "compilation_artist");
      if (ca && mfi->album_artist)
	{
	  free(mfi->album_artist);
	  mfi->album_artist = strdup(ca);
	}
      else if (ca && !mfi->album_artist)
	{
	  mfi->album_artist = strdup(ca);
	}
      else if (!ca && !mfi->album_artist)
	{
	  mfi->album_artist = strdup("");
	  mfi->album_artist_sort = strdup("");
	}
    }
  else if (mfi->media_kind == MEDIA_KIND_PODCAST) /* Podcast */
    {
      if (mfi->album_artist)
	free(mfi->album_artist);
      mfi->album_artist = strdup("");
      mfi->album_artist_sort = strdup("");
    }
  else if (!mfi->album_artist)   /* Regular media without album_artist */
    {
      mfi->album_artist = strdup(mfi->artist);
    }

  if (!mfi->album_artist_sort && (strcmp(mfi->album_artist, mfi->artist) == 0))
    mfi->album_artist_sort = strdup(mfi->artist_sort);
  else
    sort_tag_create(&mfi->album_artist_sort, mfi->album_artist);

  /* Composer is not one of our mandatory tags, so take extra care */
  if (mfi->composer_sort || mfi->composer)
    sort_tag_create(&mfi->composer_sort, mfi->composer);
}

void
library_add_media(struct media_file_info *mfi)
{
  if (!mfi->path || !mfi->fname)
    {
      DPRINTF(E_LOG, L_LIB, "Ignoring media file with missing values (path='%s', fname='%s', data_kind='%d')\n",
	      mfi->path, mfi->fname, mfi->data_kind);
      return;
    }

  if (!mfi->directory_id || !mfi->virtual_path)
    {
      // Missing informations for virtual_path and directory_id (may) lead to misplaced appearance in mpd clients
      DPRINTF(E_WARN, L_LIB, "Media file with missing values (path='%s', directory='%d', virtual_path='%s')\n",
	      mfi->path, mfi->directory_id, mfi->virtual_path);
    }

  if (!mfi->item_kind)
    mfi->item_kind = 2; /* music */
  if (!mfi->media_kind)
    mfi->media_kind = MEDIA_KIND_MUSIC; /* music */

  unicode_fixup_mfi(mfi);

  fixup_tags(mfi);

  if (mfi->id == 0)
    db_file_add(mfi);
  else
    db_file_update(mfi);
}

int
library_scan_media(const char *path, struct media_file_info *mfi)
{
  int i;
  int ret;

  DPRINTF(E_DBG, L_LIB, "Scan metadata for path '%s'\n", path);

  ret = LIBRARY_PATH_INVALID;
  for (i = 0; sources[i] && ret == LIBRARY_PATH_INVALID; i++)
    {
      if (sources[i]->disabled || !sources[i]->scan_metadata)
        {
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support scan_metadata\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->scan_metadata(path, mfi);

      if (ret == LIBRARY_OK)
	DPRINTF(E_DBG, L_LIB, "Got metadata for path '%s' from library source '%s'\n", path, sources[i]->name);
    }

  if (ret == LIBRARY_OK)
    {
      if (!mfi->virtual_path)
	mfi->virtual_path = strdup(mfi->path);
      if (!mfi->item_kind)
	mfi->item_kind = 2; /* music */
      if (!mfi->media_kind)
	mfi->media_kind = MEDIA_KIND_MUSIC; /* music */

      unicode_fixup_mfi(mfi);

      fixup_tags(mfi);
    }
  else
    {
      DPRINTF(E_LOG, L_LIB, "Failed to read metadata for path '%s' (ret=%d)\n", path, ret);
    }

  return ret;
}

int
library_add_playlist_info(const char *path, const char *title, const char *virtual_path, enum pl_type type, int parent_pl_id, int dir_id)
{
  struct playlist_info *pli;
  int plid;
  int ret;

  pli = db_pl_fetch_bypath(path);
  if (pli)
    {
      DPRINTF(E_DBG, L_LIB, "Playlist found ('%s', link %s), updating\n", title, path);

      plid = pli->id;

      pli->type = type;
      free(pli->title);
      pli->title = strdup(title);
      if (pli->virtual_path)
	free(pli->virtual_path);
      pli->virtual_path = safe_strdup(virtual_path);
      pli->directory_id = dir_id;

      ret = db_pl_update(pli);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LIB, "Error updating playlist ('%s', link %s)\n", title, path);

	  free_pli(pli, 0);
	  return -1;
	}

      db_pl_clear_items(plid);
    }
  else
    {
      DPRINTF(E_DBG, L_LIB, "Adding playlist ('%s', link %s)\n", title, path);

      pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
      if (!pli)
	{
	  DPRINTF(E_LOG, L_LIB, "Out of memory\n");

	  return -1;
	}

      memset(pli, 0, sizeof(struct playlist_info));

      pli->type = type;
      pli->title = strdup(title);
      pli->path = strdup(path);
      pli->virtual_path = safe_strdup(virtual_path);
      pli->parent_id = parent_pl_id;
      pli->directory_id = dir_id;

      ret = db_pl_add(pli, &plid);
      if ((ret < 0) || (plid < 1))
	{
	  DPRINTF(E_LOG, L_LIB, "Error adding playlist ('%s', link %s, ret %d, plid %d)\n", title, path, ret, plid);

	  free_pli(pli, 0);
	  return -1;
	}
    }

  free_pli(pli, 0);
  return plid;
}

int
library_add_queue_item(struct media_file_info *mfi)
{
  struct db_queue_item queue_item;

  memset(&queue_item, 0, sizeof(struct db_queue_item));

  if (mfi->id)
    queue_item.file_id = mfi->id;
  else
    queue_item.file_id = 9999999;

  queue_item.title = mfi->title;
  queue_item.artist = mfi->artist;
  queue_item.album_artist = mfi->album_artist;
  queue_item.album = mfi->album;
  queue_item.genre = mfi->genre;
  queue_item.artist_sort = mfi->artist_sort;
  queue_item.album_artist_sort = mfi->album_artist_sort;
  queue_item.album_sort = mfi->album_sort;
  queue_item.path = mfi->path;
  queue_item.virtual_path = mfi->virtual_path;
  queue_item.data_kind = mfi->data_kind;
  queue_item.media_kind = mfi->media_kind;
  queue_item.song_length = mfi->song_length;
  queue_item.seek = mfi->seek;
  queue_item.songalbumid = mfi->songalbumid;
  queue_item.time_modified = mfi->time_modified;
  queue_item.year = mfi->year;
  queue_item.track = mfi->track;
  queue_item.disc = mfi->disc;
  //queue_item.artwork_url

  return db_queue_add_item(&queue_item, 0, 0);
}

static void
purge_cruft(time_t start)
{
  DPRINTF(E_DBG, L_LIB, "Purging old library content\n");
  db_purge_cruft(start);
  db_groups_cleanup();
  db_queue_cleanup();

  DPRINTF(E_DBG, L_LIB, "Purging old artwork content\n");
  cache_artwork_purge_cruft(start);
}

static enum command_state
rescan(void *arg, int *ret)
{
  time_t starttime;
  time_t endtime;
  int i;

  DPRINTF(E_LOG, L_LIB, "Library rescan triggered\n");
  listener_notify(LISTENER_UPDATE);
  starttime = time(NULL);

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->rescan)
	{
	  DPRINTF(E_INFO, L_LIB, "Rescan library source '%s'\n", sources[i]->name);
	  sources[i]->rescan();
	}
      else
	{
	  DPRINTF(E_INFO, L_LIB, "Library source '%s' is disabled\n", sources[i]->name);
	}
    }

  purge_cruft(starttime);

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library rescan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);
  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);

  *ret = 0;
  return COMMAND_END;
}

static enum command_state
fullrescan(void *arg, int *ret)
{
  time_t starttime;
  time_t endtime;
  int i;

  DPRINTF(E_LOG, L_LIB, "Library full-rescan triggered\n");
  listener_notify(LISTENER_UPDATE);
  starttime = time(NULL);

  player_playback_stop();
  db_queue_clear(0);
  db_purge_all(); // Clears files, playlists, playlistitems, inotify and groups

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->fullrescan)
	{
	  DPRINTF(E_INFO, L_LIB, "Full-rescan library source '%s'\n", sources[i]->name);
	  sources[i]->fullrescan();
	}
      else
	{
	  DPRINTF(E_INFO, L_LIB, "Library source '%s' is disabled\n", sources[i]->name);
	}
    }

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library full-rescan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);
  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);

  *ret = 0;
  return COMMAND_END;
}

/*
 * Callback to notify listeners of database changes
 */
static void
update_trigger_cb(int fd, short what, void *arg)
{
  if (handle_deferred_update_notifications())
    {
      listener_notify(deferred_update_events);
      deferred_update_events = 0;
    }
}

static enum command_state
update_trigger(void *arg, int *retval)
{
  short *events = arg;

  ++deferred_update_notifications;
  deferred_update_events |= *events;

  // Only add the timer event if the update occurred outside a (init-/re-/fullre-) scan.
  // The scanning functions take care of notifying clients of database changes directly
  // after the scan finished.
  if (!scanning)
    evtimer_add(updateev, &library_update_wait);

  *retval = 0;
  return COMMAND_END;
}


/* --------------------------- LIBRARY INTERFACE -------------------------- */

void
library_rescan()
{
  if (scanning)
    {
      DPRINTF(E_INFO, L_LIB, "Scan already running, ignoring request to trigger a new init scan\n");
      return;
    }

  scanning = true; // TODO Guard "scanning" with a mutex
  commands_exec_async(cmdbase, rescan, NULL);
}

void
library_fullrescan()
{
  if (scanning)
    {
      DPRINTF(E_INFO, L_LIB, "Scan already running, ignoring request to trigger a new full rescan\n");
      return;
    }

  scanning = true; // TODO Guard "scanning" with a mutex
  commands_exec_async(cmdbase, fullrescan, NULL);
}

static void
initscan()
{
  time_t starttime;
  time_t endtime;
  bool clear_queue_disabled;
  int i;

  scanning = true;
  starttime = time(NULL);
  listener_notify(LISTENER_UPDATE);

  // Only clear the queue if enabled (default) in config
  clear_queue_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");
  if (!clear_queue_disabled)
    {
      db_queue_clear(0);
    }

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->initscan)
	sources[i]->initscan();
    }

  if (! (cfg_getbool(cfg_getsec(cfg, "library"), "filescan_disable")))
    {
      purge_cruft(starttime);

      DPRINTF(E_DBG, L_LIB, "Running post library scan jobs\n");
      db_hook_post_scan();
    }

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library init scan completed in %.f sec (%d changes)\n", difftime(endtime, starttime), deferred_update_notifications);

  scanning = false;

  if (handle_deferred_update_notifications())
    listener_notify(LISTENER_UPDATE | LISTENER_DATABASE);
  else
    listener_notify(LISTENER_UPDATE);
}

/*
 * @return true if scan is running, otherwise false
 */
bool
library_is_scanning()
{
  return scanning;
}

/*
 * @param is_scanning true if scan is running, otherwise false
 */
void
library_set_scanning(bool is_scanning)
{
  scanning = is_scanning;
}

/*
 * @return true if a running scan should be aborted due to imminent shutdown, otherwise false
 */
bool
library_is_exiting()
{
  return scan_exit;
}

/*
 * Trigger for sending the DATABASE event
 *
 * Needs to be called, if an update to the database (library tables) occurred. The DATABASE event
 * is emitted with the delay 'library_update_wait'. It is safe to call this function from any thread.
 */
void
library_update_trigger(short update_events)
{
  short *events;
  int ret;

  pthread_t current_thread = pthread_self();
  if (pthread_equal(current_thread, tid_library))
    {
      // We are already running in the library thread, it is safe to directly call update_trigger
      update_trigger(&update_events, &ret);
    }
  else
    {
      events = malloc(sizeof(short));
      *events = update_events;
      commands_exec_async(cmdbase, update_trigger, events);
    }
}

static enum command_state
playlist_add(void *arg, int *retval)
{
  struct playlist_add_param *param = arg;
  int i;
  int ret = LIBRARY_ERROR;

  DPRINTF(E_DBG, L_LIB, "Adding item '%s' to playlist '%s'\n", param->vp_item, param->vp_playlist);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->playlist_add)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support playlist_add\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->playlist_add(param->vp_playlist, param->vp_item);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Adding item '%s' to playlist '%s' with library source '%s'\n", param->vp_item, param->vp_playlist, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_playlist_add(const char *vp_playlist, const char *vp_item)
{
  struct playlist_add_param param;

  if (library_is_scanning())
    return -1;

  param.vp_playlist = vp_playlist;
  param.vp_item = vp_item;
  return commands_exec_sync(cmdbase, playlist_add, NULL, &param);
}

static enum command_state
playlist_remove(void *arg, int *retval)
{
  const char *virtual_path;
  int i;
  int ret = LIBRARY_ERROR;

  virtual_path = arg;

  DPRINTF(E_DBG, L_LIB, "Removing playlist at path '%s'\n", virtual_path);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->playlist_remove)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support playlist_remove\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->playlist_remove(virtual_path);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Removing playlist '%s' with library source '%s'\n", virtual_path, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_playlist_remove(char *virtual_path)
{
  if (library_is_scanning())
    return -1;

  return commands_exec_sync(cmdbase, playlist_remove, NULL, virtual_path);
}

static enum command_state
queue_save(void *arg, int *retval)
{
  const char *virtual_path;
  int i;
  int ret = LIBRARY_ERROR;

  virtual_path = arg;

  DPRINTF(E_DBG, L_LIB, "Saving queue to path '%s'\n", virtual_path);

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->disabled || !sources[i]->queue_save)
	{
	  DPRINTF(E_DBG, L_LIB, "Library source '%s' is disabled or does not support queue_save\n", sources[i]->name);
	  continue;
	}

      ret = sources[i]->queue_save(virtual_path);

      if (ret == LIBRARY_OK)
	{
	  DPRINTF(E_DBG, L_LIB, "Saving queue to path '%s' with library source '%s'\n", virtual_path, sources[i]->name);
	  listener_notify(LISTENER_STORED_PLAYLIST);
	  break;
	}
    }

  *retval = ret;
  return COMMAND_END;
}

int
library_queue_save(char *path)
{
  if (library_is_scanning())
    return -1;

  return commands_exec_sync(cmdbase, queue_save, NULL, path);
}

/*
 * Execute the function 'func' with the given argument 'arg' in the library thread.
 *
 * The pointer passed as argument is freed in the library thread after func returned.
 *
 * @param func The function to be executed
 * @param arg Argument passed to func
 * @return 0 if triggering the function execution succeeded, -1 on failure.
 */
int
library_exec_async(command_function func, void *arg)
{
  return commands_exec_async(cmdbase, func, arg);
}

static void *
library(void *arg)
{
  int ret;

#ifdef __linux__
  struct sched_param param;

  /* Lower the priority of the thread so forked-daapd may still respond
   * during library scan on low power devices. Param must be 0 for the SCHED_BATCH
   * policy.
   */
  memset(&param, 0, sizeof(struct sched_param));
  ret = pthread_setschedparam(pthread_self(), SCHED_BATCH, &param);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_LIB, "Warning: Could not set thread priority to SCHED_BATCH\n");
    }
#endif

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LIB, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  initscan();

  event_base_dispatch(evbase_lib);

  if (!scan_exit)
    DPRINTF(E_FATAL, L_LIB, "Scan event loop terminated ahead of time!\n");

  db_perthread_deinit();

  pthread_exit(NULL);
}

/* Thread: main */
int
library_init(void)
{
  int i;
  int ret;

  scan_exit = false;
  scanning = false;

  CHECK_NULL(L_LIB, evbase_lib = event_base_new());
  CHECK_NULL(L_LIB, updateev = evtimer_new(evbase_lib, update_trigger_cb, NULL));

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->init)
	continue;

      ret = sources[i]->init();
      if (ret < 0)
	sources[i]->disabled = 1;
    }

  CHECK_NULL(L_LIB, cmdbase = commands_base_new(evbase_lib, NULL));

  CHECK_ERR(L_LIB, pthread_create(&tid_library, NULL, library, NULL));

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_library, "library");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_library, "library");
#endif

  return 0;
}

/* Thread: main */
void
library_deinit()
{
  int i;
  int ret;

  scan_exit = true;
  commands_base_destroy(cmdbase);

  ret = pthread_join(tid_library, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_LIB, "Could not join library thread: %s\n", strerror(errno));

      return;
    }

  for (i = 0; sources[i]; i++)
    {
      if (sources[i]->deinit && !sources[i]->disabled)
      sources[i]->deinit();
    }

  event_base_free(evbase_lib);
}
