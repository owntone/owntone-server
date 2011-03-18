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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "logger.h"
#include "db.h"
#include "misc.h"
#include "filescanner.h"


int
scan_url_file(char *file, struct media_file_info *mfi)
{
  FILE *fp;
  char *head;
  char *tail;
  char buf[256];
  size_t len;
  int ret;

  DPRINTF(E_DBG, L_SCAN, "Getting URL file info\n");

  fp = fopen(file, "r");
  if (!fp)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not open '%s' for reading: %s\n", file, strerror(errno));

      return -1;
    }

  head = fgets(buf, sizeof(buf), fp);
  fclose(fp);

  if (!head)
    {
      DPRINTF(E_WARN, L_SCAN, "Error reading from file '%s': %s", file, strerror(errno));

      return -1;
    }

  len = strlen(buf);

  if (buf[len - 1] != '\n')
    {
      DPRINTF(E_WARN, L_SCAN, "URL info in file '%s' too large for buffer\n", file);

      return -1;
    }

  while (isspace(buf[len - 1]))
    {
      len--;
      buf[len] = '\0';
    }

  tail = strchr(head, ',');
  if (!tail)
    {
      DPRINTF(E_LOG, L_SCAN, "Badly formatted .url file; expected format is bitrate,descr,url\n");

      return -1;
    }

  head = tail + 1;
  tail = strchr(head, ',');
  if (!tail)
    {
      DPRINTF(E_LOG, L_SCAN, "Badly formatted .url file; expected format is bitrate,descr,url\n");

      return -1;
    }
  *tail = '\0';

  mfi->title = strdup(head);
  mfi->url = strdup(tail + 1);

  ret = safe_atou32(buf, &mfi->bitrate);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Could not read bitrate\n");

      return -1;
    }

  DPRINTF(E_DBG, L_SCAN,"  Title:    %s\n", mfi->title);
  DPRINTF(E_DBG, L_SCAN,"  Bitrate:  %d\n", mfi->bitrate);
  DPRINTF(E_DBG, L_SCAN,"  URL:      %s\n", mfi->url);

  mfi->type = strdup("pls");
  /* codectype = NULL */
  mfi->description = strdup("Playlist URL");

  return 0;
}
