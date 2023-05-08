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

#include "outputs.h"
#include "misc.h"
#include "worker.h"
#include "player.h"
#include "transcode.h"
#include "logger.h"
#include "db.h"

/* About
 *
 * This output takes the writes from the player thread, gives them to a worker
 * thread for mp3 encoding, and then the mp3 is written to a fd for the httpd
 * request handler to read and pass to clients. If there is no writing from the
 * player, but there are clients, it instead writes silence to the fd.
 */

// Seconds between sending a frame of silence when player is idle
// (to prevent client from hanging up)
#define STREAMING_SILENCE_INTERVAL 1

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
  int num_sessions; // for refcounting
  struct pipepair audio[WANTED_PIPES_MAX];
  struct pipepair metadata[WANTED_PIPES_MAX];

  enum player_format format;
  struct media_quality quality;

  struct evbuffer *audio_in;
  struct evbuffer *audio_out;
  struct encode_ctx *xcode_ctx;

  int nb_samples;
  uint8_t *frame_data;
  size_t frame_size;

  struct streaming_wanted *next;
};

struct streaming_ctx
{
  struct streaming_wanted *wanted;
  struct event *silenceev;
  struct timeval silencetv;
  struct media_quality last_quality;

  char title[4064]; // See STREAMING_ICY_METALEN_MAX in http_streaming.c

  // seqnum may wrap around so must be unsigned
  unsigned int seqnum;
  unsigned int seqnum_encode_next;
};

struct encode_cmdarg
{
  struct output_buffer *obuf;
  unsigned int seqnum;
};

static pthread_mutex_t streaming_wanted_lck;
static pthread_cond_t streaming_sequence_cond;

static struct streaming_ctx streaming =
{
  .silencetv = { STREAMING_SILENCE_INTERVAL, 0 },
};

extern struct event_base *evbase_player;


/* ------------------------------- Helpers ---------------------------------- */

static struct encode_ctx *
encoder_setup(enum player_format format, struct media_quality *quality)
{
  struct decode_ctx *decode_ctx = NULL;
  struct encode_ctx *encode_ctx = NULL;

  if (quality->bits_per_sample == 16)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM16, quality);
  else if (quality->bits_per_sample == 24)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM24, quality);
  else if (quality->bits_per_sample == 32)
    decode_ctx = transcode_decode_setup_raw(XCODE_PCM32, quality);

  if (!decode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Error setting up decoder for quality sr %d, bps %d, ch %d, cannot encode\n",
	quality->sample_rate, quality->bits_per_sample, quality->channels);
      goto out;
    }

  if (format == PLAYER_FORMAT_MP3)
    encode_ctx = transcode_encode_setup(XCODE_MP3, quality, decode_ctx, NULL, 0, 0);

  if (!encode_ctx)
    {
      DPRINTF(E_LOG, L_STREAMING, "Error setting up encoder for quality sr %d, bps %d, ch %d, cannot encode\n",
	quality->sample_rate, quality->bits_per_sample, quality->channels);
      goto out;
    }

 out:
  transcode_decode_cleanup(&decode_ctx);
  return encode_ctx;
}

static int
pipe_open(struct pipepair *p)
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

  p->writefd = fd[1];
  p->readfd = fd[0];
  return 0;
}

static void
pipe_close(struct pipepair *p)
{
  if (p->readfd >= 0)
    close(p->readfd);
  if (p->writefd >= 0)
    close(p->writefd);

  p->writefd = -1;
  p->readfd = -1;
}

static void
wanted_free(struct streaming_wanted *w)
{
  if (!w)
    return;

  for (int i = 0; i < WANTED_PIPES_MAX; i++)
    pipe_close(&w->audio[i]);
  for (int i = 0; i < WANTED_PIPES_MAX; i++)
    pipe_close(&w->metadata[i]);

  transcode_encode_cleanup(&w->xcode_ctx);
  evbuffer_free(w->audio_in);
  evbuffer_free(w->audio_out);
  free(w->frame_data);
  free(w);
}

static int
pipe_index_find_byreadfd(struct pipepair *p, int readfd)
{
  for (int i = 0; i < WANTED_PIPES_MAX; i++, p++)
    {
      if (p->readfd == readfd)
	return i;
    }

  return -1;
}

static struct streaming_wanted *
wanted_new(enum player_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  CHECK_NULL(L_STREAMING, w = calloc(1, sizeof(struct streaming_wanted)));
  CHECK_NULL(L_STREAMING, w->audio_in = evbuffer_new());
  CHECK_NULL(L_STREAMING, w->audio_out = evbuffer_new());

  w->xcode_ctx = encoder_setup(format, &quality);
  if (!w->xcode_ctx)
    goto error;

  w->format = format;
  w->quality = quality;
  w->nb_samples = transcode_encode_query(w->xcode_ctx, "samples_per_frame"); // 1152 for mp3
  w->frame_size = STOB(w->nb_samples, quality.bits_per_sample, quality.channels);

  CHECK_NULL(L_STREAMING, w->frame_data = malloc(w->frame_size));

  for (int i = 0; i < WANTED_PIPES_MAX; i++)
    {
      w->audio[i].writefd = -1;
      w->audio[i].readfd = -1;
      w->metadata[i].writefd = -1;
      w->metadata[i].readfd = -1;
    }

  return w;

 error:
  wanted_free(w);
  return NULL;
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
wanted_add(struct streaming_wanted **wanted, enum player_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  w = wanted_new(format, quality);
  w->next = *wanted;
  *wanted = w;

  return w;
}

static struct streaming_wanted *
wanted_find_byformat(struct streaming_wanted *wanted, enum player_format format, struct media_quality quality)
{
  struct streaming_wanted *w;

  for (w = wanted; w; w = w->next)
    {
      if (w->format == format && quality_is_equal(&w->quality, &quality))
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
    {
      i = pipe_index_find_byreadfd(w->audio, readfd);
      if (i != -1)
	return w;
    }

  return NULL;
}

static int
wanted_session_add(int *audiofd, int *metadatafd, struct streaming_wanted *w)
{
  int ret;
  int i;

  for (i = 0; i < WANTED_PIPES_MAX; i++)
    {
      if (w->audio[i].writefd != -1) // In use
	continue;

      ret = pipe_open(&w->audio[i]);
      if (ret < 0)
	return -1;

      ret = pipe_open(&w->metadata[i]);
      if (ret < 0)
	return -1;

      *audiofd = w->audio[i].readfd;
      *metadatafd = w->metadata[i].readfd;
      break;
    }

  if (i == WANTED_PIPES_MAX)
    {
      DPRINTF(E_LOG, L_STREAMING, "Cannot add streaming session, max pipe limit reached\n");
      return -1;
    }

  w->num_sessions++;
  DPRINTF(E_DBG, L_STREAMING, "Session register audiofd %d, metadatafd %d, wanted->num_sessions=%d\n", *audiofd, *metadatafd, w->num_sessions);
  return 0;
}

static void
wanted_session_remove(struct streaming_wanted *w, int readfd)
{
  int i;

  i = pipe_index_find_byreadfd(w->audio, readfd);
  if (i < 0)
    {
      DPRINTF(E_LOG, L_STREAMING, "Cannot remove streaming session, readfd %d not found\n", readfd);
      return;
    }

  pipe_close(&w->audio[i]);
  pipe_close(&w->metadata[i]);

  w->num_sessions--;
  DPRINTF(E_DBG, L_STREAMING, "Session deregister readfd %d, wanted->num_sessions=%d\n", readfd, w->num_sessions);
}


/* ----------------------------- Thread: Worker ----------------------------- */

static int
encode_buffer(struct streaming_wanted *w, uint8_t *buf, size_t bufsize)
{
  ssize_t remaining_bytes;
  transcode_frame *frame = NULL;
  int ret;

  if (buf)
    {
      evbuffer_add(w->audio_in, buf, bufsize);
    }
  else
    {
      // buf being null is either a silence timeout or that we could't find the
      // subscripted quality. In both cases we encode silence.
      memset(w->frame_data, 0, w->frame_size);
      evbuffer_add(w->audio_in, w->frame_data, w->frame_size);
    }

  remaining_bytes = evbuffer_get_length(w->audio_in);

  // Read and encode from 'audio_in' in chunks of 'frame_size' bytes
  while (remaining_bytes > w->frame_size)
    {
      ret = evbuffer_remove(w->audio_in, w->frame_data, w->frame_size);
      if (ret != w->frame_size)
	{
	  DPRINTF(E_LOG, L_STREAMING, "Bug! Couldn't read a frame of %zu bytes (format %d)\n", w->frame_size, w->format);
	  goto error;
	}

      remaining_bytes -= w->frame_size;

      frame = transcode_frame_new(w->frame_data, w->frame_size, w->nb_samples, &w->quality);
      if (!frame)
	{
	  DPRINTF(E_LOG, L_STREAMING, "Could not convert raw PCM to frame (format %d)\n", w->format);
	  goto error;
	}

      ret = transcode_encode(w->audio_out, w->xcode_ctx, frame, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_STREAMING, "Encoding error (format %d)\n", w->format);
	  goto error;
	}

      transcode_frame_free(frame);
    }

  return 0;

 error:
  transcode_frame_free(frame);
  return -1;
}

static void
encode_and_write(int *failed_pipe_readfd, struct streaming_wanted *w, struct output_buffer *obuf)
{
  uint8_t *buf;
  size_t bufsize;
  size_t len;
  int ret;
  int i;

  for (i = 0, buf = NULL, bufsize = 0; obuf && obuf->data[i].buffer; i++)
    {
      if (!quality_is_equal(&obuf->data[i].quality, &w->quality))
	continue;

      buf = obuf->data[i].buffer;
      bufsize = obuf->data[i].bufsize;
    }

  // If encoding fails we should kill the sessions, which for thread safety
  // and to avoid deadlocks has to be done later with player_streaming_deregister()
  ret = encode_buffer(w, buf, bufsize);
  if (ret < 0)
    {
      for (i = 0; i < WANTED_PIPES_MAX; i++)
	{
	  if (w->audio[i].writefd != -1)
	    *failed_pipe_readfd = w->audio[i].readfd;
	}

      return;
    }

  len = evbuffer_get_length(w->audio_out);
  if (len == 0)
    {
      return;
    }

  buf = evbuffer_pullup(w->audio_out, -1);
  for (i = 0; i < WANTED_PIPES_MAX; i++)
    {
      if (w->audio[i].writefd == -1)
	continue;

      ret = write(w->audio[i].writefd, buf, len);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_STREAMING, "Error writing to stream pipe %d (format %d): %s\n", w->audio[i].writefd, w->format, strerror(errno));
	  *failed_pipe_readfd = w->audio[i].readfd;
	}
    }

  evbuffer_drain(w->audio_out, -1);
}

static void
encode_data_cb(void *arg)
{
  struct encode_cmdarg *ctx = arg;
  struct output_buffer *obuf = ctx->obuf;
  struct streaming_wanted *w;
  int failed_pipe_readfd = -1;

  pthread_mutex_lock(&streaming_wanted_lck);

  // To make sure we process the frames in order
  while (ctx->seqnum != streaming.seqnum_encode_next)
    pthread_cond_wait(&streaming_sequence_cond, &streaming_wanted_lck);

  for (w = streaming.wanted; w; w = w->next)
    encode_and_write(&failed_pipe_readfd, w, obuf);

  streaming.seqnum_encode_next++;
  pthread_cond_broadcast(&streaming_sequence_cond);
  pthread_mutex_unlock(&streaming_wanted_lck);

  outputs_buffer_free(ctx->obuf);

  // We have to do this after letting go of the lock or we will deadlock. This
  // unfortunate method means we can only fail one session (pipe) each pass.
  if (failed_pipe_readfd >= 0)
    player_streaming_deregister(failed_pipe_readfd);
}

static void
metadata_write(struct streaming_wanted *w, int readfd, const char *metadata)
{
  size_t metadata_size;
  int i;
  int ret;

  for (i = 0; i < WANTED_PIPES_MAX; i++)
    {
      if (w->metadata[i].writefd == -1)
	continue;
      if (readfd >= 0 && w->metadata[i].readfd != readfd)
	continue;

      metadata_size = strlen(metadata) + 1;
      ret = write(w->metadata[i].writefd, metadata, metadata_size);
      if (ret < 0)
	DPRINTF(E_WARN, L_STREAMING, "Error writing metadata '%s' to fd %d\n", metadata, w->metadata[i].writefd);
    }
}

static void
metadata_startup_cb(void *arg)
{
  int *metadata_fd = arg;
  struct streaming_wanted *w;

  pthread_mutex_lock(&streaming_wanted_lck);
  for (w = streaming.wanted; w; w = w->next)
    metadata_write(w, *metadata_fd, streaming.title);
  pthread_mutex_unlock(&streaming_wanted_lck);
}

static void *
streaming_metadata_prepare(struct output_metadata *metadata)
{
  struct db_queue_item *queue_item;
  struct streaming_wanted *w;

  queue_item = db_queue_fetch_byitemid(metadata->item_id);
  if (!queue_item)
    {
      DPRINTF(E_LOG, L_STREAMING, "Could not fetch queue item id %d for new metadata\n", metadata->item_id);
      return NULL;
    }

  pthread_mutex_lock(&streaming_wanted_lck);
  // Save it here, we might need it later if a new session starts up
  snprintf(streaming.title, sizeof(streaming.title), "%s - %s", queue_item->title, queue_item->artist);

  for (w = streaming.wanted; w; w = w->next)
    metadata_write(w, -1, streaming.title);
  pthread_mutex_unlock(&streaming_wanted_lck);

  free_queue_item(queue_item, 0);
  return NULL;
}


/* ----------------------------- Thread: Player ----------------------------- */

static void
streaming_write(struct output_buffer *obuf)
{
  struct encode_cmdarg ctx;

  // No lock since this is just an early exit, it doesn't need to be accurate
  if (!streaming.wanted)
    return;

  // We don't want to block the player, so we can't lock to access
  // streaming.wanted and find which qualities we need. So we just copy it all
  // and pass it to a worker thread that can lock and check what is wanted, and
  // also can encode without holding the player.
  ctx.obuf = outputs_buffer_copy(obuf);
  ctx.seqnum = streaming.seqnum;

  streaming.seqnum++;

  worker_execute(encode_data_cb, &ctx, sizeof(struct encode_cmdarg), 0);

  // In case this is the last player write() we want to start streaming silence
  evtimer_add(streaming.silenceev, &streaming.silencetv);
}

static void
silenceev_cb(evutil_socket_t fd, short event, void *arg)
{
  streaming_write(NULL);
}

static void
streaming_metadata_send(struct output_metadata *metadata)
{
  // Nothing to do, metadata_prepare() did all we needed in a worker thread
}

// Since this is streaming and there is no actual device, we will be called with
// a dummy/ad hoc device that's not part in the speaker list. We don't need to
// make any callback so can ignore callback_id.
static int
streaming_start(struct output_device *device, int callback_id)
{
  struct streaming_wanted *w;
  int ret;

  pthread_mutex_lock(&streaming_wanted_lck);
  w = wanted_find_byformat(streaming.wanted, device->format, device->quality);
  if (!w)
    w = wanted_add(&streaming.wanted, device->format, device->quality);
  ret = wanted_session_add(&device->audio_fd, &device->metadata_fd, w);
  if (ret < 0)
    goto error;
  pthread_mutex_unlock(&streaming_wanted_lck);

  worker_execute(metadata_startup_cb, &(device->metadata_fd), sizeof(device->metadata_fd), 0);

  outputs_quality_subscribe(&device->quality);

  device->id = device->audio_fd;
  return 0;

 error:
  if (w->num_sessions == 0)
    wanted_remove(&streaming.wanted, w);
  pthread_mutex_unlock(&streaming_wanted_lck);
  return -1;
}

// Since this is streaming and there is no actual device, we will be called with
// a dummy/ad hoc device that's not part in the speaker list. We don't need to
// make any callback so can ignore callback_id.
static int
streaming_stop(struct output_device *device, int callback_id)
{
  struct streaming_wanted *w;

  pthread_mutex_lock(&streaming_wanted_lck);
  w = wanted_find_byreadfd(streaming.wanted, device->id);
  if (!w)
    goto error;
  device->quality = w->quality;
  wanted_session_remove(w, device->id);
  if (w->num_sessions == 0)
    wanted_remove(&streaming.wanted, w);
  pthread_mutex_unlock(&streaming_wanted_lck);

  outputs_quality_unsubscribe(&device->quality);
  return 0;

 error:
  pthread_mutex_unlock(&streaming_wanted_lck);
  return -1;
}

static int
streaming_init(void)
{
  CHECK_NULL(L_STREAMING, streaming.silenceev = event_new(evbase_player, -1, 0, silenceev_cb, NULL));
  CHECK_ERR(L_STREAMING, mutex_init(&streaming_wanted_lck));
  CHECK_ERR(L_STREAMING, pthread_cond_init(&streaming_sequence_cond, NULL));

  return 0;
}

static void
streaming_deinit(void)
{
  event_free(streaming.silenceev);
}


struct output_definition output_streaming =
{
  .name = "streaming",
  .type = OUTPUT_TYPE_STREAMING,
  .priority = 0,
  .disabled = 0,
  .init = streaming_init,
  .deinit = streaming_deinit,
  .write = streaming_write,
  .device_start = streaming_start,
  .device_probe = streaming_start,
  .device_stop = streaming_stop,
  .metadata_prepare = streaming_metadata_prepare,
  .metadata_send = streaming_metadata_send,
};
