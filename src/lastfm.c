/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#include <gcrypt.h>
#include <mxml.h>
#include <event2/buffer.h>
#include <event2/http.h>
#include <curl/curl.h>

#include "db.h"
#include "lastfm.h"
#include "logger.h"
#include "misc.h"


struct https_client_ctx
{
  const char *url;
  const char *body;
  struct evbuffer *data;
};

// LastFM becomes disabled if we get a scrobble, try initialising session,
// but can't (probably no session key in db because user does not use LastFM)
static int lastfm_disabled = 0;

/**
 * The API key and secret (not so secret being open source) is specific to 
 * forked-daapd, and is used to identify forked-daapd and to sign requests
 */
static const char *lastfm_api_key = "579593f2ed3f49673c7364fd1c9c829b";
static const char *lastfm_secret = "ce45a1d275c10b3edf0ecfa27791cb2b";

static const char *api_url = "http://ws.audioscrobbler.com/2.0/";
static const char *auth_url = "https://ws.audioscrobbler.com/2.0/";

// Session key
static char *lastfm_session_key = NULL;



/* --------------------------------- HELPERS ------------------------------- */

/* Reads a LastFM credentials file (1st line username, 2nd line password) */
static int
credentials_read(char *path, char **username, char **password)
{
  FILE *fp;
  char *u;
  char *p;
  char buf[256];
  int len;

  fp = fopen(path, "rb");
  if (!fp)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not open lastfm credentials file %s: %s\n", path, strerror(errno));
      return -1;
    }

  u = fgets(buf, sizeof(buf), fp);
  if (!u)
    {
      DPRINTF(E_LOG, L_LASTFM, "Empty lastfm credentials file %s\n", path);

      fclose(fp);
      return -1;
    }

  len = strlen(u);
  if (buf[len - 1] != '\n')
    {
      DPRINTF(E_LOG, L_LASTFM, "Invalid lastfm credentials file %s: username name too long or missing password\n", path);

      fclose(fp);
      return -1;
    }

  while (len)
    {
      if ((buf[len - 1] == '\r') || (buf[len - 1] == '\n'))
	{
	  buf[len - 1] = '\0';
	  len--;
	}
      else
	break;
    }

  if (!len)
    {
      DPRINTF(E_LOG, L_LASTFM, "Invalid lastfm credentials file %s: empty line where username expected\n", path);

      fclose(fp);
      return -1;
    }

  u = strdup(buf);
  if (!u)
    {
      DPRINTF(E_LOG, L_LASTFM, "Out of memory for username while reading %s\n", path);

      fclose(fp);
      return -1;
    }

  p = fgets(buf, sizeof(buf), fp);
  fclose(fp);
  if (!p)
    {
      DPRINTF(E_LOG, L_LASTFM, "Invalid lastfm credentials file %s: no password\n", path);

      free(u);
      return -1;
    }

  len = strlen(p);

  while (len)
    {
      if ((buf[len - 1] == '\r') || (buf[len - 1] == '\n'))
	{
	  buf[len - 1] = '\0';
	  len--;
	}
      else
	break;
    }

  p = strdup(buf);
  if (!p)
    {
      DPRINTF(E_LOG, L_LASTFM, "Out of memory for password while reading %s\n", path);

      free(u);
      return -1;
    }

  DPRINTF(E_LOG, L_LASTFM, "lastfm credentials file OK, logging in with username %s\n", u);

  *username = u;
  *password = p;

  return 0;
}

/* Converts parameters to a string in application/x-www-form-urlencoded format */
static int
body_print(char **body, struct keyval *kv)
{
  struct evbuffer *evbuf;
  struct onekeyval *okv;
  char *k;
  char *v;

  evbuf = evbuffer_new();

  for (okv = kv->head; okv; okv = okv->next)
    {
      k = evhttp_encode_uri(okv->name);
      if (!k)
        continue;

      v = evhttp_encode_uri(okv->value);
      if (!v)
	{
	  free(k);
	  continue;
	}

      evbuffer_add(evbuf, k, strlen(k));
      evbuffer_add(evbuf, "=", 1);
      evbuffer_add(evbuf, v, strlen(v));
      if (okv->next)
	evbuffer_add(evbuf, "&", 1);

      free(k);
      free(v);
    }

  evbuffer_add(evbuf, "\n", 1);

  *body = evbuffer_readln(evbuf, NULL, EVBUFFER_EOL_ANY);

  evbuffer_free(evbuf);

  DPRINTF(E_DBG, L_LASTFM, "Parameters in request are: %s\n", *body);

  return 0;
}

/* Creates an md5 signature of the concatenated parameters and adds it to keyval */
static int
param_sign(struct keyval *kv)
{
  struct onekeyval *okv;

  char hash[33];
  char ebuf[64];
  uint8_t *hash_bytes;
  size_t hash_len;
  gcry_md_hd_t md_hdl;
  gpg_error_t gc_err;
  int ret;
  int i;

  gc_err = gcry_md_open(&md_hdl, GCRY_MD_MD5, 0);
  if (gc_err != GPG_ERR_NO_ERROR)
    {
      gpg_strerror_r(gc_err, ebuf, sizeof(ebuf));
      DPRINTF(E_LOG, L_LASTFM, "Could not open MD5: %s\n", ebuf);
      return -1;
    }

  for (okv = kv->head; okv; okv = okv->next)
    {
      gcry_md_write(md_hdl, okv->name, strlen(okv->name));
      gcry_md_write(md_hdl, okv->value, strlen(okv->value));
    }  

  gcry_md_write(md_hdl, lastfm_secret, strlen(lastfm_secret));

  hash_bytes = gcry_md_read(md_hdl, GCRY_MD_MD5);
  if (!hash_bytes)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not read MD5 hash\n");
      return -1;
    }

  hash_len = gcry_md_get_algo_dlen(GCRY_MD_MD5);

  for (i = 0; i < hash_len; i++)
    sprintf(hash + (2 * i), "%02x", hash_bytes[i]);

  ret = keyval_add(kv, "api_sig", hash);

  gcry_md_close(md_hdl);

  return ret;
}

/* For compability with mxml 2.6 */
#ifndef HAVE_MXML_GETOPAQUE
const char *				/* O - Opaque string or NULL */
mxmlGetOpaque(mxml_node_t *node)	/* I - Node to get */
{
  if (!node)
    return (NULL);

  if (node->type == MXML_OPAQUE)
    return (node->value.opaque);
  else if (node->type == MXML_ELEMENT &&
           node->child &&
	   node->child->type == MXML_OPAQUE)
    return (node->child->value.opaque);
  else
    return (NULL);
}
#endif

/* --------------------------------- MAIN --------------------------------- */

static size_t
request_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  size_t realsize;
  struct https_client_ctx *ctx;
  int ret;

  realsize = size * nmemb;
  ctx = (struct https_client_ctx *)userdata;

  ret = evbuffer_add(ctx->data, ptr, realsize);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Error adding reply from LastFM to data buffer\n");
      return 0;
    }

  return realsize;
}

static void
response_proces(struct https_client_ctx *ctx)
{
  mxml_node_t *tree;
  mxml_node_t *s_node;
  mxml_node_t *e_node;
  char *body;
  char *errmsg;
  char *sk;

  // NULL-terminate the buffer
  evbuffer_add(ctx->data, "", 1);

  body = (char *)evbuffer_pullup(ctx->data, -1);
  if (!body || (strlen(body) == 0))
    {
      DPRINTF(E_LOG, L_LASTFM, "Empty response\n");
      return;
    }

  DPRINTF(E_SPAM, L_LASTFM, "LastFM response:\n%s\n", body);

  tree = mxmlLoadString(NULL, body, MXML_OPAQUE_CALLBACK);
  if (!tree)
    return;

  // Look for errors
  e_node = mxmlFindElement(tree, tree, "error", NULL, NULL, MXML_DESCEND);
  if (e_node)
    {
      errmsg = trimwhitespace(mxmlGetOpaque(e_node));
      DPRINTF(E_LOG, L_LASTFM, "Request to LastFM failed: %s\n", errmsg);

      if (errmsg)
	free(errmsg);
      mxmlDelete(tree);
      return;
    }

  // Was it a scrobble request? Then do nothing. TODO: Check for error messages
  s_node = mxmlFindElement(tree, tree, "scrobbles", NULL, NULL, MXML_DESCEND);
  if (s_node)
    {
      DPRINTF(E_DBG, L_LASTFM, "Scrobble callback\n");
      mxmlDelete(tree);
      return;
    }

  // Otherwise an auth request, so get the session key
  s_node = mxmlFindElement(tree, tree, "key", NULL, NULL, MXML_DESCEND);
  if (!s_node)
    {
      DPRINTF(E_LOG, L_LASTFM, "Session key not found\n");
      mxmlDelete(tree);
      return;
    }

  sk = trimwhitespace(mxmlGetOpaque(s_node));
  if (sk)
    {
      DPRINTF(E_LOG, L_LASTFM, "Got session key from LastFM: %s\n", sk);
      db_admin_add("lastfm_sk", sk);

      if (lastfm_session_key)
	free(lastfm_session_key);

      lastfm_session_key = sk;
    }

  mxmlDelete(tree);
}

// We use libcurl to make the request. We could use libevent and avoid the
// dependency, but for SSL, libevent needs to be v2.1 or better, which is still
// a bit too new to be in the major distros
static int
https_client_request(struct https_client_ctx *ctx)
{
  CURL *curl;
  CURLcode res;
 
  curl = curl_easy_init();
  if (!curl)
    {
      DPRINTF(E_LOG, L_LASTFM, "Error: Could not get curl handle\n");
      return -1;
    }

  curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ctx->body);
  curl_easy_setopt(curl, CURLOPT_URL, ctx->url);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, request_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);

  ctx->data = evbuffer_new();
  if (!ctx->data)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create evbuffer for LastFM response\n");
      curl_easy_cleanup(curl);
      return -1;
    }

  res = curl_easy_perform(curl);
  if (res != CURLE_OK)
    {
      DPRINTF(E_LOG, L_LASTFM, "Request to %s failed: %s\n", ctx->url, curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return -1;
    }

  response_proces(ctx);

  evbuffer_free(ctx->data);
 
  curl_easy_cleanup(curl);

  return 0;
}

static int
request_post(char *method, struct keyval *kv, int auth)
{
  struct https_client_ctx ctx;
  char *body;
  int ret;

  ret = keyval_add(kv, "method", method);
  if (ret < 0)
    return -1;

  if (!auth)
    ret = keyval_add(kv, "sk", lastfm_session_key);
  if (ret < 0)
    return -1;

  // API requires that we MD5 sign sorted param (without "format" param)
  keyval_sort(kv);
  ret = param_sign(kv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Aborting request, param_sign failed\n");
      return -1;
    }

  ret = body_print(&body, kv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Aborting request, body_print failed\n");
      return -1;
    }

  memset(&ctx, 0, sizeof(struct https_client_ctx));
  ctx.url = auth ? auth_url : api_url;
  ctx.body = body;

  ret = https_client_request(&ctx);

  return ret;
}

static int
scrobble(int id)
{
  struct media_file_info *mfi;
  struct keyval *kv;
  char duration[4];
  char trackNumber[4];
  char timestamp[16];
  int ret;

  mfi = db_file_fetch_byid(id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_LASTFM, "Scrobble failed, track id %d is unknown\n", id);
      return -1;
    }

  // Don't scrobble songs which are shorter than 30 sec
  if (mfi->song_length < 30000)
    goto noscrobble;

  // Don't scrobble non-music and radio stations
  if ((mfi->media_kind != MEDIA_KIND_MUSIC) || (mfi->data_kind == DATA_KIND_HTTP))
    goto noscrobble;

  // Don't scrobble songs with unknown artist
  if (strcmp(mfi->artist, "Unknown artist") == 0)
    goto noscrobble;

  kv = keyval_alloc();
  if (!kv)
    goto noscrobble;

  snprintf(duration, sizeof(duration), "%" PRIu32, mfi->song_length);
  snprintf(trackNumber, sizeof(trackNumber), "%" PRIu32, mfi->track);
  snprintf(timestamp, sizeof(timestamp), "%" PRIi64, (int64_t)time(NULL));

  ret = ( (keyval_add(kv, "api_key", lastfm_api_key) == 0) &&
          (keyval_add(kv, "sk", lastfm_session_key) == 0) &&
          (keyval_add(kv, "artist", mfi->artist) == 0) &&
          (keyval_add(kv, "track", mfi->title) == 0) &&
          (keyval_add(kv, "album", mfi->album) == 0) &&
          (keyval_add(kv, "albumArtist", mfi->album_artist) == 0) &&
          (keyval_add(kv, "duration", duration) == 0) &&
          (keyval_add(kv, "trackNumber", trackNumber) == 0) &&
          (keyval_add(kv, "timestamp", timestamp) == 0)
        );

  free_mfi(mfi, 0);

  if (!ret)
    {
      keyval_clear(kv);
      free(kv);
      return -1;
    }

  DPRINTF(E_INFO, L_LASTFM, "Scrobbling '%s' by '%s'\n", keyval_get(kv, "track"), keyval_get(kv, "artist"));

  ret = request_post("track.scrobble", kv, 0);

  keyval_clear(kv);
  free(kv);

  return ret;

 noscrobble:
  free_mfi(mfi, 0);

  return -1;
}



/* ---------------------------- Our lastfm API  --------------------------- */

/* Thread: filescanner */
int
lastfm_login(char *path)
{
  struct keyval *kv;
  char *username;
  char *password;
  int ret;

  DPRINTF(E_DBG, L_LASTFM, "Got LastFM login request\n");

  // Delete any existing session key
  if (lastfm_session_key)
    free(lastfm_session_key);

  lastfm_session_key = NULL;

  db_admin_delete("lastfm_sk");

  // Read the credentials file
  ret = credentials_read(path, &username, &password);
  if (ret < 0)
    return -1;

  // Enable LastFM now that we got a login attempt
  lastfm_disabled = 0;

  kv = keyval_alloc();
  if (!kv)
    {
      free(username);
      free(password);
      return -1;
    }

  ret = ( (keyval_add(kv, "api_key", lastfm_api_key) == 0) &&
          (keyval_add(kv, "username", username) == 0) &&
          (keyval_add(kv, "password", password) == 0) );

  free(username);
  free(password);

  // Send the login request
  ret = request_post("auth.getMobileSession", kv, 1);

  keyval_clear(kv);
  free(kv);

  return ret;
}

/* Thread: worker */
int
lastfm_scrobble(int id)
{
  DPRINTF(E_DBG, L_LASTFM, "Got LastFM scrobble request\n");

  // LastFM is disabled because we already tried looking for a session key, but failed
  if (lastfm_disabled)
    return -1;

  // No session key in mem or in db
  if (!lastfm_session_key)
    lastfm_session_key = db_admin_get("lastfm_sk");

  if (!lastfm_session_key)
    {
      DPRINTF(E_INFO, L_LASTFM, "No valid LastFM session key\n");
      lastfm_disabled = 1;
      return -1;
    }

  return scrobble(id);
}

