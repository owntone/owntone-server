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

#include <fcntl.h>

#include <event2/event.h>

#include "input.h"
#include "misc.h"
#include "logger.h"
#include "http.h"
#include "db.h"
#include "transcode.h"
#include "spotify.h"
#include "spotifyc/spotifyc.h"

// Haven't actually studied ffmpeg's probe size requirements, this is just a
// guess
#define SPOTIFY_PROBE_SIZE_MIN 16384

// The transcoder will say EOF if too little data is provided to it
#define SPOTIFY_BUF_MIN 4096

// Limits how much of the Spotify Ogg file we fetch and buffer (in read_buf).
// This will also in effect throttle spotifyc.
#define SPOTIFY_BUF_MAX (512 * 1024)

struct global_ctx
{
  pthread_mutex_t lock;
  pthread_cond_t cond;

  struct sp_session *session;
  bool response_pending; // waiting for a response from spotifyc
  struct spotify_status status;
};

struct download_ctx
{
  bool is_started;
  bool is_ended;
  struct transcode_ctx *xcode;

  struct evbuffer *read_buf;
  int read_fd;

  uint32_t len_ms;
  size_t len_bytes;
};

static struct global_ctx spotify_ctx;

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

// If there is evbuf size is below max, reads from a non-blocking fd until error,
// EAGAIN or evbuf full
static int
fd_read(bool *eofptr, struct evbuffer *evbuf, int fd)
{
  size_t len = evbuffer_get_length(evbuf);
  bool eof = false;
  int total = 0;
  int ret = 0;

  while (len + total < SPOTIFY_BUF_MAX && !eof)
    {
      ret = evbuffer_read(evbuf, fd, -1); // Each read is 4096 bytes (EVBUFFER_READ_MAX)

      if (ret == 0)
	eof = true;
      else if (ret < 0)
	break;

      total += ret;
    }

  if (eofptr)
    *eofptr = eof;

  if (ret < 0 && errno != EAGAIN)
    return ret;

  return total;
}

/* -------------------- Callbacks from spotifyc thread ---------------------- */

static void
progress_cb(int fd, void *cb_arg, size_t received, size_t len)
{
  DPRINTF(E_DBG, L_SPOTIFY, "Progress %zu/%zu\n", received, len);
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

static int
tcp_connect(const char *address, unsigned short port)
{
  return net_connect(address, port, SOCK_STREAM, "spotify");
}

static void
tcp_disconnect(int fd)
{
  close(fd);
}

static void
logmsg_cb(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  DVPRINTF(E_DBG, L_SPOTIFY, fmt, ap);
  va_end(ap);
}

static void
hexdump_cb(const char *msg, uint8_t *data, size_t data_len)
{
//  DHEXDUMP(E_DBG, L_SPOTIFY, data, data_len, msg);
}


/* --------------------- Implementation (input thread) ---------------------- */

struct sp_callbacks callbacks = {
  .https_get      = https_get_cb,
  .tcp_connect    = tcp_connect,
  .tcp_disconnect = tcp_disconnect,

  .hexdump  = hexdump_cb,
  .logmsg   = logmsg_cb,
};

static int64_t
download_seek(void *arg, int64_t offset, enum transcode_seek_type type)
{
  struct global_ctx *ctx = &spotify_ctx;
  struct download_ctx *download = arg;
  int64_t out;
  int ret;

  pthread_mutex_lock(&ctx->lock);

  switch (type)
    {
      case XCODE_SEEK_SIZE:
	out = download->len_bytes;
	break;
      case XCODE_SEEK_SET:
	// Flush read buffer
	evbuffer_drain(download->read_buf, -1);

	ret = spotifyc_seek(download->read_fd, offset);
	if (ret < 0)
	  goto error;

	fd_read(NULL, download->read_buf, download->read_fd);

	out = offset;
	break;
      default:
	goto error;
    }

  pthread_mutex_unlock(&ctx->lock);

  DPRINTF(E_DBG, L_SPOTIFY, "Seek to offset %" PRIi64 " requested, type %d, returning %" PRIi64 "\n", offset, type, out);

  return out;

 error:
  DPRINTF(E_WARN, L_SPOTIFY, "Seek error\n");

  pthread_mutex_unlock(&ctx->lock);
  return -1;
}

// Has to be called after we have started receiving data, since ffmpeg needs to
// probe the data to find the audio streams
static int
download_xcode_setup(struct download_ctx *download)
{
  struct transcode_ctx *xcode;
  struct transcode_evbuf_io xcode_evbuf_io = { 0 };

  CHECK_NULL(L_SPOTIFY, xcode = malloc(sizeof(struct transcode_ctx)));

  xcode_evbuf_io.evbuf = download->read_buf;
  xcode_evbuf_io.seekfn = download_seek;
  xcode_evbuf_io.seekfn_arg = download;

  xcode->decode_ctx = transcode_decode_setup(XCODE_OGG, NULL, DATA_KIND_SPOTIFY, NULL, &xcode_evbuf_io, download->len_ms);
  if (!xcode->decode_ctx)
    goto error;

  xcode->encode_ctx = transcode_encode_setup(XCODE_PCM16, NULL, xcode->decode_ctx, NULL, 0, 0);
  if (!xcode->encode_ctx)
    goto error;

  download->xcode = xcode;

  return 0;

 error:
  transcode_cleanup(&xcode);
  return -1;
}

static void
download_free(struct download_ctx *download)
{
  if (!download)
    return;

  if (download->read_fd >= 0)
    spotifyc_close(download->read_fd);

  if (download->read_buf)
    evbuffer_free(download->read_buf);

  transcode_cleanup(&download->xcode);
  free(download);
}

static struct download_ctx *
download_new(int fd, uint32_t len_ms, size_t len_bytes)
{
  struct download_ctx *download;

  CHECK_NULL(L_SPOTIFY, download = calloc(1, sizeof(struct download_ctx)));
  CHECK_NULL(L_SPOTIFY, download->read_buf = evbuffer_new());

  download->read_fd = fd;
  download->len_ms = len_ms;
  download->len_bytes = len_bytes;

  return download;
}

static int
stop(struct input_source *source)
{
  struct global_ctx *ctx = &spotify_ctx;
  struct download_ctx *download = source->input_ctx;

  DPRINTF(E_LOG, L_SPOTIFY, "stop()\n");

  pthread_mutex_lock(&ctx->lock);

  download_free(download);

  if (source->evbuf)
    evbuffer_free(source->evbuf);

  source->input_ctx = NULL;
  source->evbuf = NULL;

  pthread_mutex_unlock(&ctx->lock);

  return 0;
}

static int
setup(struct input_source *source)
{
  struct global_ctx *ctx = &spotify_ctx;
  struct download_ctx *download;
  struct sp_metadata metadata;
  int probe_bytes;
  int fd;
  int ret;

  DPRINTF(E_LOG, L_SPOTIFY, "setup()\n");

  pthread_mutex_lock(&ctx->lock);

  fd = spotifyc_open(source->path, ctx->session);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Eror opening source: %s\n", spotifyc_last_errmsg());
      goto error;
    }

  ret = spotifyc_metadata_get(&metadata, fd);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error getting track metadata: %s\n", spotifyc_last_errmsg());
      goto error;
    }

  // Seems we have a valid source, now setup a read + decoding context. The
  // closing of the fd is from now on part of closing the download_ctx, which is
  // done in stop().
  download = download_new(fd, source->len_ms, metadata.file_len);

  CHECK_NULL(L_SPOTIFY, source->evbuf = evbuffer_new());
  CHECK_NULL(L_SPOTIFY, source->input_ctx = download);

  source->quality = spotify_quality;

  // At this point enough bytes should be ready for transcode setup (ffmpeg probing)
  probe_bytes = fd_read(NULL, download->read_buf, fd);
  if (probe_bytes < SPOTIFY_PROBE_SIZE_MIN)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Not enough audio data for ffmpeg probing (%d)\n", probe_bytes);
      goto error;
    }

  ret = download_xcode_setup(download);
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
play(struct input_source *source)
{
  struct download_ctx *download = source->input_ctx;
  size_t buflen;
  int ret;

  // Starts the download. We don't do that in setup because the player/input
  // might run seek() before starting download.
  if (!download->is_started)
    {
      spotifyc_write(download->read_fd, progress_cb, download);
      download->is_started = true;
    }

  if (!download->is_ended)
    {
      ret = fd_read(&download->is_ended, download->read_buf, download->read_fd);
      if (ret < 0)
        goto error;

      buflen = evbuffer_get_length(download->read_buf);
      if (buflen < SPOTIFY_BUF_MIN)
	goto wait;
    }

  // Decode the Ogg Vorbis to PCM in chunks of 16 packets, which is pretty much
  // a randomly chosen chunk size
  ret = transcode(source->evbuf, NULL, download->xcode, 16);
  if (ret == 0)
    {
      input_write(source->evbuf, &source->quality, INPUT_FLAG_EOF);
      stop(source);
      return -1;
    }
  else if (ret < 0)
    goto error;

  ret = input_write(source->evbuf, &source->quality, 0);
  if (ret == EAGAIN)
    goto wait;

  return 0;

 error:
  input_write(NULL, NULL, INPUT_FLAG_ERROR);
  stop(source);
  return -1;

 wait:
  DPRINTF(E_DBG, L_SPOTIFY, "Waiting for data\n");
  input_wait();
  return 0;
}

static int
seek(struct input_source *source, int seek_ms)
{
  struct download_ctx *download = source->input_ctx;

  // This will make transcode call back to download_seek(), but with a byte
  // offset instead of a ms position, which is what spotifyc requires
  return transcode_seek(download->xcode, seek_ms);
}

static int
init(void)
{
  char *username = NULL;
  char *db_stored_cred = NULL;
  size_t db_stored_cred_len;
  uint8_t *stored_cred = NULL;
  size_t stored_cred_len;
  struct sp_credentials credentials;
  int ret;

  CHECK_ERR(L_SPOTIFY, mutex_init(&spotify_ctx.lock));
  CHECK_ERR(L_SPOTIFY, pthread_cond_init(&spotify_ctx.cond, NULL));

  ret = spotifyc_init(&callbacks);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error initializing Spotify: %s\n", spotifyc_last_errmsg());
      goto error;
    }

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
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error logging into Spotify: %s\n", spotifyc_last_errmsg());
      goto error;
    }

  ret = spotifyc_credentials_get(&credentials, spotify_ctx.session);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SPOTIFY, "Error getting Spotify credentials: %s\n", spotifyc_last_errmsg());
      goto error;
    }

  spotify_ctx.status.logged_in = true;
  snprintf(spotify_ctx.status.username, sizeof(spotify_ctx.status.username), "%s", credentials.username);

  DPRINTF(E_LOG, L_SPOTIFY, "Logged into Spotify succesfully with username %s\n", spotify_ctx.status.username);

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
  .play = play,
  .seek = seek,
  .init = init,
  .deinit = deinit,
};


/* ------------ Functions exposed via spotify.h (foreign threads) ----------- */

int
spotify_login_user(const char *user, const char *password, const char **errmsg)
{
  struct global_ctx *ctx = &spotify_ctx;
  struct sp_credentials credentials;
  char *db_stored_cred;
  char *ptr;
  int i;
  int ret;

  pthread_mutex_lock(&ctx->lock);

  ctx->session = spotifyc_login_password(user, password);
  if (!ctx->session)
    goto error;

  ret = spotifyc_credentials_get(&credentials, ctx->session);
  if (ret < 0)
    goto error;

  DPRINTF(E_LOG, L_SPOTIFY, "Logged into Spotify succesfully with username %s\n", credentials.username);

  db_stored_cred = malloc(2 * credentials.stored_cred_len +1);
  for (i = 0, ptr = db_stored_cred; i < credentials.stored_cred_len; i++)
    ptr += sprintf(ptr, "%02x", credentials.stored_cred[i]);

  db_admin_set("spotify_username", credentials.username);
  db_admin_set("spotify_stored_cred", db_stored_cred);

  free(db_stored_cred);

  ctx->status.logged_in = true;
  snprintf(ctx->status.username, sizeof(ctx->status.username), "%s", credentials.username);

  pthread_mutex_unlock(&ctx->lock);

  return 0;

 error:
  if (ctx->session)
    spotifyc_logout(ctx->session);
  ctx->session = NULL;

  *errmsg = spotifyc_last_errmsg();

  pthread_mutex_unlock(&ctx->lock);

  return -1;
}

void
spotify_login(char **arglist)
{
  return;
}

void
spotify_logout(void)
{
  struct global_ctx *ctx = &spotify_ctx;

  db_admin_delete("spotify_username");
  db_admin_delete("spotify_stored_cred");

  pthread_mutex_lock(&ctx->lock);

  spotifyc_logout(ctx->session);
  ctx->session = NULL;

  pthread_mutex_unlock(&ctx->lock);
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
