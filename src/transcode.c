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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#if defined(HAVE_LIBSWRESAMPLE)
# include <libswresample/swresample.h>
#elif defined(HAVE_LIBAVRESAMPLE)
# include <libavutil/opt.h>
# include <libavresample/avresample.h>
#endif

#include "logger.h"
#include "conffile.h"
#include "db.h"
#include "transcode.h"

#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 56 || (LIBAVCODEC_VERSION_MAJOR == 55 && LIBAVCODEC_VERSION_MINOR >= 18)
# define XCODE_BUFFER_SIZE ((192000 * 3) / 2)
#else
# define XCODE_BUFFER_SIZE ((AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2)
#endif

/* Interval between ICY metadata checks for streams, in seconds */
#define METADATA_ICY_INTERVAL 5

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
#if defined(HAVE_LIBSWRESAMPLE)
  SwrContext *resample_ctx;
#elif defined(HAVE_LIBAVRESAMPLE)
  AVAudioResampleContext *resample_ctx;
#else
  ReSampleContext *resample_ctx;
  int input_size;
#endif
  int need_resample;
  int16_t *re_abuffer;

  off_t offset;

  uint32_t duration;
  uint64_t samples;
  uint32_t icy_hash;

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
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted, int *icy_timer)
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
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  AVFrame *frame = NULL;
  int got_frame;
  int out_samples;
#elif LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 35)
  AVFrame *frame = NULL;
  int got_frame;
#endif
#if defined(HAVE_LIBAVRESAMPLE)
  int out_linesize;
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
#if LIBAVCODEC_VERSION_MAJOR >= 56 || (LIBAVCODEC_VERSION_MAJOR == 55 && LIBAVCODEC_VERSION_MINOR >= 29)
	  got_frame = 0;

	  if (!frame)
	    {
	      frame = av_frame_alloc();
	      if (!frame)
		{
		  DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded frame\n");

		  return -1;
		}
	    }
	  else
            av_frame_unref(frame);

	  used = avcodec_decode_audio4(ctx->acodec,
				       frame, &got_frame,
				       &ctx->apacket2);
#elif LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 35)
	  got_frame = 0;

	  if (!frame)
	    {
	      frame = avcodec_alloc_frame();
	      if (!frame)
		{
		  DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded frame\n");

		  return -1;
		}
	    }
	  else
            avcodec_get_frame_defaults(frame);

	  used = avcodec_decode_audio4(ctx->acodec,
				       frame, &got_frame,
				       &ctx->apacket2);
#else
	  buflen = XCODE_BUFFER_SIZE;

	  used = avcodec_decode_audio3(ctx->acodec,
				       ctx->abuffer, &buflen,
				       &ctx->apacket2);
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
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
	  if (!got_frame)
	    continue;
#elif LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 35)
	  if (!got_frame)
	    continue;
	  else
	    {
	      /* This part is from the libav wrapper for avcodec_decode_audio3 */
	      int ch, plane_size;
	      int planar = av_sample_fmt_is_planar(ctx->acodec->sample_fmt);
	      int data_size = av_samples_get_buffer_size(&plane_size, ctx->acodec->channels, 
				frame->nb_samples, ctx->acodec->sample_fmt, 1);

	      if (XCODE_BUFFER_SIZE < data_size)
		{
		  DPRINTF(E_WARN, L_XCODE, "Output buffer too small for frame (%d < %d)\n", XCODE_BUFFER_SIZE, data_size);

		  continue;
		}

	      memcpy(ctx->abuffer, frame->extended_data[0], plane_size);

	      if (planar && ctx->acodec->channels > 1)
		{
		  uint8_t *out = ((uint8_t *)ctx->abuffer) + plane_size;
		  for (ch = 1; ch < ctx->acodec->channels; ch++)
		    {
		      memcpy(out, frame->extended_data[ch], plane_size);
		      out += plane_size;
		    }
		}
	      buflen = data_size;
	    }
#else
	  if (buflen == 0)
	    continue;
#endif

	  // TODO Use the AVFrame resampling API's - probably much safer and easier than the following mess
	  if (ctx->need_resample)
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
	    {
	      out_samples = 0;
# if defined(HAVE_LIBSWRESAMPLE)
	      out_samples = av_rescale_rnd(
		swr_get_delay(ctx->resample_ctx, ctx->acodec->sample_rate) + frame->nb_samples,
		44100,
		ctx->acodec->sample_rate,
		AV_ROUND_UP
	      );

	      out_samples = swr_convert(ctx->resample_ctx, (uint8_t **)&ctx->re_abuffer, out_samples,
	                                       (const uint8_t **)frame->data, frame->nb_samples);
# elif defined(HAVE_LIBAVRESAMPLE)
	      av_samples_get_buffer_size(&out_linesize, 2, frame->nb_samples, AV_SAMPLE_FMT_S16, 0);

	      out_samples = avresample_convert(ctx->resample_ctx, (uint8_t **)&ctx->re_abuffer, out_linesize, XCODE_BUFFER_SIZE,
	                                       (uint8_t **)frame->data, frame->linesize[0], frame->nb_samples);
# endif
	      if (out_samples < 0)
		{
		  DPRINTF(E_LOG, L_XCODE, "Resample returned no samples!\n");

		  return -1;
		}

	      buflen = out_samples * 2 * 2; /* 16bit samples, 2 channels */
	      buf = ctx->re_abuffer;
	    }
	  else
	    {
	      buf = (int16_t *)frame->data[0];
	      buflen = av_samples_get_buffer_size(NULL, ctx->acodec->channels, frame->nb_samples, ctx->acodec->sample_fmt, 1);
	    }
#else
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
#endif

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

#if LIBAVCODEC_VERSION_MAJOR >= 56 || (LIBAVCODEC_VERSION_MAJOR == 55 && LIBAVCODEC_VERSION_MINOR >= 29)
  if (frame)
    av_frame_free(&frame);
#elif LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  if (frame)
    avcodec_free_frame(&frame);
#elif LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 35)
  if (frame)
    av_free(frame);
#endif

  *icy_timer = (ctx->offset % (METADATA_ICY_INTERVAL * 2 * 2 * 44100) < processed);
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

int
transcode_setup(struct transcode_ctx **nctx, struct media_file_info *mfi, off_t *est_size, int wavhdr)
{
  AVDictionary *options;
  struct transcode_ctx *ctx;
  int ret;

  ctx = (struct transcode_ctx *)malloc(sizeof(struct transcode_ctx));
  if (!ctx)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not allocate transcode context\n");

      return -1;
    }
  memset(ctx, 0, sizeof(struct transcode_ctx));

  options = NULL;

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
# ifndef HAVE_FFMPEG
  // Without this, libav is slow to probe some internet streams, which leads to RAOP timeouts
  if (mfi->data_kind == DATA_KIND_URL)
    {
      ctx->fmtctx = avformat_alloc_context();
      ctx->fmtctx->probesize = 64000;
    }
# endif
  if (mfi->data_kind == DATA_KIND_URL)
    av_dict_set(&options, "icy", "1", 0);

  ret = avformat_open_input(&ctx->fmtctx, mfi->path, NULL, &options);

  if (options)
    av_dict_free(&options);
#else
  ret = av_open_input_file(&ctx->fmtctx, mfi->path, NULL, 0, NULL);
#endif
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not open file %s: %s\n", mfi->fname, strerror(AVUNERROR(ret)));

      free(ctx);
      return -1;
    }

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
  ret = avformat_find_stream_info(ctx->fmtctx, NULL);
#else
  ret = av_find_stream_info(ctx->fmtctx);
#endif
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not find stream info: %s\n", strerror(AVUNERROR(ret)));

      goto setup_fail;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 64)
  ctx->astream = av_find_best_stream(ctx->fmtctx, AVMEDIA_TYPE_AUDIO, -1, -1, &ctx->adecoder, 0);

  if (ctx->astream < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Did not find audio stream or suitable decoder for %s\n", mfi->fname);

      goto setup_fail;
    }
#else
  int i;
  ctx->astream = -1;
  for (i = 0; i < ctx->fmtctx->nb_streams; i++)
    {
      if (ctx->fmtctx->streams[i]->codec->codec_type == CODEC_TYPE_AUDIO)
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

  ctx->adecoder = avcodec_find_decoder(ctx->fmtctx->streams[ctx->astream]->codec->codec_id);
  if (!ctx->adecoder)
    {
      DPRINTF(E_WARN, L_XCODE, "No suitable decoder found for codec\n");

      goto setup_fail;
    }
#endif
  ctx->acodec = ctx->fmtctx->streams[ctx->astream]->codec;

  if (ctx->adecoder->capabilities & CODEC_CAP_TRUNCATED)
    ctx->acodec->flags |= CODEC_FLAG_TRUNCATED;

#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 6)
  ctx->acodec->request_sample_fmt = AV_SAMPLE_FMT_S16;
  ctx->acodec->request_channel_layout = AV_CH_LAYOUT_STEREO;

  ret = avcodec_open2(ctx->acodec, ctx->adecoder, NULL);
#else
  ret = avcodec_open(ctx->acodec, ctx->adecoder);
#endif
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

  ctx->need_resample = (ctx->acodec->sample_fmt != AV_SAMPLE_FMT_S16)
                       || (ctx->acodec->channels != 2)
                       || (ctx->acodec->sample_rate != 44100);

  if (ctx->need_resample)
    {
#if defined(HAVE_LIBSWRESAMPLE) || defined(HAVE_LIBAVRESAMPLE)
      if (!ctx->acodec->channel_layout)
	{
	  DPRINTF(E_DBG, L_XCODE, "Resample requires channel_layout, but none from ffmpeg. Setting to default.\n");

	  ctx->acodec->channel_layout = av_get_default_channel_layout(ctx->acodec->channels);
	}

      DPRINTF(E_DBG, L_XCODE, "Will resample, decoded stream is: %s, %d channels (layout %" PRIu64 "), %d Hz\n",
              av_get_sample_fmt_name(ctx->acodec->sample_fmt),  ctx->acodec->channels,
              ctx->acodec->channel_layout, ctx->acodec->sample_rate);
#endif
#if defined(HAVE_LIBSWRESAMPLE)
      ctx->resample_ctx = swr_alloc_set_opts(
	NULL,                 // we're allocating a new context
	AV_CH_LAYOUT_STEREO,  // out_ch_layout
	AV_SAMPLE_FMT_S16,    // out_sample_fmt
	44100,                // out_sample_rate
	ctx->acodec->channel_layout, // in_ch_layout
	ctx->acodec->sample_fmt,     // in_sample_fmt
	ctx->acodec->sample_rate,    // in_sample_rate
	0,                    // log_offset
	NULL);

      if (!ctx->resample_ctx)
	{
	  DPRINTF(E_LOG, L_XCODE, "Out of memory for resample context\n");

	  goto setup_fail_codec;
	}

      ret = swr_init(ctx->resample_ctx);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not open resample context\n");

	  swr_free(&ctx->resample_ctx);
	  goto setup_fail_codec;
	}

      ctx->re_abuffer = av_realloc(ctx->re_abuffer, XCODE_BUFFER_SIZE * 2);
      if (!ctx->re_abuffer)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not allocate resample buffer\n");

	  swr_free(&ctx->resample_ctx);
	  goto setup_fail_codec;
	}
#elif defined(HAVE_LIBAVRESAMPLE)
      ctx->resample_ctx = avresample_alloc_context();
      if (!ctx->resample_ctx)
	{
	  DPRINTF(E_LOG, L_XCODE, "Out of memory for resample context\n");

	  goto setup_fail_codec;
	}
      av_opt_set_int(ctx->resample_ctx, "in_sample_fmt",      ctx->acodec->sample_fmt, 0);
      av_opt_set_int(ctx->resample_ctx, "out_sample_fmt",     AV_SAMPLE_FMT_S16, 0);
      av_opt_set_int(ctx->resample_ctx, "in_channel_layout",  ctx->acodec->channel_layout, 0);
      av_opt_set_int(ctx->resample_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
      av_opt_set_int(ctx->resample_ctx, "in_sample_rate",     ctx->acodec->sample_rate, 0);
      av_opt_set_int(ctx->resample_ctx, "out_sample_rate",    44100, 0);

      ret = avresample_open(ctx->resample_ctx);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not open resample context\n");

	  avresample_free(&ctx->resample_ctx);
	  goto setup_fail_codec;
	}

      ctx->re_abuffer = av_realloc(ctx->re_abuffer, XCODE_BUFFER_SIZE * 2);
      if (!ctx->re_abuffer)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not allocate resample buffer\n");

	  avresample_free(&ctx->resample_ctx);
	  goto setup_fail_codec;
	}
#else
      DPRINTF(E_DBG, L_XCODE, "Setting up resampling (%d@%d)\n", ctx->acodec->channels, ctx->acodec->sample_rate);

      ctx->resample_ctx = av_audio_resample_init(2,              ctx->acodec->channels,
						 44100,          ctx->acodec->sample_rate,
						 AV_SAMPLE_FMT_S16, ctx->acodec->sample_fmt,
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
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)

#elif LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 4)
      ctx->input_size = ctx->acodec->channels * av_get_bytes_per_sample(ctx->acodec->sample_fmt);
#elif LIBAVCODEC_VERSION_MAJOR >= 53
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

  *nctx = ctx;

  return 0;

 setup_fail_codec:
  avcodec_close(ctx->acodec);

 setup_fail:
#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
  avformat_close_input(&ctx->fmtctx);
#else
  av_close_input_file(ctx->fmtctx);
#endif
  free(ctx);

  return -1;
}

void
transcode_cleanup(struct transcode_ctx *ctx)
{
  if (ctx->apacket.data)
    av_free_packet(&ctx->apacket);

  if (ctx->acodec)
    avcodec_close(ctx->acodec);
#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
  if (ctx->fmtctx)
    avformat_close_input(&ctx->fmtctx);
#else
  if (ctx->fmtctx)
    av_close_input_file(ctx->fmtctx);
#endif

  av_free(ctx->abuffer);

  if (ctx->need_resample)
    {
#if defined(HAVE_LIBSWRESAMPLE)
      swr_free(&ctx->resample_ctx);
#elif defined(HAVE_LIBAVRESAMPLE)
      avresample_free(&ctx->resample_ctx);
#else
      audio_resample_close(ctx->resample_ctx);
#endif
      av_free(ctx->re_abuffer);
    }

  free(ctx);
}

int
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype)
{
  char *codectype;
  cfg_t *lib;
  int size;
  int i;

  if (!file_codectype)
    {
      DPRINTF(E_LOG, L_XCODE, "Can't determine transcode status, codec type is unknown\n");
      return -1;
    }

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

  if (!client_codecs)
    {
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
	  else if (strncmp(user_agent, "AppleCoreMedia", strlen("AppleCoreMedia")) == 0)
	    {
	      DPRINTF(E_DBG, L_XCODE, "Client is AppleCoreMedia, using iTunes codecs\n");

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

void
transcode_metadata(struct transcode_ctx *ctx, struct http_icy_metadata **metadata, int *changed)
{
  struct http_icy_metadata *m;

  *metadata = NULL;

  if (!ctx->fmtctx)
    return;

  m = http_icy_metadata_get(ctx->fmtctx, 1);
  if (!m)
    return;

  *changed = (m->hash != ctx->icy_hash);

  ctx->icy_hash = m->hash;

  *metadata = m;
}

void
transcode_metadata_artwork_url(struct transcode_ctx *ctx, char **artwork_url)
{
  struct http_icy_metadata *m;

  *artwork_url = NULL;

  if (!ctx->fmtctx || !ctx->fmtctx->filename)
    return;

  m = http_icy_metadata_get(ctx->fmtctx, 1);
  if (!m)
    return;

  if (m->artwork_url)
    *artwork_url = strdup(m->artwork_url);

  http_icy_metadata_free(m, 0);
}
