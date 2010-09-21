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

#include <uninorm.h>

#if defined(__linux__)
# include <sys/inotify.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/time.h>
# include <sys/event.h>
#endif

#if defined(HAVE_SYS_EVENTFD_H) && defined(HAVE_EVENTFD)
# define USE_EVENTFD
# include <sys/eventfd.h>
#endif

#include <event.h>

#include "logger.h"
#include "db.h"
#include "filescanner.h"
#include "conffile.h"
#include "misc.h"
#include "remote_pairing.h"


#define F_SCAN_BULK    (1 << 0)
#define F_SCAN_RESCAN  (1 << 1)

struct deferred_pl {
  char *path;
  struct deferred_pl *next;
};

struct stacked_dir {
  char *path;
  struct stacked_dir *next;
};


#ifdef USE_EVENTFD
static int exit_efd;
#else
static int exit_pipe[2];
#endif
static int scan_exit;
static int inofd;
static struct event_base *evbase_scan;
static struct event inoev;
static struct event exitev;
static pthread_t tid_scan;
static struct deferred_pl *playlists;
static struct stacked_dir *dirstack;


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


static void
normalize_fixup_tag(char **tag, char *src_tag)
{
  char *norm;
  size_t len;

  /* Note: include terminating NUL in string length for u8_normalize */

  if (!*tag)
    *tag = (char *)u8_normalize(UNINORM_NFD, (uint8_t *)src_tag, strlen(src_tag) + 1, NULL, &len);
  else
    {
      norm = (char *)u8_normalize(UNINORM_NFD, (uint8_t *)*tag, strlen(*tag) + 1, NULL, &len);
      free(*tag);
      *tag = norm;
    }
}

static void
fixup_tags(struct media_file_info *mfi)
{
  size_t len;
  char *tag;
  char *sep = " - ";

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
  if (strcmp(mfi->codectype, "unkn") == 0)
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
	  len = strlen(mfi->orchestra) + strlen(sep) + strlen(mfi ->conductor);
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
      mfi->media_kind = 64;  /* tv show */

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
    mfi->title = strdup(mfi->fname);

  /* If we don't have an album_artist, set it to artist */
  if (!mfi->album_artist)
    {
      if (mfi->compilation)
	mfi->album_artist = strdup("");
      else
	mfi->album_artist = strdup(mfi->artist);
    }

  /* Ensure sort tags are filled and normalized */
  normalize_fixup_tag(&mfi->artist_sort, mfi->artist);
  normalize_fixup_tag(&mfi->album_sort, mfi->album);
  normalize_fixup_tag(&mfi->title_sort, mfi->title);
  normalize_fixup_tag(&mfi->album_artist_sort, mfi->album_artist);

  /* Composer is not one of our mandatory tags, so take extra care */
  if (mfi->composer_sort || mfi->composer)
    normalize_fixup_tag(&mfi->composer_sort, mfi->composer);
}


static void
process_media_file(char *file, time_t mtime, off_t size, int compilation)
{
  struct media_file_info mfi;
  char *filename;
  char *ext;
  time_t stamp;
  int ret;

  stamp = db_file_stamp_bypath(file);

  if (stamp >= mtime)
    {
      db_file_ping(file);
      return;
    }

  memset(&mfi, 0, sizeof(struct media_file_info));

  if (stamp)
    mfi.id = db_file_id_bypath(file);

  filename = strrchr(file, '/');
  if (!filename)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not determine filename for %s\n", file);

      return;
    }

  mfi.fname = strdup(filename + 1);
  if (!mfi.fname)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for fname\n");

      return;
    }

  mfi.path = strdup(file);
  if (!mfi.path)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for path\n");

      free(mfi.fname);
      return;
    }

  mfi.time_modified = mtime;
  mfi.file_size = size;

  ret = -1;

  /* Special cases */
  ext = strrchr(file, '.');
  if (ext)
    {
      if ((strcmp(ext, ".pls") == 0)
	  || (strcmp(ext, ".url") == 0))
	{
	  mfi.data_kind = 1; /* url/stream */

	  ret = scan_url_file(file, &mfi);
	  if (ret < 0)
	    goto out;
	}
      else if ((strcmp(ext, ".png") == 0)
	       || (strcmp(ext, ".jpg") == 0))
	{
	  /* Artwork - don't scan */
	  goto out;
	}
    }

  /* General case */
  if (ret < 0)
    {
      ret = scan_metadata_ffmpeg(file, &mfi);
      mfi.data_kind = 0; /* real file */
    }

  if (ret < 0)
    {
      DPRINTF(E_INFO, L_SCAN, "Could not extract metadata for %s\n", file);

      goto out;
    }

  mfi.compilation = compilation;

  if (!mfi.item_kind)
    mfi.item_kind = 2; /* music */
  if (!mfi.media_kind)
    mfi.media_kind = 1; /* music */

  fixup_tags(&mfi);

  unicode_fixup_mfi(&mfi);

  if (mfi.id == 0)
    db_file_add(&mfi);
  else
    db_file_update(&mfi);

 out:
  free_mfi(&mfi, 1);
}

static void
process_playlist(char *file)
{
  char *ext;

  ext = strrchr(file, '.');
  if (ext)
    {
      if (strcmp(ext, ".m3u") == 0)
	scan_m3u_playlist(file);
#ifdef ITUNES
      else if (strcmp(ext, ".xml") == 0)
	scan_itunes_itml(file);
#endif
    }
}

/* Thread: scan */
static void
defer_playlist(char *path)
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

      process_playlist(pl->path);

      free(pl->path);
      free(pl);

      /* Run the event loop */
      event_base_loop(evbase_scan, EVLOOP_ONCE | EVLOOP_NONBLOCK);

      if (scan_exit)
	return;
    }
}

/* Thread: scan */
static void
process_file(char *file, time_t mtime, off_t size, int compilation, int flags)
{
  char *ext;

  ext = strrchr(file, '.');
  if (ext)
    {
      if ((strcmp(ext, ".m3u") == 0)
#ifdef ITUNES
	  || (strcmp(ext, ".xml") == 0)
#endif
	  )
	{
	  if (flags & F_SCAN_BULK)
	    defer_playlist(file);
	  else
	    process_playlist(file);

	  return;
	}
      else if (strcmp(ext, ".remote") == 0)
	{
	  remote_pairing_read_pin(file);

	  return;
	}
    }

  /* Not any kind of special file, so let's see if it's a media file */
  process_media_file(file, mtime, size, compilation);
}


/* Thread: scan */
static int
check_compilation(char *path)
{
  cfg_t *lib;
  int ndirs;
  int i;

  lib = cfg_getsec(cfg, "library");
  ndirs = cfg_size(lib, "compilations");

  for (i = 0; i < ndirs; i++)
    {
      if (strstr(path, cfg_getnstr(lib, "compilations", i)))
	return 1;
    }

  return 0;
}

/* Thread: scan */
static void
process_directory(char *path, int flags)
{
  struct stacked_dir *bulkstack;
  cfg_t *lib;
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
  int compilation;
  int ret;

  lib = cfg_getsec(cfg, "library");

  if (flags & F_SCAN_BULK)
    {
      /* Save our directory stack so it won't get handled inside
       * the event loop - not its business, we're in bulk mode here.
       */
      bulkstack = dirstack;
      dirstack = NULL;

      /* Run the event loop */
      event_base_loop(evbase_scan, EVLOOP_ONCE | EVLOOP_NONBLOCK);

      /* Restore our directory stack */
      dirstack = bulkstack;

      if (scan_exit)
	return;
    }

  DPRINTF(E_DBG, L_SCAN, "Processing directory %s (flags = 0x%x)\n", path, flags);

  dirp = opendir(path);
  if (!dirp)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open directory %s: %s\n", path, strerror(errno));

      return;
    }

  /* Check for a compilation directory */
  compilation = check_compilation(path);

  for (;;)
    {
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
	process_file(entry, sb.st_mtime, sb.st_size, compilation, flags);
      else if (S_ISDIR(sb.st_mode))
	push_dir(&dirstack, entry);
      else
	DPRINTF(E_LOG, L_SCAN, "Skipping %s, not a directory, symlink nor regular file\n", entry);
    }

  closedir(dirp);

  memset(&wi, 0, sizeof(struct watch_info));

#if defined(__linux__)
  /* Add inotify watch */
  wi.wd = inotify_add_watch(inofd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE | IN_DELETE | IN_MOVE_SELF);
  if (wi.wd < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not create inotify watch for %s: %s\n", path, strerror(errno));

      return;
    }

  if (!(flags & F_SCAN_RESCAN))
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
bulk_scan(void)
{
  cfg_t *lib;
  int ndirs;
  char *path;
  char *deref;
  time_t start;
  int i;

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

	  continue;
	}

      process_directories(deref, F_SCAN_BULK);

      free(deref);

      if (scan_exit)
	return;
    }

  if (playlists)
    process_deferred_playlists();

  if (scan_exit)
    return;

  if (dirstack)
    DPRINTF(E_LOG, L_SCAN, "WARNING: unhandled leftover directories\n");

  DPRINTF(E_DBG, L_SCAN, "Purging old database content\n");
  db_purge_cruft(start);
}


/* Thread: scan */
static void *
filescanner(void *arg)
{
  int ret;

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

  /* Recompute all songalbumids, in case the SQLite DB got transferred
   * to a different host; the hash is not portable.
   * It will also rebuild the groups we just cleared.
   */
  db_files_update_songalbumid();

  bulk_scan();

  if (!scan_exit)
    {
      /* Enable inotify */
      event_add(&inoev, NULL);

      event_base_dispatch(evbase_scan);
    }

  if (!scan_exit)
    DPRINTF(E_FATAL, L_SCAN, "Scan event loop terminated ahead of time!\n");

  db_perthread_deinit();

  pthread_exit(NULL);
}


#if defined(__linux__)
/* Thread: scan */
static void
process_inotify_dir(struct watch_info *wi, char *path, struct inotify_event *ie)
{
  struct watch_enum we;
  uint32_t rm_wd;
  int flags = 0;
  int ret;

  DPRINTF(E_DBG, L_SCAN, "Directory event: 0x%x, cookie 0x%x, wd %d\n", ie->mask, ie->cookie, wi->wd);

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

	  while (((ret = db_watch_enum_fetchwd(&we, &rm_wd)) == 0) && (rm_wd))
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

	  inotify_rm_watch(inofd, ie->wd);
	  db_watch_delete_bywd(ie->wd);

	  memset(&we, 0, sizeof(struct watch_enum));

	  we.match = path;

	  ret = db_watch_enum_start(&we);
	  if (ret < 0)
	    return;

	  while (((ret = db_watch_enum_fetchwd(&we, &rm_wd)) == 0) && (rm_wd))
	    {
	      inotify_rm_watch(inofd, rm_wd);
	    }

	  db_watch_enum_end(&we);

	  db_watch_delete_bymatch(path);

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
	  flags |= F_SCAN_RESCAN;
	}

      ie->mask |= IN_CREATE;
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
  char *deref = NULL;
  char *file = path;
  int compilation;
  int ret;

  DPRINTF(E_DBG, L_SCAN, "File event: 0x%x, cookie 0x%x, wd %d\n", ie->mask, ie->cookie, wi->wd);

  if (ie->mask & IN_DELETE)
    {
      db_file_delete_bypath(path);
      db_pl_delete_bypath(path);
    }

  if (ie->mask & IN_MOVED_FROM)
    {
      db_file_disable_bypath(path, wi->path, ie->cookie);
      db_pl_disable_bypath(path, wi->path, ie->cookie);
    }

  if (ie->mask & IN_MOVED_TO)
    {
      ret = db_file_enable_bycookie(ie->cookie, wi->path);

      if (ret <= 0)
	{
	  /* It's not a known media file, so it's either a new file
	   * or a playlist, known or not.
	   * We want to scan the new file and we want to rescan the
	   * playlist to update playlist items (relative items).
	   */
	  ie->mask |= IN_CREATE;
	  db_pl_enable_bycookie(ie->cookie, wi->path);
	}
    }

  if (ie->mask & (IN_MODIFY | IN_CREATE | IN_CLOSE_WRITE))
    {
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

      compilation = check_compilation(path);

      process_file(file, sb.st_mtime, sb.st_size, compilation, 0);

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

  event_add(&inoev, NULL);
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

  event_add(&inoev, NULL);
}
#endif /* __FreeBSD__ || __FreeBSD_kernel__ */


/* Thread: scan */
static void
exit_cb(int fd, short event, void *arg)
{
  event_base_loopbreak(evbase_scan);

  scan_exit = 1;
}


/* Thread: main */
int
filescanner_init(void)
{
  int ret;

  scan_exit = 0;

  evbase_scan = event_base_new();
  if (!evbase_scan)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create an event base\n");

      return -1;
    }

#ifdef USE_EVENTFD
  exit_efd = eventfd(0, EFD_CLOEXEC);
  if (exit_efd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create eventfd: %s\n", strerror(errno));

      goto pipe_fail;
    }
#else
# if defined(__linux__)
  ret = pipe2(exit_pipe, O_CLOEXEC);
# else
  ret = pipe(exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create pipe: %s\n", strerror(errno));

      goto pipe_fail;
    }
#endif /* USE_EVENTFD */

#if defined(__linux__)
  inofd = inotify_init1(IN_CLOEXEC);
  if (inofd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create inotify fd: %s\n", strerror(errno));

      goto ino_fail;
    }

  event_set(&inoev, inofd, EV_READ, inotify_cb, NULL);

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

  inofd = kqueue();
  if (inofd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create kqueue: %s\n", strerror(errno));

      goto ino_fail;
    }

  event_set(&inoev, inofd, EV_READ, kqueue_cb, NULL);
#endif

  event_base_set(evbase_scan, &inoev);

#ifdef USE_EVENTFD
  event_set(&exitev, exit_efd, EV_READ, exit_cb, NULL);
#else
  event_set(&exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
#endif
  event_base_set(evbase_scan, &exitev);
  event_add(&exitev, NULL);

  ret = pthread_create(&tid_scan, NULL, filescanner, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not spawn filescanner thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;

 thread_fail:
  close(inofd);
 ino_fail:
#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
 pipe_fail:
  event_base_free(evbase_scan);

  return -1;
}

/* Thread: main */
void
filescanner_deinit(void)
{
  int ret;

#ifdef USE_EVENTFD
  ret = eventfd_write(exit_efd, 1);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not send exit event: %s\n", strerror(errno));

      return;
    }
#else
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }
#endif

  ret = pthread_join(tid_scan, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not join filescanner thread: %s\n", strerror(errno));

      return;
    }

  event_del(&inoev);

#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
  close(inofd);
  event_base_free(evbase_scan);
}
