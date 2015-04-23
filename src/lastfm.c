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
#include <pthread.h>

#include <gcrypt.h>
#include <mxml.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http.h>
#include <curl/curl.h>

#include "db.h"
#include "lastfm.h"
#include "logger.h"
#include "misc.h"


struct lastfm_command;

typedef int (*cmd_func)(struct lastfm_command *cmd);

struct lastfm_command
{
  pthread_mutex_t lck;
  pthread_cond_t cond;

  cmd_func func;

  int nonblock;

  union {
    void *noarg;
    int id;
    struct keyval *kv;
  } arg;

  int ret;
};

struct https_client_ctx
{
  const char *url;
  const char *body;
  struct evbuffer *data;
};


/* --- Globals --- */
// lastfm thread
static pthread_t tid_lastfm;

// Event base, pipes and events
struct event_base *evbase_lastfm;
static int g_exit_pipe[2];
static int g_cmd_pipe[2];
static struct event *g_exitev;
static struct event *g_cmdev;

// Tells us if the LastFM thread has been set up
static int g_initialized = 0;

// LastFM becomes disabled if we get a scrobble, try initialising session,
// but can't (probably no session key in db because user does not use LastFM)
static int g_disabled = 0;

/**
 * The API key and secret (not so secret being open source) is specific to 
 * forked-daapd, and is used to identify forked-daapd and to sign requests
 */
const char *g_api_key = "579593f2ed3f49673c7364fd1c9c829b";
const char *g_secret = "ce45a1d275c10b3edf0ecfa27791cb2b";

const char *api_url = "http://ws.audioscrobbler.com/2.0/";
const char *auth_url = "https://ws.audioscrobbler.com/2.0/";

// Session key
char *g_session_key = NULL;



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

  gcry_md_write(md_hdl, g_secret, strlen(g_secret));

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

/* ---------------------------- COMMAND EXECUTION -------------------------- */

static int
send_command(struct lastfm_command *cmd)
{
  int ret;

  if (!cmd->func)
    {
      DPRINTF(E_LOG, L_LASTFM, "BUG: cmd->func is NULL!\n");
      return -1;
    }

  ret = write(g_cmd_pipe[1], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not send command: %s\n", strerror(errno));
      return -1;
    }

  return 0;
}

static int
nonblock_command(struct lastfm_command *cmd)
{
  int ret;

  ret = send_command(cmd);
  if (ret < 0)
    return -1;

  return 0;
}

/* Thread: main */
static void
thread_exit(void)
{
  int dummy = 42;

  DPRINTF(E_DBG, L_LASTFM, "Killing lastfm thread\n");

  if (write(g_exit_pipe[1], &dummy, sizeof(dummy)) != sizeof(dummy))
    DPRINTF(E_LOG, L_LASTFM, "Could not write to exit fd: %s\n", strerror(errno));
}



/* --------------------------------- MAIN --------------------------------- */
/*                              Thread: lastfm                              */

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

      if (g_session_key)
	free(g_session_key);

      g_session_key = sk;
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
    ret = keyval_add(kv, "sk", g_session_key);
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
login(struct lastfm_command *cmd)
{
  request_post("auth.getMobileSession", cmd->arg.kv, 1);

  keyval_clear(cmd->arg.kv);

  return 0;
}

static int
scrobble(struct lastfm_command *cmd)
{
  struct media_file_info *mfi;
  struct keyval *kv;
  char duration[4];
  char trackNumber[4];
  char timestamp[16];
  int ret;

  mfi = db_file_fetch_byid(cmd->arg.id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_LASTFM, "Scrobble failed, track id %d is unknown\n", cmd->arg.id);
      return -1;
    }

  // Don't scrobble songs which are shorter than 30 sec
  if (mfi->song_length < 30000)
    goto noscrobble;

  // Don't scrobble non-music and radio stations
  if ((mfi->media_kind != MEDIA_KIND_MUSIC) || (mfi->data_kind == DATA_KIND_URL))
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

  ret = ( (keyval_add(kv, "api_key", g_api_key) == 0) &&
          (keyval_add(kv, "sk", g_session_key) == 0) &&
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
      return -1;
    }

  DPRINTF(E_INFO, L_LASTFM, "Scrobbling '%s' by '%s'\n", keyval_get(kv, "track"), keyval_get(kv, "artist"));

  request_post("track.scrobble", kv, 0);

  keyval_clear(kv);

  return 0;

 noscrobble:
  free_mfi(mfi, 0);

  return -1;
}



static void *
lastfm(void *arg)
{
  int ret;

  DPRINTF(E_DBG, L_LASTFM, "Main loop initiating\n");

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Error: DB init failed\n");
      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_lastfm);

  if (g_initialized)
    {
      DPRINTF(E_LOG, L_LASTFM, "LastFM event loop terminated ahead of time!\n");
      g_initialized = 0;
    }

  db_perthread_deinit();

  DPRINTF(E_DBG, L_LASTFM, "Main loop terminating\n");

  pthread_exit(NULL);
}

static void
exit_cb(int fd, short what, void *arg)
{
  int dummy;
  int ret;

  ret = read(g_exit_pipe[0], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    DPRINTF(E_LOG, L_LASTFM, "Error reading from exit pipe\n");

  event_base_loopbreak(evbase_lastfm);

  g_initialized = 0;

  event_add(g_exitev, NULL);
}

static void
command_cb(int fd, short what, void *arg)
{
  struct lastfm_command *cmd;
  int ret;

  ret = read(g_cmd_pipe[0], &cmd, sizeof(cmd));
  if (ret != sizeof(cmd))
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not read command! (read %d): %s\n", ret, (ret < 0) ? strerror(errno) : "-no error-");
      goto readd;
    }

  if (cmd->nonblock)
    {
      cmd->func(cmd);

      free(cmd);
      goto readd;
    }

  pthread_mutex_lock(&cmd->lck);

  ret = cmd->func(cmd);
  cmd->ret = ret;

  pthread_cond_signal(&cmd->cond);
  pthread_mutex_unlock(&cmd->lck);

 readd:
  event_add(g_cmdev, NULL);
}


/* ---------------------------- Our lastfm API  --------------------------- */

static int
lastfm_init(void);

/* Thread: filescanner */
void
lastfm_login(char *path)
{
  struct lastfm_command *cmd;
  struct keyval *kv;
  char *username;
  char *password;
  int ret;

  DPRINTF(E_DBG, L_LASTFM, "Got LastFM login request\n");

  // Delete any existing session key
  if (g_session_key)
    free(g_session_key);

  g_session_key = NULL;

  db_admin_delete("lastfm_sk");

  // Read the credentials file
  ret = credentials_read(path, &username, &password);
  if (ret < 0)
    return;

  // Enable LastFM now that we got a login attempt
  g_disabled = 0;

  kv = keyval_alloc();
  if (!kv)
    {
      free(username);
      free(password);
      return;
    }

  ret = ( (keyval_add(kv, "api_key", g_api_key) == 0) &&
          (keyval_add(kv, "username", username) == 0) &&
          (keyval_add(kv, "password", password) == 0) );

  free(username);
  free(password);

  if (!ret)
    {
      keyval_clear(kv);
      return;
    }

  // Spawn thread
  ret = lastfm_init();
  if (ret < 0)
    {
      g_disabled = 1;
      return;
    }
  g_initialized = 1;

  // Send login command to the thread
  cmd = (struct lastfm_command *)malloc(sizeof(struct lastfm_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not allocate lastfm_command\n");
      return;
    }

  memset(cmd, 0, sizeof(struct lastfm_command));

  cmd->nonblock = 1;
  cmd->func = login;
  cmd->arg.kv = kv;

  nonblock_command(cmd);

  return;
}

/* Thread: http and player */
int
lastfm_scrobble(int id)
{
  struct lastfm_command *cmd;
  int ret;

  DPRINTF(E_DBG, L_LASTFM, "Got LastFM scrobble request\n");

  // LastFM is disabled because we already tried looking for a session key, but failed
  if (g_disabled)
    return -1;

  // No session key in mem or in db
  if (!g_session_key)
    g_session_key = db_admin_get("lastfm_sk");

  if (!g_session_key)
    {
      DPRINTF(E_INFO, L_LASTFM, "No valid LastFM session key\n");
      g_disabled = 1;
      return -1;
    }

  // Spawn LastFM thread
  ret = lastfm_init();
  if (ret < 0)
    {
      g_disabled = 1;
      return -1;
    }
  g_initialized = 1;

  // Send scrobble command to the thread
  cmd = (struct lastfm_command *)malloc(sizeof(struct lastfm_command));
  if (!cmd)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not allocate lastfm_command\n");
      return -1;
    }

  memset(cmd, 0, sizeof(struct lastfm_command));

  cmd->nonblock = 1;
  cmd->func = scrobble;
  cmd->arg.id = id;

  nonblock_command(cmd);

  return 0;
}

static int
lastfm_init(void)
{
  int ret;

  if (g_initialized)
    return 0;

  curl_global_init(CURL_GLOBAL_DEFAULT);

# if defined(__linux__)
  ret = pipe2(g_exit_pipe, O_CLOEXEC);
# else
  ret = pipe(g_exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create pipe: %s\n", strerror(errno));
      goto exit_fail;
    }

# if defined(__linux__)
  ret = pipe2(g_cmd_pipe, O_CLOEXEC);
# else
  ret = pipe(g_cmd_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create command pipe: %s\n", strerror(errno));
      goto cmd_fail;
    }

  evbase_lastfm = event_base_new();
  if (!evbase_lastfm)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create an event base\n");
      goto evbase_fail;
    }

#ifdef HAVE_LIBEVENT2
  g_exitev = event_new(evbase_lastfm, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create exit event\n");
      goto evnew_fail;
    }

  g_cmdev = event_new(evbase_lastfm, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create cmd event\n");
      goto evnew_fail;
    }
#else
  g_exitev = (struct event *)malloc(sizeof(struct event));
  if (!g_exitev)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create exit event\n");
      goto evnew_fail;
    }
  event_set(g_exitev, g_exit_pipe[0], EV_READ, exit_cb, NULL);
  event_base_set(evbase_lastfm, g_exitev);

  g_cmdev = (struct event *)malloc(sizeof(struct event));
  if (!g_cmdev)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not create cmd event\n");
      goto evnew_fail;
    }
  event_set(g_cmdev, g_cmd_pipe[0], EV_READ, command_cb, NULL);
  event_base_set(evbase_lastfm, g_cmdev);
#endif

  event_add(g_exitev, NULL);
  event_add(g_cmdev, NULL);

  DPRINTF(E_INFO, L_LASTFM, "LastFM thread init\n");

  ret = pthread_create(&tid_lastfm, NULL, lastfm, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_LASTFM, "Could not spawn LastFM thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;
  
 thread_fail:
 evnew_fail:
  event_base_free(evbase_lastfm);
  evbase_lastfm = NULL;

 evbase_fail:
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);

 cmd_fail:
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);

 exit_fail:
  return -1;
}

void
lastfm_deinit(void)
{
  int ret;

  if (!g_initialized)
    return;

  curl_global_cleanup();

  thread_exit();

  ret = pthread_join(tid_lastfm, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_LASTFM, "Could not join lastfm thread: %s\n", strerror(errno));
      return;
    }

  // Free event base (should free events too)
  event_base_free(evbase_lastfm);

  // Close pipes
  close(g_cmd_pipe[0]);
  close(g_cmd_pipe[1]);
  close(g_exit_pipe[0]);
  close(g_exit_pipe[1]);
}
