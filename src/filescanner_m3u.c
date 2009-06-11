/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "logger.h"
#include "db.h"
#include "filescanner.h"


void
scan_m3u_playlist(char *file)
{
  FILE *fp;
  struct playlist_info *pli;
  struct stat sb;
  char buf[PATH_MAX];
  char rel_entry[PATH_MAX];
  char *pl_base;
  char *entry;
  char *filename;
  char *ptr;
  size_t len;
  int pl_id;
  int mf_id;
  int ret;

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
      if (pli->db_timestamp >= sb.st_mtime)
	{
	  DPRINTF(E_DBG, L_SCAN, "Playlist up-to-date\n");

	  db_pl_ping(pli->id);

	  free_pli(pli, 0);
	  return;
	}
      else
	{
	  DPRINTF(E_DBG, L_SCAN, "Playlist needs update\n");

	  db_pl_delete(pli->id);
	  free_pli(pli, 0);
	}
    }

  fp = fopen(file, "r");
  if (!fp)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not open playlist '%s': %s\n", file, strerror(errno));

      return;
    }

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

  ptr = strrchr(file, '/');
  if (!ptr)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not determine playlist base path\n");

      return;
    }

  *ptr = '\0';
  pl_base = strdup(file);
  *ptr = '/';

  if (!pl_base)
    {
      DPRINTF(E_WARN, L_SCAN, "Out of memory\n");

      return;
    }

  DPRINTF(E_INFO, L_SCAN, "Added playlist as id %d\n", pl_id);

  while (fgets(buf, sizeof(buf), fp) != NULL)
    {
      len = strlen(buf);
      if (buf[len - 1] != '\n')
	{
	  DPRINTF(E_WARN, L_SCAN, "Entry exceeds PATH_MAX, discarding\n");

	  while (fgets(buf, sizeof(buf), fp) != NULL)
	    {
	      if (buf[strlen(buf) - 1] == '\n')
		break;
	    }

	  continue;
	}

      if ((buf[0] == ';') || (buf[0] == '#'))
	continue;

      while (isspace(buf[len - 1]))
	{
	  len--;
	  buf[len] = '\0';
	}

      /* Absolute vs. relative path */
      if (buf[0] == '/')
	{
	  entry = buf;
	}
      else
	{
	  ret = snprintf(rel_entry, sizeof(rel_entry),"%s/%s", pl_base, buf);
	  if ((ret < 0) || (ret >= sizeof(rel_entry)))
	    {
	      DPRINTF(E_WARN, L_SCAN, "Skipping entry, PATH_MAX exceeded\n");

	      continue;
	    }

	  entry = rel_entry;
	}

	filename = realpath(entry, NULL);
	if (!filename)
	  {
	    DPRINTF(E_WARN, L_SCAN, "Could not determine real path for '%s': %s\n", entry, strerror(errno));

	    continue;
	  }

	DPRINTF(E_DBG, L_SCAN, "Checking %s\n", filename);

	ret = db_file_id_bypath(filename, &mf_id);
	if (ret < 0)
	  {
	    DPRINTF(E_WARN, L_SCAN, "Playlist entry '%s' not found\n", entry);

	    free(filename);
	    continue;
	  }

	DPRINTF(E_DBG, L_SCAN, "Resolved %s to %d\n", filename, mf_id);
	ret = db_pl_add_item(pl_id, mf_id);
	if (ret < 0)
	  DPRINTF(E_WARN, L_SCAN, "Could not add %s to playlist\n", filename);
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
