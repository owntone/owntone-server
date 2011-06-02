/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 *
 * Adapted from mt-daapd:
 * Copyright (C) 2006-2007 Ron Pedde <ron@pedde.com>
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
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>

#if defined(__linux__) || defined(__GLIBC__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/endian.h>
#endif

#include <event.h>
#include "evhttp/evhttp.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "logger.h"
#include "conffile.h"
#include "db.h"
#include "transcode.h"


#define XCODE_BUFFER_SIZE ((AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2)


struct transcode_ctx {
  AVFormatContext *fmtctx;
  /* Audio stream */
  int astream;
  AVCodecContext *acodec; /* pCodecCtx */
  AVCodec *adecoder; /* pCodec */
  AVPacket apacket;
  AVPacket apacket2;
  int16_t *abuffer;

  /* Resampling */
  int need_resample;
  int input_size;
  ReSampleContext *resample_ctx;
  int16_t *re_abuffer;

  off_t offset;

  uint32_t duration;
  uint64_t samples;

  /* WAV header */
  int wavhdr;
  uint8_t header[44];
};


static char *default_codecs = "mpeg,wav";
static char *roku_codecs = "mpeg,mp4a,wma,wav";
static char *itunes_codecs = "mpeg,mp4a,mp4v,alac,wav";


static inline void
add_le16(uint8_t *dst, uint16_t val)
{
  dst[0] = val & 0xff;
  dst[1] = (val >> 8) & 0xff;
}

static inline void
add_le32(uint8_t *dst, uint32_t val)
{
  dst[0] = val & 0xff;
  dst[1] = (val >> 8) & 0xff;
  dst[2] = (val >> 16) & 0xff;
  dst[3] = (val >> 24) & 0xff;
}

static void
make_wav_header(struct transcode_ctx *ctx, off_t *est_size)
{
  uint32_t wav_len;
  int duration;

  if (ctx->duration)
    duration = ctx->duration;
  else
    duration = 3 * 60 * 1000; /* 3 minutes, in ms */

  if (ctx->samples && !ctx->need_resample)
    wav_len = 2 * 2 * ctx->samples;
  else
    wav_len = 2 * 2 * 44100 * (duration / 1000);

  *est_size = wav_len + sizeof(ctx->header);

  memcpy(ctx->header, "RIFF", 4);
  add_le32(ctx->header + 4, 36 + wav_len);
  memcpy(ctx->header + 8, "WAVEfmt ", 8);
  add_le32(ctx->header + 16, 16);
  add_le16(ctx->header + 20, 1);
  add_le16(ctx->header + 22, 2);               /* channels */
  add_le32(ctx->header + 24, 44100);           /* samplerate */
  add_le32(ctx->header + 28, 44100 * 2 * 2);   /* byte rate */
  add_le16(ctx->header + 32, 2 * 2);           /* block align */
  add_le16(ctx->header + 34, 16);              /* bits per sample */
  memcpy(ctx->header + 36, "data", 4);
  add_le32(ctx->header + 40, wav_len);
}


int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted)
{
  int16_t *buf;
  int buflen;
  int processed;
  int used;
  int stop;
  int ret;
#if BYTE_ORDER == BIG_ENDIAN
  int i;
#endif

  processed = 0;

  if (ctx->wavhdr && (ctx->offset == 0))
    {
      evbuffer_add(evbuf, ctx->header, sizeof(ctx->header));
      processed += sizeof(ctx->header);
      ctx->offset += sizeof(ctx->header);
    }

  stop = 0;
  while ((processed < wanted) && !stop)
    {
      /* Decode data */
      while (ctx->apacket2.size > 0)
	{
	  buflen = XCODE_BUFFER_SIZE;

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
	  /* FFmpeg 0.6 */
	  used = avcodec_decode_audio3(ctx->acodec,
				       ctx->abuffer, &buflen,
				       &ctx->apacket2);
#else
	  used = avcodec_decode_audio2(ctx->acodec,
				       ctx->abuffer, &buflen,
				       ctx->apacket2.data, ctx->apacket2.size);
#endif

	  if (used < 0)
	    {
	      /* Something happened, skip this packet */
	      ctx->apacket2.size = 0;
	      break;
	    }

	  ctx->apacket2.data += used;
	  ctx->apacket2.size -= used;

	  /* No frame decoded this time around */
	  if (buflen == 0)
	    continue;

	  if (ctx->need_resample)
	    {
	      buflen = audio_resample(ctx->resample_ctx, ctx->re_abuffer, ctx->abuffer, buflen / ctx->input_size);

	      if (buflen == 0)
		{
		  DPRINTF(E_WARN, L_XCODE, "Resample returned no samples!\n");
		  continue;
		}

	      buflen = buflen * 2 * 2; /* 16bit samples, 2 channels */
	      buf = ctx->re_abuffer;
	    }
	  else
	    buf = ctx->abuffer;

#if BYTE_ORDER == BIG_ENDIAN
	  /* swap buffer, LE16 */
	  for (i = 0; i < (buflen / 2); i++)
	    {
	      buf[i] = htole16(buf[i]);
	    }
#endif

	  ret = evbuffer_add(evbuf, buf, buflen);
	  if (ret != 0)
	    {
	      DPRINTF(E_WARN, L_XCODE, "Could not copy WAV data to buffer\n");

	      return -1;
	    }

	  processed += buflen;
	}

      /* Read more data */
      do
	{
	  if (ctx->apacket.data)
	    av_free_packet(&ctx->apacket);

	  ret = av_read_frame(ctx->fmtctx, &ctx->apacket);
	  if (ret < 0)
	    {
	      DPRINTF(E_WARN, L_XCODE, "Could not read more data\n");

	      stop = 1;
	      break;
	    }
	}
      while (ctx->apacket.stream_index != ctx->astream);

      /* Copy apacket and do not mess with it */
      ctx->apacket2 = ctx->apacket;
    }

  ctx->offset += processed;

  return processed;
}

int
transcode_seek(struct transcode_ctx *ctx, int ms)
{
  int64_t start_time;
  int64_t target_pts;
  int64_t got_pts;
  int got_ms;
  int flags;
  int ret;

  start_time = ctx->fmtctx->streams[ctx->astream]->start_time;

  target_pts = ms;
  target_pts = target_pts * AV_TIME_BASE / 1000;
  target_pts = av_rescale_q(target_pts, AV_TIME_BASE_Q, ctx->fmtctx->streams[ctx->astream]->time_base);

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    target_pts += start_time;

  ret = av_seek_frame(ctx->fmtctx, ctx->astream, target_pts, AVSEEK_FLAG_BACKWARD);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not seek into stream: %s\n", strerror(AVUNERROR(ret)));

      return -1;
    }

  avcodec_flush_buffers(ctx->acodec);

#if LIBAVCODEC_VERSION_MAJOR >= 53
  ctx->acodec->skip_frame = AVDISCARD_NONREF;
#else
  ctx->acodec->hurry_up = 1;
#endif

  flags = 0;
  while (1)
    {
      if (ctx->apacket.data)
	av_free_packet(&ctx->apacket);

      ret = av_read_frame(ctx->fmtctx, &ctx->apacket);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not read more data while seeking\n");

	  flags = 1;
	  break;
	}

      if (ctx->apacket.stream_index != ctx->astream)
	continue;

      /* Need a pts to return the real position */
      if (ctx->apacket.pts == AV_NOPTS_VALUE)
	continue;

      break;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 53
  ctx->acodec->skip_frame = AVDISCARD_DEFAULT;
#else
  ctx->acodec->hurry_up = 0;
#endif

  /* Error while reading frame above */
  if (flags)
    return -1;

  /* Copy apacket and do not mess with it */
  ctx->apacket2 = ctx->apacket;

  /* Compute position in ms from pts */
  got_pts = ctx->apacket.pts;

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    got_pts -= start_time;

  got_pts = av_rescale_q(got_pts, ctx->fmtctx->streams[ctx->astream]->time_base, AV_TIME_BASE_Q);
  got_ms = got_pts / (AV_TIME_BASE / 1000);

  DPRINTF(E_DBG, L_XCODE, "Seek wanted %d ms, got %d ms\n", ms, got_ms);

  return got_ms;
}


struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, off_t *est_size, int wavhdr)
{
  struct transcode_ctx *ctx;
  int i;
  int ret;

  ctx = (struct transcode_ctx *)malloc(sizeof(struct transcode_ctx));
  if (!ctx)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not allocate transcode context\n");

      return NULL;
    }
  memset(ctx, 0, sizeof(struct transcode_ctx));

  ret = av_open_input_file(&ctx->fmtctx, mfi->path, NULL, 0, NULL);
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not open file %s: %s\n", mfi->fname, strerror(AVUNERROR(ret)));

      free(ctx);
      return NULL;
    }

  ret = av_find_stream_info(ctx->fmtctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not find stream info: %s\n", strerror(AVUNERROR(ret)));

      goto setup_fail;
    }

  ctx->astream = -1;
  for (i = 0; i < ctx->fmtctx->nb_streams; i++)
    {
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 64)
      if (ctx->fmtctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
#else
      if (ctx->fmtctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
#endif
	{
	  ctx->astream = i;

	  break;
	}
    }

  if (ctx->astream < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "No audio stream found in file %s\n", mfi->fname);

      goto setup_fail;
    }

  ctx->acodec = ctx->fmtctx->streams[ctx->astream]->codec;

  ctx->adecoder = avcodec_find_decoder(ctx->acodec->codec_id);
  if (!ctx->adecoder)
    {
      DPRINTF(E_WARN, L_XCODE, "No suitable decoder found for codec\n");

      goto setup_fail;
    }

  if (ctx->adecoder->capabilities & CODEC_CAP_TRUNCATED)
    ctx->acodec->flags |= CODEC_FLAG_TRUNCATED;

  ret = avcodec_open(ctx->acodec, ctx->adecoder);
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not open codec: %s\n", strerror(AVUNERROR(ret)));

      goto setup_fail;
    }

  ctx->abuffer = (int16_t *)av_malloc(XCODE_BUFFER_SIZE);
  if (!ctx->abuffer)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not allocate transcode buffer\n");

      goto setup_fail_codec;
    }

  if ((ctx->acodec->sample_fmt != SAMPLE_FMT_S16)
      || (ctx->acodec->channels != 2)
      || (ctx->acodec->sample_rate != 44100))
    {
      DPRINTF(E_DBG, L_XCODE, "Setting up resampling (%d@%d)\n", ctx->acodec->channels, ctx->acodec->sample_rate);

      ctx->resample_ctx = av_audio_resample_init(2,              ctx->acodec->channels,
						 44100,          ctx->acodec->sample_rate,
						 SAMPLE_FMT_S16, ctx->acodec->sample_fmt,
						 16, 10, 0, 0.8);

      if (!ctx->resample_ctx)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not init resample from %d@%d to 2@44100\n", ctx->acodec->channels, ctx->acodec->sample_rate);

	  goto setup_fail_codec;
	}

      ctx->re_abuffer = (int16_t *)av_malloc(XCODE_BUFFER_SIZE * 2);
      if (!ctx->re_abuffer)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not allocate resample buffer\n");

	  audio_resample_close(ctx->resample_ctx);
	  goto setup_fail_codec;
	}

      ctx->need_resample = 1;
#if LIBAVCODEC_VERSION_MAJOR >= 53
      ctx->input_size = ctx->acodec->channels * av_get_bits_per_sample_fmt(ctx->acodec->sample_fmt) / 8;
#else
      ctx->input_size = ctx->acodec->channels * av_get_bits_per_sample_format(ctx->acodec->sample_fmt) / 8;
#endif
    }

  ctx->duration = mfi->song_length;
  ctx->samples = mfi->sample_count;
  ctx->wavhdr = wavhdr;

  if (wavhdr)
    make_wav_header(ctx, est_size);

  return ctx;

 setup_fail_codec:
  avcodec_close(ctx->acodec);

 setup_fail:
  av_close_input_file(ctx->fmtctx);
  free(ctx);

  return NULL;
}

void
transcode_cleanup(struct transcode_ctx *ctx)
{
  if (ctx->apacket.data)
    av_free_packet(&ctx->apacket);

  avcodec_close(ctx->acodec);
  av_close_input_file(ctx->fmtctx);

  av_free(ctx->abuffer);

  if (ctx->need_resample)
    {
      audio_resample_close(ctx->resample_ctx);
      av_free(ctx->re_abuffer);
    }

  free(ctx);
}


int
transcode_needed(struct evkeyvalq *headers, char *file_codectype)
{
  const char *client_codecs;
  const char *user_agent;
  char *codectype;
  cfg_t *lib;
  int size;
  int i;

  DPRINTF(E_DBG, L_XCODE, "Determining transcoding status for codectype %s\n", file_codectype);

  lib = cfg_getsec(cfg, "library");

  size = cfg_size(lib, "no_transcode");
  if (size > 0)
    {
      for (i = 0; i < size; i++)
	{
	  codectype = cfg_getnstr(lib, "no_transcode", i);

	  if (strcmp(file_codectype, codectype) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Codectype is in no_transcode\n");

	      return 0;
	    }
	}
    }

  size = cfg_size(lib, "force_transcode");
  if (size > 0)
    {
      for (i = 0; i < size; i++)
	{
	  codectype = cfg_getnstr(lib, "force_transcode", i);

	  if (strcmp(file_codectype, codectype) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Codectype is in force_transcode\n");

	      return 1;
	    }
	}
    }

  client_codecs = evhttp_find_header(headers, "Accept-Codecs");
  if (!client_codecs)
    {
      user_agent = evhttp_find_header(headers, "User-Agent");
      if (user_agent)
	{
	  DPRINTF(E_DBG, L_XCODE, "User-Agent: %s\n", user_agent);

	  if (strncmp(user_agent, "iTunes", strlen("iTunes")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is iTunes\n");

	      client_codecs = itunes_codecs;
	    }
	  else if (strncmp(user_agent, "QuickTime", strlen("QuickTime")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is QuickTime, using iTunes codecs\n");

	      client_codecs = itunes_codecs;
	    }
	  else if (strncmp(user_agent, "Front%20Row", strlen("Front%20Row")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is Front Row, using iTunes codecs\n");

	      client_codecs = itunes_codecs;
	    }
	  else if (strncmp(user_agent, "Remote", strlen("Remote")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is Remote, using iTunes codecs\n");

	      client_codecs = itunes_codecs;
	    }
	  else if (strncmp(user_agent, "Roku", strlen("Roku")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is a Roku device\n");

	      client_codecs = roku_codecs;
	    }
	  else if (strncmp(user_agent, "Hifidelio", strlen("Hifidelio")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is a Hifidelio device, allegedly cannot transcode\n");

	      /* Allegedly can't transcode for Hifidelio because their
	       * HTTP implementation doesn't honour Connection: close.
	       * At least, that's why mt-daapd didn't do it.
	       */
	      return 0;
	    }
	}
    }
  else
    DPRINTF(E_DBG, L_XCODE, "Client advertises codecs: %s\n", client_codecs);

  if (!client_codecs)
    {
      DPRINTF(E_DBG, L_XCODE, "Could not identify client, using default codectype set\n");

      client_codecs = default_codecs;
    }

  if (strstr(client_codecs, file_codectype))
    {
      DPRINTF(E_DBG, L_XCODE, "Codectype supported by client, no transcoding needed\n");
      return 0;
    }

  DPRINTF(E_DBG, L_XCODE, "Will transcode\n");

  return 1;
}
