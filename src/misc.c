/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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

#include "err.h"
#include "misc.h"


int
safe_atoi(const char *str, int *val)
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

  if (intval > INT_MAX)
    {
      DPRINTF(E_DBG, L_MISC, "Integer value too large (%s)\n", str);

      return -1;
    }

  *val = (int)intval;

  return 0;
}

int
safe_atol(const char *str, long *val)
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

  *val = intval;

  return 0;
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
