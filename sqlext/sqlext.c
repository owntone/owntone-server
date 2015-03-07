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


/* Taken from "extension-functions.c" by Liam Healy (2010-02-06 15:45:07)
   http://www.sqlite.org/contrib/download/extension-functions.c?get=25 */
/* LMH from sqlite3 3.3.13 */
/*
** This table maps from the first byte of a UTF-8 character to the number
** of trailing bytes expected. A value '4' indicates that the table key
** is not a legal first byte for a UTF-8 character.
*/
static const uint8_t xtra_utf8_bytes[256]  = {
/* 0xxxxxxx */
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,
0, 0, 0, 0, 0, 0, 0, 0,     0, 0, 0, 0, 0, 0, 0, 0,

/* 10wwwwww */
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,
4, 4, 4, 4, 4, 4, 4, 4,     4, 4, 4, 4, 4, 4, 4, 4,

/* 110yyyyy */
1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,
1, 1, 1, 1, 1, 1, 1, 1,     1, 1, 1, 1, 1, 1, 1, 1,

/* 1110zzzz */
2, 2, 2, 2, 2, 2, 2, 2,     2, 2, 2, 2, 2, 2, 2, 2,

/* 11110yyy */
3, 3, 3, 3, 3, 3, 3, 3,     4, 4, 4, 4, 4, 4, 4, 4,
};


/*
** This table maps from the number of trailing bytes in a UTF-8 character
** to an integer constant that is effectively calculated for each character
** read by a naive implementation of a UTF-8 character reader. The code
** in the READ_UTF8 macro explains things best.
*/
static const int xtra_utf8_bits[] =  {
  0,
  12416,          /* (0xC0 << 6) + (0x80) */
  925824,         /* (0xE0 << 12) + (0x80 << 6) + (0x80) */
  63447168        /* (0xF0 << 18) + (0x80 << 12) + (0x80 << 6) + 0x80 */
};

/*
** If a UTF-8 character contains N bytes extra bytes (N bytes follow
** the initial byte so that the total character length is N+1) then
** masking the character with utf8_mask[N] must produce a non-zero
** result.  Otherwise, we have an (illegal) overlong encoding.
*/
static const int utf_mask[] = {
  0x00000000,
  0xffffff80,
  0xfffff800,
  0xffff0000,
};

/* LMH salvaged from sqlite3 3.3.13 source code src/utf.c */
#define READ_UTF8(zIn, c) { \
  int xtra;                                            \
  c = *(zIn)++;                                        \
  xtra = xtra_utf8_bytes[c];                           \
  switch( xtra ){                                      \
    case 4: c = (int)0xFFFD; break;                    \
    case 3: c = (c<<6) + *(zIn)++;                     \
    case 2: c = (c<<6) + *(zIn)++;                     \
    case 1: c = (c<<6) + *(zIn)++;                     \
    c -= xtra_utf8_bits[xtra];                         \
    if( (utf_mask[xtra]&c)==0                          \
        || (c&0xFFFFF800)==0xD800                      \
        || (c&0xFFFFFFFE)==0xFFFE ){  c = 0xFFFD; }    \
  }                                                    \
}

static int sqlite3ReadUtf8(const unsigned char *z)
{
  int c;
  READ_UTF8(z, c);
  return c;
}

/*
 * X is a pointer to the first byte of a UTF-8 character.  Increment
 * X so that it points to the next character.  This only works right
 * if X points to a well-formed UTF-8 string.
 */
#define sqliteNextChar(X)  while( (0xc0&*++(X))==0x80 ){}
#define sqliteCharVal(X)   sqlite3ReadUtf8(X)

/*
 * Given a string z1, retutns the (0 based) index of it's first occurence
 * in z2 after the first s characters.
 * Returns -1 when there isn't a match.
 * updates p to point to the character where the match occured.
 * This is an auxiliary function.
*/
static int _substr(const char* z1, const char* z2, int s, const char** p)
{
  int c = 0;
  int rVal = -1;
  const char* zt1;
  const char* zt2;
  int c1, c2;

  if ('\0' == *z1)
    {
      return -1;
    }

  while ((sqliteCharVal((unsigned char *)z2) != 0) && (c++) < s)
    {
      sqliteNextChar(z2);
    }

  c = 0;
  while ((sqliteCharVal((unsigned char * )z2)) != 0)
    {
      zt1 = z1;
      zt2 = z2;

      do
	{
	  c1 = sqliteCharVal((unsigned char * )zt1);
	  c2 = sqliteCharVal((unsigned char * )zt2);
	  sqliteNextChar(zt1);
	  sqliteNextChar(zt2);
	} while (c1 == c2 && c1 != 0 && c2 != 0);

      if (c1 == 0)
	{
	  rVal = c;
	  break;
	}

      sqliteNextChar(z2);
      ++c;
    }
  if (p)
    {
      *p = z2;
    }
  return rVal >= 0 ? rVal + s : rVal;
}

/*
 * Taken from "extension-functions.c" (function charindexFunc) by Liam Healy (2010-02-06 15:45:07)
 * http://www.sqlite.org/contrib/download/extension-functions.c?get=25
 *
 * Given 2 input strings (s1,s2) and an integer (n) searches from the nth character
 * for the string s1. Returns the position where the match occured.
 * Characters are counted from 1.
 * 0 is returned when no match occurs.
 */
static void sqlext_daap_charindex_xfunc(sqlite3_context *context, int argc, sqlite3_value **argv)
{
  const uint8_t *z1; /* s1 string */
  uint8_t *z2; /* s2 string */
  int s = 0;
  int rVal = 0;

  //assert(argc == 3 || argc == 2);
  if (argc != 2 && argc != 3)
    {
      sqlite3_result_error(context, "daap_charindex() requires 2 or 3 parameters", -1);
      return;
    }

  if ( SQLITE_NULL == sqlite3_value_type(argv[0]) || SQLITE_NULL == sqlite3_value_type(argv[1]))
    {
      sqlite3_result_null(context);
      return;
    }

  z1 = sqlite3_value_text(argv[0]);
  if (z1 == 0)
    return;
  z2 = (uint8_t*) sqlite3_value_text(argv[1]);
  if (argc == 3)
    {
      s = sqlite3_value_int(argv[2]) - 1;
      if (s < 0)
	{
	  s = 0;
	}
    }
  else
    {
      s = 0;
    }

  rVal = _substr((char *) z1, (char *) z2, s, NULL);
  sqlite3_result_int(context, rVal + 1);
}

/*
 * Taken from "extension-functions.c" (function leftFunc) by Liam Healy (2010-02-06 15:45:07)
 * http://www.sqlite.org/contrib/download/extension-functions.c?get=25
 *
 * Given a string (s) and an integer (n) returns the n leftmost (UTF-8) characters
 * if the string has a length<=n or is NULL this function is NOP
 */
static void sqlext_daap_leftstr_xfunc(sqlite3_context *context, int argc, sqlite3_value **argv)
{
  int c = 0;
  int cc = 0;
  int l = 0;
  const unsigned char *z; /* input string */
  const unsigned char *zt;
  unsigned char *rz; /* output string */

  //assert( argc==2);
  if (argc != 2 && argc != 3)
    {
      sqlite3_result_error(context, "daap_leftstr() requires 2 parameters", -1);
      return;
    }

  if ( SQLITE_NULL == sqlite3_value_type(argv[0]) || SQLITE_NULL == sqlite3_value_type(argv[1]))
    {
      sqlite3_result_null(context);
      return;
    }

  z = sqlite3_value_text(argv[0]);
  l = sqlite3_value_int(argv[1]);
  zt = z;

  while ( sqliteCharVal(zt) && c++ < l)
    sqliteNextChar(zt);

  cc = zt - z;

  rz = sqlite3_malloc(zt - z + 1);
  if (!rz)
    {
      sqlite3_result_error_nomem(context);
      return;
    }
  strncpy((char*) rz, (char*) z, zt - z);
  *(rz + cc) = '\0';
  sqlite3_result_text(context, (char*) rz, -1, SQLITE_TRANSIENT);
  sqlite3_free(rz);
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

  ret = sqlite3_create_function(db, "daap_leftstr", 2, SQLITE_UTF8, NULL, sqlext_daap_leftstr_xfunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      if (pzErrMsg)
      *pzErrMsg = sqlite3_mprintf("Could not create daap_leftstr function: %s\n", sqlite3_errmsg(db));

      return -1;
    }

  ret = sqlite3_create_function(db, "daap_charindex", 3, SQLITE_UTF8, NULL, sqlext_daap_charindex_xfunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      if (pzErrMsg)
      *pzErrMsg = sqlite3_mprintf("Could not create daap_charindex function: %s\n", sqlite3_errmsg(db));

      return -1;
    }

  return 0;
}
