/*
 * Copyright (C) 2018 Christian Meffert <christian.meffert@googlemail.com>
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

#include "smartpl_query.h"
#include "logger.h"
#include "misc.h"


int
smartpl_query_parse_file(struct smartpl *smartpl, const char *file)
{
  return -1;
}

int
smartpl_query_parse_string(struct smartpl *smartpl, const char *expression)
{
  return -1;
}


void
free_smartpl(struct smartpl *smartpl, int content_only)
{
  if (!smartpl)
    return;

  free(smartpl->title);
  free(smartpl->query_where);
  free(smartpl->having);
  free(smartpl->order);

  if (!content_only)
    free(smartpl);
  else
    memset(smartpl, 0, sizeof(struct smartpl));
}

