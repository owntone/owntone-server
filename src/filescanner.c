/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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

/* TODO: inotify vs. playlists */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include <sys/inotify.h>

#include <event.h>

#include "logger.h"
#include "db.h"
#include "filescanner.h"
#include "conffile.h"


#define F_SCAN_BULK    (1 << 0)

struct deferred_pl {
  char *path;
  struct deferred_pl *next;
};

struct stacked_dir {
  char *path;
  struct stacked_dir *next;
};


static int exit_pipe[2];
static int scan_exit;
static int inofd;
static struct event_base *evbase_scan;
static struct event inoev;
static struct event exitev;
static pthread_t tid_scan;
static struct deferred_pl *playlists;
static struct stacked_dir *dirstack;


static int
push_dir(char *path)
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

  d->next = dirstack;
  dirstack = d;

  return 0;
}

static char *
pop_dir(void)
{
  struct stacked_dir *d;
  char *ret;

  if (!dirstack)
    return NULL;

  d = dirstack;
  dirstack = d->next;
  ret = d->path;

  free(d);

  return ret;
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

  /* Check the 4 top-tags are filled */
  if (!mfi->artist)
    mfi->artist = strdup("Unknown artist");
  if (!mfi->album)
    mfi->album = strdup("Unknown album");
  if (!mfi->genre)
    mfi->genre = strdup("Unknown genre");
  if (!mfi->title)
    mfi->title = strdup(mfi->fname);
}


static void
process_media_file(char *file, time_t mtime, off_t size, int compilation)
{
  struct media_file_info *mfi;
  char *filename;
  char *ext;
  int need_update;
  int ret;

  mfi = db_file_fetch_bypath(file);

  need_update = (!mfi || (mfi->db_timestamp < mtime) || mfi->force_update);

  if (!need_update)
    {
      db_file_ping(mfi->id);

      free_mfi(mfi, 0);
      return;
    }

  if (mfi)
    {
      ret = mfi->id;
      free_mfi(mfi, 1);
    }
  else
    {
      ret = 0;
      mfi = (struct media_file_info *)malloc(sizeof(struct media_file_info));
      if (!mfi)
	{
	  DPRINTF(E_WARN, L_SCAN, "Out of memory for media_file_info\n");
	  return;
	}
    }

  memset(mfi, 0, sizeof(struct media_file_info));
  mfi->id = ret;

  filename = strrchr(file, '/');
  if (!filename)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not determine filename for %s\n", file);

      free(mfi);
      return;
    }

  mfi->fname = strdup(filename + 1);
  if (!mfi->fname)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for fname\n");

      free(mfi);
      return;
    }

  mfi->path = strdup(file);
  if (!mfi->path)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for path\n");

      free(mfi->fname);
      free(mfi);
      return;
    }

  mfi->time_modified = mtime;
  mfi->file_size = size;

  ret = -1;

  /* Special cases */
  ext = strrchr(file, '.');
  if (ext)
    {
      if ((strcmp(ext, ".pls") == 0)
	  || (strcmp(ext, ".url") == 0))
	{
	  ret = scan_url_file(file, mfi);
	  if (ret == 0)
	    mfi->data_kind = 1; /* url/stream */
	}
    }

  /* General case */
  if (ret < 0)
    {
      ret = scan_metadata_ffmpeg(file, mfi);
      mfi->data_kind = 0; /* real file */
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not extract metadata for %s\n", file);

      free_mfi(mfi, 0);
      return;
    }

  mfi->compilation = compilation;
  mfi->item_kind = 2; /* music */

  fixup_tags(mfi);

  if (mfi->id == 0)
    db_file_add(mfi);
  else
    db_file_update(mfi);

  free_mfi(mfi, 0);
}

static void
process_playlist(char *file)
{
  char *ext;

  ext = strrchr(file, '.');
  if (ext)
    {
      if (strcmp(ext, ".m3u") == 0)
	{
	  scan_m3u_playlist(file);

	  return;
	}
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
      if (strcmp(ext, ".m3u") == 0)
	{
	  if (flags & F_SCAN_BULK)
	    defer_playlist(file);
	  else
	    process_playlist(file);

	  return;
	}
    }

  /* Not any kind of special file, so let's see if it's a media file */
  process_media_file(file, mtime, size, compilation);
}


/* Thread: scan */
static int
check_compilation(int libidx, char *path)
{
  cfg_t *lib;
  int ndirs;
  int i;

  lib = cfg_getnsec(cfg, "library", libidx);
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
process_directory(int libidx, char *path, int flags)
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
  int compilation;
  int ret;

  lib = cfg_getnsec(cfg, "library", libidx);

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
  compilation = check_compilation(libidx, path);

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
	  deref = realpath(entry, NULL);
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
	push_dir(entry);
      else
	DPRINTF(E_LOG, L_SCAN, "Skipping %s, not a directory, symlink nor regular file\n", entry);
    }

  closedir(dirp);

  memset(&wi, 0, sizeof(struct watch_info));

  /* Add inotify watch */
  wi.wd = inotify_add_watch(inofd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE | IN_DELETE | IN_MOVE_SELF);
  if (wi.wd < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not create inotify watch for %s: %s\n", path, strerror(errno));

      return;
    }

  wi.libidx = libidx;
  wi.cookie = 0;
  wi.path = path;

  db_watch_add(&wi);
}

/* Thread: scan */
static void
process_directories(int libidx, char *root, int flags)
{
  char *path;

  process_directory(libidx, root, flags);

  if (scan_exit)
    return;

  while ((path = pop_dir()))
    {
      process_directory(libidx, path, flags);

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
  int nlib;
  int ndirs;
  char *path;
  time_t start;
  int i;
  int j;

  start = time(NULL);

  playlists = NULL;
  dirstack = NULL;

  nlib = cfg_size(cfg, "library");
  for (i = 0; i < nlib; i++)
    {
      lib = cfg_getnsec(cfg, "library", i);

      ndirs = cfg_size(lib, "directories");
      for (j = 0; j < ndirs; j++)
	{
	  path = cfg_getnstr(lib, "directories", j);

	  process_directories(i, path, F_SCAN_BULK);

	  if (scan_exit)
	    return;
	}
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


/* Thread: scan */
static void
process_inotify_dir(struct watch_info *wi, char *path, struct inotify_event *ie)
{
  struct watch_enum we;
  uint32_t rm_wd;
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
	}
      else
	ie->mask |= IN_CREATE;
    }

  if (ie->mask & IN_CREATE)
    {
      process_directories(wi->libidx, path, 0);

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
	ret = db_pl_enable_bycookie(ie->cookie, wi->path);

      if (ret <= 0)
	ie->mask |= IN_CREATE;
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
	  deref = realpath(path, NULL);
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

      compilation = check_compilation(wi->libidx, path);

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

  ret = pipe2(exit_pipe, O_CLOEXEC);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create pipe: %s\n", strerror(errno));

      goto pipe_fail;
    }

  inofd = inotify_init1(IN_CLOEXEC);
  if (inofd < 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create inotify fd: %s\n", strerror(errno));

      goto ino_fail;
    }

  event_set(&exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_scan, &exitev);
  event_add(&exitev, NULL);

  event_set(&inoev, inofd, EV_READ, inotify_cb, NULL);
  event_base_set(evbase_scan, &inoev);

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
  int dummy = 42;
  int ret;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }

  ret = pthread_join(tid_scan, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not join filescanner thread: %s\n", strerror(errno));

      return;
    }

  close(exit_pipe[0]);
  close(exit_pipe[1]);
  close(inofd);
  event_base_free(evbase_scan);
}
