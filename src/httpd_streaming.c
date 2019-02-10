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

#include <event2/event.h>

#include "httpd_streaming.h"
#include "logger.h"
#include "conffile.h"
#include "transcode.h"
#include "player.h"
#include "listener.h"

/* httpd event base, from httpd.c */
extern struct event_base *evbase_httpd;

// Seconds between sending silence when player is idle
// (to prevent client from hanging up)
#define STREAMING_SILENCE_INTERVAL 1
// How many bytes we try to read at a time from the httpd pipe
#define STREAMING_READ_SIZE STOB(352, 16, 2)

// Linked list of mp3 streaming requests
struct streaming_session {
  struct evhttp_request *req;
  struct streaming_session *next;
};
static struct streaming_session *streaming_sessions;

// Means we're not able to encode to mp3
static bool streaming_not_supported;

// Interval for sending silence when playback is paused
static struct timeval streaming_silence_tv = { STREAMING_SILENCE_INTERVAL, 0 };

// Input buffer, output buffer and encoding ctx for transcode
static struct encode_ctx *streaming_encode_ctx;
static struct evbuffer *streaming_encoded_data;
static struct media_quality streaming_quality;

// Used for pushing events and data from the player
static struct event *streamingev;
static struct event *metaev;
static struct player_status streaming_player_status;
static int streaming_player_changed;
static int streaming_pipe[2];
static int streaming_meta[2];


static void
streaming_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct streaming_session *this;
  struct streaming_session *session;
  struct streaming_session *prev;

  this = (struct streaming_session *)arg;

  DPRINTF(E_WARN, L_STREAMING, "Connection failed; stopping mp3 streaming to client\n");

  prev = NULL;
  for (session = streaming_sessions; session; session = session->next)
    {
      if (session->req == this->req)
	break;

      prev = session;
    }

  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "Bug! Got a failure callback for an unknown stream\n");
      free(this);
      return;
    }

  if (!prev)
    streaming_sessions = session->next;
  else
    prev->next = session->next;

  free(session);

  if (!streaming_sessions)
    {
      DPRINTF(E_INFO, L_STREAMING, "No more clients, will stop streaming\n");
      event_del(streamingev);
    }
}

static void
streaming_meta_cb(evutil_socket_t fd, short event, void *arg)
{
  struct media_quality quality;
  struct decode_ctx *decode_ctx;
  int ret;

  ret = read(fd, &quality, sizeof(struct media_quality));
  if (ret != sizeof(struct media_quality))
    goto error;

  streaming_quality = quality;

  decode_ctx = NULL;
  if (quality.sample_rate == 44100 && quality.bits_per_sample == 16)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM16_44100);
  else if (quality.sample_rate == 44100 && quality.bits_per_sample == 24)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM24_44100);
  else if (quality.sample_rate == 48000 && quality.bits_per_sample == 16)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM16_48000);
  else if (quality.sample_rate == 48000 && quality.bits_per_sample == 24)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM24_48000);

  if (!decode_ctx)
    goto error;

  streaming_encode_ctx = transcode_encode_setup(XCODE_MP3, decode_ctx, NULL, 0, 0);
  transcode_decode_cleanup(&decode_ctx);
  if (!streaming_encode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Will not be able to stream MP3, libav does not support MP3 encoding\n");
      streaming_not_supported = 1;
      return;
    }

  streaming_not_supported = 0;

 error:
  DPRINTF(E_LOG, L_STREAMING, "Unknown or unsupported quality of input data, cannot MP3 encode\n");
  transcode_encode_cleanup(&streaming_encode_ctx);
  streaming_not_supported = 1;
}

static int
encode_buffer(uint8_t *buffer, size_t size)
{
  transcode_frame *frame;
  int samples;
  int ret;

  samples = BTOS(size, streaming_quality.bits_per_sample, streaming_quality.channels);

  frame = transcode_frame_new(buffer, size, samples, streaming_quality.sample_rate, streaming_quality.bits_per_sample);
  if (!frame)
    {
      DPRINTF(E_LOG, L_STREAMING, "Could not convert raw PCM to frame\n");
      return -1;
    }

  ret = transcode_encode(streaming_encoded_data, streaming_encode_ctx, frame, 0);
  transcode_frame_free(frame);

  return ret;
}

static void
streaming_send_cb(evutil_socket_t fd, short event, void *arg)
{
  struct streaming_session *session;
  struct evbuffer *evbuf;
  uint8_t rawbuf[STREAMING_READ_SIZE];
  uint8_t *buf;
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

	  ret = encode_buffer(rawbuf, ret);
	  if (ret < 0)
	    return;
	}
    }
  // Event timed out, let's see what the player is doing and send silence if it is paused
  else
    {
      if (streaming_player_changed)
	{
	  streaming_player_changed = 0;
	  player_get_status(&streaming_player_status);
	}

      if (streaming_player_status.status != PLAY_PAUSED)
	return;

      memset(&rawbuf, 0, sizeof(rawbuf));
      ret = encode_buffer(rawbuf, sizeof(rawbuf));
      if (ret < 0)
	return;
    }

  len = evbuffer_get_length(streaming_encoded_data);
  if (len == 0)
    return;

  // Send data
  evbuf = evbuffer_new();
  for (session = streaming_sessions; session; session = session->next)
    {
      if (session->next)
	{
	  buf = evbuffer_pullup(streaming_encoded_data, -1);
	  evbuffer_add(evbuf, buf, len);
	  evhttp_send_reply_chunk(session->req, evbuf);
	}
      else
	evhttp_send_reply_chunk(session->req, streaming_encoded_data);
    }

  evbuffer_free(evbuf);
}

// Thread: player (not fully thread safe, but hey...)
static void
player_change_cb(short event_mask)
{
  streaming_player_changed = 1;
}

// Thread: player (also prone to race conditions, mostly during deinit)
void
streaming_write(struct output_buffer *obuf)
{
  int ret;

  if (!streaming_sessions)
    return;

  if (!quality_is_equal(&obuf->frames[0].quality, &streaming_quality))
    {
      ret = write(streaming_meta[1], &obuf->frames[0].quality, sizeof(struct media_quality));
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_STREAMING, "Error writing to streaming pipe: %s\n", strerror(errno));
	  return;
	}
    }

  ret = write(streaming_pipe[1], obuf->frames[0].buffer, obuf->frames[0].bufsize);
  if (ret < 0)
    {
      if (errno == EAGAIN)
	DPRINTF(E_WARN, L_STREAMING, "Streaming pipe full, skipping write\n");
      else
	DPRINTF(E_LOG, L_STREAMING, "Error writing to streaming pipe: %s\n", strerror(errno));
    }
}

int
streaming_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct streaming_session *session;
  struct evhttp_connection *evcon;
  struct evkeyvalq *output_headers;
  cfg_t *lib;
  const char *name;
  char *address;
  ev_uint16_t port;

  if (!streaming_not_supported)
    {
      DPRINTF(E_LOG, L_STREAMING, "Got MP3 streaming request, but cannot encode to MP3\n");

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return -1;
    }

  evcon = evhttp_request_get_connection(req);
  evhttp_connection_get_peer(evcon, &address, &port);

  DPRINTF(E_INFO, L_STREAMING, "Beginning mp3 streaming to %s:%d\n", address, (int)port);

  lib = cfg_getsec(cfg, "library");
  name = cfg_getstr(lib, "name");

  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Content-Type", "audio/mpeg");
  evhttp_add_header(output_headers, "Server", "forked-daapd/" VERSION);
  evhttp_add_header(output_headers, "Cache-Control", "no-cache");
  evhttp_add_header(output_headers, "Pragma", "no-cache");
  evhttp_add_header(output_headers, "Expires", "Mon, 31 Aug 2015 06:00:00 GMT");
  evhttp_add_header(output_headers, "icy-name", name);
  evhttp_add_header(output_headers, "Access-Control-Allow-Origin", "*");
  evhttp_add_header(output_headers, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");

  // TODO ICY metaint
  evhttp_send_reply_start(req, HTTP_OK, "OK");

  session = malloc(sizeof(struct streaming_session));
  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "Out of memory for streaming request\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  if (!streaming_sessions)
    event_add(streamingev, &streaming_silence_tv);

  session->req = req;
  session->next = streaming_sessions;
  streaming_sessions = session;

  evhttp_connection_set_closecb(evcon, streaming_fail_cb, session);

  return 0;
}

int
streaming_is_request(const char *path)
{
  char *ptr;

  ptr = strrchr(path, '/');
  if (ptr && (strcasecmp(ptr, "/stream.mp3") == 0))
    return 1;

  return 0;
}

int
streaming_init(void)
{
  int ret;

  // Non-blocking because otherwise httpd and player thread may deadlock
#ifdef HAVE_PIPE2
  ret = pipe2(streaming_pipe, O_CLOEXEC | O_NONBLOCK);
#else
  if ( pipe(streaming_pipe) < 0 ||
       fcntl(streaming_pipe[0], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 ||
       fcntl(streaming_pipe[1], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 )
    ret = -1;
  else
    ret = 0;
#endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_STREAMING, "Could not create pipe: %s\n", strerror(errno));
      goto error;
    }

#ifdef HAVE_PIPE2
  ret = pipe2(streaming_meta, O_CLOEXEC | O_NONBLOCK);
#else
  if ( pipe(streaming_meta) < 0 ||
       fcntl(streaming_meta[0], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 ||
       fcntl(streaming_meta[1], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 )
    ret = -1;
  else
    ret = 0;
#endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_STREAMING, "Could not create pipe: %s\n", strerror(errno));
      goto error;
    }

  // Listen to playback changes so we don't have to poll to check for pausing
  ret = listener_add(player_change_cb, LISTENER_PLAYER);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_STREAMING, "Could not add listener\n");
      goto error;
    }

  // Initialize buffer for encoded mp3 audio and event for pipe reading
  CHECK_NULL(L_STREAMING, streaming_encoded_data = evbuffer_new());

  CHECK_NULL(L_STREAMING, streamingev = event_new(evbase_httpd, streaming_pipe[0], EV_TIMEOUT | EV_READ | EV_PERSIST, streaming_send_cb, NULL));
  CHECK_NULL(L_STREAMING, metaev = event_new(evbase_httpd, streaming_meta[0], EV_READ | EV_PERSIST, streaming_meta_cb, NULL));

  return 0;

 error:
  close(streaming_pipe[0]);
  close(streaming_pipe[1]);
  close(streaming_meta[0]);
  close(streaming_meta[1]);

  return -1;
}

void
streaming_deinit(void)
{
  struct streaming_session *session;
  struct streaming_session *next;

  session = streaming_sessions;
  streaming_sessions = NULL; // Stops writing and sending

  next = NULL;
  while (session)
    {
      evhttp_send_reply_end(session->req);
      next = session->next;
      free(session);
      session = next;
    }

  event_free(streamingev);

  listener_remove(player_change_cb);

  close(streaming_pipe[0]);
  close(streaming_pipe[1]);
  close(streaming_meta[0]);
  close(streaming_meta[1]);

  transcode_encode_cleanup(&streaming_encode_ctx);
  evbuffer_free(streaming_encoded_data);
}
