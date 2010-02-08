/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 *
 * Some code included below is in the public domain, check comments
 * in the file.
 *
 * Pieces of code adapted from mt-daapd:
 * Copyright (C) 2003-2007 Ron Pedde (ron@pedde.com)
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include <sys/param.h>

#include "logger.h"
#include "misc.h"


int
safe_atoi32(const char *str, int32_t *val)
{
  char *end;
  long intval;

  errno = 0;
  intval = strtol(str, &end, 10);

  if (((errno == ERANGE) && ((intval == LONG_MAX) || (intval == LONG_MIN)))
      || ((errno != 0) && (intval == 0)))
    {
      DPRINTF(E_DBG, L_MISC, "Invalid integer in string (%s): %s\n", str, strerror(errno));

      return -1;
    }

  if (end == str)
    {
      DPRINTF(E_DBG, L_MISC, "No integer found in string (%s)\n", str);

      return -1;
    }

  if (intval > INT32_MAX)
    {
      DPRINTF(E_DBG, L_MISC, "Integer value too large (%s)\n", str);

      return -1;
    }

  *val = (int32_t)intval;

  return 0;
}

int
safe_atou32(const char *str, uint32_t *val)
{
  char *end;
  unsigned long intval;

  errno = 0;
  intval = strtoul(str, &end, 10);

  if (((errno == ERANGE) && (intval == ULONG_MAX))
      || ((errno != 0) && (intval == 0)))
    {
      DPRINTF(E_DBG, L_MISC, "Invalid integer in string (%s): %s\n", str, strerror(errno));

      return -1;
    }

  if (end == str)
    {
      DPRINTF(E_DBG, L_MISC, "No integer found in string (%s)\n", str);

      return -1;
    }

  if (intval > UINT32_MAX)
    {
      DPRINTF(E_DBG, L_MISC, "Integer value too large (%s)\n", str);

      return -1;
    }

  *val = (uint32_t)intval;

  return 0;
}

int
safe_atoi64(const char *str, int64_t *val)
{
  char *end;
  long long intval;

  errno = 0;
  intval = strtoll(str, &end, 10);

  if (((errno == ERANGE) && ((intval == LLONG_MAX) || (intval == LLONG_MIN)))
      || ((errno != 0) && (intval == 0)))
    {
      DPRINTF(E_DBG, L_MISC, "Invalid integer in string (%s): %s\n", str, strerror(errno));

      return -1;
    }

  if (end == str)
    {
      DPRINTF(E_DBG, L_MISC, "No integer found in string (%s)\n", str);

      return -1;
    }

  if (intval > INT64_MAX)
    {
      DPRINTF(E_DBG, L_MISC, "Integer value too large (%s)\n", str);

      return -1;
    }

  *val = (int64_t)intval;

  return 0;
}

int
safe_atou64(const char *str, uint64_t *val)
{
  char *end;
  unsigned long long intval;

  errno = 0;
  intval = strtoull(str, &end, 10);

  if (((errno == ERANGE) && (intval == ULLONG_MAX))
      || ((errno != 0) && (intval == 0)))
    {
      DPRINTF(E_DBG, L_MISC, "Invalid integer in string (%s): %s\n", str, strerror(errno));

      return -1;
    }

  if (end == str)
    {
      DPRINTF(E_DBG, L_MISC, "No integer found in string (%s)\n", str);

      return -1;
    }

  if (intval > UINT64_MAX)
    {
      DPRINTF(E_DBG, L_MISC, "Integer value too large (%s)\n", str);

      return -1;
    }

  *val = (uint64_t)intval;

  return 0;
}

char *
m_realpath(const char *pathname)
{
  char buf[PATH_MAX];
  char *ret;

  ret = realpath(pathname, buf);
  if (!ret)
    return NULL;

  ret = strdup(buf);
  if (!ret)
    {
      DPRINTF(E_LOG, L_MISC, "Out of memory for realpath\n");

      return NULL;
    }

  return ret;
}

uint32_t
djb_hash(void *data, size_t len)
{
  unsigned char *bytes = data;
  uint32_t hash = 5381;

  while (len--)
    {
      hash = ((hash << 5) + hash) + *bytes;
      bytes++;
    }

  return hash;
}


static unsigned char b64_decode_table[256];

char *
b64_decode(const char *b64)
{
  char *str;
  const unsigned char *iptr;
  unsigned char *optr;
  unsigned char c;
  int len;
  int i;

  if (b64_decode_table[0] == 0)
    {
      memset(b64_decode_table, 0xff, sizeof(b64_decode_table));

      /* Base64 encoding: A-Za-z0-9+/ */
      for (i = 0; i < 26; i++)
	{
	  b64_decode_table['A' + i] = i;
	  b64_decode_table['a' + i] = i + 26;
	}

      for (i = 0; i < 10; i++)
	b64_decode_table['0' + i] = i + 52;

      b64_decode_table['+'] = 62;
      b64_decode_table['/'] = 63;

      /* Stop on '=' */
      b64_decode_table['='] = 100; /* > 63 */
    }

  len = strlen(b64);

  str = (char *)malloc(len);
  if (!str)
    return NULL;

  memset(str, 0, len);

  iptr = (const unsigned char *)b64;
  optr = (unsigned char *)str;
  i = 0;

  while (len)
    {
      if (*iptr == '=')
	break;

      c = b64_decode_table[*iptr];
      if (c > 63)
	{
	  iptr++;
	  len--;
	  continue;
	}

      switch (i)
	{
	  case 0:
	    optr[0] = c << 2;
	    break;
	  case 1:
	    optr[0] |= c >> 4;
	    optr[1] = c << 4;
	    break;
	  case 2:
	    optr[1] |= c >> 2;
	    optr[2] = c << 6;
	    break;
	  case 3:
	    optr[2] |= c;
	    break;
	}

      i++;
      if (i == 4)
	{
	  optr += 3;
	  i = 0;
	}

      len--;
      iptr++;
    }

  return str;
}

/*
 * MurmurHash2, 64-bit versions, by Austin Appleby
 *
 * Code released under the public domain, as per
 *    <http://murmurhash.googlepages.com/>
 * as of 2010-01-03.
 */

#if SIZEOF_VOID_P == 8 /* 64bit platforms */

uint64_t
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

uint64_t
murmur_hash64(const void *key, int len, uint32_t seed)
{
  const int r = 24;
  const uint32_t m = 0x5bd1e995;

  const uint32_t *data;
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
      len -= 4;

      k2 = *data++;
      k2 *= m; k2 ^= k2 >> r; k2 *= m;
      h2 *= m; h2 ^= k2;
      len -= 4;
    }

  if (len >= 4)
    {
      k1 = *data++;
      k1 *= m; k1 ^= k1 >> r; k1 *= m;
      h1 *= m; h1 ^= k1;
      len -= 4;
    }

  switch(len)
    {
      case 3:
	h2 ^= (uint32_t)(data[2]) << 16;
      case 2:
	h2 ^= (uint32_t)(data[1]) << 8;
      case 1:
	h2 ^= (uint32_t)(data[0]);
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
