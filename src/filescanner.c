/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 *
 * Bits and pieces from mt-daapd:
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include <unistr.h>
#include <unictype.h>
#include <uninorm.h>

#if defined(__linux__)
# include <sys/inotify.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/time.h>
# include <sys/event.h>
#endif

#ifdef HAVE_REGEX_H
# include <regex.h>
#endif

#include <event2/event.h>

#include "logger.h"
#include "db.h"
#include "filescanner.h"
#include "conffile.h"
#include "misc.h"
#include "remote_pairing.h"
#include "player.h"
#include "cache.h"
#include "artwork.h"

#ifdef LASTFM
# include "lastfm.h"
#endif
#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif


struct filescanner_command;

typedef int (*cmd_func)(struct filescanner_command *cmd);

struct filescanner_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  int ret;
};

#define F_SCAN_BULK    (1 << 0)
#define F_SCAN_RESCAN  (1 << 1)
#define F_SCAN_FAST    (1 << 2)
#define F_SCAN_MOVED   (1 << 3)

enum file_type {
  FILE_UNKNOWN = 0,
  FILE_IGNORE,
  FILE_REGULAR,
  FILE_PLAYLIST,
  FILE_SMARTPL,
  FILE_ITUNES,
  FILE_ARTWORK,
  FILE_CTRL_REMOTE,
  FILE_CTRL_LASTFM,
  FILE_CTRL_SPOTIFY,
  FILE_CTRL_INITSCAN,
  FILE_CTRL_FULLSCAN,
};

struct deferred_pl {
  char *path;
  time_t mtime;
  struct deferred_pl *next;
};

struct stacked_dir {
  char *path;
  struct stacked_dir *next;
};


static int cmd_pipe[2];
static int exit_pipe[2];
static int scan_exit;
static int inofd;
static struct event_base *evbase_scan;
static struct event *inoev;
static struct event *exitev;
static struct event *cmdev;
static pthread_t tid_scan;
static struct deferred_pl *playlists;
static struct stacked_dir *dirstack;

/* Count of files scanned during a bulk scan */
static int counter;

/* Flag for scan in progress */
static int scanning;

/* When copying into the lib (eg. if a file is moved to the lib by copying into
 * a Samba network share) inotify might give us IN_CREATE -> n x IN_ATTRIB ->
 * IN_CLOSE_WRITE, but we don't want to do any scanning before the
 * IN_CLOSE_WRITE. So we register new files (by path hashes) in this ring buffer
 * when we get the IN_CREATE and then ignore the IN_ATTRIB for these files.
 */
#define INCOMINGFILES_BUFFER_SIZE 50
static int incomingfiles_idx;
static uint32_t incomingfiles_buffer[INCOMINGFILES_BUFFER_SIZE];

/* Forward */
static void
bulk_scan(int flags);
static int
inofd_event_set(void);
static void
inofd_event_unset(void);
static int
filescanner_initscan(struct filescanner_command *cmd);
static int
filescanner_fullrescan(struct filescanner_command *cmd);


/* ---------------------------- COMMAND EXECUTION -------------------------- */

static int
send_command(struct filescanner_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_SCAN, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_SCAN, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
nonblock_command(struct filescanner_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

static int
push_dir(struct stacked_dir **s, char *path)
{
  struct stacked_dir *d;

  d = (struct stacked_dir *)malloc(sizeof(struct stacked_dir));
  if (!d)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stack directory %s; out of memory\n", path);
      return -1;
    }

  d->path = strdup(path);
  if (!d->path)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stack directory %s; out of memory for path\n", path);
      return -1;
    }

  d->next = *s;
  *s = d;

  return 0;
}

static char *
pop_dir(struct stacked_dir **s)
{
  struct stacked_dir *d;
  char *ret;

  if (!*s)
    return NULL;

  d = *s;
  *s = d->next;
  ret = d->path;

  free(d);

  return ret;
}

#ifdef HAVE_REGEX_H
/* Checks if the file path is configured to be ignored */
static int
file_path_ignore(const char *path)
{
  cfg_t *lib;
  regex_t regex;
  int n;
  int i;
  int ret;

  lib = cfg_getsec(cfg, "library");
  n = cfg_size(lib, "filepath_ignore");

  for (i = 0; i < n; i++)
    {
      ret = regcomp(&regex, cfg_getnstr(lib, "filepath_ignore", i), 0);
      if (ret != 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Could not compile regex for matching with file path\n");
	  return 0;
	}

      ret = regexec(&regex, path, 0, NULL, 0);
      regfree(&regex);

      if (ret == 0)
	{
	  DPRINTF(E_DBG, L_SCAN, "Regex match: %s\n", path);
	  return 1;
	}
    }

  return 0;
}
#endif

/* Checks if the file extension is in the ignore list */
static int
file_type_ignore(const char *ext)
{
  cfg_t *lib;
  int n;
  int i;

  lib = cfg_getsec(cfg, "library");
  n = cfg_size(lib, "filetypes_ignore");

  for (i = 0; i < n; i++)
    {
      if (strcasecmp(ext, cfg_getnstr(lib, "filetypes_ignore", i)) == 0)
	return 1;
    }

  return 0;
}

static enum file_type
file_type_get(const char *path) {
  const char *filename;
  const char *ext;

  filename = strrchr(path, '/');
  if ((!filename) || (strlen(filename) == 1))
    filename = path;
  else
    filename++;

#ifdef HAVE_REGEX_H
  if (file_path_ignore(path))
    return FILE_IGNORE;
#endif

  ext = strrchr(path, '.');
  if (!ext || (strlen(ext) == 1))
    return FILE_REGULAR;

  if ((strcasecmp(ext, ".m3u") == 0) || (strcasecmp(ext, ".pls") == 0))
    return FILE_PLAYLIST;

  if (strcasecmp(ext, ".smartpl") == 0)
    return FILE_SMARTPL;

  if (artwork_file_is_artwork(filename))
    return FILE_ARTWORK;

  if ((strcasecmp(ext, ".jpg") == 0) || (strcasecmp(ext, ".png") == 0))
    return FILE_IGNORE;

#ifdef ITUNES
  if (strcasecmp(ext, ".xml") == 0)
    return FILE_ITUNES;
#endif

  if (strcasecmp(ext, ".remote") == 0)
    return FILE_CTRL_REMOTE;

#ifdef LASTFM
  if (strcasecmp(ext, ".lastfm") == 0)
    return FILE_CTRL_LASTFM;
#endif

#ifdef HAVE_SPOTIFY_H
  if (strcasecmp(ext, ".spotify") == 0)
    return FILE_CTRL_SPOTIFY;
#endif

  if (strcasecmp(ext, ".init-rescan") == 0)
    return FILE_CTRL_INITSCAN;

  if (strcasecmp(ext, ".full-rescan") == 0)
    return FILE_CTRL_FULLSCAN;

  if (strcasecmp(ext, ".url") == 0)
    {
      DPRINTF(E_INFO, L_SCAN, "No support for .url, use .m3u or .pls\n");
      return FILE_IGNORE;
    }

  if (file_type_ignore(ext))
    return FILE_IGNORE;

  if ((filename[0] == '_') || (filename[0] == '.'))
    return FILE_IGNORE;

  return FILE_REGULAR;
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
      DPRINTF(E_DBG, L_SCAN, "Existing sort tag will be normalized: %s\n", *sort_tag);
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
filescanner_process_media(char *path, time_t mtime, off_t size, int type, struct media_file_info *external_mfi)
{
  struct media_file_info *mfi;
  char *filename;
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
	  DPRINTF(E_LOG, L_SCAN, "Out of memory for mfi\n");
	  return;
	}

      memset(mfi, 0, sizeof(struct media_file_info));
    }
  else
    mfi = external_mfi;

  if (stamp)
    mfi->id = db_file_id_bypath(path);

  mfi->fname = strdup(filename);
  if (!mfi->fname)
    {
      DPRINTF(E_LOG, L_SCAN, "Out of memory for fname\n");
      goto out;
    }

  mfi->path = strdup(path);
  if (!mfi->path)
    {
      DPRINTF(E_LOG, L_SCAN, "Out of memory for path\n");
      goto out;
    }

  mfi->time_modified = mtime;
  mfi->file_size = size;

  if (type & F_SCAN_TYPE_COMPILATION)
    mfi->compilation = 1;
  if (type & F_SCAN_TYPE_PODCAST)
    mfi->media_kind = MEDIA_KIND_PODCAST; /* podcast */
  if (type & F_SCAN_TYPE_AUDIOBOOK)
    mfi->media_kind = MEDIA_KIND_AUDIOBOOK; /* audiobook */

  if (type & F_SCAN_TYPE_FILE)
    {
      mfi->data_kind = DATA_KIND_FILE;
      ret = scan_metadata_ffmpeg(path, mfi);
    }
  else if (type & F_SCAN_TYPE_URL)
    {
      mfi->data_kind = DATA_KIND_HTTP;
      ret = scan_metadata_ffmpeg(path, mfi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Playlist URL is unavailable for probe/metadata, assuming MP3 encoding\n");
	  mfi->type = strdup("mp3");
	  mfi->codectype = strdup("mpeg");
	  mfi->description = strdup("MPEG audio file");
	  ret = 1;
	}
    }
  else if (type & F_SCAN_TYPE_SPOTIFY)
    {
      mfi->data_kind = DATA_KIND_SPOTIFY;
      ret = mfi->artist && mfi->album && mfi->title;
    }
  else if (type & F_SCAN_TYPE_PIPE)
    {
      mfi->data_kind = DATA_KIND_PIPE;
      mfi->type = strdup("wav");
      mfi->codectype = strdup("wav");
      mfi->description = strdup("PCM16 pipe");
      ret = 1;
    }
  else
    {
      DPRINTF(E_LOG, L_SCAN, "Unknown scan type for %s, this error should not occur\n", path);
      ret = -1;
    }

  if (ret < 0)
    {
      DPRINTF(E_INFO, L_SCAN, "Could not extract metadata for %s\n", path);
      goto out;
    }

  if (!mfi->item_kind)
    mfi->item_kind = 2; /* music */
  if (!mfi->media_kind)
    mfi->media_kind = MEDIA_KIND_MUSIC; /* music */

  unicode_fixup_mfi(mfi);

  fixup_tags(mfi);

  if (type & F_SCAN_TYPE_URL)
    {
      snprintf(virtual_path, PATH_MAX, "/http:/%s", mfi->title);
      mfi->virtual_path = strdup(virtual_path);
    }
  else if (type & F_SCAN_TYPE_SPOTIFY)
    {
      snprintf(virtual_path, PATH_MAX, "/spotify:/%s/%s/%s", mfi->album_artist, mfi->album, mfi->title);
      mfi->virtual_path = strdup(virtual_path);
    }
  else
    {
      snprintf(virtual_path, PATH_MAX, "/file:%s", mfi->path);
      mfi->virtual_path = strdup(virtual_path);
    }

  if (mfi->id == 0)
    db_file_add(mfi);
  else
    db_file_update(mfi);

 out:
  if (!external_mfi)
    free_mfi(mfi, 0);
}

static void
process_playlist(char *file, time_t mtime)
{
  enum file_type ft;

  ft = file_type_get(file);
  if (ft == FILE_PLAYLIST)
    scan_playlist(file, mtime);
#ifdef ITUNES
  else if (ft == FILE_ITUNES)
    scan_itunes_itml(file);
#endif
}

/* Thread: scan */
static void
defer_playlist(char *path, time_t mtime)
{
  struct deferred_pl *pl;

  pl = (struct deferred_pl *)malloc(sizeof(struct deferred_pl));
  if (!pl)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for deferred playlist\n");

      return;
    }

  memset(pl, 0, sizeof(struct deferred_pl));

  pl->path = strdup(path);
  if (!pl->path)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for deferred playlist\n");

      free(pl);
      return;
    }

  pl->mtime = mtime;
  pl->next = playlists;
  playlists = pl;

  DPRINTF(E_INFO, L_SCAN, "Deferred playlist %s\n", path);
}

/* Thread: scan (bulk only) */
static void
process_deferred_playlists(void)
{
  struct deferred_pl *pl;

  while ((pl = playlists))
    {
      playlists = pl->next;

      process_playlist(pl->path, pl->mtime);

      free(pl->path);
      free(pl);

      if (scan_exit)
	return;
    }
}

/* Thread: scan */
static void
process_file(char *file, time_t mtime, off_t size, int type, int flags)
{
  int is_bulkscan;

  is_bulkscan = (flags & F_SCAN_BULK);

  switch (file_type_get(file))
    {
      case FILE_REGULAR:
	filescanner_process_media(file, mtime, size, type, NULL);

	cache_artwork_ping(file, mtime, !is_bulkscan);
	// TODO [artworkcache] If entry in artwork cache exists for no artwork available, delete the entry if media file has embedded artwork

	counter++;

	/* When in bulk mode, split transaction in pieces of 200 */
	if ((flags & F_SCAN_BULK) && (counter % 200 == 0))
	  {
	    DPRINTF(E_LOG, L_SCAN, "Scanned %d files...\n", counter);
	    db_transaction_end();
	    db_transaction_begin();
	  }
	break;

      case FILE_PLAYLIST:
      case FILE_ITUNES:
	if (flags & F_SCAN_BULK)
	  defer_playlist(file, mtime);
	else
	  process_playlist(file, mtime);
	break;

      case FILE_SMARTPL:
	DPRINTF(E_DBG, L_SCAN, "Smart playlist file: %s\n", file);
	scan_smartpl(file, mtime);
	break;

      case FILE_ARTWORK:
	DPRINTF(E_DBG, L_SCAN, "Artwork file: %s\n", file);
	cache_artwork_ping(file, mtime, !is_bulkscan);

	// TODO [artworkcache] If entry in artwork cache exists for no artwork available for a album with files in the same directory, delete the entry

	break;

      case FILE_CTRL_REMOTE:
	remote_pairing_read_pin(file);
	break;

#ifdef LASTFM
      case FILE_CTRL_LASTFM:
	lastfm_login(file);
	break;
#endif

#ifdef HAVE_SPOTIFY_H
      case FILE_CTRL_SPOTIFY:
	spotify_login(file);
	break;
#endif

      case FILE_CTRL_INITSCAN:
	if (flags & F_SCAN_BULK)
	  break;

	DPRINTF(E_LOG, L_SCAN, "Startup rescan triggered, found init-rescan file: %s\n", file);

	filescanner_initscan(NULL);
	break;

      case FILE_CTRL_FULLSCAN:
	if (flags & F_SCAN_BULK)
	  break;

	DPRINTF(E_LOG, L_SCAN, "Full rescan triggered, found full-rescan file: %s\n", file);

	filescanner_fullrescan(NULL);
	break;

      default:
	DPRINTF(E_WARN, L_SCAN, "Ignoring file: %s\n", file);
    }
}

/* Thread: scan */
static int
check_speciallib(char *path, const char *libtype)
{
  cfg_t *lib;
  int ndirs;
  int i;

  lib = cfg_getsec(cfg, "library");
  ndirs = cfg_size(lib, libtype);
  for (i = 0; i < ndirs; i++)
    {
      if (strstr(path, cfg_getnstr(lib, libtype, i)))
	return 1;
    }

  return 0;
}

/* Thread: scan */
static void
process_directory(char *path, int flags)
{
  DIR *dirp;
  struct dirent buf;
  struct dirent *de;
  char entry[PATH_MAX];
  char *deref;
  struct stat sb;
  struct watch_info wi;
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  struct kevent kev;
#endif
  int type;
  int ret;

  DPRINTF(E_DBG, L_SCAN, "Processing directory %s (flags = 0x%x)\n", path, flags);

  dirp = opendir(path);
  if (!dirp)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open directory %s: %s\n", path, strerror(errno));

      return;
    }

  /* Check if compilation and/or podcast directory */
  type = 0;
  if (check_speciallib(path, "compilations"))
    type |= F_SCAN_TYPE_COMPILATION;
  if (check_speciallib(path, "podcasts"))
    type |= F_SCAN_TYPE_PODCAST;
  if (check_speciallib(path, "audiobooks"))
    type |= F_SCAN_TYPE_AUDIOBOOK;

  for (;;)
    {
      if (scan_exit)
	break;

      ret = readdir_r(dirp, &buf, &de);
      if (ret != 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "readdir_r error in %s: %s\n", path, strerror(errno));

	  break;
	}

      if (de == NULL)
	break;

      if (buf.d_name[0] == '.')
	continue;

      ret = snprintf(entry, sizeof(entry), "%s/%s", path, buf.d_name);
      if ((ret < 0) || (ret >= sizeof(entry)))
	{
	  DPRINTF(E_LOG, L_SCAN, "Skipping %s/%s, PATH_MAX exceeded\n", path, buf.d_name);

	  continue;
	}

      ret = lstat(entry, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Skipping %s, lstat() failed: %s\n", entry, strerror(errno));

	  continue;
	}

      if (S_ISLNK(sb.st_mode))
	{
	  deref = m_realpath(entry);
	  if (!deref)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Skipping %s, could not dereference symlink: %s\n", entry, strerror(errno));

	      continue;
	    }

	  ret = stat(deref, &sb);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Skipping %s, stat() failed: %s\n", deref, strerror(errno));

	      free(deref);
	      continue;
	    }

	  ret = snprintf(entry, sizeof(entry), "%s", deref);
	  free(deref);
	  if ((ret < 0) || (ret >= sizeof(entry)))
	    {
	      DPRINTF(E_LOG, L_SCAN, "Skipping %s, PATH_MAX exceeded\n", deref);

	      continue;
	    }
	}

      if (S_ISREG(sb.st_mode))
	{
	  if (!(flags & F_SCAN_FAST))
	    process_file(entry, sb.st_mtime, sb.st_size, F_SCAN_TYPE_FILE | type, flags);
	}
      else if (S_ISFIFO(sb.st_mode))
	{
	  if (!(flags & F_SCAN_FAST))
	    process_file(entry, sb.st_mtime, sb.st_size, F_SCAN_TYPE_PIPE | type, flags);
	}
      else if (S_ISDIR(sb.st_mode))
	push_dir(&dirstack, entry);
      else
	DPRINTF(E_LOG, L_SCAN, "Skipping %s, not a directory, symlink, pipe nor regular file\n", entry);
    }

  closedir(dirp);

  memset(&wi, 0, sizeof(struct watch_info));

#if defined(__linux__)
  /* Add inotify watch */
  wi.wd = inotify_add_watch(inofd, path, IN_ATTRIB | IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVE | IN_DELETE | IN_MOVE_SELF);
  if (wi.wd < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not create inotify watch for %s: %s\n", path, strerror(errno));

      return;
    }

  if (!(flags & F_SCAN_MOVED))
    {
      wi.cookie = 0;
      wi.path = path;

      db_watch_add(&wi);
    }

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  memset(&kev, 0, sizeof(struct kevent));

  wi.wd = open(path, O_RDONLY | O_NONBLOCK);
  if (wi.wd < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not open directory %s for watching: %s\n", path, strerror(errno));

      return;
    }

  /* Add kevent */
  EV_SET(&kev, wi.wd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_DELETE | NOTE_WRITE | NOTE_RENAME, 0, NULL);

  ret = kevent(inofd, &kev, 1, NULL, 0, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not add kevent for %s: %s\n", path, strerror(errno));

      close(wi.wd);
      return;
    }

  wi.cookie = 0;
  wi.path = path;

  db_watch_add(&wi);
#endif
}

/* Thread: scan */
static void
process_directories(char *root, int flags)
{
  char *path;

  process_directory(root, flags);

  if (scan_exit)
    return;

  while ((path = pop_dir(&dirstack)))
    {
      process_directory(path, flags);

      free(path);

      if (scan_exit)
	return;
    }
}


/* Thread: scan */
static void
bulk_scan(int flags)
{
  cfg_t *lib;
  int ndirs;
  char *path;
  char *deref;
  time_t start;
  time_t end;
  int i;

  // Set global flag to avoid queued scan requests
  scanning = 1;

  start = time(NULL);

  playlists = NULL;
  dirstack = NULL;

  lib = cfg_getsec(cfg, "library");

  ndirs = cfg_size(lib, "directories");
  for (i = 0; i < ndirs; i++)
    {
      path = cfg_getnstr(lib, "directories", i);

      deref = m_realpath(path);
      if (!deref)
	{
	  DPRINTF(E_LOG, L_SCAN, "Skipping library directory %s, could not dereference: %s\n", path, strerror(errno));

	  /* Assume dir is mistakenly not mounted, so just disable everything and update timestamps */
	  db_file_disable_bymatch(path, "", 0);
	  db_pl_disable_bymatch(path, "", 0);

	  db_file_ping_bymatch(path, 1);
	  db_pl_ping_bymatch(path, 1);

	  continue;
	}

      counter = 0;
      db_transaction_begin();
      process_directories(deref, flags);
      db_transaction_end();

      free(deref);

      if (scan_exit)
	return;
    }

  if (!(flags & F_SCAN_FAST) && playlists)
    process_deferred_playlists();

  if (scan_exit)
    return;

  if (dirstack)
    DPRINTF(E_LOG, L_SCAN, "WARNING: unhandled leftover directories\n");

  end = time(NULL);

  if (flags & F_SCAN_FAST)
    {
      DPRINTF(E_LOG, L_SCAN, "Bulk library scan completed in %.f sec (with file scan disabled)\n", difftime(end, start));
    }
  else
    {
      /* Protect spotify from the imminent purge if rescanning */
      if (flags & F_SCAN_RESCAN)
	{
	  db_file_ping_bymatch("spotify:", 0);
	  db_pl_ping_bymatch("spotify:", 0);
	}

      DPRINTF(E_DBG, L_SCAN, "Purging old database content\n");
      db_purge_cruft(start);
      cache_artwork_purge_cruft(start);

      DPRINTF(E_LOG, L_SCAN, "Bulk library scan completed in %.f sec\n", difftime(end, start));

      DPRINTF(E_DBG, L_SCAN, "Running post library scan jobs\n");
      db_hook_post_scan();
    }

  // Set scan in progress flag to FALSE
  scanning = 0;
}


/* Thread: scan */
static void *
filescanner(void *arg)
{
  int ret;
#if defined(__linux__)
  struct sched_param param;

  /* Lower the priority of the thread so forked-daapd may still respond
   * during file scan on low power devices. Param must be 0 for the SCHED_BATCH
   * policy.
   */
  memset(&param, 0, sizeof(struct sched_param));
  ret = pthread_setschedparam(pthread_self(), SCHED_BATCH, &param);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Warning: Could not set thread priority to SCHED_BATCH\n");
    }
#endif

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  ret = db_watch_clear();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error: could not clear old watches from DB\n");

      pthread_exit(NULL);
    }

  ret = db_groups_clear();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error: could not clear old groups from DB\n");

      pthread_exit(NULL);
    }

  /* Recompute all songartistids and songalbumids, in case the SQLite DB got transferred
   * to a different host; the hash is not portable.
   * It will also rebuild the groups we just cleared.
   */
  db_files_update_songartistid();
  db_files_update_songalbumid();

  if (cfg_getbool(cfg_getsec(cfg, "library"), "filescan_disable"))
    bulk_scan(F_SCAN_BULK | F_SCAN_FAST);
  else
    bulk_scan(F_SCAN_BULK);

  if (!scan_exit)
    {
#ifdef HAVE_SPOTIFY_H
      spotify_login(NULL);
#endif

      /* Enable inotify */
      event_add(inoev, NULL);

      event_base_dispatch(evbase_scan);
    }

  if (!scan_exit)
    DPRINTF(E_FATAL, L_SCAN, "Scan event loop terminated ahead of time!\n");

  db_perthread_deinit();

  pthread_exit(NULL);
}


#if defined(__linux__)
static int
watches_clear(uint32_t wd, char *path)
{
  struct watch_enum we;
  uint32_t rm_wd;
  int ret;

  inotify_rm_watch(inofd, wd);
  db_watch_delete_bywd(wd);

  memset(&we, 0, sizeof(struct watch_enum));

  we.match = path;

  ret = db_watch_enum_start(&we);
  if (ret < 0)
    return -1;

  while ((db_watch_enum_fetchwd(&we, &rm_wd) == 0) && (rm_wd))
    {
      inotify_rm_watch(inofd, rm_wd);
    }

  db_watch_enum_end(&we);

  db_watch_delete_bymatch(path);

  return 0;
}

/* Thread: scan */
static void
process_inotify_dir(struct watch_info *wi, char *path, struct inotify_event *ie)
{
  struct watch_enum we;
  uint32_t rm_wd;
  char *s;
  int flags = 0;
  int ret;

  DPRINTF(E_SPAM, L_SCAN, "Directory event: 0x%x, cookie 0x%x, wd %d\n", ie->mask, ie->cookie, wi->wd);

  if (ie->mask & IN_UNMOUNT)
    {
      db_file_disable_bymatch(path, "", 0);
      db_pl_disable_bymatch(path, "", 0);
    }

  if (ie->mask & IN_MOVE_SELF)
    {
      /* A directory we know about, that got moved from a place
       * we know about to a place we know nothing about
       */
      if (wi->cookie)
	{
	  memset(&we, 0, sizeof(struct watch_enum));

	  we.cookie = wi->cookie;

	  ret = db_watch_enum_start(&we);
	  if (ret < 0)
	    return;

	  while ((db_watch_enum_fetchwd(&we, &rm_wd) == 0) && (rm_wd))
	    {
	      inotify_rm_watch(inofd, rm_wd);
	    }

	  db_watch_enum_end(&we);

	  db_watch_delete_bycookie(wi->cookie);
	}
      else
	{
	  /* If the directory exists, it has been moved and we've
	   * kept track of it successfully, so we're done
	   */
	  ret = access(path, F_OK);
	  if (ret == 0)
	    return;

	  /* Most probably a top-level dir is getting moved,
	   * and we can't tell where it's going
	   */

	  ret = watches_clear(ie->wd, path);
	  if (ret < 0)
	    return;

	  db_file_disable_bymatch(path, "", 0);
	  db_pl_disable_bymatch(path, "", 0);
	}
    }

  if (ie->mask & IN_MOVED_FROM)
    {
      db_watch_mark_bypath(path, path, ie->cookie);
      db_watch_mark_bymatch(path, path, ie->cookie);
      db_file_disable_bymatch(path, path, ie->cookie);
      db_pl_disable_bymatch(path, path, ie->cookie);
    }

  if (ie->mask & IN_MOVED_TO)
    {
      if (db_watch_cookie_known(ie->cookie))
	{
	  db_watch_move_bycookie(ie->cookie, path);
	  db_file_enable_bycookie(ie->cookie, path);
	  db_pl_enable_bycookie(ie->cookie, path);

	  /* We'll rescan the directory tree to update playlists */
	  flags |= F_SCAN_MOVED;
	}

      ie->mask |= IN_CREATE;
    }

  if (ie->mask & IN_ATTRIB)
    {
      DPRINTF(E_DBG, L_SCAN, "Directory permissions changed (%s): %s\n", wi->path, path);

      // Find out if we are already watching the dir (ret will be 0)
      s = wi->path;
      wi->path = path;
      ret = db_watch_get_bypath(wi);
      if (ret == 0)
	free(wi->path);
      wi->path = s;

#ifdef HAVE_EUIDACCESS
      if (euidaccess(path, (R_OK | X_OK)) < 0)
#else
      if (access(path, (R_OK | X_OK)) < 0)
#endif
	{
	  DPRINTF(E_LOG, L_SCAN, "Directory access to '%s' failed: %s\n", path, strerror(errno));

	  if (ret == 0)
	    watches_clear(wi->wd, path);

	  db_file_disable_bymatch(path, "", 0);
	  db_pl_disable_bymatch(path, "", 0);
	}
      else if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Directory access to '%s' achieved\n", path);

	  ie->mask |= IN_CREATE;
	}
      else
	{
	  DPRINTF(E_INFO, L_SCAN, "Directory event, but '%s' already being watched\n", path);
	}
    }

  if (ie->mask & IN_CREATE)
    {
      process_directories(path, flags);

      if (dirstack)
	DPRINTF(E_LOG, L_SCAN, "WARNING: unhandled leftover directories\n");
    }
}

/* Thread: scan */
static void
process_inotify_file(struct watch_info *wi, char *path, struct inotify_event *ie)
{
  struct stat sb;
  uint32_t path_hash;
  char *deref = NULL;
  char *file = path;
  int type;
  int i;
  int ret;

  DPRINTF(E_SPAM, L_SCAN, "File event: 0x%x, cookie 0x%x, wd %d\n", ie->mask, ie->cookie, wi->wd);

  path_hash = djb_hash(path, strlen(path));

  if (ie->mask & IN_DELETE)
    {
      DPRINTF(E_DBG, L_SCAN, "File deleted: %s\n", path);

      db_file_delete_bypath(path);
      db_pl_delete_bypath(path);
      cache_artwork_delete_by_path(path);
    }

  if (ie->mask & IN_MOVED_FROM)
    {
      DPRINTF(E_DBG, L_SCAN, "File moved from: %s\n", path);

      db_file_disable_bypath(path, path, ie->cookie);
      db_pl_disable_bypath(path, path, ie->cookie);
    }

  if (ie->mask & IN_ATTRIB)
    {
      DPRINTF(E_DBG, L_SCAN, "File attributes changed: %s\n", path);

      // Ignore the IN_ATTRIB if we just got an IN_CREATE
      for (i = 0; i < INCOMINGFILES_BUFFER_SIZE; i++)
	{
	  if (incomingfiles_buffer[i] == path_hash)
	    return;
	}

#ifdef HAVE_EUIDACCESS
      if (euidaccess(path, R_OK) < 0)
#else
      if (access(path, R_OK) < 0)
#endif
	{
	  DPRINTF(E_LOG, L_SCAN, "File access to '%s' failed: %s\n", path, strerror(errno));

	  db_file_delete_bypath(path);
	  cache_artwork_delete_by_path(path);
	}
      else if ((file_type_get(path) == FILE_REGULAR) && (db_file_id_bypath(path) <= 0)) // TODO Playlists
	{
	  DPRINTF(E_LOG, L_SCAN, "File access to '%s' achieved\n", path);

	  ie->mask |= IN_CLOSE_WRITE;
	}
    }

  if (ie->mask & IN_MOVED_TO)
    {
      DPRINTF(E_DBG, L_SCAN, "File moved to: %s\n", path);

      ret = db_file_enable_bycookie(ie->cookie, path);

      if (ret <= 0)
	{
	  /* It's not a known media file, so it's either a new file
	   * or a playlist, known or not.
	   * We want to scan the new file and we want to rescan the
	   * playlist to update playlist items (relative items).
	   */
	  ie->mask |= IN_CLOSE_WRITE;
	  db_pl_enable_bycookie(ie->cookie, path);
	}
    }

  if (ie->mask & IN_CREATE)
    {
      DPRINTF(E_DBG, L_SCAN, "File created: %s\n", path);

      ret = lstat(path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Could not lstat() '%s': %s\n", path, strerror(errno));

	  return;
	}

      // Add to the list of files where we ignore IN_ATTRIB until the file is closed again
      if (S_ISREG(sb.st_mode))
	{
	  DPRINTF(E_SPAM, L_SCAN, "Incoming file created '%s' (%d), index %d\n", path, (int)path_hash, incomingfiles_idx);

	  incomingfiles_buffer[incomingfiles_idx] = path_hash;
	  incomingfiles_idx = (incomingfiles_idx + 1) % INCOMINGFILES_BUFFER_SIZE;
	}
      else if (S_ISFIFO(sb.st_mode))
	ie->mask |= IN_CLOSE_WRITE;
    }

  if (ie->mask & IN_CLOSE_WRITE)
    {
      DPRINTF(E_DBG, L_SCAN, "File closed: %s\n", path);

      // File has been closed so remove from the IN_ATTRIB ignore list
      for (i = 0; i < INCOMINGFILES_BUFFER_SIZE; i++)
	if (incomingfiles_buffer[i] == path_hash)
	  {
	    DPRINTF(E_SPAM, L_SCAN, "Incoming file closed '%s' (%d), index %d\n", path, (int)path_hash, i);

	    incomingfiles_buffer[i] = 0;
	  }

      ret = lstat(path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Could not lstat() '%s': %s\n", path, strerror(errno));

	  return;
	}

      if (S_ISLNK(sb.st_mode))
	{
	  deref = m_realpath(path);
	  if (!deref)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Could not dereference symlink '%s': %s\n", path, strerror(errno));

	      return;
	    }

	  file = deref;

	  ret = stat(deref, &sb);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Could not stat() '%s': %s\n", file, strerror(errno));

	      free(deref);
	      return;
	    }

	  if (S_ISDIR(sb.st_mode))
	    {
	      process_inotify_dir(wi, deref, ie);

	      free(deref);
	      return;
	    }
	}

      type = 0;
      if (check_speciallib(path, "compilations"))
	type |= F_SCAN_TYPE_COMPILATION;
      if (check_speciallib(path, "podcasts"))
	type |= F_SCAN_TYPE_PODCAST;
      if (check_speciallib(path, "audiobooks"))
	type |= F_SCAN_TYPE_AUDIOBOOK;

      if (S_ISREG(sb.st_mode))
	process_file(file, sb.st_mtime, sb.st_size, F_SCAN_TYPE_FILE | type, 0);
      else if (S_ISFIFO(sb.st_mode))
	process_file(file, sb.st_mtime, sb.st_size, F_SCAN_TYPE_PIPE | type, 0);

      if (deref)
	free(deref);
    }
}

/* Thread: scan */
static void
inotify_cb(int fd, short event, void *arg)
{
  struct inotify_event *buf;
  struct inotify_event *ie;
  struct watch_info wi;
  char path[PATH_MAX];
  int qsize;
  int namelen;
  int ret;

  /* Determine the size of the inotify queue */
  ret = ioctl(fd, FIONREAD, &qsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not determine inotify queue size: %s\n", strerror(errno));

      return;
    }

  buf = (struct inotify_event *)malloc(qsize);
  if (!buf)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not allocate %d bytes for inotify events\n", qsize);

      return;
    }

  ret = read(fd, buf, qsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "inotify read failed: %s\n", strerror(errno));

      free(buf);
      return;
    }

  /* ioctl(FIONREAD) returns the number of bytes, now we need the number of elements */
  qsize /= sizeof(struct inotify_event);

  /* Loop through all the events we got */
  for (ie = buf; (ie - buf) < qsize; ie += (1 + (ie->len / sizeof(struct inotify_event))))
    {
      memset(&wi, 0, sizeof(struct watch_info));

      /* ie[0] contains the inotify event information
       * the memory space for ie[1+] contains the name of the file
       * see the inotify documentation
       */
      wi.wd = ie->wd;
      ret = db_watch_get_bywd(&wi);
      if (ret < 0)
	{
	  if (!(ie->mask & IN_IGNORED))
	    DPRINTF(E_LOG, L_SCAN, "No matching watch found, ignoring event (0x%x)\n", ie->mask);

	  continue;
	}

      if (ie->mask & IN_IGNORED)
	{
	  DPRINTF(E_DBG, L_SCAN, "%s deleted or backing filesystem unmounted!\n", wi.path);

	  db_watch_delete_bywd(ie->wd);
	  free(wi.path);
	  continue;
	}

      path[0] = '\0';

      ret = snprintf(path, PATH_MAX, "%s", wi.path);
      if ((ret < 0) || (ret >= PATH_MAX))
	{
	  DPRINTF(E_LOG, L_SCAN, "Skipping event under %s, PATH_MAX exceeded\n", wi.path);

	  free(wi.path);
	  continue;
	}

      if (ie->len > 0)
	{
	  namelen = PATH_MAX - ret;
	  ret = snprintf(path + ret, namelen, "/%s", ie->name);
	  if ((ret < 0) || (ret >= namelen))
	    {
	      DPRINTF(E_LOG, L_SCAN, "Skipping %s/%s, PATH_MAX exceeded\n", wi.path, ie->name);

	      free(wi.path);
	      continue;
	    }
	}

      /* ie->len == 0 catches events on the subject of the watch itself.
       * As we only watch directories, this catches directories.
       * General watch events like IN_UNMOUNT and IN_IGNORED do not come
       * with the IN_ISDIR flag set.
       */
      if ((ie->mask & IN_ISDIR) || (ie->len == 0))
	process_inotify_dir(&wi, path, ie);
      else
	process_inotify_file(&wi, path, ie);

      free(wi.path);
    }

  free(buf);

  event_add(inoev, NULL);
}
#endif /* __linux__ */


#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
/* Thread: scan */
static void
kqueue_cb(int fd, short event, void *arg)
{
  struct kevent kev;
  struct timespec ts;
  struct watch_info wi;
  struct watch_enum we;
  struct stacked_dir *rescan;
  struct stacked_dir *d;
  struct stacked_dir *dprev;
  char *path;
  uint32_t wd;
  int d_len;
  int w_len;
  int need_rescan;
  int ret;

  ts.tv_sec = 0;
  ts.tv_nsec = 0;

  we.cookie = 0;

  rescan = NULL;

  DPRINTF(E_DBG, L_SCAN, "Library changed!\n");

  /* We can only monitor directories with kqueue; to monitor files, we'd need
   * to have an open fd on every file in the library, which is totally insane.
   * Unfortunately, that means we only know when directories get renamed,
   * deleted or changed. We don't get directory/file names when directories/files
   * are created/deleted/renamed in the directory, so we have to rescan.
   */
  while (kevent(fd, NULL, 0, &kev, 1, &ts) > 0)
    {
      /* This should not happen, and if it does, we'll end up in
       * an infinite loop.
       */
      if (kev.filter != EVFILT_VNODE)
	continue;

      wi.wd = kev.ident;

      ret = db_watch_get_bywd(&wi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Found no matching watch for kevent, killing this event\n");

	  close(kev.ident);
	  continue;
	}

      /* Whatever the type of event that happened, disable matching watches and
       * files before we trigger an eventual rescan.
       */
      we.match = wi.path;

      ret = db_watch_enum_start(&we);
      if (ret < 0)
	{
	  free(wi.path);
	  continue;
	}

      while ((db_watch_enum_fetchwd(&we, &wd) == 0) && (wd))
	{
	  close(wd);
	}

      db_watch_enum_end(&we);

      db_watch_delete_bymatch(wi.path);

      close(wi.wd);
      db_watch_delete_bywd(wi.wd);

      /* Disable files */
      db_file_disable_bymatch(wi.path, "", 0);
      db_pl_disable_bymatch(wi.path, "", 0);

      if (kev.flags & EV_ERROR)
	{
	  DPRINTF(E_LOG, L_SCAN, "kevent reports EV_ERROR (%s): %s\n", wi.path, strerror(kev.data));

	  ret = access(wi.path, F_OK);
	  if (ret != 0)
	    {
	      free(wi.path);
	      continue;
	    }

	  /* The directory still exists, so try to add it back to the library */
	  kev.fflags |= NOTE_WRITE;
	}

      /* No further action on NOTE_DELETE & NOTE_RENAME; NOTE_WRITE on the
       * parent directory will trigger a rescan in both cases and the
       * renamed directory will be picked up then.
       */

      if (kev.fflags & NOTE_WRITE)
	{
          DPRINTF(E_DBG, L_SCAN, "Got NOTE_WRITE (%s)\n", wi.path);

	  need_rescan = 1;
	  w_len = strlen(wi.path);

	  /* Abusing stacked_dir a little bit here */
	  dprev = NULL;
	  d = rescan;
	  while (d)
	    {
	      d_len = strlen(d->path);

	      if (d_len > w_len)
		{
		  /* Stacked dir child of watch dir? */
		  if ((d->path[w_len] == '/') && (strncmp(d->path, wi.path, w_len) == 0))
		    {
		      DPRINTF(E_DBG, L_SCAN, "Watched directory is a parent\n");

		      if (dprev)
			dprev->next = d->next;
		      else
			rescan = d->next;

		      free(d->path);
		      free(d);

		      if (dprev)
			d = dprev->next;
		      else
			d = rescan;

		      continue;
		    }
		}
	      else if (w_len > d_len)
		{
		  /* Watch dir child of stacked dir? */
		  if ((wi.path[d_len] == '/') && (strncmp(wi.path, d->path, d_len) == 0))
		    {
		      DPRINTF(E_DBG, L_SCAN, "Watched directory is a child\n");

		      need_rescan = 0;
		      break;
		    }
		}
	      else if (strcmp(wi.path, d->path) == 0)
		{
		  DPRINTF(E_DBG, L_SCAN, "Watched directory already listed\n");

		  need_rescan = 0;
		  break;
		}

	      dprev = d;
	      d = d->next;
	    }

	  if (need_rescan)
	    push_dir(&rescan, wi.path);
	}

      free(wi.path);
    }

  while ((path = pop_dir(&rescan)))
    {
      process_directories(path, 0);

      free(path);

      if (rescan)
	DPRINTF(E_LOG, L_SCAN, "WARNING: unhandled leftover directories\n");
    }

  event_add(inoev, NULL);
}
#endif /* __FreeBSD__ || __FreeBSD_kernel__ */

/* Thread: main & scan */
static int
inofd_event_set(void)
{
#if defined(__linux__)
  inofd = inotify_init1(IN_CLOEXEC);
  if (inofd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create inotify fd: %s\n", strerror(errno));

      return -1;
    }

  inoev = event_new(evbase_scan, inofd, EV_READ, inotify_cb, NULL);

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

  inofd = kqueue();
  if (inofd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create kqueue: %s\n", strerror(errno));

      return -1;
    }

  inoev = event_new(evbase_scan, inofd, EV_READ, kqueue_cb, NULL);
#endif

  return 0;
}

/* Thread: main & scan */
static void
inofd_event_unset(void)
{
  event_free(inoev);
  close(inofd);
}

/* Thread: scan */
static void
exit_cb(int fd, short event, void *arg)
{
  event_base_loopbreak(evbase_scan);

  scan_exit = 1;
}

static void
command_cb(int fd, short what, void *arg)
{
  struct filescanner_command *cmd;
  int ret;

  ret = read(cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_SCAN, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
      goto readd;
    }

  if (cmd->nonblock)
    {
      cmd->func(cmd);

      free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  ret = cmd->func(cmd);
  cmd->ret = ret;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

 readd:
  event_add(cmdev, NULL);
}

static int
filescanner_initscan(struct filescanner_command *cmd)
{
  DPRINTF(E_LOG, L_SCAN, "Startup rescan triggered\n");

  inofd_event_unset(); // Clears all inotify watches
  db_watch_clear();

  inofd_event_set();
  bulk_scan(F_SCAN_BULK | F_SCAN_RESCAN);

  return 0;
}

static int
filescanner_fullrescan(struct filescanner_command *cmd)
{
  DPRINTF(E_LOG, L_SCAN, "Full rescan triggered\n");

  player_playback_stop();
  player_queue_clear();
  inofd_event_unset(); // Clears all inotify watches
  db_purge_all(); // Clears files, playlists, playlistitems, inotify and groups

  inofd_event_set();
  bulk_scan(F_SCAN_BULK);

  return 0;
}

void
filescanner_trigger_initscan(void)
{
  struct filescanner_command *cmd;

  if (scanning)
    {
      DPRINTF(E_INFO, L_SCAN, "Scan already running, ignoring request to trigger a new init scan\n");
      return;
    }


  cmd = (struct filescanner_command *)malloc(sizeof(struct filescanner_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not allocate cache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct filescanner_command));

  cmd->nonblock = 1;

  cmd->func = filescanner_initscan;

  nonblock_command(cmd);
}

void
filescanner_trigger_fullrescan(void)
{
  struct filescanner_command *cmd;

  if (scanning)
    {
      DPRINTF(E_INFO, L_SCAN, "Scan already running, ignoring request to trigger a new init scan\n");
      return;
    }

  cmd = (struct filescanner_command *)malloc(sizeof(struct filescanner_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not allocate cache_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct filescanner_command));

  cmd->nonblock = 1;

  cmd->func = filescanner_fullrescan;

  nonblock_command(cmd);
}

/*
 * Query the status of the filescanner
 * @return 1 if scan is running, otherwise 0
 */
int
filescanner_scanning(void)
{
  return scanning;
}

/* Thread: main */
int
filescanner_init(void)
{
  int ret;

  scan_exit = 0;
  scanning = 0;

  evbase_scan = event_base_new();
  if (!evbase_scan)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create an event base\n");

      return -1;
    }

  ret = pipe2(exit_pipe, O_CLOEXEC);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create pipe: %s\n", strerror(errno));

      goto pipe_fail;
    }

  exitev = event_new(evbase_scan, exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!exitev || (event_add(exitev, NULL) < 0))
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create/add command event\n");
      goto exitev_fail;
    }

  ret = inofd_event_set();
  if (ret < 0)
    {
      goto ino_fail;
    }

  ret = pipe2(cmd_pipe, O_CLOEXEC);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

  cmdev = event_new(evbase_scan, cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!cmdev || (event_add(cmdev, NULL) < 0))
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create/add command event\n");
      goto cmd_fail;
    }

  ret = pthread_create(&tid_scan, NULL, filescanner, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not spawn filescanner thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;

 thread_fail:
 cmd_fail:
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
  close(inofd);
 exitev_fail:
 ino_fail:
  close(exit_pipe[0]);
  close(exit_pipe[1]);
 pipe_fail:
  event_base_free(evbase_scan);

  return -1;
}

/* Thread: main */
void
filescanner_deinit(void)
{
  int ret;
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }

  scan_exit = 1;

  ret = pthread_join(tid_scan, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not join filescanner thread: %s\n", strerror(errno));

      return;
    }

  inofd_event_unset();

  close(exit_pipe[0]);
  close(exit_pipe[1]);
  close(cmd_pipe[0]);
  close(cmd_pipe[1]);
  event_base_free(evbase_scan);
}
