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

#define STREAMING_MP3_SAMPLE_RATE 44100
#define STREAMING_MP3_BPS         16
#define STREAMING_MP3_CHANNELS    2
#define STREAMING_MP3_BIT_RATE    192000


// Linked list of mp3 streaming requests
struct streaming_session {
  struct evhttp_request *req;
  struct streaming_session *next;

  bool     require_icy; // Client requested icy meta
  size_t   bytes_sent;  // Audio bytes sent since last metablock
};
static pthread_mutex_t streaming_sessions_lck;
static struct streaming_session *streaming_sessions;

// Means we're not able to encode to mp3
static bool streaming_not_supported;

// Interval for sending silence when playback is paused
static struct timeval streaming_silence_tv = { STREAMING_SILENCE_INTERVAL, 0 };

// Input buffer, output buffer and encoding ctx for transcode
static struct encode_ctx *streaming_encode_ctx;
static struct evbuffer *streaming_encoded_data;
static struct media_quality streaming_quality_in;
static struct media_quality streaming_quality_out = { STREAMING_MP3_SAMPLE_RATE, STREAMING_MP3_BPS, STREAMING_MP3_CHANNELS, STREAMING_MP3_BIT_RATE };

// Used for pushing events and data from the player
static struct event *streamingev;
static struct event *metaev;
static struct player_status streaming_player_status;
static int streaming_player_changed;
static int streaming_pipe[2];
static int streaming_meta[2];

#define STREAMING_ICY_METALEN_MAX      4080  // 255*16 incl header/footer (16bytes)
#define STREAMING_ICY_METATITLELEN_MAX 4064  // STREAMING_ICY_METALEN_MAX -16 (not incl header/footer)

/* As streaming quality goes up, we send more data to the remote client.  With a
 * smaller ICY_METAINT value we have to splice metadata more frequently - on 
 * some devices with small input buffers, a higher quality stream and low 
 * ICY_METAINT can lead to stuttering as observed on a Roku Soundbridge
 */
#define STREAMING_ICY_METAINT_DEFAULT  16384
static unsigned short streaming_icy_metaint = STREAMING_ICY_METAINT_DEFAULT;
static unsigned streaming_icy_clients;
static char streaming_icy_title[STREAMING_ICY_METATITLELEN_MAX];


static void
streaming_close_cb(struct evhttp_connection *evcon, void *arg)
{
  struct streaming_session *this;
  struct streaming_session *session;
  struct streaming_session *prev;
  char *address;
  ev_uint16_t port;

  this = (struct streaming_session *)arg;

  evhttp_connection_get_peer(evcon, &address, &port);
  DPRINTF(E_INFO, L_STREAMING, "Stopping mp3 streaming to %s:%d\n", address, (int)port);

  pthread_mutex_lock(&streaming_sessions_lck);
  if (!streaming_sessions)
    {
      // This close comes during deinit() - we don't free `this` since it is
      // already a dangling ptr (free'd in deinit()) at this stage
      pthread_mutex_unlock(&streaming_sessions_lck);
      return;
    }

  prev = NULL;
  for (session = streaming_sessions; session; session = session->next)
    {
      if (session->req == this->req)
	break;

      prev = session;
    }

  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "Bug! Got a failure callback for an unknown stream (%s:%d)\n", address, (int)port);
      free(this);
      pthread_mutex_unlock(&streaming_sessions_lck);
      return;
    }

  if (!prev)
    streaming_sessions = session->next;
  else
    prev->next = session->next;

  if (session->require_icy)
    --streaming_icy_clients;

  // Valgrind says libevent doesn't free the request on disconnect (even though it owns it - libevent bug?),
  // so we do it with a reply end
  evhttp_send_reply_end(session->req);
  free(session);

  if (!streaming_sessions)
    {
      DPRINTF(E_INFO, L_STREAMING, "No more clients, will stop streaming\n");
      event_del(streamingev);
      event_del(metaev);
    }

  pthread_mutex_unlock(&streaming_sessions_lck);
}

static void
streaming_end(void)
{
  struct streaming_session *session;
  struct evhttp_connection *evcon;
  char *address;
  ev_uint16_t port;

  pthread_mutex_lock(&streaming_sessions_lck);
  for (session = streaming_sessions; streaming_sessions; session = streaming_sessions)
    {
      evcon = evhttp_request_get_connection(session->req);
      if (evcon)
	{
	  evhttp_connection_set_closecb(evcon, NULL, NULL);
	  evhttp_connection_get_peer(evcon, &address, &port);
	  DPRINTF(E_INFO, L_STREAMING, "Force close stream to %s:%d\n", address, (int)port);
	}
      evhttp_send_reply_end(session->req);

      streaming_sessions = session->next;
      free(session);
    }
  pthread_mutex_unlock(&streaming_sessions_lck);

  event_del(streamingev);
  event_del(metaev);
}

static void
streaming_meta_cb(evutil_socket_t fd, short event, void *arg)
{
  struct media_quality quality;
  struct decode_ctx *decode_ctx;
  int ret;

  transcode_encode_cleanup(&streaming_encode_ctx);

  ret = read(fd, &quality, sizeof(struct media_quality));
  if (ret != sizeof(struct media_quality))
    goto error;

  decode_ctx = NULL;
  if (quality.bits_per_sample == 16)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM16, &quality);
  else if (quality.bits_per_sample == 24)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM24, &quality);
  else if (quality.bits_per_sample == 32)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM32, &quality);

  if (!decode_ctx)
    goto error;

  streaming_encode_ctx = transcode_encode_setup(XCODE_MP3, &streaming_quality_out, decode_ctx, NULL, 0, 0);
  transcode_decode_cleanup(&decode_ctx);
  if (!streaming_encode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Will not be able to stream MP3, libav does not support MP3 encoding: %d/%d/%d @ %d\n", streaming_quality_out.sample_rate, streaming_quality_out.bits_per_sample, streaming_quality_out.channels, streaming_quality_out.bit_rate);
      streaming_not_supported = 1;
      streaming_end();
      return;
    }

  streaming_quality_in = quality;
  streaming_not_supported = 0;

  return;

 error:
  DPRINTF(E_LOG, L_STREAMING, "Unknown or unsupported quality of input data (%d/%d/%d), cannot MP3 encode\n", quality.sample_rate, quality.bits_per_sample, quality.channels);
  streaming_not_supported = 1;
  streaming_end();
}

static int
encode_buffer(uint8_t *buffer, size_t size)
{
  transcode_frame *frame;
  int samples;
  int ret;

  if (streaming_not_supported)
    {
      DPRINTF(E_LOG, L_STREAMING, "Streaming unsupported\n");
      return -1;
    }

  if (streaming_quality_in.channels == 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "Streaming quality is zero (%d/%d/%d)\n", streaming_quality_in.sample_rate, streaming_quality_in.bits_per_sample, streaming_quality_in.channels);
      return -1;
    }

  samples = BTOS(size, streaming_quality_in.bits_per_sample, streaming_quality_in.channels);

  frame = transcode_frame_new(buffer, size, samples, &streaming_quality_in);
  if (!frame)
    {
      DPRINTF(E_LOG, L_STREAMING, "Could not convert raw PCM to frame\n");
      return -1;
    }

  ret = transcode_encode(streaming_encoded_data, streaming_encode_ctx, frame, 0);
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
  unsigned titlelen;
  unsigned metalen;
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
streaming_icy_meta_splice(const uint8_t *data, size_t datalen, off_t offset, size_t *len)
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
streaming_player_status_update(void)
{
  struct db_queue_item *queue_item;
  uint32_t prev_id;

  prev_id = streaming_player_status.id;
  player_get_status(&streaming_player_status);

  if (prev_id == streaming_player_status.id || !streaming_icy_clients)
    {
      return;
    }

  queue_item = db_queue_fetch_byfileid(streaming_player_status.id);
  if (!queue_item)
    {
      streaming_icy_title[0] = '\0';
      return;
    }

  snprintf(streaming_icy_title, sizeof(streaming_icy_title), "%s - %s", queue_item->title, queue_item->artist);
  free_queue_item(queue_item, 0);
}

static void
streaming_send_cb(evutil_socket_t fd, short event, void *arg)
{
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

	  if (streaming_player_changed)
	    {
	      streaming_player_changed = 0;
	      streaming_player_status_update();
	    }

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
	  streaming_player_status_update();
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
  pthread_mutex_lock(&streaming_sessions_lck);
  for (session = streaming_sessions; session; session = session->next)
    {
      // Does this session want ICY meta data and is it time to send?
      count = session->bytes_sent+len;
      if (session->require_icy && count > streaming_icy_metaint)
	{
	  overflow = count%streaming_icy_metaint;
	  buf = evbuffer_pullup(streaming_encoded_data, -1);

	  // DPRINTF(E_DBG, L_STREAMING, "session=%x sent=%ld len=%ld overflow=%ld\n", session, session->bytes_sent, len, overflow);

	  // Splice the 'icy title' in with the encoded audio data
	  splice_len = 0;
	  splice_buf = streaming_icy_meta_splice(buf, len, len-overflow, &splice_len);

	  evbuffer_add(evbuf, splice_buf, splice_len);

	  free(splice_buf);
	  splice_buf = NULL;

	  evhttp_send_reply_chunk(session->req, evbuf);

	  if (session->next == NULL)
	    {
	      // We're the last session, drop the contents of the encoded buffer
	      evbuffer_drain(streaming_encoded_data, len);
	    }
	  session->bytes_sent = overflow;
	}
      else
	{
	  if (session->next)
	    {
	      buf = evbuffer_pullup(streaming_encoded_data, -1);
	      evbuffer_add(evbuf, buf, len);
	      evhttp_send_reply_chunk(session->req, evbuf);
	    }
	  else
	    {
	      evhttp_send_reply_chunk(session->req, streaming_encoded_data);
	    }
	  session->bytes_sent += len;
	}
    }
  pthread_mutex_unlock(&streaming_sessions_lck);

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

  // Explicit no-lock - let the write to pipes fail if during deinit
  if (!streaming_sessions)
    return;

  if (!quality_is_equal(&obuf->data[0].quality, &streaming_quality_in))
    {
      ret = write(streaming_meta[1], &obuf->data[0].quality, sizeof(struct media_quality));
      if (ret < 0)
	{
	  if (errno == EBADF)
	    DPRINTF(E_LOG, L_STREAMING, "streaming pipe already closed\n");
	  else
	    DPRINTF(E_LOG, L_STREAMING, "Error writing to streaming pipe: %s\n", strerror(errno));
	  return;
	}
    }

  ret = write(streaming_pipe[1], obuf->data[0].buffer, obuf->data[0].bufsize);
  if (ret < 0)
    {
      if (errno == EAGAIN)
	DPRINTF(E_WARN, L_STREAMING, "Streaming pipe full, skipping write\n");
      else
	{
	  if (errno == EBADF)
	    DPRINTF(E_LOG, L_STREAMING, "Streaming pipe already closed\n");
	  else
	    DPRINTF(E_LOG, L_STREAMING, "Error writing to streaming pipe: %s\n", strerror(errno));
	}
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
  const char *param;
  bool require_icy = false;
  char buf[9];

  if (streaming_not_supported)
    {
      DPRINTF(E_LOG, L_STREAMING, "Got MP3 streaming request, but cannot encode to MP3\n");

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return -1;
    }

  evcon = evhttp_request_get_connection(req);
  evhttp_connection_get_peer(evcon, &address, &port);
  param = evhttp_find_header( evhttp_request_get_input_headers(req), "Icy-MetaData");
  if (param && strcmp(param, "1") == 0)
    require_icy = true;

  DPRINTF(E_INFO, L_STREAMING, "Beginning mp3 streaming (with icy=%d, icy_metaint=%d) to %s:%d\n", require_icy, streaming_icy_metaint, address, (int)port);

  lib = cfg_getsec(cfg, "library");
  name = cfg_getstr(lib, "name");

  output_headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(output_headers, "Content-Type", "audio/mpeg");
  evhttp_add_header(output_headers, "Server", PACKAGE_NAME "/" VERSION);
  evhttp_add_header(output_headers, "Cache-Control", "no-cache");
  evhttp_add_header(output_headers, "Pragma", "no-cache");
  evhttp_add_header(output_headers, "Expires", "Mon, 31 Aug 2015 06:00:00 GMT");
  if (require_icy)
    {
      ++streaming_icy_clients;
      evhttp_add_header(output_headers, "icy-name", name);
      snprintf(buf, sizeof(buf)-1, "%d", streaming_icy_metaint);
      evhttp_add_header(output_headers, "icy-metaint", buf);
    }
  evhttp_add_header(output_headers, "Access-Control-Allow-Origin", "*");
  evhttp_add_header(output_headers, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");

  evhttp_send_reply_start(req, HTTP_OK, "OK");

  session = calloc(1, sizeof(struct streaming_session));
  if (!session)
    {
      DPRINTF(E_LOG, L_STREAMING, "Out of memory for streaming request\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  pthread_mutex_lock(&streaming_sessions_lck);

  if (!streaming_sessions)
    {
      event_add(streamingev, &streaming_silence_tv);
      event_add(metaev, NULL);
    }

  session->req = req;
  session->next = streaming_sessions;
  session->require_icy = require_icy;
  session->bytes_sent = 0;
  streaming_sessions = session;

  pthread_mutex_unlock(&streaming_sessions_lck);

  evhttp_connection_set_closecb(evcon, streaming_close_cb, session);

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
  cfg_t *cfgsec;
  int val;

  cfgsec = cfg_getsec(cfg, "streaming");

  val = cfg_getint(cfgsec, "sample_rate");
  // Validate against the variations of libmp3lame's supported sample rates: 32000/44100/48000
  if (val % 11025 > 0 && val % 12000 > 0 && val % 8000 > 0)
    DPRINTF(E_LOG, L_STREAMING, "Non standard streaming sample_rate=%d, defaulting\n", val);
  else
    streaming_quality_out.sample_rate = val;

  val = cfg_getint(cfgsec, "bit_rate");
  switch (val)
  {
    case  64:
    case  96:
    case 128:
    case 192:
    case 320:
      streaming_quality_out.bit_rate = val*1000;
      break;

    default:
      DPRINTF(E_LOG, L_STREAMING, "Unsuppported streaming bit_rate=%d, supports: 64/96/128/192/320, defaulting\n", val);
  }

  DPRINTF(E_INFO, L_STREAMING, "Streaming quality: %d/%d/%d @ %dkbps\n", streaming_quality_out.sample_rate, streaming_quality_out.bits_per_sample, streaming_quality_out.channels, streaming_quality_out.bit_rate/1000);

  val = cfg_getint(cfgsec, "icy_metaint");
  // Too low a value forces server to send more meta than data
  if (val >= 4096 && val <= 131072)
    streaming_icy_metaint = val;
  else
    DPRINTF(E_INFO, L_STREAMING, "Unsupported icy_metaint=%d, supported range: 4096..131072, defaulting to %d\n", val, streaming_icy_metaint);

  ret = mutex_init(&streaming_sessions_lck);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_STREAMING, "Could not initialize mutex (%d): %s\n", ret, strerror(ret));
      goto error;
    }

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

  streaming_icy_clients = 0;

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
  streaming_end();

  event_free(metaev);
  event_free(streamingev);
  streamingev = NULL;

  listener_remove(player_change_cb);

  close(streaming_pipe[0]);
  close(streaming_pipe[1]);
  close(streaming_meta[0]);
  close(streaming_meta[1]);

  transcode_encode_cleanup(&streaming_encode_ctx);
  evbuffer_free(streaming_encoded_data);

  pthread_mutex_destroy(&streaming_sessions_lck);
}
