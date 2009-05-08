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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include <sys/inotify.h>

#include <event.h>
#include <avl.h>

#include "logger.h"
#include "db-generic.h"
#include "filescanner.h"
#include "conffile.h"


struct wdpath {
  int wd;
  char *path;
  cfg_t *lib;
};

struct deferred_pl {
  char *path;
  struct deferred_pl *next;
};


static int exit_pipe[2];
static int scan_exit;
static int inofd;
static struct event_base *evbase_scan;
static struct event inoev;
static struct event exitev;
static pthread_t tid_scan;
static struct deferred_pl *playlists;
avl_tree_t *wd2path;


static void
wdpath_free(void *v)
{
  struct wdpath *w = (struct wdpath *)v;

  free(w->path);
  free(w);
}

static int
wdpath_compare(const void *aa, const void *bb)
{
  struct wdpath *a = (struct wdpath *)aa;
  struct wdpath *b = (struct wdpath *)bb;

  if (a->wd < b->wd)
    return -1;

  if (a->wd > b->wd)
    return 1;

  return 0;
}

static void
free_mfi(struct media_file_info *mfi)
{
  if (mfi->path)
    free(mfi->path);

  if (mfi->fname)
    free(mfi->fname);

  if (mfi->title)
    free(mfi->title);

  if (mfi->artist)
    free(mfi->artist);

  if (mfi->album)
    free(mfi->album);

  if (mfi->genre)
    free(mfi->genre);

  if (mfi->comment)
    free(mfi->comment);

  if (mfi->type)
    free(mfi->type);

  if (mfi->composer)
    free(mfi->composer);

  if (mfi->orchestra)
    free(mfi->orchestra);

  if (mfi->conductor)
    free(mfi->conductor);

  if (mfi->grouping)
    free(mfi->grouping);

  if (mfi->description)
    free(mfi->description);

  if (mfi->codectype)
    free(mfi->codectype);

  if (mfi->album_artist)
    free(mfi->album_artist);
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
  struct media_file_info *db_mfi;
  struct media_file_info *mfi;
  char *filename;
  char *ext;
  int need_update;
  int ret;

  db_mfi = db_fetch_path(NULL, file, 0);

  need_update = (!db_mfi || (db_mfi->db_timestamp < mtime) || db_mfi->force_update);

  db_dispose_item(db_mfi);

  if (!need_update)
    return;

  mfi = (struct media_file_info *)malloc(sizeof(struct media_file_info));
  if (!mfi)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for media_file_info\n");

      db_dispose_item(db_mfi);
      return;
    }

  memset(mfi, 0, sizeof(struct media_file_info));

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
      DPRINTF(E_LOG, L_SCAN, "Could not extract metadata for %s\n");

      free_mfi(mfi);
      free(mfi);
      return;
    }

  mfi->compilation = compilation;
  mfi->item_kind = 2; /* music */

  fixup_tags(mfi);

  db_add(NULL, mfi, NULL);

  free_mfi(mfi);
  free(mfi);
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

  DPRINTF(E_INF, L_SCAN, "Deferred playlist %s\n", path);
}

/* Thread: scan */
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
    }
}

/* Thread: scan */
static void
process_file(char *file, time_t mtime, off_t size, int compilation, int bulk)
{
  char *ext;

  ext = strrchr(file, '.');
  if (ext)
    {
      if (strcmp(ext, ".m3u") == 0)
	{
	  if (bulk)
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
check_compilation(cfg_t *lib, char *path)
{
  int ndirs;
  int i;

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
process_directory(cfg_t *lib, char *path, int bulk)
{
  DIR *dirp;
  struct dirent buf;
  struct dirent *de;
  char entry[PATH_MAX];
  char *deref;
  struct stat sb;
  int wd;
  struct wdpath *w2p;
  avl_node_t *node;
  int compilation;
  int ret;

  dirp = opendir(path);
  if (!dirp)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open directory %s: %s\n", path, strerror(errno));

      return;
    }

  /* Check for a compilation directory */
  compilation = check_compilation(lib, path);

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
	process_file(entry, sb.st_mtime, sb.st_size, compilation, bulk);
      else if (S_ISDIR(sb.st_mode))
	process_directory(lib, entry, bulk);
      else
	DPRINTF(E_LOG, L_SCAN, "Skipping %s, not a directory, symlink nor regular file\n", entry);

      if (bulk)
	{
	  /* Run the event loop */
	  event_base_loop(evbase_scan, EVLOOP_ONCE | EVLOOP_NONBLOCK);
	  if (scan_exit)
	    return;
	}
    }

  closedir(dirp);

  /* Add inotify watch */
  wd = inotify_add_watch(inofd, path, IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF);
  if (wd < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not create inotify watch for %s: %s\n", path, strerror(errno));

      return;
    }

  w2p = (struct wdpath *)malloc(sizeof(struct wdpath));
  if (!w2p)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for struct wdpath\n");

      return;
    }

  w2p->wd = wd;
  w2p->lib = lib;
  w2p->path = strdup(path);
  if (!w2p->path)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory for path inside struct wdpath\n");

      free(w2p);
      return;
    }

  node = avl_insert(wd2path, w2p);
  if (!node)
    {
      if (errno != EEXIST)
	DPRINTF(E_WARN, L_SCAN, "Could not insert w2p in wd2path: %s\n", strerror(errno));

      wdpath_free(w2p);
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
  int i;
  int j;

  playlists = NULL;

  nlib = cfg_size(cfg, "library");
  for (i = 0; i < nlib; i++)
    {
      lib = cfg_getnsec(cfg, "library", i);

      ndirs = cfg_size(lib, "directories");
      for (j = 0; j < ndirs; j++)
	{
	  path = cfg_getnstr(lib, "directories", j);

	  process_directory(lib, path, 1);

	  if (scan_exit)
	    return;
	}
    }

  if (playlists)
    process_deferred_playlists();
}


/* Thread: scan */
static void *
filescanner(void *arg)
{
  bulk_scan();

  if (!scan_exit)
    {
      /* Enable inotify */
      event_add(&inoev, NULL);

      event_base_dispatch(evbase_scan);
    }

  if (!scan_exit)
    DPRINTF(E_FATAL, L_SCAN, "Scan event loop terminated ahead of time!\n");

  pthread_exit(NULL);
}


/* Thread: scan */
static void
process_inotify_dir(char *path, struct wdpath *w2p, struct inotify_event *ie)
{
  if (ie->mask & IN_CREATE)
    {
      process_directory(w2p->lib, path, 0);
    }

  /* TODO: other cases need more support from the DB */
  /* IN_DELETE, IN_MODIFY, IN_MOVE_FROM / IN_MOVE_TO, IN_DELETE_SELF, IN_MOVE_SELF */
}

/* Thread: scan */
static void
process_inotify_file(char *path, struct wdpath *w2p, struct inotify_event *ie)
{
  struct stat sb;
  char *deref = NULL;
  char *file = path;
  int compilation;
  int ret;

  if (ie->mask & (IN_MODIFY | IN_CREATE))
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
	}

      compilation = check_compilation(w2p->lib, path);

      process_file(file, sb.st_mtime, sb.st_size, compilation, 0);
  
      if (deref)
	free(deref);
    }

  /* TODO: other cases need more support from the DB */
  /* IN_DELETE, IN_MOVE_FROM / IN_MOVE_TO */
}

/* Thread: scan */
static void
inotify_cb(int fd, short event, void *arg)
{
  struct inotify_event *buf;
  struct inotify_event *ie;
  struct wdpath wdsearch;
  struct wdpath *w2p;
  avl_node_t *node;
  char path[PATH_MAX];
  int qsize;
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
      /* ie[0] contains the inotify event information
       * the memory space for ie[1+] contains the name of the file
       * see the inotify documentation
       */

      if ((ie->len == 0) || (ie->name == NULL))
        {
          DPRINTF(E_DBG, L_SCAN, "inotify event with no name\n");

          continue;
        }

      wdsearch.wd = ie->wd;
      node = avl_search(wd2path, &wdsearch);
      if (!node)
	{
	  DPRINTF(E_LOG, L_SCAN, "No matching wdpath found, ignoring event\n");

	  continue;
	}

      w2p = (struct wdpath *)node->item;

      ret = snprintf(path, PATH_MAX, "%s/%s", w2p->path, ie->name);
      if ((ret < 0) || (ret >= sizeof(path)))
	{
	  DPRINTF(E_LOG, L_SCAN, "Skipping %s/%s, PATH_MAX exceeded\n", w2p->path, ie->name);

	  continue;
	}

      if (ie->mask & IN_ISDIR)
	process_inotify_dir(path, w2p, ie);
      else
	process_inotify_file(path, w2p, ie);
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

  wd2path = avl_alloc_tree(wdpath_compare, wdpath_free);
  if (!wd2path)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not allocate AVL tree\n");

      return -1;
    }

  evbase_scan = event_base_new();
  if (!evbase_scan)
    {
      DPRINTF(E_FATAL, L_SCAN, "Could not create an event base\n");

      goto evbase_fail;
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
 evbase_fail:
  avl_free_tree(wd2path);

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
  avl_free_tree(wd2path);
}
