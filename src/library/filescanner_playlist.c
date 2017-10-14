/*
 * Copyright (C) 2015-2017 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "logger.h"
#include "db.h"
#include "library/filescanner.h"
#include "misc.h"
#include "library.h"

/* Formats we can read so far */
#define PLAYLIST_PLS 1
#define PLAYLIST_M3U 2

/* Get metadata from the EXTINF tag */
static int
extinf_get(char *string, struct media_file_info *mfi, int *extinf)
{
  char *ptr;

  if (strncmp(string, "#EXTINF:", strlen("#EXTINF:")) != 0)
    return 0;

  ptr = strchr(string, ',');
  if (!ptr || strlen(ptr) < 2)
    return 0;

  /* New extinf found, so clear old data */
  free_mfi(mfi, 1);

  *extinf = 1;
  mfi->artist = strdup(ptr + 1);

  ptr = strstr(mfi->artist, " -");
  if (ptr && strlen(ptr) > 3)
    mfi->title = strdup(ptr + 3);
  else
    mfi->title = strdup("");
  if (ptr)
    *ptr = '\0';

  return 1;
}

static int
parent_dir(const char **current, const char *path)
{
  const char *ptr;

  if (*current)
    ptr = *current;
  else
    ptr = strrchr(path, '/');

  if (!ptr || (ptr == path))
    return -1;

  for (ptr--; (ptr > path) && (*ptr != '/'); ptr--)
    ;

  *current = ptr;

  return 0;
}

static int
process_url(int pl_id, const char *path, time_t mtime, int extinf, struct media_file_info *mfi)
{
  char virtual_path[PATH_MAX];
  int ret;

  if (extinf)
    DPRINTF(E_INFO, L_SCAN, "Playlist has EXTINF metadata, artist is '%s', title is '%s'\n", mfi->artist, mfi->title);

  mfi->id = db_file_id_bypath(path);
  mfi->path = strdup(path);
  mfi->fname = strdup(filename_from_path(path));
  mfi->data_kind = DATA_KIND_HTTP;
  mfi->time_modified = mtime;
  mfi->directory_id = DIR_HTTP;

  ret = scan_metadata_ffmpeg(path, mfi);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Playlist URL '%s' is unavailable for probe/metadata, assuming MP3 encoding\n", path);
      mfi->type = strdup("mp3");
      mfi->codectype = strdup("mpeg");
      mfi->description = strdup("MPEG audio file");
    }

  if (!mfi->title)
    mfi->title = strdup(mfi->fname);

  snprintf(virtual_path, PATH_MAX, "/http:/%s", mfi->title);
  mfi->virtual_path = strdup(virtual_path);

  library_add_media(mfi);

  return db_pl_add_item_bypath(pl_id, path);
}

static int
process_regular_file(int pl_id, char *path)
{
  struct query_params qp;
  char filter[PATH_MAX];
  const char *a;
  const char *b;
  char *dbpath;
  char *winner;
  int score;
  int i;
  int ret;

  // Playlist might be from Windows so we change backslash to forward slash
  for (i = 0; i < strlen(path); i++)
    {
      if (path[i] == '\\')
	path[i] = '/';
    }

  ret = snprintf(filter, sizeof(filter), "(f.fname = '%s')", filename_from_path(path)); // TODO make case insensitive?
  if (ret < 0 || ret >= sizeof(filter))
    {
      DPRINTF(E_LOG, L_SCAN, "Playlist contains bad filename: '%s'\n", path);
      return -1;
    }

  // Fast path - only works if there are not multiple items in the lib with the
  // same filename
/*  ret = db_pl_add_item_byfile(pl_id, filename_from_path(path));
  if (ret == 1)
    return 0;

  DPRINTF(E_DBG, L_SCAN, "Fast path adding '%s' to playlist did not work (ret=%d), now searching\n", path, ret);
*/

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_BROWSE_PATH;
  qp.sort = S_NONE;
  qp.filter = filter;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      db_query_end(&qp);
      return -1;
    }

  winner = NULL;
  score = 0;
  while (((ret = db_query_fetch_string(&qp, &dbpath)) == 0) && (dbpath))
    {
      if (qp.results == 1)
	{
	  DPRINTF(E_DBG, L_SCAN, "Adding '%s' to playlist %d (fast path)\n", dbpath, pl_id);

	  winner = strdup(dbpath);
	  break;
	}

      for (i = 0, a = NULL, b = NULL; (parent_dir(&a, path) == 0) && (parent_dir(&b, dbpath) == 0) && (strcasecmp(a, b) == 0); i++)
	;

      DPRINTF(E_DBG, L_SCAN, "Comparison of '%s' and '%s' gave score %d\n", dbpath, path, i);

      if (i > score)
	{
	  free(winner);
	  winner = strdup(dbpath);
	  score = ret;
	}
      else if (i == score)
	{
	  free(winner);
	  winner = NULL;
	}
    }

  db_query_end(&qp);

  if (!winner)
    {
      DPRINTF(E_LOG, L_SCAN, "No file in the library matches playlist entry '%s'\n", path);
      return -1;
    }

  db_pl_add_item_bypath(pl_id, winner);
  free(winner);

  return 0;
}

void
scan_playlist(char *file, time_t mtime, int dir_id)
{
  FILE *fp;
  struct media_file_info mfi;
  struct playlist_info *pli;
  struct stat sb;
  char buf[PATH_MAX];
  char *path;
  const char *filename;
  char *ptr;
  size_t len;
  int extinf;
  int pl_id;
  int pl_format;
  int counter;
  int ret;
  char virtual_path[PATH_MAX];

  ptr = strrchr(file, '.');
  if (!ptr)
    return;

  if (strcasecmp(ptr, ".m3u") == 0)
    pl_format = PLAYLIST_M3U;
  else if (strcasecmp(ptr, ".pls") == 0)
    pl_format = PLAYLIST_PLS;
  else
    return;

  filename = filename_from_path(file);

  /* Fetch or create playlist */
  pli = db_pl_fetch_bypath(file);
  if (pli)
    {
      db_pl_ping(pli->id);

      if (mtime && (pli->db_timestamp >= mtime))
	{
	  DPRINTF(E_LOG, L_SCAN, "Playlist '%s' is not modified, no processing required\n", file);

	  // Protect this playlist's radio stations from purge after scan
	  db_pl_ping_items_bymatch("http://", pli->id);
	  free_pli(pli, 0);
	  return;
	}

      DPRINTF(E_LOG, L_SCAN, "Playlist '%s' is modified, processing\n", file);

      pl_id = pli->id;
      db_pl_clear_items(pl_id);
    }
  else
    {
      DPRINTF(E_LOG, L_SCAN, "Playlist '%s' is new, processing\n", file);

      CHECK_NULL(L_SCAN, pli = calloc(1, sizeof(struct playlist_info)));

      pli->type = PL_PLAIN;

      /* Get only the basename, to be used as the playlist title */
      pli->title = strip_extension(filename);

      pli->path = strdup(file);
      snprintf(virtual_path, PATH_MAX, "/file:%s", file);
      pli->virtual_path = strip_extension(virtual_path);

      pli->directory_id = dir_id;

      ret = db_pl_add(pli, &pl_id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Error adding playlist '%s'\n", file);

	  free_pli(pli, 0);
	  return;
	}

      DPRINTF(E_INFO, L_SCAN, "Added new playlist as id %d\n", pl_id);
    }

  free_pli(pli, 0);

  ret = stat(file, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stat() '%s': %s\n", file, strerror(errno));
      return;
    }

  fp = fopen(file, "r");
  if (!fp)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open playlist '%s': %s\n", file, strerror(errno));
      return;
    }

  db_transaction_begin();

  extinf = 0;
  memset(&mfi, 0, sizeof(struct media_file_info));
  counter = 0;

  while (fgets(buf, sizeof(buf), fp) != NULL)
    {
      len = strlen(buf);

      /* rtrim and check that length is sane (ignore blank lines) */
      while ((len > 0) && isspace(buf[len - 1]))
	{
	  len--;
	  buf[len] = '\0';
	}
      if (len < 1)
	continue;

      /* Saves metadata in mfi if EXTINF metadata line */
      if ((pl_format == PLAYLIST_M3U) && extinf_get(buf, &mfi, &extinf))
	continue;

      /* For pls files we are only interested in the part after the FileX= entry */
      path = NULL;
      if ((pl_format == PLAYLIST_PLS) && (strncasecmp(buf, "file", strlen("file")) == 0))
	path = strchr(buf, '=') + 1;
      else if (pl_format == PLAYLIST_M3U)
	path = buf;

      if (!path)
	continue;

      /* Check that first char is sane for a path */
      if ((!isalnum(path[0])) && (path[0] != '/') && (path[0] != '.'))
	continue;

      /* Check if line is an URL, will be added to library, otherwise it should already be there */
      if (strncasecmp(path, "http://", 7) == 0)
	ret = process_url(pl_id, path, sb.st_mtime, extinf, &mfi);
      else
	ret = process_regular_file(pl_id, path);

      if (ret == 0)
	{
	  counter++;
	  if (counter % 200 == 0)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Added %d items to playlist...\n", counter);
	      db_transaction_end();
	      db_transaction_begin();
	    }
	}

      /* Clean up in preparation for next item */
      extinf = 0;
      free_mfi(&mfi, 1);
    }

  db_transaction_end();

  /* We had some extinf that we never got to use, free it now */
  if (extinf)
    free_mfi(&mfi, 1);

  if (!feof(fp))
    DPRINTF(E_LOG, L_SCAN, "Error reading playlist '%s' (only added %d tracks): %s\n", file, counter, strerror(errno));
  else
    DPRINTF(E_LOG, L_SCAN, "Done processing playlist, added/modified %d items\n", counter);

  fclose(fp);
}
