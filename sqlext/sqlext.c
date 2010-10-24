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


/*
 * MurmurHash2, 64-bit versions, by Austin Appleby
 *
 * Code released under the public domain, as per
 *    <http://murmurhash.googlepages.com/>
 * as of 2010-01-03.
 */

#if SIZEOF_VOID_P == 8 /* 64bit platforms */

static uint64_t
murmur_hash64(const void *key, int len, uint32_t seed)
{
  const int r = 47;
  const uint64_t m = 0xc6a4a7935bd1e995;

  const uint64_t *data;
  const uint64_t *end;
  const unsigned char *data_tail;
  uint64_t h;
  uint64_t k;

  h = seed ^ (len * m);
  data = (const uint64_t *)key;
  end = data + (len / 8);

  while (data != end)
    {
      k = *data++;

      k *= m;
      k ^= k >> r;
      k *= m;

      h ^= k;
      h *= m;
    }

  data_tail = (const unsigned char *)data;

  switch (len & 7)
    {
      case 7:
	h ^= (uint64_t)(data_tail[6]) << 48;
      case 6:
	h ^= (uint64_t)(data_tail[5]) << 40;
      case 5:
	h ^= (uint64_t)(data_tail[4]) << 32;
      case 4:
	h ^= (uint64_t)(data_tail[3]) << 24;
      case 3:
	h ^= (uint64_t)(data_tail[2]) << 16;
      case 2:
	h ^= (uint64_t)(data_tail[1]) << 8;
      case 1:
	h ^= (uint64_t)(data_tail[0]);
	h *= m;
    }

  h ^= h >> r;
  h *= m;
  h ^= h >> r;

  return h;
}

#elif SIZEOF_VOID_P == 4 /* 32bit platforms */

static uint64_t
murmur_hash64(const void *key, int len, uint32_t seed)
{
  const int r = 24;
  const uint32_t m = 0x5bd1e995;

  const uint32_t *data;
  const unsigned char *data_tail;
  uint32_t k1;
  uint32_t h1;
  uint32_t k2;
  uint32_t h2;

  uint64_t h;

  h1 = seed ^ len;
  h2 = 0;

  data = (const uint32_t *)key;

  while (len >= 8)
    {
      k1 = *data++;
      k1 *= m; k1 ^= k1 >> r; k1 *= m;
      h1 *= m; h1 ^= k1;

      k2 = *data++;
      k2 *= m; k2 ^= k2 >> r; k2 *= m;
      h2 *= m; h2 ^= k2;

      len -= 8;
    }

  if (len >= 4)
    {
      k1 = *data++;
      k1 *= m; k1 ^= k1 >> r; k1 *= m;
      h1 *= m; h1 ^= k1;
      len -= 4;
    }

  data_tail = (const unsigned char *)data;

  switch(len)
    {
      case 3:
	h2 ^= (uint32_t)(data_tail[2]) << 16;
      case 2:
	h2 ^= (uint32_t)(data_tail[1]) << 8;
      case 1:
	h2 ^= (uint32_t)(data_tail[0]);
	h2 *= m;
    };

  h1 ^= h2 >> 18; h1 *= m;
  h2 ^= h1 >> 22; h2 *= m;
  h1 ^= h2 >> 17; h1 *= m;
  h2 ^= h1 >> 19; h2 *= m;

  h = h1;
  h = (h << 32) | h2;

  return h;
}

#else
# error Platform not supported
#endif

static void
sqlext_daap_songalbumid_xfunc(sqlite3_context *pv, int n, sqlite3_value **ppv)
{
  const char *album_artist;
  const char *album;
  char *hashbuf;
  sqlite3_int64 result;

  if (n != 2)
    {
      sqlite3_result_error(pv, "daap_songalbumid() requires 2 parameters, album_artist and album", -1);
      return;
    }

  if ((sqlite3_value_type(ppv[0]) != SQLITE_TEXT)
      || (sqlite3_value_type(ppv[1]) != SQLITE_TEXT))
    {
      sqlite3_result_error(pv, "daap_songalbumid() requires 2 text parameters", -1);
      return;
    }

  album_artist = (const char *)sqlite3_value_text(ppv[0]);
  album = (const char *)sqlite3_value_text(ppv[1]);

  hashbuf = sqlite3_mprintf("%s==%s", (album_artist) ? album_artist : "", (album) ? album : "");
  if (!hashbuf)
    {
      sqlite3_result_error(pv, "daap_songalbumid() out of memory for hashbuf", -1);
      return;
    }

  /* Limit hash length to 63 bits, due to signed type in sqlite */
  result = murmur_hash64(hashbuf, strlen(hashbuf), 0) >> 1;

  sqlite3_free(hashbuf);

  sqlite3_result_int64(pv, result);
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

  ret = sqlite3_create_function(db, "daap_songalbumid", 2, SQLITE_UTF8, NULL, sqlext_daap_songalbumid_xfunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      if (pzErrMsg)
	*pzErrMsg = sqlite3_mprintf("Could not create daap_songalbumid function: %s\n", sqlite3_errmsg(db));

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
