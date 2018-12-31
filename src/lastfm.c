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
#include <stdbool.h>

#include <gcrypt.h>
#include <mxml.h>
#include <event2/buffer.h>
#include <event2/http.h>

#include "mxml-compat.h"

#include "db.h"
#include "lastfm.h"
#include "listener.h"
#include "logger.h"
#include "misc.h"
#include "http.h"

// LastFM becomes disabled if we get a scrobble, try initialising session,
// but can't (probably no session key in db because user does not use LastFM)
static bool lastfm_disabled = false;

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


/* --------------------------------- MAIN --------------------------------- */

static int
response_process(struct http_client_ctx *ctx, char **errmsg)
{
  mxml_node_t *tree;
  mxml_node_t *s_node;
  mxml_node_t *e_node;
  char *body;
  char *sk;
  int ret;

  // NULL-terminate the buffer
  evbuffer_add(ctx->input_body, "", 1);

  body = (char *)evbuffer_pullup(ctx->input_body, -1);
  if (!body || (strlen(body) == 0))
    {
      DPRINTF(E_LOG, L_LASTFM, "Empty response\n");
      return -1;
    }

  tree = mxmlLoadString(NULL, body, MXML_OPAQUE_CALLBACK);
  if (!tree)
    {
      DPRINTF(E_LOG, L_LASTFM, "Failed to parse LastFM response:\n%s\n", body);
      return -1;
    }

  // Look for errors
  e_node = mxmlFindElement(tree, tree, "error", NULL, NULL, MXML_DESCEND);
  if (e_node)
    {
      DPRINTF(E_LOG, L_LASTFM, "Request to LastFM failed: %s\n", mxmlGetOpaque(e_node));
      DPRINTF(E_DBG, L_LASTFM, "LastFM response:\n%s\n", body);

      if (errmsg)
	*errmsg = atrim(mxmlGetOpaque(e_node));

      mxmlDelete(tree);
      return -1;
    }

  DPRINTF(E_SPAM, L_LASTFM, "LastFM response:\n%s\n", body);

  // Was it a scrobble request? Then do nothing. TODO: Check for error messages
  s_node = mxmlFindElement(tree, tree, "scrobbles", NULL, NULL, MXML_DESCEND);
  if (s_node)
    {
      DPRINTF(E_DBG, L_LASTFM, "Scrobble callback\n");
      mxmlDelete(tree);
      return 0;
    }

  // Otherwise an auth request, so get the session key
  s_node = mxmlFindElement(tree, tree, "key", NULL, NULL, MXML_DESCEND);
  if (!s_node)
    {
      DPRINTF(E_LOG, L_LASTFM, "Session key not found\n");
      mxmlDelete(tree);
      return -1;
    }

  sk = atrim(mxmlGetOpaque(s_node));
  if (sk)
    {
      DPRINTF(E_LOG, L_LASTFM, "Got session key from LastFM: %s\n", sk);
      db_admin_set(DB_ADMIN_LASTFM_SESSION_KEY, sk);

      if (lastfm_session_key)
	free(lastfm_session_key);

      lastfm_session_key = sk;
      lastfm_disabled = false;
      ret = 0;
    }
  else
    {
      ret = -1;
    }

  mxmlDelete(tree);
  return ret;
}

/*
 * Post request against the Last.fm api
 *
 * Important:
 * The Last.fm API requires that we MD5 sign sorted parameters (without "format" parameters),
 * therefor the keyval parameters must be sorted alphabetically by their key.
 *
 * @param url API endpoint url
 * @param kv Alphabetically sorted post parameters
 * @param errmsg (Optional) returns the error message (or NULL) if request failed
 */
static int
request_post(const char *url, struct keyval *kv, char **errmsg)
{
  struct http_client_ctx ctx;
  int ret;

  // API requires that we MD5 sign sorted param (without "format" param)
  ret = param_sign(kv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Aborting request, param_sign failed\n");
      return -1;
    }

  memset(&ctx, 0, sizeof(struct http_client_ctx));

  ctx.output_body = http_form_urlencode(kv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Aborting request, http_form_urlencode failed\n");
      return -1;
    }

  ctx.url = url;
  ctx.input_body = evbuffer_new();

  ret = http_client_request(&ctx);
  if (ret < 0)
    goto out_free_ctx;

  ret = response_process(&ctx, errmsg);

 out_free_ctx:
  free(ctx.output_body);
  evbuffer_free(ctx.input_body);

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

  ret = (
	  (keyval_add(kv, "album", mfi->album) == 0) &&
	  (keyval_add(kv, "albumArtist", mfi->album_artist) == 0) &&
	  (keyval_add(kv, "api_key", lastfm_api_key) == 0) &&
	  (keyval_add(kv, "artist", mfi->artist) == 0) &&
	  (keyval_add(kv, "duration", duration) == 0) &&
	  (keyval_add(kv, "method", "track.scrobble") == 0) &&
	  (keyval_add(kv, "sk", lastfm_session_key) == 0) &&
	  (keyval_add(kv, "timestamp", timestamp) == 0) &&
	  (keyval_add(kv, "track", mfi->title) == 0) &&
	  (keyval_add(kv, "trackNumber", trackNumber) == 0)
      );

  free_mfi(mfi, 0);

  if (!ret)
    {
      keyval_clear(kv);
      free(kv);
      return -1;
    }

  DPRINTF(E_INFO, L_LASTFM, "Scrobbling '%s' by '%s'\n", keyval_get(kv, "track"), keyval_get(kv, "artist"));

  ret = request_post(api_url, kv, NULL);

  keyval_clear(kv);
  free(kv);

  return ret;

 noscrobble:
  free_mfi(mfi, 0);

  return -1;
}



/* ---------------------------- Our lastfm API  --------------------------- */

/* Thread: filescanner, httpd */
static void
stop_scrobbling()
{
  // Delete any existing session key
  free(lastfm_session_key);
  lastfm_session_key = NULL;

  // Disable LastFM, will be enabled after successful login request
  lastfm_disabled = true;

  db_admin_delete(DB_ADMIN_LASTFM_SESSION_KEY);
}

/* Thread: filescanner, httpd */
int
lastfm_login_user(const char *user, const char *password, char **errmsg)
{
  struct keyval *kv;
  int ret;

  DPRINTF(E_LOG, L_LASTFM, "LastFM credentials file OK, logging in with username %s\n", user);

  // Stop active scrobbling session
  stop_scrobbling();

  kv = keyval_alloc();
  if (!kv)
    return -1;

  ret = (
	  (keyval_add(kv, "api_key", lastfm_api_key) == 0) &&
	  (keyval_add(kv, "method", "auth.getMobileSession") == 0) &&
	  (keyval_add(kv, "password", password) == 0) &&
	  (keyval_add(kv, "username", user) == 0)
      );
  if (!ret)
    goto out_free_kv;

  // Send the login request
  ret = request_post(auth_url, kv, errmsg);

 out_free_kv:
  keyval_clear(kv);
  free(kv);

  listener_notify(LISTENER_LASTFM);

  return ret;
}

/* Thread: filescanner */
void
lastfm_login(char **arglist)
{
  if (arglist)
    lastfm_login_user(arglist[0], arglist[1], NULL);
  else
    lastfm_login_user(NULL, NULL, NULL);
}

void
lastfm_logout(void)
{
  stop_scrobbling();
  listener_notify(LISTENER_LASTFM);
}

/* Thread: worker */
int
lastfm_scrobble(int id)
{
  DPRINTF(E_DBG, L_LASTFM, "Got LastFM scrobble request\n");

  // LastFM is disabled because we already tried looking for a session key, but failed
  if (lastfm_disabled)
    return -1;

  return scrobble(id);
}

/* Thread: httpd */
bool
lastfm_is_enabled(void)
{
  return !lastfm_disabled;
}

/* Thread: main */
int
lastfm_init(void)
{
  lastfm_session_key = db_admin_get(DB_ADMIN_LASTFM_SESSION_KEY);
  if (!lastfm_session_key)
    {
      DPRINTF(E_DBG, L_LASTFM, "No valid LastFM session key\n");
      lastfm_disabled = true;
    }

  return 0;
}

