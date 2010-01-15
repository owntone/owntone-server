/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
 *
 * iTunes - Remote pairing hash function published by Michael Paul Bailey
 *   <http://jinxidoru.blogspot.com/2009/06/itunes-remote-pairing-code.html>
 * Derived from Xine-lib source code, src/input/libreal/real.c::hash(), GPLv2+.
 *
 * Pairing process based on the work by
 *  - Michael Croes
 *    <http://blog.mycroes.nl/2008/08/pairing-itunes-remote-app-with-your-own.html>
 *  - Jeffrey Sharkey
 *    <http://dacp.jsharkey.org/>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>

#if defined(__linux__) || defined(__GLIBC__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/endian.h>
#endif

#include <avahi-common/malloc.h>

#include <event.h>
#include "evhttp/evhttp.h"

#include "logger.h"
#include "conffile.h"
#include "mdns_avahi.h"
#include "misc.h"
#include "remote_pairing.h"


struct remote_info {
  char *id;
  char *name;

  char *paircode;
  char *pin;

  int port;
  char *address;

  struct evhttp_connection *evcon;

  struct remote_info *next;
};


static int exit_pipe[2];
static int pairing_pipe[2];
static struct event_base *evbase_pairing;
static struct event exitev;
static struct event pairingev;
static int pairing_exit;
static pthread_t tid_pairing;
static pthread_mutex_t remote_lck = PTHREAD_MUTEX_INITIALIZER;
static struct remote_info *remote_list;
static uint64_t libhash;


/* iTunes - Remote pairing hash */
static char *
itunes_pairing_hash(char *paircode, char *pin)
{
  char param[64];
  char hash[33];
  uint32_t *input;
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;

  if (strlen(paircode) != 16)
    {
      DPRINTF(E_LOG, L_REMOTE, "Paircode length != 16, cannot compute pairing hash\n");
      return NULL;
    }

  if (strlen(pin) != 4)
    {
      DPRINTF(E_LOG, L_REMOTE, "Pin length != 4, cannot compute pairing hash\n");
      return NULL;
    }

  /* Initialization */
  a = 0x67452301;
  b = 0xefcdab89;
  c = 0x98badcfe;
  d = 0x10325476;
  
  memset(param, 0, sizeof(param));
  param[56] = '\xc0';
  param[24] = '\x80';

  strncpy(param, paircode, 16);
  strncpy(param + 16, pin, 4);

  param[22] = param[19];
  param[20] = param[18];
  param[18] = param[17];
  param[19] = param[17] = 0;

  input = (uint32_t *)param;

  a = ((b & c) | (~b & d)) + input[0]  + a - 0x28955B88;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + input[1]  + d - 0x173848AA;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + input[2]  + c + 0x242070DB;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + input[3]  + b - 0x3E423112;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + input[4]  + a - 0x0A83F051;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + input[5]  + d + 0x4787C62A;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + input[6]  + c - 0x57CFB9ED;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + input[7]  + b - 0x02B96AFF;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + input[8]  + a + 0x698098D8;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + input[9]  + d - 0x74BB0851;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + input[10] + c - 0x0000A44F;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + input[11] + b - 0x76A32842;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + input[12] + a + 0x6B901122;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + input[13] + d - 0x02678E6D;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + input[14] + c - 0x5986BC72;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + input[15] + b + 0x49B40821;
  b = ((b << 0x16) | (b >> 0x0a)) + c;

  a = ((b & d) | (~d & c)) + input[1]  + a - 0x09E1DA9E;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + input[6]  + d - 0x3FBF4CC0;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + input[11] + c + 0x265E5A51;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + input[0]  + b - 0x16493856;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + input[5]  + a - 0x29D0EFA3;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + input[10] + d + 0x02441453;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + input[15] + c - 0x275E197F;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + input[4]  + b - 0x182C0438;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + input[9]  + a + 0x21E1CDE6;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + input[14] + d - 0x3CC8F82A;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + input[3]  + c - 0x0B2AF279;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + input[8]  + b + 0x455A14ED;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + input[13] + a - 0x561C16FB;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + input[2]  + d - 0x03105C08;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + input[7]  + c + 0x676F02D9;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + input[12] + b - 0x72D5B376;
  b = ((b << 0x14) | (b >> 0x0c)) + c;

  a = (b ^ c ^ d) + input[5]  + a - 0x0005C6BE;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + input[8]  + d - 0x788E097F;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + input[11] + c + 0x6D9D6122;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + input[14] + b - 0x021AC7F4;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + input[1]  + a - 0x5B4115BC;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + input[4]  + d + 0x4BDECFA9;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + input[7]  + c - 0x0944B4A0;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + input[10] + b - 0x41404390;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + input[13] + a + 0x289B7EC6;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + input[0]  + d - 0x155ED806;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + input[3]  + c - 0x2B10CF7B;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + input[6]  + b + 0x04881D05;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + input[9]  + a - 0x262B2FC7;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + input[12] + d - 0x1924661B;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + input[15] + c + 0x1fa27cf8;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + input[2]  + b - 0x3B53A99B;
  b = ((b << 0x17) | (b >> 0x09)) + c;

  a = ((~d | b) ^ c) + input[0]  + a - 0x0BD6DDBC;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + input[7]  + d + 0x432AFF97;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + input[14] + c - 0x546BDC59;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + input[5]  + b - 0x036C5FC7;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + input[12] + a + 0x655B59C3;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + input[3]  + d - 0x70F3336E;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + input[10] + c - 0x00100B83;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + input[1]  + b - 0x7A7BA22F;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + input[8]  + a + 0x6FA87E4F;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + input[15] + d - 0x01D31920;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + input[6]  + c - 0x5CFEBCEC;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + input[13] + b + 0x4E0811A1;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + input[4]  + a - 0x08AC817E;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + input[11] + d - 0x42C50DCB;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + input[2]  + c + 0x2AD7D2BB;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + input[9]  + b - 0x14792C6F;
  b = ((b << 0x15) | (b >> 0x0b)) + c;

  a += 0x67452301;
  b += 0xefcdab89;
  c += 0x98badcfe;
  d += 0x10325476;

  /* Byteswap to big endian */
  a = htobe32(a);
  b = htobe32(b);
  c = htobe32(c);
  d = htobe32(d);

  /* Write out the pairing hash */
  snprintf(hash, sizeof(hash), "%0X%0X%0X%0X", a, b, c, d);

  return strdup(hash);
}


/* Operations on the remote list must happen
 * with the list lock held by the caller
 */
static struct remote_info *
add_remote(void)
{
  struct remote_info *ri;

  ri = (struct remote_info *)malloc(sizeof(struct remote_info));
  if (!ri)
    {
      DPRINTF(E_WARN, L_REMOTE, "Out of memory for struct remote_info\n");
      return NULL;
    }

  memset(ri, 0, sizeof(struct remote_info));

  ri->next = remote_list;
  remote_list = ri;

  return ri;
}

static void
unlink_remote(struct remote_info *ri)
{
  struct remote_info *p;

  if (ri == remote_list)
    remote_list = ri->next;
  else
    {
      for (p = remote_list; p && (p->next != ri); p = p->next)
	; /* EMPTY */

      if (!p)
	{
	  DPRINTF(E_LOG, L_REMOTE, "WARNING: struct remote_info not found in list; BUG!\n");
	  return;
	}

      p->next = ri->next;
    }
}

static void
free_remote(struct remote_info *ri)
{
  if (ri->id)
    free(ri->id);

  if (ri->name)
    free(ri->name);

  if (ri->paircode)
    free(ri->paircode);

  if (ri->pin)
    free(ri->pin);

  if (ri->address)
    free(ri->address);

  free(ri);
}

static void
remove_remote(struct remote_info *ri)
{
  unlink_remote(ri);

  free_remote(ri);
}

static void
remove_remote_byid(const char *id)
{
  struct remote_info *ri;

  for (ri = remote_list; ri; ri = ri->next)
    {
      if (!ri->id)
	continue;

      if (strcmp(ri->id, id) == 0)
	break;
    }

  if (!ri)
    {
      DPRINTF(E_WARN, L_REMOTE, "Remote %s not found in list\n", id);
      return;
    }

  remove_remote(ri);
}

static int
add_remote_mdns_data(const char *id, const char *address, int port, char *name, char *paircode)
{
  struct remote_info *ri;
  int ret;

  for (ri = remote_list; ri; ri = ri->next)
    {
      if ((ri->id) && (strcmp(ri->id, id) == 0))
	break;
    }

  if (!ri)
    {
      DPRINTF(E_DBG, L_REMOTE, "Remote id %s not known, adding\n", id);

      ri = add_remote();
      if (!ri)
	return -1;

      ret = 0;
    }
  else
    {
      DPRINTF(E_DBG, L_REMOTE, "Remote id %s found\n", id);

      if (ri->id)
	free(ri->id);

      if (ri->address)
	free(ri->address);

      if (ri->name)
	free(ri->name);

      if (ri->paircode)
	free(ri->paircode);

      ret = 1;
    }

  ri->id = strdup(id);
  ri->address = strdup(address);

  if (!ri->id || !ri->address)
    {
      DPRINTF(E_LOG, L_REMOTE, "Out of memory for remote pairing data\n");

      remove_remote(ri);
      return -1;
    }

  ri->port = port;
  ri->name = name;
  ri->paircode = paircode;

  return ret;
}

static int
add_remote_pin_data(char *devname, char *pin)
{
  struct remote_info *ri;

  for (ri = remote_list; ri; ri = ri->next)
    {
      if (strcmp(ri->name, devname) == 0)
	break;
    }

  if (!ri)
    {
      DPRINTF(E_LOG, L_REMOTE, "Remote '%s' not known from mDNS, ignoring\n", devname);

      return -1;
    }

  DPRINTF(E_DBG, L_REMOTE, "Remote '%s' found\n", devname);

  if (ri->name)
    free(ri->name);

  if (ri->pin)
    free(ri->pin);

  ri->name = devname;
  ri->pin = pin;

  return 0;
}

static void
kickoff_pairing(void)
{
  int dummy = 42;
  int ret;

  ret = write(pairing_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_REMOTE, "Could not write to pairing fd: %s\n", strerror(errno));
}


/* Thread: filescanner */
void
remote_pairing_read_pin(char *path)
{
  char buf[256];
  FILE *fp;
  char *devname;
  char *pin;
  int len;
  int ret;

  fp = fopen(path, "rb");
  if (!fp)
    {
      DPRINTF(E_LOG, L_REMOTE, "Could not open Remote pairing file %s: %s\n", path, strerror(errno));
      return;
    }

  devname = fgets(buf, sizeof(buf), fp);
  if (!devname)
    {
      DPRINTF(E_LOG, L_REMOTE, "Invalid Remote pairing file %s\n", path);

      fclose(fp);
      return;
    }

  len = strlen(devname);
  if (buf[len - 1] == '\n')
    buf[len - 1] = '\0';
  else
    {
      DPRINTF(E_LOG, L_REMOTE, "Invalid Remote pairing file %s: device name too long or missing pin\n", path);

      fclose(fp);
      return;
    }

  devname = strdup(buf);
  if (!devname)
    {
      DPRINTF(E_LOG, L_REMOTE, "Out of memory for device name while reading %s\n", path);

      fclose(fp);
      return;
    }

  pin = fgets(buf, sizeof(buf), fp);
  fclose(fp);
  if (!pin)
    {
      DPRINTF(E_LOG, L_REMOTE, "Invalid Remote pairing file %s: no pin\n", path);

      free(devname);
      return;
    }

  len = strlen(pin);
  if (buf[len - 1] == '\n')
    {
      buf[len - 1] = '\0';
      len--;
    }

  if (len != 4)
    {
      DPRINTF(E_LOG, L_REMOTE, "Invalid pin in Remote pairing file %s: pin length should be 4, got %d\n", path, len);

      free(devname);
      return;
    }

  pin = strdup(buf);
  if (!pin)
    {
      DPRINTF(E_LOG, L_REMOTE, "Out of memory for device pin while reading %s\n", path);

      free(devname);
      return;
    }

  DPRINTF(E_DBG, L_REMOTE, "Adding Remote pin data: name '%s', pin '%s'\n", devname, pin);

  pthread_mutex_lock(&remote_lck);

  ret = add_remote_pin_data(devname, pin);

  if (ret < 0)
    {
      free(devname);
      free(pin);
    }
  else
    kickoff_pairing();

  pthread_mutex_unlock(&remote_lck);
}


/* Thread: main (mdns) */
static void
touch_remote_cb(const char *name, const char *type, const char *domain, const char *hostname, const char *address, int port, AvahiStringList *txt)
{
  AvahiStringList *p;
  char *devname;
  char *paircode;
  char *key;
  char *val;
  size_t valsz;
  int ret;

  if (port < 0)
    {
      /* If Remote stops advertising itself, the pairing either succeeded or
       * failed; any subsequent attempt will need a new pairing pin, so
       * we can just forget everything we know about the remote.
       */
      pthread_mutex_lock(&remote_lck);

      remove_remote_byid(name);

      pthread_mutex_unlock(&remote_lck);
    }
  else
    {
      /* Get device name (DvNm field in TXT record) */
      p = avahi_string_list_find(txt, "DvNm");
      if (!p)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Remote %s: no DvNm in TXT record!\n", name);

	  return;
	}

      avahi_string_list_get_pair(p, &key, &val, &valsz);
      avahi_free(key);
      if (!val)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Remote %s: DvNm has no value\n", name);

	  return;
	}

      devname = strndup(val, valsz);
      avahi_free(val);
      if (!devname)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Out of memory for device name\n");

	  return;
	}

      /* Get pairing code (Pair field in TXT record) */
      p = avahi_string_list_find(txt, "Pair");
      if (!p)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Remote %s: no Pair in TXT record!\n", name);

	  free(devname);
	  return;
	}

      avahi_string_list_get_pair(p, &key, &val, &valsz);
      avahi_free(key);
      if (!val)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Remote %s: Pair has no value\n", name);

	  free(devname);
	  return;
	}

      paircode = strndup(val, valsz);
      avahi_free(val);
      if (!paircode)
	{
	  DPRINTF(E_LOG, L_REMOTE, "Out of memory for paircode\n");

	  free(devname);
	  return;
	}

      DPRINTF(E_DBG, L_REMOTE, "Discovered remote %s (id %s) at %s:%d, paircode %s\n", devname, name, address, port, paircode);

      /* Add the data to the list, adding the remote to the list if needed */
      pthread_mutex_lock(&remote_lck);

      ret = add_remote_mdns_data(name, address, port, devname, paircode);

      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_REMOTE, "Could not add Remote mDNS data, id %s\n", name);

	  free(devname);
	  free(paircode);
	}
      else if (ret == 1)
	kickoff_pairing();

      pthread_mutex_unlock(&remote_lck);
    }
}


/* Thread: pairing */
static void
pairing_request_cb(struct evhttp_request *req, void *arg)
{
  struct remote_info *ri;

  ri = (struct remote_info *)arg;

  if (!req)
    goto cleanup;

  if (req->response_code != HTTP_OK)
    {
      DPRINTF(E_LOG, L_REMOTE, "Pairing failed with Remote %s/%s, HTTP response code %d\n", ri->id, ri->name, req->response_code);

      goto cleanup;
    }

  DPRINTF(E_INFO, L_REMOTE, "Pairing succeeded with Remote '%s' (id %s)\n", ri->name, ri->id);

 cleanup:
  evhttp_connection_free(ri->evcon);
  free_remote(ri);
}


/* Thread: pairing */
static void
do_pairing(struct remote_info *ri)
{
  char req_uri[128];
  struct evhttp_connection *evcon;
  struct evhttp_request *req;
  char *pairing_hash;
  int ret;

  pairing_hash = itunes_pairing_hash(ri->paircode, ri->pin);
  if (!pairing_hash)
    {
      DPRINTF(E_LOG, L_REMOTE, "Could not compute pairing hash!\n");

      goto hash_fail;
    }

  DPRINTF(E_DBG, L_REMOTE, "Pairing hash for %s/%s: %s\n", ri->id, ri->name, pairing_hash);

  /* Prepare request URI */
  /* The servicename variable is the mDNS service group name; currently it's
   * a hash of the library name, but in iTunes the service name and the library
   * ID (DbId) are different (see comment in main.c).
   * Remote uses the service name to perform mDNS lookups.
   */
  ret = snprintf(req_uri, sizeof(req_uri), "/pair?pairingcode=%s&servicename=%08" PRIX64, pairing_hash, libhash);
  free(pairing_hash);
  if ((ret < 0) || (ret >= sizeof(req_uri)))
    {
      DPRINTF(E_WARN, L_REMOTE, "Request URI for pairing exceeds buffer size\n");

      goto req_uri_fail;
    }

  /* Fire up the request */
  evcon = evhttp_connection_new(ri->address, ri->port);
  if (!evcon)
    {
      DPRINTF(E_WARN, L_REMOTE, "Could not create connection for pairing\n");

      goto evcon_fail;
    }

  evhttp_connection_set_base(evcon, evbase_pairing);

  req = evhttp_request_new(pairing_request_cb, ri);
  if (!req)
    {
      DPRINTF(E_WARN, L_REMOTE, "Could not create HTTP request for pairing\n");

      goto request_fail;
    }

  ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, req_uri);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_REMOTE, "Could not make pairing request\n");

      goto make_request_fail;
    }

  ri->evcon = evcon;

  return;

 make_request_fail:
  evhttp_request_free(req);

 request_fail:
  evhttp_connection_free(evcon);

 evcon_fail:
 req_uri_fail:
 hash_fail:
  free_remote(ri);
}


/* Thread: pairing */
static void
pairing_cb(int fd, short event, void *arg)
{
  struct remote_info *ri;
  int dummy;

  /* Drain the pipe */
  while (read(pairing_pipe[0], &dummy, sizeof(dummy)) >= 0)
    ; /* EMPTY */

  for (;;)
    {
      pthread_mutex_lock(&remote_lck);

      for (ri = remote_list; ri; ri = ri->next)
	{
	  /* We've got both the mDNS data and the pin */
	  if (ri->paircode && ri->pin)
	    {
	      unlink_remote(ri);
	      break;
	    }
	}

      pthread_mutex_unlock(&remote_lck);

      if (!ri)
	break;

      do_pairing(ri);
    }
}


/* Thread: pairing */
static void
exit_cb(int fd, short event, void *arg)
{
  event_base_loopbreak(evbase_pairing);

  pairing_exit = 1;
}

/* Thread: pairing */
static void *
pairing_agent(void *arg)
{
  event_base_dispatch(evbase_pairing);

  if (!pairing_exit)
    DPRINTF(E_FATAL, L_REMOTE, "Pairing agent event loop terminated ahead of time!\n");

  pthread_exit(NULL);
}


/* Thread: main */
int
remote_pairing_init(void)
{
  char *libname;
  int ret;

  remote_list = NULL;
  pairing_exit = 0;

#if defined(__linux__)
  ret = pipe2(exit_pipe, O_CLOEXEC);
#else
  ret = pipe(exit_pipe);
#endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not create exit pipe: %s\n", strerror(errno));

      return -1;
    }

#if defined(__linux__)
  ret = pipe2(pairing_pipe, O_CLOEXEC | O_NONBLOCK);
#else
  ret = pipe(pairing_pipe);
#endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not create pairing pipe: %s\n", strerror(errno));

      goto pairing_pipe_fail;
    }

#ifndef __linux__
  ret = fcntl(pairing_pipe[0], F_SETFL, O_NONBLOCK);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not set O_NONBLOCK: %s\n", strerror(errno));

      goto pairing_pipe_fail;
    }
#endif

  evbase_pairing = event_base_new();
  if (!evbase_pairing)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not create an event base\n");

      goto evbase_fail;
    }

  event_set(&exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_pairing, &exitev);
  event_add(&exitev, NULL);

  event_set(&pairingev, pairing_pipe[0], EV_READ, pairing_cb, NULL);
  event_base_set(evbase_pairing, &pairingev);
  event_add(&pairingev, NULL);

  ret = mdns_browse("_touch-remote._tcp", touch_remote_cb);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not browse for Remote services\n");

      goto mdns_browse_fail;
    }

  libname = cfg_getstr(cfg_getnsec(cfg, "library", 0), "name");
  libhash = murmur_hash64(libname, strlen(libname), 0);

  ret = pthread_create(&tid_pairing, NULL, pairing_agent, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not spawn pairing agent thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;

 thread_fail:
 mdns_browse_fail:
  event_base_free(evbase_pairing);

 evbase_fail:
  close(pairing_pipe[0]);
  close(pairing_pipe[1]);

 pairing_pipe_fail:
  close(exit_pipe[0]);
  close(exit_pipe[1]);

  return -1;
}

/* Thread: main */
void
remote_pairing_deinit(void)
{
  int dummy = 42;
  int ret;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }

  ret = pthread_join(tid_pairing, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_REMOTE, "Could not join pairing agent thread: %s\n", strerror(errno));

      return;
    }

  close(pairing_pipe[0]);
  close(pairing_pipe[1]);
  close(exit_pipe[0]);
  close(exit_pipe[1]);
  event_base_free(evbase_pairing);
}
