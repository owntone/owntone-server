/*
 * Copyright (C) 2015 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <uninorm.h>
#include <unistd.h>
#include <pthread.h>

#include <event2/event.h>

#include "httpd_streaming.h"
#include "logger.h"
#include "conffile.h"
#include "transcode.h"
#include "player.h"
#include "listener.h"
#include "db.h"

/* httpd event base, from httpd.c */
extern struct event_base *evbase_httpd;

// Seconds between sending silence when player is idle
// (to prevent client from hanging up)
#define STREAMING_SILENCE_INTERVAL 1
// How many bytes we try to read at a time from the httpd pipe
#define STREAMING_READ_SIZE STOB(352, 16, 2)

#define STREAMING_SESSIONS_MAX 10

#define STREAMING_MP3_SAMPLE_RATE 44100
#define STREAMING_MP3_BPS         16
#define STREAMING_MP3_CHANNELS    2
#define STREAMING_MP3_BIT_RATE    192000

#define STREAMING_ICY_METALEN_MAX      4080  // 255*16 incl header/footer (16bytes)
#define STREAMING_ICY_METATITLELEN_MAX 4064  // STREAMING_ICY_METALEN_MAX -16 (not incl header/footer)

static const struct media_quality  streaming_quality_in = {
    44100, 32, 2, 0
};

struct streaming_ctx;  // Fwd decl

// Linked list of streaming requests
struct streaming_session {
  struct evhttp_request *req;
  struct streaming_session *next;

  bool     require_icy; // Client requested icy meta
  size_t   bytes_sent;  // Audio bytes sent since last metablock

  struct streaming_ctx *ctx;  // what ctx does this session belong
};

// Context representing a supported output (ie mp3, wav441, wav48)
// Each context maintains a list of subscribed sessions/clients (sessions), IPC
// data structures for receiving notifications from the player (audio packets,
// event changes (via pipes) and buffers for any icy metadata for clients and
// the encoded output audio stream
struct streaming_ctx
{
  const char *name;
  const char *endpoint;
  const char *mime;

  const enum transcode_profile xcode;

  pthread_mutex_t sessions_lck;
  struct streaming_session *sessions;
  unsigned available_sessions;
  bool not_supported;

  unsigned icy_clients;

  // Input buffer, utput buffer and encoding ctx for transcode
  struct encode_ctx *encode_ctx;
  struct evbuffer *encoded_data;
  struct media_quality quality_in;
  struct media_quality quality_out;

  // Used for pushing events and data from the player
  struct event *streamingev;

  pthread_mutex_t player_lck;
  struct player_status streaming_player_status;
  int player_changed;

  int streaming_pipe[2];

  char icy_title[STREAMING_ICY_METATITLELEN_MAX];
};

static struct streaming_ctx streaming_ctxs[] = {
  {
    .name = "MP3",
    .endpoint = "/stream.mp3",
    .mime = "audio/mpeg",
    .xcode = XCODE_MP3,
    .available_sessions = STREAMING_SESSIONS_MAX,
    .quality_in = streaming_quality_in,
    .quality_out = { STREAMING_MP3_SAMPLE_RATE, STREAMING_MP3_BPS, STREAMING_MP3_CHANNELS, STREAMING_MP3_BIT_RATE },
    .icy_title[0] = '\0'
  },
  {
    .name = NULL
  }
};

// Interval for sending silence when playback is paused
static const struct timeval streaming_silence_tv = { STREAMING_SILENCE_INTERVAL, 0 };

/* As streaming quality goes up, we send more data to the remote client.  With a
 * smaller ICY_METAINT value we have to splice metadata more frequently - on 
 * some devices with small input buffers, a higher quality stream and low 
 * ICY_METAINT can lead to stuttering as observed on a Roku Soundbridge
 */
#define STREAMING_ICY_METAINT_DEFAULT  16384
static unsigned short streaming_icy_metaint = STREAMING_ICY_METAINT_DEFAULT;


static void
streaming_close_cb(struct evhttp_connection *evcon, void *arg)
{
  struct streaming_ctx *ctx = ((struct streaming_session*)arg)->ctx;

  struct streaming_session *this;
  struct streaming_session *session;
  struct streaming_session *prev;
  char *address;
  ev_uint16_t port;

  this = ctx->sessions;

  evhttp_connection_get_peer(evcon, &address, &port);
  DPRINTF(E_INFO, L_STREAMING, "%s: Stopping streaming to %s:%d\n", ctx->name, address, (int)port);

  pthread_mutex_lock(&ctx->sessions_lck);
  if (!ctx->sessions)
    {
      // This close comes during deinit() - we don't free `this` since it is
      // already a dangling ptr (free'd in deinit()) at this stage
      pthread_mutex_unlock(&ctx->sessions_lck);
      return;
    }

  prev = NULL;
  for (session = ctx->sessions; session; session = session->next)
    {
      if (session->req == this->req)
	break;

      prev = session;
    }

  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "Bug! Got a failure callback for an unknown stream (%s:%d)\n", address, (int)port);
      free(this);
      pthread_mutex_unlock(&ctx->sessions_lck);
      return;
    }

  if (!prev)
    ctx->sessions = session->next;
  else
    prev->next = session->next;

  ++(ctx->available_sessions);
  if (session->require_icy)
    --(ctx->icy_clients);

  // Valgrind says libevent doesn't free the request on disconnect (even though it owns it - libevent bug?),
  // so we do it with a reply end
  evhttp_send_reply_end(session->req);
  free(session);
  session = NULL;

  if (!ctx->sessions)
    {
      DPRINTF(E_INFO, L_STREAMING, "%s: No more clients, will stop streaming\n", ctx->name);
      event_del(ctx->streamingev);
      outputs_quality_unsubscribe(&ctx->quality_in);
      transcode_encode_cleanup(&ctx->encode_ctx);
      ctx->encode_ctx = NULL;
    }

  pthread_mutex_unlock(&ctx->sessions_lck);
}

static void
streaming_end(struct streaming_ctx *ctx)
{
  struct streaming_session *session;
  struct evhttp_connection *evcon;
  char *address;
  ev_uint16_t port;

  pthread_mutex_lock(&ctx->sessions_lck);
  if (ctx->sessions)
    outputs_quality_unsubscribe(&ctx->quality_in);

  for (session = ctx->sessions; ctx->sessions; session = ctx->sessions)
    {
      evcon = evhttp_request_get_connection(session->req);
      if (evcon)
	{
	  evhttp_connection_set_closecb(evcon, NULL, NULL);
	  evhttp_connection_get_peer(evcon, &address, &port);
	  DPRINTF(E_INFO, L_STREAMING, "%s: Force close stream to %s:%d\n", ctx->name, address, (int)port);
	}
      evhttp_send_reply_end(session->req);

      ctx->sessions = session->next;
      ++(ctx->available_sessions);
      free(session);
    }
  pthread_mutex_unlock(&ctx->sessions_lck);

  transcode_encode_cleanup(&ctx->encode_ctx);
  ctx->encode_ctx = NULL;
  event_del(ctx->streamingev);
}

static int
encode_buffer(struct streaming_ctx *ctx, uint8_t *buffer, size_t size)
{
  transcode_frame *frame;
  int samples;
  int ret;

  if (ctx->not_supported)
    {
      DPRINTF(E_LOG, L_STREAMING, "%s: Streaming unsupported\n", ctx->name);
      return -1;
    }

  if (ctx->quality_in.channels == 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "%s: Streaming quality is zero (%d/%d/%d)\n", ctx->name, ctx->quality_in.sample_rate, ctx->quality_in.bits_per_sample, ctx->quality_in.channels);
      return -1;
    }

  samples = BTOS(size, ctx->quality_in.bits_per_sample, ctx->quality_in.channels);

  frame = transcode_frame_new(buffer, size, samples, &ctx->quality_in);
  if (!frame)
    {
      DPRINTF(E_LOG, L_STREAMING, "%s: Could not convert raw PCM to frame for stream\n", ctx->name);
      return -1;
    }

  ret = transcode_encode(ctx->encoded_data, ctx->encode_ctx, frame, 0);
  transcode_frame_free(frame);

  return ret;
}

/* We know that the icymeta is limited to 1+255*16 (ie 4081) bytes so caller must
 * provide a buf of this size to avoid needless mallocs
 *
 * The icy meta block is defined by a single byte indicating how many double byte
 * words used for the actual meta.  Unused bytes are null padded
 *
 * https://stackoverflow.com/questions/4911062/pulling-track-info-from-an-audio-stream-using-php/4914538#4914538
 * http://www.smackfu.com/stuff/programming/shoutcast.html
 */
static uint8_t *
streaming_icy_meta_create(uint8_t buf[STREAMING_ICY_METALEN_MAX+1], const char *title, unsigned *buflen)
{
  unsigned titlelen = 0;
  unsigned metalen = 0;
  uint8_t no16s;

  *buflen = 0;

  if (title == NULL)
    {
      no16s = 0;
      memcpy(buf, &no16s, 1);

      *buflen = 1;
    }
  else
    {
      titlelen = strlen(title);
      if (titlelen > STREAMING_ICY_METATITLELEN_MAX)
	titlelen = STREAMING_ICY_METATITLELEN_MAX;  // dont worry about the null byte

      // [0]    1x byte N, indicate the total number of 16 bytes words required
      //        to represent the meta data
      // [1..N] meta data book ended by "StreamTitle='" and "';"
      //
      // The '15' is strlen of StreamTitle=' + ';
      no16s = (15 + titlelen)/16 +1;
      metalen = 1 + no16s*16;
      memset(buf, 0, metalen);

      memcpy(buf,             &no16s, 1);
      memcpy(buf+1,           (const uint8_t*)"StreamTitle='", 13);
      memcpy(buf+14,          title, titlelen);
      memcpy(buf+14+titlelen, (const uint8_t*)"';", 2);

      *buflen = metalen;
    }

  return buf;
}

static uint8_t *
streaming_icy_meta_splice(const char *streaming_icy_title, const uint8_t *data, size_t datalen, off_t offset, size_t *len)
{
  uint8_t  meta[STREAMING_ICY_METALEN_MAX+1];  // Buffer, of max sz, for the created icymeta
  unsigned metalen;     // How much of the buffer is in use
  uint8_t *buf;         // Client returned buffer; contains the audio (from data) spliced w/meta (from meta)

  if (data == NULL || datalen == 0)
    return NULL;

  memset(meta, 0, sizeof(meta));
  streaming_icy_meta_create(meta, streaming_icy_title, &metalen);

  *len = datalen + metalen;
  // DPRINTF(E_DBG, L_STREAMING, "splicing meta, audio block=%d bytes, offset=%d, metalen=%d new buflen=%d\n", datalen, offset, metalen, *len);
  buf = malloc(*len);
  memcpy(buf,                data, offset);
  memcpy(buf+offset,         &meta[0], metalen);
  memcpy(buf+offset+metalen, data+offset, datalen-offset);

  return buf;
}

static void
streaming_player_status_update(struct streaming_ctx *ctx)
{
  struct db_queue_item *queue_item;
  uint32_t prev_id;

  prev_id = ctx->streaming_player_status.id;
  player_get_status(&ctx->streaming_player_status);

  if (prev_id == ctx->streaming_player_status.id || !ctx->icy_clients)
    {
      return;
    }

  queue_item = db_queue_fetch_byfileid(ctx->streaming_player_status.id);
  if ( !queue_item)
    {
      ctx->icy_title[0] = '\0';
      return;
    }

  snprintf(ctx->icy_title, sizeof(ctx->icy_title), "%s - %s", queue_item->title, queue_item->artist);
  free_queue_item(queue_item, 0);
}

static void
streaming_send_cb(evutil_socket_t fd, short event, void *arg)
{
  struct streaming_ctx *ctx = arg;

  struct streaming_session *session;
  struct evbuffer *evbuf;
  uint8_t rawbuf[STREAMING_READ_SIZE];
  uint8_t *buf;
  uint8_t *splice_buf = NULL;
  size_t splice_len;
  size_t count;
  int overflow;
  int len;
  int ret;

  // Player wrote data to the pipe (EV_READ)
  if (event & EV_READ)
    {
      while (1)
	{
	  ret = read(fd, &rawbuf, sizeof(rawbuf));
	  if (ret <= 0)
	    break;

	  pthread_mutex_lock(&ctx->player_lck);
	  if (ctx->player_changed)
	    {
	      ctx->player_changed = 0;
	      streaming_player_status_update(ctx);
	    }
	  pthread_mutex_unlock(&ctx->player_lck);

	  ret = encode_buffer(ctx, rawbuf, ret);
	  if (ret < 0)
	    return;
	}
    }
  // Event timed out, let's see what the player is doing and send silence if it is paused
  else
    {
      pthread_mutex_lock(&ctx->player_lck);
      if (ctx->player_changed)
	{
	  ctx->player_changed = 0;
	  streaming_player_status_update(ctx);
	}
      pthread_mutex_unlock(&ctx->player_lck);

      if (ctx->streaming_player_status.status != PLAY_PAUSED)
	return;

      memset(&rawbuf, 0, sizeof(rawbuf));
      ret = encode_buffer(ctx, rawbuf, sizeof(rawbuf));
      if (ret < 0)
	return;
    }

  len = evbuffer_get_length(ctx->encoded_data);
  if (len == 0)
    return;

  // Send data
  evbuf = evbuffer_new();
  pthread_mutex_lock(&ctx->sessions_lck);
  for (session = ctx->sessions; session; session = session->next)
    {
      // Does this session want ICY meta data and is it time to send?
      count = session->bytes_sent+len;
      if (session->require_icy && count > streaming_icy_metaint)
	{
	  overflow = count%streaming_icy_metaint;
	  buf = evbuffer_pullup(ctx->encoded_data, -1);

	  // DPRINTF(E_DBG, L_STREAMING, "session=%x sent=%ld len=%ld overflow=%ld\n", session, session->bytes_sent, len, overflow);

	  // Splice the 'icy title' in with the encoded audio data
	  splice_len = 0;
	  splice_buf = streaming_icy_meta_splice(ctx->icy_title, buf, len, len-overflow, &splice_len);

	  evbuffer_add(evbuf, splice_buf, splice_len);

	  free(splice_buf);
	  splice_buf = NULL;

	  evhttp_send_reply_chunk(session->req, evbuf);

	  if (session->next == NULL)
	    {
	      // We're the last session, drop the contents of the encoded buffer
	      evbuffer_drain(ctx->encoded_data, len);
	    }
	  session->bytes_sent = overflow;
	}
      else
	{
	  if (session->next)
	    {
	      buf = evbuffer_pullup(ctx->encoded_data, -1);
	      evbuffer_add(evbuf, buf, len);
	      evhttp_send_reply_chunk(session->req, evbuf);
	    }
	  else
	    {
	      evhttp_send_reply_chunk(session->req, ctx->encoded_data);
	    }
	  session->bytes_sent += len;
	}
    }
  pthread_mutex_unlock(&ctx->sessions_lck);

  evbuffer_free(evbuf);
}

// Thread: player (not fully thread safe, but hey...)
static void
player_change_cb(short event_mask)
{
  struct streaming_ctx *ctx = streaming_ctxs;

  while (ctx->name)
    {
      pthread_mutex_lock(&ctx->sessions_lck);
      if (!ctx->sessions)
	{
	  pthread_mutex_unlock(&ctx->sessions_lck);
	  ++ctx;
	  continue;
	}
      pthread_mutex_unlock(&ctx->sessions_lck);

      pthread_mutex_lock(&ctx->player_lck);
      ctx->player_changed = 1;
      pthread_mutex_unlock(&ctx->player_lck);
      ++ctx;
    }
}

// Thread: player (also prone to race conditions, mostly during deinit)
void
streaming_write(struct output_buffer *obuf)
{
  int ret;
  struct streaming_ctx *ctx = streaming_ctxs;

  // Explicit no-lock - let the write to pipes fail if during deinit
  while (ctx->name)
    {
      pthread_mutex_lock(&ctx->sessions_lck);
      if (!ctx->sessions)
	goto next;

      ret = write(ctx->streaming_pipe[1], obuf->data[0].buffer, obuf->data[0].bufsize);
      if (ret < 0)
        {
          if (errno == EAGAIN)
            DPRINTF(E_WARN, L_STREAMING, "%s: Streaming pipe full, skipping write\n", ctx->name);
          else
            {
              if (errno == EBADF)
                DPRINTF(E_LOG, L_STREAMING, "%s: Streaming pipe already closed\n", ctx->name);
              else
                DPRINTF(E_LOG, L_STREAMING, "%s: Error writing to streaming pipe: %s\n", ctx->name, strerror(errno));
            }
        }
next:
      pthread_mutex_unlock(&ctx->sessions_lck);
      ++ctx;
    }
}

int
streaming_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct decode_ctx *decode_ctx;
  struct streaming_session *session;
  struct evhttp_connection *evcon;
  struct evkeyvalq *output_headers;
  cfg_t *lib;
  const char *name;
  char *address;
  ev_uint16_t port;
  const char *param;
  bool require_icy = false;
  char buf[9];
  int ret;
  const char *err = "no error";

  char *ptr;
  struct streaming_ctx *ctx = streaming_ctxs;

  // Find appropriate supported ctx
  ptr = strrchr(uri_parsed->path, '/');
  if (!ptr)
    {
      DPRINTF(E_LOG, L_STREAMING, "Bug! Unable to find streaming ctx '%s'\n", uri_parsed->path);
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request (no streaming endpoint)");
      return -1;
    }

  while (ctx->name)
    {
      if (strcasecmp(ptr, ctx->endpoint) == 0)
        break;
      ++ctx;
    }

  if (!ctx->name)
    {
      DPRINTF(E_LOG, L_STREAMING, "Bug! Unable to find streaming ctx '%s'\n", uri_parsed->path);
      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found (streaming endpoint)");
      return -1;
    }

  evcon = evhttp_request_get_connection(req);
  evhttp_connection_get_peer(evcon, &address, &port);
  if (ctx->not_supported)
    {
      DPRINTF(E_INFO, L_STREAMING, "%s: Unsupported stream request from %s:%d\n", ctx->name, address, (int)port);
      evhttp_send_error(req, HTTP_NOTIMPLEMENTED, "Not Implemented");
      return -1;
    }

  if (ctx->available_sessions == 0)
    {
      DPRINTF(E_WARN, L_STREAMING, "%s: Exceeded available sessions, rejecting streaming to %s:%d\n", ctx->name, address, (int)port);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "No streaming sessions currently available");
      return -1;
    }

  param = evhttp_find_header( evhttp_request_get_input_headers(req), "Icy-MetaData");
  if (param && strcmp(param, "1") == 0)
    require_icy = true;

  DPRINTF(E_INFO, L_STREAMING, "%s: Streaming request (with icy=%d, icy_metaint=%d) to %s:%d\n", ctx->name, require_icy, streaming_icy_metaint, address, (int)port);

  lib = cfg_getsec(cfg, "library");
  name = cfg_getstr(lib, "name");

  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Content-Type", ctx->mime);
  evhttp_add_header(output_headers, "Server", PACKAGE_NAME "/" VERSION);
  evhttp_add_header(output_headers, "Cache-Control", "no-cache");
  evhttp_add_header(output_headers, "Pragma", "no-cache");
  evhttp_add_header(output_headers, "Expires", "Mon, 31 Aug 2015 06:00:00 GMT");
  if (require_icy)
    {
      ++(ctx->icy_clients);
      evhttp_add_header(output_headers, "icy-name", name);
      snprintf(buf, sizeof(buf)-1, "%d", streaming_icy_metaint);
      evhttp_add_header(output_headers, "icy-metaint", buf);
    }
  evhttp_add_header(output_headers, "Access-Control-Allow-Origin", "*");
  evhttp_add_header(output_headers, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");


  session = calloc(1, sizeof(struct streaming_session));
  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "%s: Out of memory for streaming request\n", ctx->name);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }
  session->ctx = ctx;

  pthread_mutex_lock(&ctx->sessions_lck);

  if (!ctx->sessions)
    {
      ret = outputs_quality_subscribe(&ctx->quality_in);
      if (ret < 0)
	{
	  err = "subscribe requested quality";
	  goto error;
	}

      event_add(ctx->streamingev, &streaming_silence_tv);

      decode_ctx = transcode_decode_setup_raw(XCODE_PCM32, &ctx->quality_in);
      if (!decode_ctx)
	{
	  err = "decode setup";
	  goto error;
	}

      ctx->encode_ctx = transcode_encode_setup(ctx->xcode, &ctx->quality_out, decode_ctx, NULL, 0, 0);
      transcode_decode_cleanup(&decode_ctx);
      if (!ctx->encode_ctx)
	{
	  err = "encode setup";
	  goto error;
	}
    }

  session->req = req;
  session->next = ctx->sessions;
  session->require_icy = require_icy;
  session->bytes_sent = 0;
  ctx->sessions = session;

  --(ctx->available_sessions);
  pthread_mutex_unlock(&ctx->sessions_lck);

  evhttp_send_reply_start(req, HTTP_OK, "OK");
  evhttp_connection_set_closecb(evcon, streaming_close_cb, session);

  return 0;

error:
  pthread_mutex_unlock(&ctx->sessions_lck);

  free(session);
  transcode_encode_cleanup(&ctx->encode_ctx);
  ctx->encode_ctx = NULL;

  DPRINTF(E_LOG, L_STREAMING, "%s: Unable to stream, libav does not support encoding (%s): %d/%d/%d @ %d\n", ctx->name, err, ctx->quality_out.sample_rate, ctx->quality_out.bits_per_sample, ctx->quality_out.channels, ctx->quality_out.bit_rate);
  ctx->not_supported = 1;

  evhttp_send_reply_start(req, HTTP_SERVUNAVAIL, "Service Unavailable (no xcode available)");
  return -1;
}

int
streaming_is_request(const char *path)
{
  char *ptr;
  struct streaming_ctx *ctx = streaming_ctxs;

  ptr = strrchr(path, '/');
  if (ptr)
    {
      while (ctx->name)
	{
	  if (strcasecmp(ptr, ctx->endpoint) == 0)
	    return 1;
	  ++ctx;
	}
    }

  return 0;
}

int
streaming_init(void)
{
  int ret;
  cfg_t *cfgsec;
  int val;
  int max_sessions;
  struct streaming_ctx *ctx = streaming_ctxs;
  int i, secn;
  const char *s;

  secn = cfg_size(cfg, "streaming");

  // Listen to playback changes so we don't have to poll to check for pausing
  ret = listener_add(player_change_cb, LISTENER_PLAYER);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_STREAMING, "Could not add listener\n");
      goto error;
    }

  while (ctx->name)
    {
      s = NULL;
      for (i=0; i<secn; ++i)
	{
	  cfgsec = cfg_getnsec(cfg, "streaming", i);

	  s = cfg_title(cfgsec);
	  if (strcmp(s, "*") == 0)
	  {
	    // streaming global section

	    val = cfg_getint(cfgsec, "icy_metaint");
	    // Too low a value forces server to send more meta than data
	    if (val >= 4096 && val <= 131072)
	      streaming_icy_metaint = val;
	    else
	      DPRINTF(E_INFO, L_STREAMING, "Unsupported icy_metaint=%d, supported range: 4096..131072, defaulting to %d\n", val, streaming_icy_metaint);
	    s = NULL;
	    continue;
	  }

	  if (strcmp(s, ctx->name) != 0)
	    {
	      s = NULL;
	      continue;
	    }


	  max_sessions = cfg_getint(cfgsec, "max_sessions");
	  if (max_sessions <= 0 || max_sessions > STREAMING_SESSIONS_MAX)
	    {
	      DPRINTF(E_DBG, L_STREAMING, "Invalid max_sessions=%d , defaulting\n", max_sessions);
	      max_sessions = STREAMING_SESSIONS_MAX;
	    }
	  ctx->available_sessions = max_sessions;

	  // lazy validation at point of client connect (libav xcode setup time)
	  ctx->quality_out.sample_rate = cfg_getint(cfgsec, "sample_rate");
	  ctx->quality_out.bit_rate    = cfg_getint(cfgsec, "bit_rate") * 1000;
	}


      DPRINTF(E_INFO, L_STREAMING, "%s: Streaming (max clients: %d) quality: %d/%d/%d @ %dkbps\n", ctx->name, ctx->available_sessions, ctx->quality_out.sample_rate, ctx->quality_out.bits_per_sample, ctx->quality_out.channels, ctx->quality_out.bit_rate/1000);

      // Non-blocking because otherwise httpd and player thread may deadlock
#ifdef HAVE_PIPE2
      ret = pipe2(ctx->streaming_pipe, O_CLOEXEC | O_NONBLOCK);
#else
      if ( pipe(ctx->streaming_pipe) < 0 ||
	   fcntl(ctx->streaming_pipe[0], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 ||
	   fcntl(ctx->streaming_pipe[1], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 )
	ret = -1;
      else
	ret = 0;
#endif
      if (ret < 0)
	{
	  DPRINTF(E_FATAL, L_STREAMING, "Could not create pipe: %s\n", strerror(errno));
	  goto error;
	}

      if (ret < 0)
	{
	  DPRINTF(E_FATAL, L_STREAMING, "Could not create pipe: %s\n", strerror(errno));
	  goto error;
	}

      pthread_mutex_init(&ctx->sessions_lck, NULL);
      pthread_mutex_init(&ctx->player_lck, NULL);

      // Initialize buffer for encoded audio and event for pipe reading
      CHECK_NULL(L_STREAMING, ctx->encoded_data = evbuffer_new());
      CHECK_NULL(L_STREAMING, ctx->streamingev = event_new(evbase_httpd, ctx->streaming_pipe[0], EV_TIMEOUT | EV_READ | EV_PERSIST, streaming_send_cb, ctx));

      ++ctx;
    }

  return 0;

 error:
  ctx = streaming_ctxs;
  while (ctx->name)
    {
      close(ctx->streaming_pipe[0]);
      close(ctx->streaming_pipe[1]);
      ++ctx;
    }

  return -1;
}

void
streaming_deinit(void)
{
  struct streaming_ctx *ctx = streaming_ctxs;

  while (ctx->name)
    {
      streaming_end(ctx);

      event_free(ctx->streamingev);
      ctx->streamingev = NULL;

      listener_remove(player_change_cb);

      close(ctx->streaming_pipe[0]);
      close(ctx->streaming_pipe[1]);

      evbuffer_free(ctx->encoded_data);
      ctx->encoded_data = NULL;

      pthread_mutex_destroy(&ctx->sessions_lck);
      pthread_mutex_destroy(&ctx->player_lck);

      ++ctx;
    }
}
