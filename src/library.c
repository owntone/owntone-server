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

#include "library.h"

#include <errno.h>
#include <event2/event.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <uninorm.h>
#include <unistr.h>

#include "cache.h"
#include "commands.h"
#include "conffile.h"
#include "db.h"
#include "logger.h"
#include "misc.h"


static struct commands_base *cmdbase;
static pthread_t tid_library;

struct event_base *evbase_lib;

/* Flag for aborting scan on exit */
static bool scan_exit;

/* Flag for scan in progress */
static bool scanning;

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
	  len = snprintf(NULL, 0, "%s, Season %d", mfi->tv_series_name, mfi->tv_season_num);

	  mfi->album = (char *)malloc(len + 1);
	  if (mfi->album)
	    sprintf(mfi->album, "%s, Season %d", mfi->tv_series_name, mfi->tv_season_num);
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
library_process_media(const char *path, time_t mtime, off_t size, enum data_kind data_kind, enum media_kind force_media_kind, bool force_compilation, struct media_file_info *external_mfi, int dir_id)
{
  struct media_file_info *mfi;
  const char *filename;
  time_t stamp;
  int id;
  char virtual_path[PATH_MAX];
  int ret;

  filename = strrchr(path, '/');
  if ((!filename) || (strlen(filename) == 1))
    filename = path;
  else
    filename++;

  db_file_stamp_bypath(path, &stamp, &id);

  if (stamp && (stamp >= mtime))
    {
      db_file_ping(id);
      return;
    }

  if (!external_mfi)
    {
      mfi = (struct media_file_info*)malloc(sizeof(struct media_file_info));
      if (!mfi)
	{
	  DPRINTF(E_LOG, L_LIB, "Out of memory for mfi\n");
	  return;
	}

      memset(mfi, 0, sizeof(struct media_file_info));
    }
  else
    mfi = external_mfi;

  if (stamp)
    mfi->id = id;

  mfi->fname = strdup(filename);
  if (!mfi->fname)
    {
      DPRINTF(E_LOG, L_LIB, "Out of memory for fname\n");
      goto out;
    }

  mfi->path = strdup(path);
  if (!mfi->path)
    {
      DPRINTF(E_LOG, L_LIB, "Out of memory for path\n");
      goto out;
    }

  mfi->time_modified = mtime;
  mfi->file_size = size;

  if (force_compilation)
    mfi->compilation = 1;
  if (force_media_kind)
    mfi->media_kind = force_media_kind;

  if (data_kind == DATA_KIND_FILE)
    {
      mfi->data_kind = DATA_KIND_FILE;
      ret = scan_metadata_ffmpeg(path, mfi);
    }
  else if (data_kind == DATA_KIND_HTTP)
    {
      mfi->data_kind = DATA_KIND_HTTP;
      ret = scan_metadata_ffmpeg(path, mfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_LIB, "Playlist URL '%s' is unavailable for probe/metadata, assuming MP3 encoding\n", path);
	  mfi->type = strdup("mp3");
	  mfi->codectype = strdup("mpeg");
	  mfi->description = strdup("MPEG audio file");
	  ret = 1;
	}
    }
  else if (data_kind == DATA_KIND_SPOTIFY)
    {
      mfi->data_kind = DATA_KIND_SPOTIFY;
      ret = mfi->artist && mfi->album && mfi->title;
    }
  else if (data_kind == DATA_KIND_PIPE)
    {
      mfi->data_kind = DATA_KIND_PIPE;
      mfi->type = strdup("wav");
      mfi->codectype = strdup("wav");
      mfi->description = strdup("PCM16 pipe");
      ret = 1;
    }
  else
    {
      DPRINTF(E_LOG, L_LIB, "Unknown scan type for '%s', this error should not occur\n", path);
      ret = -1;
    }

  if (ret < 0)
    {
      DPRINTF(E_INFO, L_LIB, "Could not extract metadata for '%s'\n", path);
      goto out;
    }

  if (!mfi->item_kind)
    mfi->item_kind = 2; /* music */
  if (!mfi->media_kind)
    mfi->media_kind = MEDIA_KIND_MUSIC; /* music */

  unicode_fixup_mfi(mfi);

  fixup_tags(mfi);

  if (data_kind == DATA_KIND_HTTP)
    {
      snprintf(virtual_path, PATH_MAX, "/http:/%s", mfi->title);
      mfi->virtual_path = strdup(virtual_path);
    }
  else if (data_kind == DATA_KIND_SPOTIFY)
    {
      snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", mfi->album_artist, mfi->album, mfi->title);
      mfi->virtual_path = strdup(virtual_path);
    }
  else
    {
      snprintf(virtual_path, PATH_MAX, "/file:%s", mfi->path);
      mfi->virtual_path = strdup(virtual_path);
    }

  mfi->directory_id = dir_id;

  if (mfi->id == 0)
    db_file_add(mfi);
  else
    db_file_update(mfi);

 out:
  if (!external_mfi)
    free_mfi(mfi, 0);
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

  starttime = time(NULL);

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->rescan)
	sources[i]->rescan();
    }

  purge_cruft(starttime);

  endtime = time(NULL);
  DPRINTF(E_LOG, L_LIB, "Library rescan completed in %.f sec\n", difftime(endtime, starttime));

  scanning = false;
  *ret = 0;
  return COMMAND_END;
}

static enum command_state
fullrescan(void *arg, int *ret)
{
  int i;

  for (i = 0; sources[i]; i++)
    {
      if (!sources[i]->disabled && sources[i]->fullrescan)
	sources[i]->fullrescan();
    }

  scanning = false;
  *ret = 0;
  return COMMAND_END;
}

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

  // Only clear the queue if enabled (default) in config
  clear_queue_disabled = cfg_getbool(cfg_getsec(cfg, "mpd"), "clear_queue_on_stop_disable");
  if (!clear_queue_disabled)
    {
      db_queue_clear();
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
  DPRINTF(E_LOG, L_LIB, "Library init scan completed in %.f sec\n", difftime(endtime, starttime));

  scanning = false;
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

  evbase_lib = event_base_new();
  if (!evbase_lib)
    {
      DPRINTF(E_FATAL, L_LIB, "Could not create an event base\n");

      return -1;
    }

  for (i = 0; sources[i]; i++)
    {
      ret = sources[i]->init();
      if (ret < 0)
	sources[i]->disabled = 1;
    }

  cmdbase = commands_base_new(evbase_lib, NULL);

  ret = pthread_create(&tid_library, NULL, library, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_LIB, "Could not spawn library thread: %s\n", strerror(errno));

      goto thread_fail;
    }

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_library, "library");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_library, "library");
#endif

  return 0;

 thread_fail:
  event_base_free(evbase_lib);

  return -1;
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
      sources[i]->deinit();
    }

  event_base_free(evbase_lib);
}
