/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 *
 * Rewritten from mt-daapd code:
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
#include "filescanner.h"
#include "misc.h"

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

void
scan_playlist(char *file, time_t mtime)
{
  FILE *fp;
  struct media_file_info mfi;
  struct playlist_info *pli;
  struct stat sb;
  char buf[PATH_MAX];
  char *path;
  char *entry;
  char *filename;
  char *ptr;
  size_t len;
  int extinf;
  int pl_id;
  int pl_format;
  int mfi_id;
  int ret;
  char virtual_path[PATH_MAX];
  int i;

  DPRINTF(E_LOG, L_SCAN, "Processing static playlist: %s\n", file);

  ptr = strrchr(file, '.');
  if (!ptr)
    return;

  if (strcasecmp(ptr, ".m3u") == 0)
    pl_format = PLAYLIST_M3U;
  else if (strcasecmp(ptr, ".pls") == 0)
    pl_format = PLAYLIST_PLS;
  else
    return;

  filename = strrchr(file, '/');
  if (!filename)
    filename = file;
  else
    filename++;

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

  /* Fetch or create playlist */
  pli = db_pl_fetch_bypath(file);
  if (pli)
    {
      DPRINTF(E_DBG, L_SCAN, "Found playlist '%s', updating\n", file);

      pl_id = pli->id;

      db_pl_ping(pl_id);
      db_pl_clear_items(pl_id);
    }
  else
    {
      pli = (struct playlist_info *)malloc(sizeof(struct playlist_info));
      if (!pli)
	{
	  DPRINTF(E_LOG, L_SCAN, "Out of memory\n");

	  return;
	}

      memset(pli, 0, sizeof(struct playlist_info));

      pli->type = PL_PLAIN;

      /* Get only the basename, to be used as the playlist title */
      ptr = strrchr(filename, '.');
      if (ptr)
	*ptr = '\0';

      pli->title = strdup(filename);

      /* Restore the full filename */
      if (ptr)
	*ptr = '.';

      pli->path = strdup(file);
      snprintf(virtual_path, PATH_MAX, "/file:%s", file);
      pli->virtual_path = strdup(virtual_path);

      ret = db_pl_add(pli, &pl_id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Error adding playlist '%s'\n", file);

	  free_pli(pli, 0);
	  return;
	}

      DPRINTF(E_INFO, L_SCAN, "Added playlist as id %d\n", pl_id);
    }

  free_pli(pli, 0);

  extinf = 0;
  memset(&mfi, 0, sizeof(struct media_file_info));

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

      /* Check if line is an URL, will be added to library */
      if (strncasecmp(path, "http://", strlen("http://")) == 0)
	{
	  DPRINTF(E_DBG, L_SCAN, "Playlist contains URL entry\n");

	  filename = strdup(path);
	  if (!filename)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Out of memory for playlist filename\n");

	      continue;
	    }

	  if (extinf)
	    DPRINTF(E_INFO, L_SCAN, "Playlist has EXTINF metadata, artist is '%s', title is '%s'\n", mfi.artist, mfi.title);

	  filescanner_process_media(filename, mtime, 0, F_SCAN_TYPE_URL, &mfi);
	}
      /* Regular file, should already be in library */
      else
	{
	  /* Playlist might be from Windows so we change backslash to forward slash */
	  for (i = 0; i < strlen(path); i++)
	    {
	      if (path[i] == '\\')
	        path[i] = '/';
	    }

          /* Now search for the library item where the path has closest match to playlist item */
          /* Succes is when we find an unambiguous match, or when we no longer can expand the  */
          /* the path to refine our search.                                                    */
	  entry = NULL;
	  do
	    {
	      ptr = strrchr(path, '/');
	      if (entry)
		*(entry - 1) = '/';
	      if (ptr)
		{
		  *ptr = '\0';
		  entry = ptr + 1;
		}
	      else
		entry = path;

	      DPRINTF(E_SPAM, L_SCAN, "Playlist entry is now %s\n", entry);
	      ret = db_files_get_count_bymatch(entry);

	    } while (ptr && (ret > 1));

	  if (ret > 0)
	    {
	      mfi_id = db_file_id_bymatch(entry);
	      DPRINTF(E_DBG, L_SCAN, "Found playlist entry match, id is %d, entry is %s\n", mfi_id, entry);

	      filename = db_file_path_byid(mfi_id);
	      if (!filename)
		{
		  DPRINTF(E_LOG, L_SCAN, "Playlist entry %s matches file id %d, but file path is missing.\n", entry, mfi_id);

		  continue;
		}
	    }
	  else
	    {
	      DPRINTF(E_DBG, L_SCAN, "No match for playlist entry %s\n", entry);

	      continue;
	    }
	}

      ret = db_pl_add_item_bypath(pl_id, filename);
      if (ret < 0)
	DPRINTF(E_WARN, L_SCAN, "Could not add %s to playlist\n", filename);

      /* Clean up in preparation for next item */
      extinf = 0;
      free_mfi(&mfi, 1);
      free(filename);
    }

  /* We had some extinf that we never got to use, free it now */
  if (extinf)
    free_mfi(&mfi, 1);

  if (!feof(fp))
    {
      DPRINTF(E_LOG, L_SCAN, "Error reading playlist '%s': %s\n", file, strerror(errno));

      fclose(fp);
      return;
    }

  fclose(fp);

  DPRINTF(E_INFO, L_SCAN, "Done processing playlist\n");
}
