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
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/param.h>
#ifndef CLOCK_REALTIME
#include <sys/time.h>
#endif

#include <unistr.h>
#include <uniconv.h>

#include "logger.h"
#include "conffile.h"
#include "misc.h"


static char *buildopts[] =
  {
#ifdef HAVE_FFMPEG
    "ffmpeg",
#else
    "libav",
#endif
#ifdef ITUNES
    "iTunes XML",
#endif
#ifdef SPOTIFY
    "Spotify",
#endif
#ifdef LASTFM
    "LastFM",
#endif
#ifdef CHROMECAST
    "Chromecast",
#endif
#ifdef MPD
    "MPD",
#endif
#ifdef RAOP_VERIFICATION
    "Device verification",
#endif
#ifdef HAVE_LIBWEBSOCKETS
    "Websockets",
#endif
#ifdef HAVE_ALSA
    "ALSA",
#endif
#ifdef HAVE_LIBPULSE
    "Pulseaudio",
#endif
#ifdef WEBINTERFACE
    "Webinterface",
#endif
    NULL
  };

char **
buildopts_get()
{
  return buildopts;
}

int
safe_atoi32(const char *str, int32_t *val)
{
  char *end;
  long intval;

  *val = 0;

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

  *val = 0;

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
safe_hextou32(const char *str, uint32_t *val)
{
  char *end;
  unsigned long intval;

  *val = 0;

  errno = 0;
  intval = strtoul(str, &end, 16);

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

  *val = 0;

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

  *val = 0;

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

int
safe_hextou64(const char *str, uint64_t *val)
{
  char *end;
  unsigned long long intval;

  *val = 0;

  errno = 0;
  intval = strtoull(str, &end, 16);

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
safe_strdup(const char *str)
{
  if (str == NULL)
    return NULL;

  return strdup(str);
}

/*
 * Wrapper function for vasprintf by Intel Corporation
 * Published under the L-GPL 2.1 licence as part of clr-boot-manager
 *
 * https://github.com/clearlinux/clr-boot-manager
 */
char *
safe_asprintf(const char *fmt, ...)
{
  char *ret = NULL;
  va_list va;

  va_start(va, fmt);
  if (vasprintf(&ret, fmt, va) < 0)
    {
      DPRINTF(E_FATAL, L_MISC, "Out of memory for safe_asprintf\n");
      abort();
    }
  va_end(va);

  return ret;
}

int
safe_snprintf_cat(char *dst, size_t n, const char *fmt, ...)
{
  size_t dstlen;
  va_list va;
  int ret;

  if (!dst || !fmt)
    return -1;

  dstlen = strlen(dst);
  if (n < dstlen)
    return -1;

  va_start(va, fmt);
  ret = vsnprintf(dst + dstlen, n - dstlen, fmt, va);
  va_end(va);

  if (ret >= 0 && ret < n - dstlen)
    return 0;
  else
    return -1;
}


/* Key/value functions */
struct keyval *
keyval_alloc(void)
{
  struct keyval *kv;

  kv = (struct keyval *)malloc(sizeof(struct keyval));
  if (!kv)
    {
      DPRINTF(E_LOG, L_MISC, "Out of memory for keyval alloc\n");

      return NULL;
    }

  memset(kv, 0, sizeof(struct keyval));

  return kv;
}

int
keyval_add_size(struct keyval *kv, const char *name, const char *value, size_t size)
{
  struct onekeyval *okv;
  const char *val;

  if (!kv)
    return -1;

  /* Check for duplicate key names */
  val = keyval_get(kv, name);
  if (val)
    {
      /* Same value, fine */
      if (strcmp(val, value) == 0)
        return 0;
      else /* Different value, bad */
        return -1;
    }

  okv = (struct onekeyval *)malloc(sizeof(struct onekeyval));
  if (!okv)
    {
      DPRINTF(E_LOG, L_MISC, "Out of memory for new keyval\n");

      return -1;
    }

  okv->name = strdup(name);
  if (!okv->name)
    {
      DPRINTF(E_LOG, L_MISC, "Out of memory for new keyval name\n");

      free(okv);
      return -1;
    }

  okv->value = (char *)malloc(size + 1);
  if (!okv->value)
    {
      DPRINTF(E_LOG, L_MISC, "Out of memory for new keyval value\n");

      free(okv->name);
      free(okv);
      return -1;
    }

  memcpy(okv->value, value, size);
  okv->value[size] = '\0';

  okv->next = NULL;

  if (!kv->head)
    kv->head = okv;

  if (kv->tail)
    kv->tail->next = okv;

  kv->tail = okv;

  return 0;
}

int
keyval_add(struct keyval *kv, const char *name, const char *value)
{
  return keyval_add_size(kv, name, value, strlen(value));
}

void
keyval_remove(struct keyval *kv, const char *name)
{
  struct onekeyval *okv;
  struct onekeyval *pokv;

  if (!kv)
    return;

  for (pokv = NULL, okv = kv->head; okv; pokv = okv, okv = okv->next)
    {
      if (strcasecmp(okv->name, name) == 0)
        break;
    }

  if (!okv)
    return;

  if (okv == kv->head)
    kv->head = okv->next;

  if (okv == kv->tail)
    kv->tail = pokv;

  if (pokv)
    pokv->next = okv->next;

  free(okv->name);
  free(okv->value);
  free(okv);
}

const char *
keyval_get(struct keyval *kv, const char *name)
{
  struct onekeyval *okv;

  if (!kv)
    return NULL;

  for (okv = kv->head; okv; okv = okv->next)
    {
      if (strcasecmp(okv->name, name) == 0)
        return okv->value;
    }

  return NULL;
}

void
keyval_clear(struct keyval *kv)
{
  struct onekeyval *hokv;
  struct onekeyval *okv;

  if (!kv)
    return;

  hokv = kv->head;

  for (okv = hokv; hokv; okv = hokv)
    {
      hokv = okv->next;

      free(okv->name);
      free(okv->value);
      free(okv);
    }

  kv->head = NULL;
  kv->tail = NULL;
}

void
keyval_sort(struct keyval *kv)
{
  struct onekeyval *head;
  struct onekeyval *okv;
  struct onekeyval *sokv;

  if (!kv || !kv->head)
    return;

  head = kv->head;
  for (okv = kv->head; okv; okv = okv->next)
    {
      okv->sort = NULL;
      for (sokv = kv->head; sokv; sokv = sokv->next)
	{
	  // We try to find a name which is greater than okv->name
	  // but less than our current candidate (okv->sort->name)
	  if ( (strcmp(sokv->name, okv->name) > 0) &&
	       ((okv->sort == NULL) || (strcmp(sokv->name, okv->sort->name) < 0)) )
	    okv->sort = sokv;
	}

      // Find smallest name, which will be the new head
      if (strcmp(okv->name, head->name) < 0)
	head = okv;
    }

  while ((okv = kv->head))
    {
      kv->head  = okv->next;
      okv->next = okv->sort;
    }

  kv->head = head;
  for (okv = kv->head; okv; okv = okv->next)
    kv->tail = okv;

  DPRINTF(E_DBG, L_MISC, "Keyval sorted. New head: %s. New tail: %s.\n", kv->head->name, kv->tail->name);
}


char **
m_readfile(const char *path, int num_lines)
{
  char buf[256];
  FILE *fp;
  char **lines;
  char *line;
  int i;

  // Alloc array of char pointers
  lines = calloc(num_lines, sizeof(char *));
  if (!lines)
    return NULL;

  fp = fopen(path, "rb");
  if (!fp)
    {
      DPRINTF(E_LOG, L_MISC, "Could not open file '%s' for reading: %s\n", path, strerror(errno));
      free(lines);
      return NULL;
    }

  for (i = 0; i < num_lines; i++)
    {
      line = fgets(buf, sizeof(buf), fp);
      if (!line)
	{
	  DPRINTF(E_LOG, L_MISC, "File '%s' has fewer lines than expected (found %d, expected %d)\n", path, i, num_lines);
	  goto error;
	}

      lines[i] = atrim(line);
      if (!lines[i] || (strlen(lines[i]) == 0))
	{
	  DPRINTF(E_LOG, L_MISC, "Line %d in '%s' is invalid\n", i+1, path);
	  goto error;
	}
    }

  fclose(fp);

  return lines;

 error:
  for (i = 0; i < num_lines; i++)
    free(lines[i]);

  free(lines);
  fclose(fp);
  return NULL;
}

char *
unicode_fixup_string(char *str, const char *fromcode)
{
  uint8_t *ret;
  size_t len;

  if (!str)
    return NULL;

  len = strlen(str);

  /* String is valid UTF-8 */
  if (!u8_check((uint8_t *)str, len))
    {
      if (len >= 3)
	{
	  /* Check for and strip byte-order mark */
	  if (memcmp("\xef\xbb\xbf", str, 3) == 0)
	    memmove(str, str + 3, len - 3 + 1);
	}

      return str;
    }

  ret = u8_strconv_from_encoding(str, fromcode, iconveh_question_mark);
  if (!ret)
    {
      DPRINTF(E_LOG, L_MISC, "Could not convert string '%s' to UTF-8: %s\n", str, strerror(errno));

      return NULL;
    }

  return (char *)ret;
}

char *
trim(char *str)
{
  size_t start; // Position of first non-space char
  size_t term;  // Position of 0-terminator

  if (!str)
    return NULL;

  start = 0;
  term  = strlen(str);

  while ((start < term) && isspace(str[start]))
    start++;
  while ((term > start) && isspace(str[term - 1]))
    term--;

  str[term] = '\0';

  // Shift chars incl. terminator
  if (start)
    memmove(str, str + start, term - start + 1);

  return str;
}

char *
atrim(const char *str)
{
  size_t start; // Position of first non-space char
  size_t term;  // Position of 0-terminator
  size_t size;
  char *result;

  if (!str)
    return NULL;

  start = 0;
  term  = strlen(str);

  while ((start < term) && isspace(str[start]))
    start++;
  while ((term > start) && isspace(str[term - 1]))
    term--;

  size = term - start + 1;

  result = malloc(size);

  memcpy(result, str + start, size);
  result[size - 1] = '\0';

  return result;
}

void
swap_pointers(char **a, char **b)
{
  char *t = *a;
  *a = *b;
  *b = t;
}

uint32_t
djb_hash(const void *data, size_t len)
{
  const unsigned char *bytes = data;
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

  CHECK_NULL(L_MISC, str = malloc(len));

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

static const char b64_encode_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
b64_encode_block(const uint8_t *in, char *out, int len)
{
  out[0] = b64_encode_table[in[0] >> 2];

  out[2] = out[3] = '=';

  if (len == 1)
    out[1] = b64_encode_table[((in[0] & 0x03) << 4)];
  else
    {
      out[1] = b64_encode_table[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];

      if (len == 2)
	out[2] = b64_encode_table[((in[1] & 0x0f) << 2)];
      else
	{
	  out[2] = b64_encode_table[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)];
	  out[3] = b64_encode_table[in[2] & 0x3f];
	}
    }
}

static void
b64_encode_full_block(const uint8_t *in, char *out)
{
  out[0] = b64_encode_table[in[0] >> 2];
  out[1] = b64_encode_table[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
  out[2] = b64_encode_table[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)];
  out[3] = b64_encode_table[in[2] & 0x3f];
}

char *
b64_encode(const uint8_t *in, size_t len)
{
  char *encoded;
  char *out;

  /* 3 in chars -> 4 out chars */
  encoded = (char *)malloc(len + (len / 3) + 4 + 1);
  if (!encoded)
    return NULL;

  out = encoded;

  while (len >= 3)
    {
      b64_encode_full_block(in, out);

      len -= 3;
      in += 3;
      out += 4;
    }

  if (len > 0)
    {
      b64_encode_block(in, out, len);
      out += 4;
    }

  out[0] = '\0';

  return encoded;
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
	h ^= (uint64_t)(data_tail[6]) << 48; /* FALLTHROUGH */
      case 6:
	h ^= (uint64_t)(data_tail[5]) << 40; /* FALLTHROUGH */
      case 5:
	h ^= (uint64_t)(data_tail[4]) << 32; /* FALLTHROUGH */
      case 4:
	h ^= (uint64_t)(data_tail[3]) << 24; /* FALLTHROUGH */
      case 3:
	h ^= (uint64_t)(data_tail[2]) << 16; /* FALLTHROUGH */
      case 2:
	h ^= (uint64_t)(data_tail[1]) << 8; /* FALLTHROUGH */
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


bool
peer_address_is_trusted(const char *addr)
{
  cfg_t *section;
  const char *network;
  int i;
  int n;

  if (!addr)
    return false;

  if (strncmp(addr, "::ffff:", strlen("::ffff:")) == 0)
    addr += strlen("::ffff:");

  section = cfg_getsec(cfg, "general");

  n = cfg_size(section, "trusted_networks");
  for (i = 0; i < n; i++)
    {
      network = cfg_getnstr(section, "trusted_networks", i);

      if (!network || network[0] == '\0')
	return false;

      if (strncmp(network, addr, strlen(network)) == 0)
	return true;

      if ((strcmp(network, "localhost") == 0) && (strcmp(addr, "127.0.0.1") == 0 || strcmp(addr, "::1") == 0))
	return true;

      if (strcmp(network, "any") == 0)
	return true;
    }

  return false;
}


int
clock_gettime_with_res(clockid_t clock_id, struct timespec *tp, struct timespec *res)
{
  int ret;

  if ((!tp) || (!res))
    return -1;

  ret = clock_gettime(clock_id, tp);
  /* this will only work for sub-second resolutions. */
  if (ret == 0 && res->tv_nsec > 1)
    tp->tv_nsec = (tp->tv_nsec/res->tv_nsec)*res->tv_nsec;

  return ret;
}

struct timespec
timespec_add(struct timespec time1, struct timespec time2)
{
  struct timespec result;

  result.tv_sec = time1.tv_sec + time2.tv_sec;
  result.tv_nsec = time1.tv_nsec + time2.tv_nsec;
  if (result.tv_nsec >= 1000000000L)
    {
      result.tv_sec++;
      result.tv_nsec -= 1000000000L;
    }
  return result;
}

int
timespec_cmp(struct timespec time1, struct timespec time2)
{
  /* Less than. */
  if (time1.tv_sec < time2.tv_sec)
    return -1;
  /* Greater than. */
  else if (time1.tv_sec > time2.tv_sec)
    return 1;
  /* Less than. */
  else if (time1.tv_nsec < time2.tv_nsec)
    return -1;
  /* Greater than. */
  else if (time1.tv_nsec > time2.tv_nsec)
    return 1;
  /* Equal. */
  else
    return 0;
}

struct timespec
timespec_reltoabs(struct timespec relative)
{
  struct timespec absolute;

#ifdef CLOCK_REALTIME
  clock_gettime(CLOCK_REALTIME, &absolute);
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  TIMEVAL_TO_TIMESPEC(&tv, &absolute);
#endif
  return timespec_add(absolute, relative);
}

#if defined(HAVE_MACH_CLOCK) || defined(HAVE_MACH_TIMER)

#include <mach/mach_time.h> /* mach_absolute_time */
#include <mach/mach.h>      /* host_get_clock_service */
#include <mach/clock.h>     /* clock_get_time */

/* mach monotonic clock port */
extern mach_port_t clock_port;

#ifndef HAVE_CLOCK_GETTIME

int
clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  static int clock_init = 0;
  static clock_serv_t clock;

  mach_timespec_t mts;
  int ret;

  if (! clock_init) {
    clock_init = 1;
    if (host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &clock))
      abort(); /* unlikely */
  }

  if(! tp)
    return -1;

  switch (clock_id) {

  case CLOCK_REALTIME:

    /* query mach for calendar time */
    ret = clock_get_time(clock, &mts);
    if (! ret) {
      tp->tv_sec = mts.tv_sec;
      tp->tv_nsec = mts.tv_nsec;
    }
    break;

  case CLOCK_MONOTONIC:

    /* query mach for monotinic time */
    ret = clock_get_time(clock_port, &mts);
    if (! ret) {
      tp->tv_sec = mts.tv_sec;
      tp->tv_nsec = mts.tv_nsec;
    }
    break;

  default:
    ret = -1;
    break;
  }

  return ret;
}

int
clock_getres(clockid_t clock_id, struct timespec *res)
{
  if (! res)
    return -1;

  /* hardcode ms resolution */
  res->tv_sec = 0;
  res->tv_nsec = 1000;

  return 0;
}

#endif /* HAVE_CLOCK_GETTIME */

#ifndef HAVE_TIMER_SETTIME

#include <sys/time.h> /* ITIMER_REAL */

int
timer_create(clockid_t clock_id, void *sevp, timer_t *timer_id)
{
  if (clock_id != CLOCK_MONOTONIC)
    return -1;
  if (sevp)
    return -1;

  /* setitimer only supports one timer */
  *timer_id = 0;

  return 0;
}

int
timer_delete(timer_t timer_id)
{
  struct itimerval timerval;

  if (timer_id != 0)
    return -1;

  memset(&timerval, 0, sizeof(struct itimerval));

  return setitimer(ITIMER_REAL, &timerval, NULL);
}

int
timer_settime(timer_t timer_id, int flags, const struct itimerspec *tp, struct itimerspec *old)
{
  struct itimerval tv;

  if (timer_id != 0 || ! tp || old)
    return -1;

  TIMESPEC_TO_TIMEVAL(&(tv.it_value), &(tp->it_value));
  TIMESPEC_TO_TIMEVAL(&(tv.it_interval), &(tp->it_interval));

  return setitimer(ITIMER_REAL, &tv, NULL);
}

int
timer_getoverrun(timer_t timer_id)
{
  /* since we don't know if there have been signals that weren't delivered,
     assume none */
  return 0;
}

#endif /* HAVE_TIMER_SETTIME */

#endif /* HAVE_MACH_CLOCK */

int
mutex_init(pthread_mutex_t *mutex)
{
  pthread_mutexattr_t mattr;
  int err;

  CHECK_ERR(L_MISC, pthread_mutexattr_init(&mattr));
  CHECK_ERR(L_MISC, pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ERRORCHECK));
  err = pthread_mutex_init(mutex, &mattr);
  CHECK_ERR(L_MISC, pthread_mutexattr_destroy(&mattr));

  return err;
}

void
log_fatal_err(int domain, const char *func, int line, int err)
{
  DPRINTF(E_FATAL, domain, "%s failed at line %d, error %d (%s)\n", func, line, err, strerror(err));
  abort();
}

void
log_fatal_errno(int domain, const char *func, int line)
{
  DPRINTF(E_FATAL, domain, "%s failed at line %d, error %d (%s)\n", func, line, errno, strerror(errno));
  abort();
}

void
log_fatal_null(int domain, const char *func, int line)
{
  DPRINTF(E_FATAL, domain, "%s returned NULL at line %d\n", func, line);
  abort();
}


