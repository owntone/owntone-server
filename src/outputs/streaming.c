/*
 * Copyright (C) 2023 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <uninorm.h>
#include <fcntl.h>

#include "streaming.h"
#include "outputs.h"
#include "misc.h"
#include "worker.h"
#include "transcode.h"
#include "logger.h"

/* About
 *
 * This output takes the writes from the player thread, gives them to a worker
 * thread for mp3 encoding, and then the mp3 is written to a fd for the httpd
 * request handler to read and pass to clients. If there is no writing from the
 * player, but there are clients, it instead writes silence to the fd.
 */

// How many times per second we send silence when player is idle (to prevent
// client from hanging up). This value matches the player tick interval.
#define SILENCE_TICKS_PER_SEC 100

// The wanted structure represents a particular format and quality that should
// be produced for one or more sessions. A pipe pair is created for each session
// for the i/o.
#define WANTED_PIPES_MAX 8

struct pipepair
{
  int writefd;
  int readfd;
};

struct streaming_wanted
{
  int refcount;
  struct pipepair pipes[WANTED_PIPES_MAX];

  enum streaming_format format;
  struct media_quality quality_in;
  struct media_quality quality_out;

  struct encode_ctx *xcode_ctx;
  struct evbuffer *encoded_data;

  struct streaming_wanted *next;
};

struct streaming_ctx
{
  struct streaming_wanted *wanted;
  struct event *silenceev;
  struct timeval silencetv;
  struct media_quality last_quality;
};

struct encode_cmdarg
{
  uint8_t *buf;
  size_t bufsize;
  int samples;
  struct media_quality quality;
};

static pthread_mutex_t streaming_wanted_lck;
static struct streaming_ctx streaming =
{
  .silencetv = { 0, (1000000 / SILENCE_TICKS_PER_SEC) },
};

extern struct event_base *evbase_player;


/* ------------------------------- Helpers ---------------------------------- */

static int
pipe_open(struct pipepair *pipe)
{
  int fd[2];
  int ret;

#ifdef HAVE_PIPE2
  ret = pipe2(fd, O_CLOEXEC | O_NONBLOCK);
#else
  if ( pipe(fd) < 0 ||
       fcntl(fd[0], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 ||
       fcntl(fd[1], F_SETFL, O_CLOEXEC | O_NONBLOCK) < 0 )
    ret = -1;
  else
    ret = 0;
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "Could not create pipe: %s\n", strerror(errno));
      return -1;
    }

  pipe->writefd = fd[1];
  pipe->readfd = fd[0];
  return 0;
}

static void
pipe_close(struct pipepair *pipe)
{
  if (pipe->readfd >= 0)
    close(pipe->readfd);
  if (pipe->writefd >= 0)
    close(pipe->writefd);

  pipe->writefd = -1;
  pipe->readfd = -1;
}

static void
wanted_free(struct streaming_wanted *w)
{
  if (!w)
    return;

  for (int i = 0; i < WANTED_PIPES_MAX; i++)
    pipe_close(&w->pipes[i]);

  transcode_encode_cleanup(&w->xcode_ctx);
  evbuffer_free(w->encoded_data);
  free(w);
}

static struct streaming_wanted *
wanted_new(enum streaming_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  CHECK_NULL(L_STREAMING, w = calloc(1, sizeof(struct streaming_wanted)));
  CHECK_NULL(L_STREAMING, w->encoded_data = evbuffer_new());

  w->quality_out = quality;
  w->format = format;

  for (int i = 0; i < WANTED_PIPES_MAX; i++)
    {
      w->pipes[i].writefd = -1;
      w->pipes[i].readfd = -1;
    }

  return w;
}

static void
wanted_remove(struct streaming_wanted **wanted, struct streaming_wanted *remove)
{
  struct streaming_wanted *prev = NULL;
  struct streaming_wanted *w;

  for (w = *wanted; w; w = w->next)
    {
      if (w == remove)
	break;

      prev = w;
    }

  if (!w)
    return;

  if (!prev)
    *wanted = remove->next;
  else
    prev->next = remove->next;

  wanted_free(remove);
}

static struct streaming_wanted *
wanted_add(struct streaming_wanted **wanted, enum streaming_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  w = wanted_new(format, quality);
  w->next = *wanted;
  *wanted = w;

  return w;
}

static struct streaming_wanted *
wanted_find_byformat(struct streaming_wanted *wanted, enum streaming_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  for (w = wanted; w; w = w->next)
    {
      if (w->format == format && quality_is_equal(&w->quality_out, &quality))
	return w;
    }

  return NULL;
}

static struct streaming_wanted *
wanted_find_byreadfd(struct streaming_wanted *wanted, int readfd)
{
  struct streaming_wanted *w;
  int i;

  for (w = wanted; w; w = w->next)
    for (i = 0; i < WANTED_PIPES_MAX; i++)
      {
	if (w->pipes[i].readfd == readfd)
	  return w;
      }

  return NULL;
}

static int
wanted_session_add(struct pipepair *pipe, struct streaming_wanted *w)
{
  int ret;
  int i;

  for (i = 0; i < WANTED_PIPES_MAX; i++)
    {
      if (w->pipes[i].writefd != -1) // In use
	continue;

      ret = pipe_open(&w->pipes[i]);
      if (ret < 0)
	return -1;

      memcpy(pipe, &w->pipes[i], sizeof(struct pipepair));
      break;
    }

  if (i == WANTED_PIPES_MAX)
    {
      DPRINTF(E_LOG, L_STREAMING, "Cannot add streaming session, max pipe limit reached\n");
      return -1;
    }

  w->refcount++;
  DPRINTF(E_DBG, L_STREAMING, "Session register readfd %d, wanted->refcount=%d\n", pipe->readfd, w->refcount);
  return 0;
}


static void
wanted_session_remove(struct streaming_wanted *w, int readfd)
{
  int i;

  for (i = 0; i < WANTED_PIPES_MAX; i++)
    {
      if (w->pipes[i].readfd != readfd)
	continue;

      pipe_close(&w->pipes[i]);
      break;
    }

  if (i == WANTED_PIPES_MAX)
    {
      DPRINTF(E_LOG, L_STREAMING, "Cannot remove streaming session, readfd %d not found\n", readfd);
      return;
    }

  w->refcount--;
  DPRINTF(E_DBG, L_STREAMING, "Session deregister readfd %d, wanted->refcount=%d\n", readfd, w->refcount);
}


/* ----------------------------- Thread: Worker ----------------------------- */

static int
encode_reset(struct streaming_wanted *w, struct media_quality quality_in)
{
  struct media_quality quality_out = w->quality_out;
  struct decode_ctx *decode_ctx = NULL;

  transcode_encode_cleanup(&w->xcode_ctx);

  if (quality_in.bits_per_sample == 16)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM16, &quality_in);
  else if (quality_in.bits_per_sample == 24)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM24, &quality_in);
  else if (quality_in.bits_per_sample == 32)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM32, &quality_in);

  if (!decode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Error setting up decoder for input quality sr %d, bps %d, ch %d, cannot MP3 encode\n",
	quality_in.sample_rate, quality_in.bits_per_sample, quality_in.channels);
      goto error;
    }

  w->quality_in = quality_in;
  w->xcode_ctx = transcode_encode_setup(XCODE_MP3, &quality_out, decode_ctx, NULL, 0, 0);
  if (!w->xcode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Error setting up encoder for output quality sr %d, bps %d, ch %d, cannot MP3 encode\n",
	quality_out.sample_rate, quality_out.bits_per_sample, quality_out.channels);
      goto error;
    }

  transcode_decode_cleanup(&decode_ctx);
  return 0;

 error:
  transcode_decode_cleanup(&decode_ctx);
  return -1;
}

static int
encode_frame(struct streaming_wanted *w, struct media_quality quality_in, transcode_frame *frame)
{
  int ret;

  if (!w->xcode_ctx || !quality_is_equal(&quality_in, &w->quality_in))
    {
      DPRINTF(E_DBG, L_STREAMING, "Resetting transcode context\n");
      if (encode_reset(w, quality_in) < 0)
	return -1;
    }

  ret = transcode_encode(w->encoded_data, w->xcode_ctx, frame, 0);
  if (ret < 0)
    {
      return -1;
    }

  return 0;
}

static void
encode_write(uint8_t *buf, size_t buflen, struct streaming_wanted *w, struct pipepair *pipe)
{
  int ret;

  if (pipe->writefd < 0)
    return;

  ret = write(pipe->writefd, buf, buflen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "Error writing to stream pipe %d (format %d): %s\n", pipe->writefd, w->format, strerror(errno));
      wanted_session_remove(w, pipe->readfd);
    }
}

static void
encode_data_cb(void *arg)
{
  struct encode_cmdarg *ctx = arg;
  transcode_frame *frame;
  struct streaming_wanted *w;
  struct streaming_wanted *next;
  uint8_t *buf;
  size_t len;
  int ret;
  int i;

  frame = transcode_frame_new(ctx->buf, ctx->bufsize, ctx->samples, &ctx->quality);
  if (!frame)
    {
      DPRINTF(E_LOG, L_STREAMING, "Could not convert raw PCM to frame\n");
      goto out;
    }

  pthread_mutex_lock(&streaming_wanted_lck);
  for (w = streaming.wanted; w; w = next)
    {
      next = w->next;
      ret = encode_frame(w, ctx->quality, frame);
      if (ret < 0)
	wanted_remove(&streaming.wanted, w); // This will close all the fds, so readers get an error

      len = evbuffer_get_length(w->encoded_data);
      if (len == 0)
	continue;

      buf = evbuffer_pullup(w->encoded_data, -1);

      for (i = 0; i < WANTED_PIPES_MAX; i++)
	encode_write(buf, len, w, &w->pipes[i]);

      evbuffer_drain(w->encoded_data, -1);

      if (w->refcount == 0)
	wanted_remove(&streaming.wanted, w);
    }
  pthread_mutex_unlock(&streaming_wanted_lck);

 out:
  transcode_frame_free(frame);
  free(ctx->buf);
}


/* ----------------------------- Thread: Player ----------------------------- */

static void
encode_worker_invoke(uint8_t *buf, size_t bufsize, int samples, struct media_quality quality)
{
  struct encode_cmdarg ctx;

  if (quality.channels == 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "Streaming quality is zero (%d/%d/%d)\n",
	quality.sample_rate, quality.bits_per_sample, quality.channels);
      return;
    }

  ctx.buf = buf;
  ctx.bufsize = bufsize;
  ctx.samples = samples;
  ctx.quality = quality;

  worker_execute(encode_data_cb, &ctx, sizeof(struct encode_cmdarg), 0);
}

static void
streaming_write(struct output_buffer *obuf)
{
  uint8_t *rawbuf;

  if (!streaming.wanted)
    return;

  // Need to make a copy since it will be passed of to the async worker
  CHECK_NULL(L_STREAMING, rawbuf = malloc(obuf->data[0].bufsize));
  memcpy(rawbuf, obuf->data[0].buffer, obuf->data[0].bufsize);

  encode_worker_invoke(rawbuf, obuf->data[0].bufsize, obuf->data[0].samples, obuf->data[0].quality);

  streaming.last_quality = obuf->data[0].quality;

  // In case this is the last player write() we want to start streaming silence
  evtimer_add(streaming.silenceev, &streaming.silencetv);
}

static void
silenceev_cb(evutil_socket_t fd, short event, void *arg)
{
  uint8_t *rawbuf;
  size_t bufsize;
  int samples;

  // TODO what if everyone has disconnected? Check for streaming.wanted?

  samples = streaming.last_quality.sample_rate / SILENCE_TICKS_PER_SEC;
  bufsize = STOB(samples, streaming.last_quality.bits_per_sample, streaming.last_quality.channels);

  CHECK_NULL(L_STREAMING, rawbuf = calloc(1, bufsize));

  encode_worker_invoke(rawbuf, bufsize, samples, streaming.last_quality);

  evtimer_add(streaming.silenceev, &streaming.silencetv);
}

/* ----------------------------- Thread: httpd ------------------------------ */

int
streaming_session_register(enum streaming_format format, struct media_quality quality)
{
  struct streaming_wanted *w;
  struct pipepair pipe;
  int ret;

  pthread_mutex_lock(&streaming_wanted_lck);
  w = wanted_find_byformat(streaming.wanted, format, quality);
  if (!w)
    w = wanted_add(&streaming.wanted, format, quality);

  ret = wanted_session_add(&pipe, w);
  if (ret < 0)
    pipe.readfd = -1;

  pthread_mutex_unlock(&streaming_wanted_lck);

  return pipe.readfd;
}

void
streaming_session_deregister(int readfd)
{
  struct streaming_wanted *w;

  pthread_mutex_lock(&streaming_wanted_lck);
  w = wanted_find_byreadfd(streaming.wanted, readfd);
  if (!w)
    goto out;

  wanted_session_remove(w, readfd);

  if (w->refcount == 0)
    wanted_remove(&streaming.wanted, w);

 out:
  pthread_mutex_unlock(&streaming_wanted_lck);
}

static int
streaming_init(void)
{
  CHECK_NULL(L_STREAMING, streaming.silenceev = event_new(evbase_player, -1, 0, silenceev_cb, NULL));
  CHECK_ERR(L_STREAMING,  mutex_init(&streaming_wanted_lck));

  return 0;
}

static void
streaming_deinit(void)
{
  event_free(streaming.silenceev);
}

struct output_definition output_streaming =
{
  .name = "mp3 streaming",
  .type = OUTPUT_TYPE_STREAMING,
  .priority = 0,
  .disabled = 0,
  .init = streaming_init,
  .deinit = streaming_deinit,
  .write = streaming_write,
};
