/*
 * Copyright (C) 2021-2022 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "daap_query.h"
#include "parsers/daap_parser.h"
#include "logger.h"
#include "misc.h"


char *
daap_query_parse_sql(const char *daap_query)
{
  struct daap_result result;

  if (daap_lex_parse(&result, daap_query) != 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not parse '%s': %s\n", daap_query, result.errmsg);
      return NULL;
    }

  return safe_strdup(result.str);
}
