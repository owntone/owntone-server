/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2010 Kai Elwert <elwertk@googlemail.com>
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

#include <string.h>
#include <stdint.h>

#include <unistr.h>
#include <unictype.h>
#include <unicase.h>

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1

static void
sqlext_daap_no_zero_xfunc(sqlite3_context *pv, int n, sqlite3_value **ppv)
{
  sqlite3_int64 new_value;
  sqlite3_int64 old_value;

  if (n != 2)
    {
      sqlite3_result_error(pv, "daap_no_zero() requires 2 parameters, new_value and old_value", -1);
      return;
    }

  if ((sqlite3_value_type(ppv[0]) != SQLITE_INTEGER)
      || (sqlite3_value_type(ppv[1]) != SQLITE_INTEGER))
    {
      sqlite3_result_error(pv, "daap_no_zero() requires 2 integer parameters", -1);
      return;
    }

  new_value = sqlite3_value_int64(ppv[0]);
  old_value = sqlite3_value_int64(ppv[1]);

  if (new_value != 0)
    sqlite3_result_int64(pv, new_value);
  else
    sqlite3_result_int64(pv, old_value);
}

static int
sqlext_daap_unicode_xcollation(void *notused, int llen, const void *left, int rlen, const void *right)
{
  ucs4_t lch;
  ucs4_t rch;
  int lalpha;
  int ralpha;
  int rpp;
  int ret;

  /* Extract first utf-8 character */
  ret = u8_mbtoucr(&lch, (const uint8_t *)left, llen);
  if (ret < 0)
    return 0;

  ret = u8_mbtoucr(&rch, (const uint8_t *)right, rlen);
  if (ret < 0)
    return 0;

  /* Ensure digits and other non-alphanum sort to tail */
  lalpha = uc_is_alpha(lch);
  ralpha = uc_is_alpha(rch);

  if (!lalpha && ralpha)
    return 1;
  else if (lalpha && !ralpha)
    return -1;

  /* Compare case and normalization insensitive */
  ret = u8_casecmp((const uint8_t *)left, llen, (const uint8_t*)right, rlen, NULL, UNINORM_NFD, &rpp);
  if (ret < 0)
    return 0;

  return rpp;
}

int
sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
  SQLITE_EXTENSION_INIT2(pApi);
  int ret;

  ret = sqlite3_create_function(db, "daap_no_zero", 2, SQLITE_UTF8, NULL, sqlext_daap_no_zero_xfunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      if (pzErrMsg)
	*pzErrMsg = sqlite3_mprintf("Could not create daap_no_zero function: %s\n", sqlite3_errmsg(db));

      return -1;
    }

  ret = sqlite3_create_collation(db, "DAAP", SQLITE_UTF8, NULL, sqlext_daap_unicode_xcollation);
  if (ret != SQLITE_OK)
    {
      if (pzErrMsg)
	*pzErrMsg = sqlite3_mprintf("Could not create sqlite3 custom collation DAAP: %s\n", sqlite3_errmsg(db));

      return -1;
    }

  return 0;
}
