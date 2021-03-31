/*
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

#include <event2/event.h>

#include "input.h"
#include "misc.h"
#include "logger.h"
#include "http.h"
#include "db.h"
#include "transcode.h"
#include "spotify.h"
#include "spotifyc/spotifyc.h"

#define SPOTIFY_PROBE_SIZE_MIN 65536

struct global_ctx
{
  pthread_mutex_t lock;
  pthread_cond_t cond;

  struct sp_session *session;
  bool response_pending; // waiting for a response from spotifyc
  struct spotify_status status;
};

struct playback_ctx
{
  struct input_source *source;
  bool xcode_is_ready;
  struct transcode_ctx xcode;

  int read_fd;
  struct event *read_ev;
  struct evbuffer *read_buf;
  size_t read_bytes;
};

static struct global_ctx spotify_ctx;

static bool db_is_initialized;
static struct media_quality spotify_quality = { 44100, 16, 2, 0 };


/* ------------------------------ Utility funcs ----------------------------- */

static void
hextobin(uint8_t *data, size_t data_len, const char *hexstr, size_t hexstr_len)
{
  char hex[] = { 0, 0, 0 };
  const char *ptr;
  int i;

  if (2 * data_len < hexstr_len)
    {
      memset(data, 0, data_len);
      return;
    }

  ptr = hexstr;
  for (i = 0; i < data_len; i++, ptr+=2)
    {
      memcpy(hex, ptr, 2);
      data[i] = strtol(hex, NULL, 16);
    }
}


/* -------------------- Callbacks from spotifyc thread ---------------------- */

static void
got_reply(struct global_ctx *ctx)
{
  pthread_mutex_lock(&ctx->lock);

  ctx->response_pending = false;

  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
}

static void
error_cb(struct sp_session *session, void *cb_arg, int err, const char *errmsg)
{
  struct global_ctx *ctx = cb_arg;

  got_reply(ctx);

  DPRINTF(E_LOG, L_SPOTIFY, "%s (error code %d)\n", errmsg, err);
}

static void
logged_in_cb(struct sp_session *session, void *cb_arg, struct sp_credentials *credentials)
{
  struct global_ctx *ctx = cb_arg;
  char *db_stored_cred;
  char *ptr;
  int ret;
  int i;

  if (!db_is_initialized)
    {
      ret = db_perthread_init();
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_SPOTIFY, "Error: DB init failed (spotify thread)\n");
	  return;
	}

      db_is_initialized = true;
    }

  DPRINTF(E_LOG, L_SPOTIFY, "Logged into Spotify succesfully\n");

  if (!credentials->username || !credentials->stored_cred)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "No credentials returned by Spotify, automatic login will not be possible\n");
      return;
    }

  db_stored_cred = malloc(2 * credentials->stored_cred_len +1);
  for (i = 0, ptr = db_stored_cred; i < credentials->stored_cred_len; i++)
    ptr += sprintf(ptr, "%02x", credentials->stored_cred[i]);

  db_admin_set("spotify_username", credentials->username);
  db_admin_set("spotify_stored_cred", db_stored_cred);

  free(db_stored_cred);

  pthread_mutex_lock(&ctx->lock);

  ctx->response_pending = false;
  ctx->status.logged_in = true;
  snprintf(ctx->status.username, sizeof(ctx->status.username), "%s", credentials->username);

  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
}

static void
logged_out_cb(struct sp_session *session, void *cb_arg, struct sp_credentials *credentials)
{
  db_admin_delete("spotify_username");
  db_admin_delete("spotify_stored_cred");

  if (db_is_initialized)
    db_perthread_deinit();

  db_is_initialized = false;
}

static void
track_opened_cb(struct sp_session *session, void *cb_arg, int fd)
{
  struct global_ctx *ctx = cb_arg;

  pthread_mutex_lock(&ctx->lock);

  ctx->response_pending = false;
  ctx->status.track_opened = true;

  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
}

static void
track_closed_cb(struct sp_session *session, void *cb_arg, int fd)
{
  struct global_ctx *ctx = cb_arg;

  DPRINTF(E_DBG, L_SPOTIFY, "track_closed_cb()\n");

  pthread_mutex_lock(&ctx->lock);

  ctx->response_pending = false;
  ctx->status.track_opened = false;

  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lock);
}

static int
https_get_cb(char **out, const char *url)
{
  struct http_client_ctx ctx = { 0 };
  char *body;
  size_t len;
  int ret;

  ctx.url = url;
  ctx.input_body = evbuffer_new();

  ret = http_client_request(&ctx);
  if (ret < 0 || ctx.response_code != HTTP_OK)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Failed to AP list from '%s' (return %d, error code %d)\n", ctx.url, ret, ctx.response_code);
      goto error;
    }

  len = evbuffer_get_length(ctx.input_body);
  body = malloc(len + 1);

  evbuffer_remove(ctx.input_body, body, len);
  body[len] = '\0'; // For safety

  *out = body;

  evbuffer_free(ctx.input_body);
  return 0;

 error:
  evbuffer_free(ctx.input_body);
  return -1;
}

static void
logmsg_cb(const char *fmt, ...)
{
/*  va_list ap;

  va_start(ap, fmt);
  DVPRINTF(E_DBG, L_SPOTIFY, fmt, ap);
  va_end(ap);
*/
}

static void
hexdump_cb(const char *msg, uint8_t *data, size_t data_len)
{
//  DHEXDUMP(E_DBG, L_SPOTIFY, data, data_len, msg);
}


/* --------------------- Implementation (input thread) ---------------------- */

struct sp_callbacks callbacks = {
  .error        = error_cb,
  .logged_in    = logged_in_cb,
  .logged_out   = logged_out_cb,
  .track_opened = track_opened_cb,
  .track_closed = track_closed_cb,
  .https_get    = https_get_cb,

  .hexdump  = hexdump_cb,
  .logmsg   = logmsg_cb,
};

// Forward
static void
read_cb(int fd, short what, void *arg);

// Has to be called after we have started receiving data, since ffmpeg needs to
// probe the data to find the audio streams
static int
playback_xcode_setup(struct playback_ctx *playback)
{
  playback->xcode.decode_ctx = transcode_decode_setup(XCODE_OGG, NULL, DATA_KIND_SPOTIFY, NULL, playback->read_buf, 0);
  if (!playback->xcode.decode_ctx)
    return -1;;

  playback->xcode.encode_ctx = transcode_encode_setup(XCODE_PCM16, NULL, playback->xcode.decode_ctx, NULL, 0, 0);
  if (!playback->xcode.encode_ctx)
    return -1;

  playback->xcode_is_ready = true;

  return 0;
}

static void
playback_free(struct playback_ctx *playback)
{
  if (!playback)
    return;

  if (playback->read_ev)
    event_free(playback->read_ev);
  if (playback->read_buf)
    evbuffer_free(playback->read_buf);
  if (playback->read_fd >= 0)
    close(playback->read_fd);

  transcode_decode_cleanup(&playback->xcode.decode_ctx);
  transcode_encode_cleanup(&playback->xcode.encode_ctx);
  free(playback);
}

static struct playback_ctx *
playback_new(struct input_source *source, int fd)
{
  struct playback_ctx *playback;

  CHECK_NULL(L_SPOTIFY, playback = calloc(1, sizeof(struct playback_ctx)));
  playback->read_fd = fd;
  playback->source = source;

  CHECK_NULL(L_SPOTIFY, playback->read_buf = evbuffer_new());
  playback->read_ev = event_new(source->evbase, fd, EV_READ | EV_PERSIST, read_cb, playback);
  if (!playback->read_ev)
    goto error;

  event_add(playback->read_ev, NULL);

  return playback;

 error:
  playback_free(playback);
  return NULL;
}

static int
stop(struct input_source *source)
{
  struct playback_ctx *playback = source->input_ctx;

  if (playback)
    {
      spotifyc_stop(playback->read_fd);
      playback_free(playback);
    }

  if (source->evbuf)
    evbuffer_free(source->evbuf);

  source->input_ctx = NULL;
  source->evbuf = NULL;

  spotify_ctx.status.track_opened = false;

  return 0;
}

static int
setup(struct input_source *source)
{
  struct global_ctx *ctx = &spotify_ctx;
  int ret;
  int fd;

  // First try to open the source (no downloading yet)
  pthread_mutex_lock(&ctx->lock);

  fd = spotifyc_open(source->path, ctx->session);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Could not create fd for Spotify playback\n");
      goto error;
    }

  ctx->response_pending = true;
  while (ctx->response_pending)
    pthread_cond_wait(&ctx->cond, &ctx->lock);

  if (!ctx->status.track_opened)
    {
      close(fd);
      goto error;
    }

  // Seems we have a valid source, now setup a read + decoding context. The
  // closing of the fd is from now on part of closing the playback_ctx, which is
  // done in stop().
  source->evbuf = evbuffer_new();
  source->input_ctx = playback_new(source, fd);
  if (!source->evbuf || !source->input_ctx)
    goto error;

  source->quality = spotify_quality;

  ret = spotifyc_play(fd);
  if (ret < 0)
    goto error;

  pthread_mutex_unlock(&ctx->lock);
  return 0;

 error:
  pthread_mutex_unlock(&ctx->lock);
  stop(source);

  return -1;
}

static int
seek(struct input_source *source, int seek_ms)
{
  return -1;
}

static void
read_cb(int fd, short what, void *arg)
{
  struct playback_ctx *playback_ctx = arg;
  struct input_source *source = playback_ctx->source;
  struct transcode_ctx *xcode_ctx = &playback_ctx->xcode;
  int got;
  int ret;

  got = evbuffer_read(playback_ctx->read_buf, fd, -1);
  if (got < 0)
    goto error;

  playback_ctx->read_bytes += got;
  if (playback_ctx->read_bytes < SPOTIFY_PROBE_SIZE_MIN)
    return; // ffmpeg requires more data to be able to probe the Ogg

  if (!playback_ctx->xcode_is_ready)
    {
      ret = playback_xcode_setup(playback_ctx);
      if (ret < 0)
	goto error;
    }

  // Decode the Ogg Vorbis to PCM
  ret = transcode(source->evbuf, NULL, xcode_ctx, 1);
  if (ret == 0)
    {
      input_write(source->evbuf, &source->quality, INPUT_FLAG_EOF);
      stop(source);
    }
  else if (ret < 0)
    goto error;

  input_write(source->evbuf, &source->quality, 0);

  return;

 error:
  input_write(NULL, NULL, INPUT_FLAG_ERROR);
  stop(source);
}

static int
init(void)
{
  char *username = NULL;
  char *db_stored_cred = NULL;
  size_t db_stored_cred_len;
  uint8_t *stored_cred = NULL;
  size_t stored_cred_len;
  int ret;

  CHECK_ERR(L_SPOTIFY, mutex_init(&spotify_ctx.lock));
  CHECK_ERR(L_SPOTIFY, pthread_cond_init(&spotify_ctx.cond, NULL));

  ret = spotifyc_init(&callbacks, &spotify_ctx);
  if (ret < 0)
    goto error;

  if ( db_admin_get(&username, "spotify_username") < 0 ||
       db_admin_get(&db_stored_cred, "spotify_stored_cred") < 0 ||
       !username || !db_stored_cred )
    goto end; // User not logged in yet

  db_stored_cred_len = strlen(db_stored_cred);
  stored_cred_len = db_stored_cred_len / 2;

  CHECK_NULL(L_SPOTIFY, stored_cred = malloc(stored_cred_len));
  hextobin(stored_cred, stored_cred_len, db_stored_cred, db_stored_cred_len);

  spotify_ctx.session = spotifyc_login_stored_cred(username, stored_cred, stored_cred_len);
  if (!spotify_ctx.session)
    goto error;

 end:
  free(username);
  free(db_stored_cred);
  free(stored_cred);
  return 0;

 error:
  free(username);
  free(db_stored_cred);
  free(stored_cred);
  return -1;
}

static void
deinit(void)
{
  spotifyc_deinit();
}

struct input_definition input_spotify =
{
  .name = "Spotify",
  .type = INPUT_TYPE_SPOTIFY,
  .disabled = 0,
  .setup = setup,
  .stop = stop,
  .seek = seek,
  .init = init,
  .deinit = deinit,
};


/* ------------ Functions exposed via spotify.h (foreign threads) ----------- */

int
spotify_login_user(const char *user, const char *password, const char **errmsg)
{
  struct global_ctx *ctx = &spotify_ctx;
  int ret;

  pthread_mutex_lock(&ctx->lock);

  ctx->response_pending = true;

  ctx->session = spotifyc_login_password(user, password);
  if (!ctx->session)
    {
      pthread_mutex_unlock(&ctx->lock);
      *errmsg = "Error creating Spotify session";
      return -1;
    }

  while (ctx->response_pending)
    pthread_cond_wait(&ctx->cond, &ctx->lock);

  ret = ctx->status.logged_in ? 0 : -1;
  if (ret < 0)
    *errmsg = spotifyc_last_errmsg();

  pthread_mutex_unlock(&ctx->lock);

  return ret;
}

void
spotify_login(char **arglist)
{
  return;
}

void
spotify_logout(void)
{
  return;
}

void
spotify_status_get(struct spotify_status *status)
{
  struct global_ctx *ctx = &spotify_ctx;

  pthread_mutex_lock(&ctx->lock);

  memcpy(status->username, ctx->status.username, sizeof(status->username));
  status->logged_in = ctx->status.logged_in;
  status->installed = true;

  pthread_mutex_unlock(&ctx->lock);
}
