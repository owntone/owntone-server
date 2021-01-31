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
#include "misc.h"
#include "smartpl_query.h"
#include "library/filescanner.h"
#include "library.h"


void
scan_smartpl(const char *file, time_t mtime, int dir_id)
{
  struct smartpl smartpl;
  struct playlist_info *pli;
  int ret;

  /* Fetch or create playlist */
  pli = db_pl_fetch_bypath(file);
  if (!pli)
    {
      CHECK_NULL(L_SCAN, pli = calloc(1, sizeof(struct playlist_info)));

      ret = playlist_fill(pli, file);
      if (ret < 0)
	goto free_pli;

      pli->type = PL_SMART;
    }

  pli->directory_id = dir_id;

  memset(&smartpl, 0, sizeof(struct smartpl));
  ret = smartpl_query_parse_file(&smartpl, file);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error parsing smart playlist '%s'\n", file);
      free_smartpl(&smartpl, 1);
      goto free_pli;
    }

  swap_pointers(&pli->title, &smartpl.title);
  swap_pointers(&pli->query, &smartpl.query_where);
  swap_pointers(&pli->query_order, &smartpl.order);
  pli->query_limit = (smartpl.limit > 0) ? smartpl.limit : 0;

  free_smartpl(&smartpl, 1);

  ret = library_playlist_save(pli);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error saving smart playlist '%s'\n", file);
      goto free_pli;
    }

  DPRINTF(E_INFO, L_SCAN, "Added or updated smart playlist '%s' with id %d\n", file, ret);

 free_pli:
  free_pli(pli, 0);
  return;
}
