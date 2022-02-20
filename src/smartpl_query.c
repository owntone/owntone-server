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
#include "parsers/smartpl_parser.h"
#include "logger.h"
#include "misc.h"

#define SMARTPL_SIZE_MAX 8192

int
smartpl_query_parse_file(struct smartpl *smartpl, const char *file)
{
  char *expression = NULL;
  size_t size;
  size_t got;
  int ret;
  FILE *f;

  f = fopen(file, "rb");
  if (!f)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not open smart playlist '%s'\n", file);
      goto error;
    }

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  if (size <= 0 || size > SMARTPL_SIZE_MAX)
    {
      DPRINTF(E_LOG, L_SCAN, "Smart playlist '%s' is zero bytes or too large (max size is %d)\n", file, SMARTPL_SIZE_MAX);
      goto error;
    }

  fseek(f, 0, SEEK_SET);

  CHECK_NULL(L_SCAN, expression = calloc(1, size + 1));

  got = fread(expression, 1, size, f);
  if (got != size)
    {
      DPRINTF(E_LOG, L_SCAN, "Unknown error reading smart playlist '%s'\n", file);
      goto error;
    }

  fclose(f);

  ret = smartpl_query_parse_string(smartpl, expression);
  free(expression);
  return ret;

 error:
  free(expression);
  if (f)
    fclose(f);
  return -1;
}

int
smartpl_query_parse_string(struct smartpl *smartpl, const char *expression)
{
  struct smartpl_result result;

  if (!expression)
    {
      DPRINTF(E_WARN, L_SCAN, "Parse smartpl query input is null\n");
      return -1;
    }

  DPRINTF(E_SPAM, L_SCAN, "Parse smartpl query input '%s'\n", expression);

  if (smartpl_lex_parse(&result, expression) != 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not parse '%s': %s\n", expression, result.errmsg);
      return -1;
    }

  if (result.title[0] == '\0' || !result.where)
    {
      DPRINTF(E_LOG, L_SCAN, "Missing title or filter when parsing '%s'\n", expression);
      return -1;
    }

  free_smartpl(smartpl, 1);

  // Note the fields returned by the smartpl parser will not be prefixed with
  // "f." (unlike the daap parser results and most other queries). The reason is
  // that the smartpl syntax allows the user to request ordering by a calculated
  // field in a group query, and calculated fields are not in the "f" namespace.
  // An example of this happening is if the JSON API search is called with
  // type=album and the smartpl expression has "order+by+time_played+desc".
  smartpl->title = strdup(result.title);
  smartpl->query_where = strdup(result.where);
  smartpl->having = safe_strdup(result.having);
  smartpl->order = safe_strdup(result.order);
  smartpl->limit = result.limit;

  DPRINTF(E_SPAM, L_SCAN, "Parse smartpl query output '%s': WHERE %s HAVING %s ORDER BY %s LIMIT %d\n",
    smartpl->title, smartpl->query_where, smartpl->having, smartpl->order, smartpl->limit);

  return 0;
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

