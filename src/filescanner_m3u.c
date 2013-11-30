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


/* Get metadata from the EXTINF tag */
static int
extinf_get(char *string, struct extinf_ctx *extinf)
{
  char *ptr;

  if (strcmp(string, "#EXTINF:") <= 0)
    return 0;

  ptr = strchr(string, ',');
  if (!ptr || strlen(ptr) < 2)
    return 0;

  /* New extinf found, so clear old data */
  if (extinf->found)
    {
      free(extinf->artist);
      free(extinf->title);
    }

  extinf->found = 1;
  extinf->artist = strdup(ptr + 1);

  ptr = strstr(extinf->artist, " -");
  if (ptr && strlen(ptr) > 3)
    extinf->title = strdup(ptr + 3);
  else
    extinf->title = strdup("");
  if (ptr)
    *ptr = '\0';

  return 1;
}

static void
extinf_reset(struct extinf_ctx *extinf)
{
  if (extinf->found)
    {
      free(extinf->artist);
      free(extinf->title);
    }
  extinf->found = 0;
}

void
scan_m3u_playlist(char *file, time_t mtime)
{
  FILE *fp;
  struct playlist_info *pli;
  struct stat sb;
  struct extinf_ctx extinf;
  char buf[PATH_MAX];
  char *entry;
  char *filename;
  char *ptr;
  size_t len;
  int pl_id;
  int mfi_id;
  int ret;
  int i;

  DPRINTF(E_INFO, L_SCAN, "Processing static playlist: %s\n", file);

  ret = stat(file, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not stat() '%s': %s\n", file, strerror(errno));

      return;
    }

  filename = strrchr(file, '/');
  if (!filename)
    filename = file;
  else
    filename++;

  pli = db_pl_fetch_bypath(file);

  if (pli)
    {
      DPRINTF(E_DBG, L_SCAN, "Playlist found, updating\n");

      pl_id = pli->id;

      free_pli(pli, 0);

      db_pl_ping(pl_id);
      db_pl_clear_items(pl_id);
    }
  else
    pl_id = 0;

  fp = fopen(file, "r");
  if (!fp)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not open playlist '%s': %s\n", file, strerror(errno));

      return;
    }

  if (pl_id == 0)
    {
      /* Get only the basename, to be used as the playlist name */
      ptr = strrchr(filename, '.');
      if (ptr)
	*ptr = '\0';

      /* Safe: filename is a subset of file which is <= PATH_MAX already */
      strncpy(buf, filename, sizeof(buf));

      /* Restore the full filename */
      if (ptr)
	*ptr = '.';

      ret = db_pl_add(buf, file, &pl_id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SCAN, "Error adding m3u playlist '%s'\n", file);

	  return;
	}

      DPRINTF(E_INFO, L_SCAN, "Added playlist as id %d\n", pl_id);
    }

  extinf.found = 0;

  while (fgets(buf, sizeof(buf), fp) != NULL)
    {
      len = strlen(buf);
      if (len >= (sizeof(buf) - 1))
	{
	  DPRINTF(E_LOG, L_SCAN, "Playlist entry exceeds PATH_MAX, discarding\n");

	  continue;
	}

      /* rtrim and check that length is sane (ignore blank lines) */
      while ((len > 0) && isspace(buf[len - 1]))
	{
	  len--;
	  buf[len] = '\0';
	}
      if (len < 1)
	continue;

      /* Saves metadata in extinf if EXTINF metadata line */
      if (extinf_get(buf, &extinf))
	continue;

      /* Check that first char is sane for a path */
      if ((!isalnum(buf[0])) && (buf[0] != '/') && (buf[0] != '.'))
	continue;

      /* Check if line is an URL */
      if (strcmp(buf, "http://") > 0)
	{
	  DPRINTF(E_DBG, L_SCAN, "Playlist contains URL entry\n");

	  filename = strdup(buf);
	  if (!filename)
	    {
	      DPRINTF(E_LOG, L_SCAN, "Out of memory for playlist filename\n");

	      continue;
	    }

	  if (extinf.found)
	    DPRINTF(E_INFO, L_SCAN, "Playlist has EXTINF metadata, artist is '%s', title is '%s'\n", extinf.artist, extinf.title);

	  process_media_file(filename, mtime, 0, F_SCAN_TYPE_URL, &extinf);
	}
      /* Regular file */
      else
	{
	  /* m3u might be from Windows so we change backslash to forward slash */
	  for (i = 0; i < strlen(buf); i++)
	    {
	      if (buf[i] == '\\')
	        buf[i] = '/';
	    }

          /* Now search for the library item where the path has closest match to playlist item */
          /* Succes is when we find an unambiguous match, or when we no longer can expand the  */
          /* the path to refine our search.                                                    */
	  entry = NULL;
	  do
	    {
	      ptr = strrchr(buf, '/');
	      if (entry)
		*(entry - 1) = '/';
	      if (ptr)
		{
		  *ptr = '\0';
		  entry = ptr + 1;
		}
	      else
		entry = buf;

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

      extinf_reset(&extinf);
      free(filename);
    }

  if (!feof(fp))
    {
      DPRINTF(E_LOG, L_SCAN, "Error reading playlist '%s': %s\n", file, strerror(errno));

      fclose(fp);
      return;
    }

  fclose(fp);

  DPRINTF(E_INFO, L_SCAN, "Done processing playlist\n");
}
