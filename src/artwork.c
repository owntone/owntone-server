/*
 * Copyright (C) 2010-2011 Julien BLACHE <jb@jblache.org>
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
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "db.h"
#include "misc.h"
#include "logger.h"
#include "conffile.h"
#include "cache.h"

#if LIBAVFORMAT_VERSION_MAJOR >= 53
# include "avio_evbuffer.h"
#endif
#include "artwork.h"

#ifndef HAVE_LIBEVENT2
# define evbuffer_get_length(x) (x)->off
#endif

#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

static const char *cover_extension[] =
  {
    "jpg", "png",
  };

static int
artwork_read(char *filename, struct evbuffer *evbuf)
{
  uint8_t buf[4096];
  struct stat sb;
  int fd;
  int ret;

  fd = open(filename, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not open artwork file '%s': %s\n", filename, strerror(errno));

      return -1;
    }

  ret = fstat(fd, &sb);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not stat() artwork file '%s': %s\n", filename, strerror(errno));

      goto out_fail;
    }

  ret = evbuffer_expand(evbuf, sb.st_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for artwork\n");

      goto out_fail;
    }

  while ((ret = read(fd, buf, sizeof(buf))) > 0)
    evbuffer_add(evbuf, buf, ret);

  close(fd);

  return 0;

 out_fail:
  close(fd);
  return -1;
}

static int
rescale_needed(AVCodecContext *src, int max_w, int max_h, int *target_w, int *target_h)
{
  int need_rescale;

  DPRINTF(E_DBG, L_ART, "Original image dimensions: w %d h %d\n", src->width, src->height);

  need_rescale = 1;

  if ((max_w <= 0) || (max_h <= 0)) /* No valid dimensions, use original */
    {
      need_rescale = 0;

      *target_w = src->width;
      *target_h = src->height;
    }
  else if ((src->width <= max_w) && (src->height <= max_h)) /* Smaller than target */
    {
      need_rescale = 0;

      *target_w = src->width;
      *target_h = src->height;
    }
  else if (src->width * max_h > src->height * max_w)   /* Wider aspect ratio than target */
    {
      *target_w = max_w;
      *target_h = (double)max_w * ((double)src->height / (double)src->width);
    }
  else                                                 /* Taller or equal aspect ratio */
    {
      *target_w = (double)max_h * ((double)src->width / (double)src->height);
      *target_h = max_h;
    }

  DPRINTF(E_DBG, L_ART, "Raw destination width %d height %d\n", *target_w, *target_h);

  if ((*target_h > max_h) && (max_h > 0))
    *target_h = max_h;

  /* PNG prefers even row count */
  *target_w += *target_w % 2;

  if ((*target_w > max_w) && (max_w > 0))
    *target_w = max_w - (max_w % 2);

  DPRINTF(E_DBG, L_ART, "Destination width %d height %d\n", *target_w, *target_h);

  return need_rescale;
}

static int
artwork_rescale(AVFormatContext *src_ctx, int s, int out_w, int out_h, struct evbuffer *evbuf)
{
  uint8_t *buf;
  uint8_t *outbuf;

  AVCodecContext *src;

  AVFormatContext *dst_ctx;
  AVCodecContext *dst;
  AVOutputFormat *dst_fmt;
  AVStream *dst_st;

  AVCodec *img_decoder;
  AVCodec *img_encoder;

  AVFrame *i_frame;
  AVFrame *o_frame;

  struct SwsContext *swsctx;

  AVPacket pkt;
  int have_frame;

  int outbuf_len;

  int ret;

  src = src_ctx->streams[s]->codec;

  img_decoder = avcodec_find_decoder(src->codec_id);
  if (!img_decoder)
    {
      DPRINTF(E_LOG, L_ART, "No suitable decoder found for artwork %s\n", src_ctx->filename);

      return -1;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 6)
  ret = avcodec_open2(src, img_decoder, NULL);
#else
  ret = avcodec_open(src, img_decoder);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open codec for decoding: %s\n", strerror(AVUNERROR(ret)));

      return -1;
    }

  /* Set up output */
#if LIBAVFORMAT_VERSION_MAJOR >= 53 || (LIBAVFORMAT_VERSION_MAJOR == 52 && LIBAVFORMAT_VERSION_MINOR >= 45)
  /* FFmpeg 0.6 */
  dst_fmt = av_guess_format("image2", NULL, NULL);
#else
  dst_fmt = guess_format("image2", NULL, NULL);
#endif
  if (!dst_fmt)
    {
      DPRINTF(E_LOG, L_ART, "ffmpeg image2 muxer not available\n");

      ret = -1;
      goto out_close_src;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  dst_fmt->video_codec = AV_CODEC_ID_NONE;

  /* Try to keep same codec if possible */
  if (src->codec_id == AV_CODEC_ID_PNG)
    dst_fmt->video_codec = AV_CODEC_ID_PNG;
  else if (src->codec_id == AV_CODEC_ID_MJPEG)
    dst_fmt->video_codec = AV_CODEC_ID_MJPEG;

  /* If not possible, select new codec */
  if (dst_fmt->video_codec == AV_CODEC_ID_NONE)
    {
      dst_fmt->video_codec = AV_CODEC_ID_PNG;
      //dst_fmt->video_codec = AV_CODEC_ID_MJPEG;
    }
#else
  dst_fmt->video_codec = CODEC_ID_NONE;

  /* Try to keep same codec if possible */
  if (src->codec_id == CODEC_ID_PNG)
    dst_fmt->video_codec = CODEC_ID_PNG;
  else if (src->codec_id == CODEC_ID_MJPEG)
    dst_fmt->video_codec = CODEC_ID_MJPEG;

  /* If not possible, select new codec */
  if (dst_fmt->video_codec == CODEC_ID_NONE)
    {
      dst_fmt->video_codec = CODEC_ID_PNG;
      //dst_fmt->video_codec = CODEC_ID_MJPEG;
    }
#endif

  img_encoder = avcodec_find_encoder(dst_fmt->video_codec);
  if (!img_encoder)
    {
      DPRINTF(E_LOG, L_ART, "No suitable encoder found for codec ID %d\n", dst_fmt->video_codec);

      ret = -1;
      goto out_close_src;
    }

  dst_ctx = avformat_alloc_context();
  if (!dst_ctx)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for format context\n");

      ret = -1;
      goto out_close_src;
    }

  dst_ctx->oformat = dst_fmt;

#if LIBAVFORMAT_VERSION_MAJOR >= 53
  dst_fmt->flags &= ~AVFMT_NOFILE;
#else
  ret = snprintf(dst_ctx->filename, sizeof(dst_ctx->filename), "evbuffer:%p", evbuf);
  if ((ret < 0) || (ret >= sizeof(dst_ctx->filename)))
    {
      DPRINTF(E_LOG, L_ART, "Output artwork URL too long\n");

      ret = -1;
      goto out_free_dst_ctx;
    }
#endif

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
  dst_st = avformat_new_stream(dst_ctx, NULL);
#else
  dst_st = av_new_stream(dst_ctx, 0);
#endif
  if (!dst_st)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for new output stream\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  dst = dst_st->codec;

#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 35)
  avcodec_get_context_defaults3(dst, NULL);
#else
  avcodec_get_context_defaults2(dst, AVMEDIA_TYPE_VIDEO);
#endif

  if (dst_fmt->flags & AVFMT_GLOBALHEADER)
    dst->flags |= CODEC_FLAG_GLOBAL_HEADER;

  dst->codec_id = dst_fmt->video_codec;
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 64)
  dst->codec_type = AVMEDIA_TYPE_VIDEO;
#else
  dst->codec_type = CODEC_TYPE_VIDEO;
#endif

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
# ifndef FFMPEG_INCOMPATIBLE_API
  dst->pix_fmt = avcodec_find_best_pix_fmt2((enum AVPixelFormat *)img_encoder->pix_fmts, src->pix_fmt, 1, NULL);
# else
  dst->pix_fmt = avcodec_find_best_pix_fmt_of_list((enum AVPixelFormat *)img_encoder->pix_fmts, src->pix_fmt, 1, NULL);
# endif
#else
  const enum PixelFormat *pix_fmts;
  int64_t pix_fmt_mask = 0;

  pix_fmts = img_encoder->pix_fmts;
  while (pix_fmts && (*pix_fmts != -1))
    {
      pix_fmt_mask |= (1 << *pix_fmts);
      pix_fmts++;
    }

  dst->pix_fmt = avcodec_find_best_pix_fmt(pix_fmt_mask, src->pix_fmt, 1, NULL);
#endif

  if (dst->pix_fmt < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not determine best pixel format\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  DPRINTF(E_DBG, L_ART, "Selected pixel format: %d\n", dst->pix_fmt);

  dst->time_base.num = 1;
  dst->time_base.den = 25;

  dst->width = out_w;
  dst->height = out_h;

#if LIBAVFORMAT_VERSION_MAJOR <= 52 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR <= 1)
  ret = av_set_parameters(dst_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Invalid parameters for artwork output: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_free_dst_ctx;
    }
#endif

  /* Open encoder */
#if LIBAVCODEC_VERSION_MAJOR >= 54 || (LIBAVCODEC_VERSION_MAJOR == 53 && LIBAVCODEC_VERSION_MINOR >= 6)
  ret = avcodec_open2(dst, img_encoder, NULL);
#else
  ret = avcodec_open(dst, img_encoder);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open codec for encoding: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_free_dst_ctx;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 56 || (LIBAVCODEC_VERSION_MAJOR == 55 && LIBAVCODEC_VERSION_MINOR >= 29)
  i_frame = av_frame_alloc();
  o_frame = av_frame_alloc();
#else
  i_frame = avcodec_alloc_frame();
  o_frame = avcodec_alloc_frame();
#endif

  if (!i_frame || !o_frame)
    {
      DPRINTF(E_LOG, L_ART, "Could not allocate input/output frame\n");

      ret = -1;
      goto out_free_frames;
    }

  ret = avpicture_get_size(dst->pix_fmt, src->width, src->height);

  DPRINTF(E_DBG, L_ART, "Artwork buffer size: %d\n", ret);

  buf = (uint8_t *)av_malloc(ret);
  if (!buf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for artwork buffer\n");

      ret = -1;
      goto out_free_frames;
    }

  avpicture_fill((AVPicture *)o_frame, buf, dst->pix_fmt, src->width, src->height);

  swsctx = sws_getContext(src->width, src->height, src->pix_fmt,
			  dst->width, dst->height, dst->pix_fmt,
			  SWS_BICUBIC, NULL, NULL, NULL);
  if (!swsctx)
    {
      DPRINTF(E_LOG, L_ART, "Could not get SWS context\n");

      ret = -1;
      goto out_free_buf;
    }

  /* Get frame */
  have_frame = 0;
  while (av_read_frame(src_ctx, &pkt) == 0)
    {
      if (pkt.stream_index != s)
	{
	  av_free_packet(&pkt);
	  continue;
	}

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 32)
      /* FFmpeg 0.6 */
      avcodec_decode_video2(src, i_frame, &have_frame, &pkt);
#else
      avcodec_decode_video(src, i_frame, &have_frame, pkt.data, pkt.size);
#endif

      break;
    }

  if (!have_frame)
    {
      DPRINTF(E_LOG, L_ART, "Could not decode artwork\n");

      av_free_packet(&pkt);
      sws_freeContext(swsctx);

      ret = -1;
      goto out_free_buf;
    }

  /* Scale */
#if LIBSWSCALE_VERSION_MAJOR >= 1 || (LIBSWSCALE_VERSION_MAJOR == 0 && LIBSWSCALE_VERSION_MINOR >= 9)
  /* FFmpeg 0.6, libav 0.6+ */
  sws_scale(swsctx, (const uint8_t * const *)i_frame->data, i_frame->linesize, 0, src->height, o_frame->data, o_frame->linesize);
#else
  sws_scale(swsctx, i_frame->data, i_frame->linesize, 0, src->height, o_frame->data, o_frame->linesize);
#endif

  sws_freeContext(swsctx);
  av_free_packet(&pkt);

  /* Open output file */
#if LIBAVFORMAT_VERSION_MAJOR >= 53
  dst_ctx->pb = avio_evbuffer_open(evbuf);
#else
  ret = url_fopen(&dst_ctx->pb, dst_ctx->filename, URL_WRONLY);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open artwork destination buffer\n");

      ret = -1;
      goto out_free_buf;
    }

  /* Encode frame */
  outbuf_len = dst->width * dst->height * 3;
  if (outbuf_len < FF_MIN_BUFFER_SIZE)
    outbuf_len = FF_MIN_BUFFER_SIZE;

  outbuf = (uint8_t *)av_malloc(outbuf_len);
  if (!outbuf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for encoded artwork buffer\n");

#if LIBAVFORMAT_VERSION_MAJOR >= 53
      avio_evbuffer_close(dst_ctx->pb);
#else
      url_fclose(dst_ctx->pb);
#endif

      ret = -1;
      goto out_free_buf;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 54
  av_init_packet(&pkt);
  pkt.data = outbuf;
  pkt.size = outbuf_len;
  ret = avcodec_encode_video2(dst, &pkt, o_frame, &have_frame);
  if (!ret && have_frame && dst->coded_frame) 
    {
      dst->coded_frame->pts       = pkt.pts;
      dst->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }
  else if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not encode artwork\n");

      ret = -1;
      goto out_fclose_dst;
    }
#else
  ret = avcodec_encode_video(dst, outbuf, outbuf_len, o_frame);
  if (ret <= 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not encode artwork\n");

      ret = -1;
      goto out_fclose_dst;
    }

  av_init_packet(&pkt);
  pkt.stream_index = 0;
  pkt.data = outbuf;
  pkt.size = ret;
#endif

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
  ret = avformat_write_header(dst_ctx, NULL);
#else
  ret = av_write_header(dst_ctx);
#endif
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not write artwork header: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_fclose_dst;
    }

  ret = av_interleaved_write_frame(dst_ctx, &pkt);

  if (ret != 0)
    {
      DPRINTF(E_LOG, L_ART, "Error writing artwork\n");

      ret = -1;
      goto out_fclose_dst;
    }

  ret = av_write_trailer(dst_ctx);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not write artwork trailer: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_fclose_dst;
    }

  switch (dst_fmt->video_codec)
    {
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_PNG:
#else
      case CODEC_ID_PNG:
#endif
	ret = ART_FMT_PNG;
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_MJPEG:
#else
      case CODEC_ID_MJPEG:
#endif
	ret = ART_FMT_JPEG;
	break;

      default:
	DPRINTF(E_LOG, L_ART, "Unhandled rescale output format\n");
	ret = -1;
	break;
    }

 out_fclose_dst:
#if LIBAVFORMAT_VERSION_MAJOR >= 53
  avio_evbuffer_close(dst_ctx->pb);
#else
  url_fclose(dst_ctx->pb);
#endif
  av_free(outbuf);

 out_free_buf:
  av_free(buf);

 out_free_frames:
#if LIBAVCODEC_VERSION_MAJOR >= 56 || (LIBAVCODEC_VERSION_MAJOR == 55 && LIBAVCODEC_VERSION_MINOR >= 29)
  if (i_frame)
    av_frame_free(&i_frame);
  if (o_frame)
    av_frame_free(&o_frame);
#elif LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  if (i_frame)
    avcodec_free_frame(&i_frame);
  if (o_frame)
    avcodec_free_frame(&o_frame);
#else
  if (i_frame)
    av_free(i_frame);
  if (o_frame)
    av_free(o_frame);
#endif
  avcodec_close(dst);

 out_free_dst_ctx:
  avformat_free_context(dst_ctx);

 out_close_src:
  avcodec_close(src);

  return ret;
}

static int
artwork_get(char *filename, int max_w, int max_h, struct evbuffer *evbuf)
{
  AVFormatContext *src_ctx;
  int s;
  int target_w;
  int target_h;
  int format_ok;
  int ret;

  DPRINTF(E_DBG, L_ART, "Getting artwork (max destination width %d height %d)\n", max_w, max_h);

  src_ctx = NULL;

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
  ret = avformat_open_input(&src_ctx, filename, NULL, NULL);
#else
  ret = av_open_input_file(&src_ctx, filename, NULL, 0, NULL);
#endif
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open artwork file '%s': %s\n", filename, strerror(AVUNERROR(ret)));

      return -1;
    }

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
  ret = avformat_find_stream_info(src_ctx, NULL);
#else
  ret = av_find_stream_info(src_ctx);
#endif
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
      avformat_close_input(&src_ctx);
#else
      av_close_input_file(src_ctx);
#endif
      return -1;
    }

  format_ok = 0;
  for (s = 0; s < src_ctx->nb_streams; s++)
    {
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_PNG)
#else
      if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_PNG)
#endif
	{
	  format_ok = ART_FMT_PNG;
	  break;
	}
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      else if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_MJPEG)
#else
      else if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_MJPEG)
#endif
	{
	  format_ok = ART_FMT_JPEG;
	  break;
	}
    }

  if (s == src_ctx->nb_streams)
    {
      DPRINTF(E_LOG, L_ART, "Artwork file '%s' not a PNG or JPEG file\n", filename);

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
      avformat_close_input(&src_ctx);
#else
      av_close_input_file(src_ctx);
#endif
      return -1;
    }

  ret = rescale_needed(src_ctx->streams[s]->codec, max_w, max_h, &target_w, &target_h);

  /* Fastpath */
  if (!ret && format_ok)
    {
      ret = artwork_read(filename, evbuf);
      if (ret == 0)
	ret = format_ok;
    }
  else
    ret = artwork_rescale(src_ctx, s, target_w, target_h, evbuf);

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
  avformat_close_input(&src_ctx);
#else
  av_close_input_file(src_ctx);
#endif

  if (ret < 0)
    {
      if (evbuffer_get_length(evbuf) > 0)
	evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
    }

  return ret;
}

#if LIBAVFORMAT_VERSION_MAJOR >= 55 || (LIBAVFORMAT_VERSION_MAJOR == 54 && LIBAVFORMAT_VERSION_MINOR >= 6)
static int
artwork_get_embedded_image(char *filename, int max_w, int max_h, struct evbuffer *evbuf)
{
  AVFormatContext *src_ctx;
  AVStream *src_st;
  int s;
  int target_w;
  int target_h;
  int format_ok;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying embedded artwork in %s\n", filename);

  src_ctx = NULL;

  ret = avformat_open_input(&src_ctx, filename, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open media file '%s': %s\n", filename, strerror(AVUNERROR(ret)));

      return -1;
    }

  ret = avformat_find_stream_info(src_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

      avformat_close_input(&src_ctx);
      return -1;
    }

  format_ok = 0;
  for (s = 0; s < src_ctx->nb_streams; s++)
    {
      if (src_ctx->streams[s]->disposition & AV_DISPOSITION_ATTACHED_PIC)
	{
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
	  if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_PNG)
#else
	  if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_PNG)
#endif
	    {
	      format_ok = ART_FMT_PNG;
	      break;
	    }
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
	  else if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_MJPEG)
#else
	  else if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_MJPEG)
#endif
	    {
	      format_ok = ART_FMT_JPEG;
	      break;
	    }
	}
    }

  if (s == src_ctx->nb_streams)
    {
      DPRINTF(E_SPAM, L_ART, "Did not find embedded artwork in '%s'\n", filename);

      avformat_close_input(&src_ctx);
      return -1;
    }
  else
    DPRINTF(E_DBG, L_ART, "Found embedded artwork in '%s'\n", filename);

  src_st = src_ctx->streams[s];

  ret = rescale_needed(src_st->codec, max_w, max_h, &target_w, &target_h);

  /* Fastpath */
  if (!ret && format_ok)
    {
      DPRINTF(E_DBG, L_ART, "Artwork not too large, using original image\n");

      ret = evbuffer_expand(evbuf, src_st->attached_pic.size);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "Out of memory for artwork\n");

	  avformat_close_input(&src_ctx);
	  return -1;
	}

      ret = evbuffer_add(evbuf, src_st->attached_pic.data, src_st->attached_pic.size);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "Could not add embedded image to event buffer\n");
	}
      else
        ret = format_ok;
    }
  else
    {
      DPRINTF(E_DBG, L_ART, "Artwork too large, rescaling image\n");

      ret = artwork_rescale(src_ctx, s, target_w, target_h, evbuf);
    }

  avformat_close_input(&src_ctx);

  if (ret < 0)
    {
      if (evbuffer_get_length(evbuf) > 0)
	evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
    }

  return ret;
}
#endif

static int
artwork_get_dir_image(char *path, int max_w, int max_h, char *filename, struct evbuffer *evbuf)
{
  char artwork[PATH_MAX];
  char parentdir[PATH_MAX];
  int i;
  int j;
  int len;
  int ret;
  cfg_t *lib;
  int nbasenames;
  int nextensions;
  char *ptr;

  ret = snprintf(artwork, sizeof(artwork), "%s", path);
  if ((ret < 0) || (ret >= sizeof(artwork)))
    {
      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX\n");

      return -1;
    }

  len = strlen(artwork);

  lib = cfg_getsec(cfg, "library");
  nbasenames = cfg_size(lib, "artwork_basenames");

  if (nbasenames == 0)
    return -1;

  nextensions = sizeof(cover_extension) / sizeof(cover_extension[0]);

  for (i = 0; i < nbasenames; i++)
    {
      for (j = 0; j < nextensions; j++)
	{
	  ret = snprintf(artwork + len, sizeof(artwork) - len, "/%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(artwork) - len))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);

	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying directory artwork file %s\n", artwork);

	  ret = access(artwork, F_OK);
	  if (ret < 0)
	    continue;

	  // If artwork file exists (ret == 0), exit the loop
	  break;
	}

      // In case the previous loop exited early, we found an existing artwork file and exit the outer loop
      if (j < nextensions)
	break;
    }

  // If the loop for directory artwork did not exit early, look for parent directory artwork
  if (i == nbasenames)
    {
      ptr = strrchr(artwork, '/');
      if (ptr)
	*ptr = '\0';

      ptr = strrchr(artwork, '/');
      if ((!ptr) || (strlen(ptr) <= 1))
	return -1;
      strcpy(parentdir, ptr + 1);

      len = strlen(artwork);

      for (i = 0; i < nextensions; i++)
	{
	  ret = snprintf(artwork + len, sizeof(artwork) - len, "/%s.%s", parentdir, cover_extension[i]);
	  if ((ret < 0) || (ret >= sizeof(artwork) - len))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", parentdir, cover_extension[i]);
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying parent directory artwork file %s\n", artwork);

	  ret = access(artwork, F_OK);
	  if (ret < 0)
	    continue;

	  break;
	}

      if (i == nextensions)
	return -1;
    }

  DPRINTF(E_DBG, L_ART, "Found directory artwork file %s\n", artwork);
  strcpy(filename, artwork);

  return artwork_get(artwork, max_w, max_h, evbuf);
}

static int
artwork_get_item_path(char *path, int artwork, int max_w, int max_h, struct evbuffer *evbuf)
{
  int ret;

  ret = 0;
  switch (artwork)
    {
      case ARTWORK_NONE:
	break;
#ifdef HAVE_SPOTIFY_H
      case ARTWORK_SPOTIFY:
	ret = spotify_artwork_get(evbuf, path, max_w, max_h);
	(ret < 0) ? (ret = 0) : (ret = ART_FMT_JPEG);
	break;
#endif
#if LIBAVFORMAT_VERSION_MAJOR >= 55 || (LIBAVFORMAT_VERSION_MAJOR == 54 && LIBAVFORMAT_VERSION_MINOR >= 6)
      case ARTWORK_EMBEDDED:
	ret = artwork_get_embedded_image(path, max_w, max_h, evbuf);

	break;
#endif
    }

  if (ret > 0)
    return ret;
  else
    return -1;
}

/*
 * Get the cached artwork image for the given persistent id and width/height
 *
 * @param persistentid persistent songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param evbuf the event buffer that will contain the cached image
 * @return -1 if no cache entry exists, otherwise the format of the cache entry

static int artwork_cache_get(int64_t persistentid, int max_w, int max_h, struct evbuffer *evbuf)
{
  int cached;
  char *data;
  int datalen;
  int format;
  int ret;

  format = 0;
  cached = 0;

  ret = cache_artwork_get(persistentid, max_w, max_h, &cached, &format, evbuf);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching artwork cache entry for persistent id %" PRId64 "\n", persistentid);
      return ret;
    }

  if (!cached)
    {
      DPRINTF(E_DBG, L_ART, "Artwork entry not found in artwork cache for persistent id %" PRId64 "\n", persistentid);
      return -1;
    }

  DPRINTF(E_DBG, L_ART, "Artwork entry found in artwork cache for persistent id %" PRId64 "\n", persistentid);

  if (format == 0)
    {
      DPRINTF(E_DBG, L_ART, "No artwork avaiable for persistent id %" PRId64 "\n", persistentid);
    }
  else
    {
      evbuffer_add(evbuf, data, datalen);
      DPRINTF(E_DBG, L_ART, "Artwork with length %d found for persistent id %" PRId64 "\n", datalen, persistentid);
    }

  free(data);

  return format;
}*/

/*
 * Save the artwork image for the given persistent id and width/height in the artwork cache
 *
 * @param persistentid persistent songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param format ART_FMT_PNG for png, ART_FMT_JPEG for jpeg or 0 if no artwork available
 * @param path the full path to the artwork file (could be an jpg/png image or a media file with embedded artwork) or empty if no artwork available
 * @param evbuf the event buffer that contain the (scaled) image
 * @return the format of the artwork image

static int artwork_cache_save(int64_t persistentid, int max_w, int max_h, int format, char *path, struct evbuffer *evbuf)
{
  char *data;

  data = malloc(evbuffer_get_length(evbuf));
  evbuffer_copyout(evbuf, data, evbuffer_get_length(evbuf));

  cache_artwork_add(persistentid, max_w, max_h, format, path, evbuf);

  free(data);

  return format;
}*/

/*
 * Get the artwork image for the given persistentid and the given maximum width/height
 *
 * The function first checks if there is a cache entry, if not it will first look for directory artwork files.
 * If no directory artwork files are found, it looks for individual artwork (embedded images or images from spotify).
 *
 * @param persistentid persistent songalbumid or songartistid
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param evbuf the event buffer that will contain the (scaled) image
 */
static int
artwork_get_group_persistentid(int64_t persistentid, int max_w, int max_h, struct evbuffer *evbuf)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  char *dir;
  int got_art;
  int cached;
  int format;
  char filename[PATH_MAX];
  int ret;
  int artwork;
  int got_spotifyitem;
  uint32_t data_kind;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %" PRId64 "\n", persistentid);

  ret = 0;
  got_spotifyitem = 0;

  /*
   * First check if the artwork cache has a cached entry for the given persistent id and requested width/height
   */
  ret = cache_artwork_get(persistentid, max_w, max_h, &cached, &format, evbuf);
  if (ret == 0 && cached)
    {
      if (format > 0)
	{
	  // Artwork found in cache "ret" contains the format of the image
	  DPRINTF(E_DBG, L_ART, "Artwork found in cache for group %" PRId64 "\n", persistentid);
	  return format;
	}
      else if (format == 0)
	{
	  // Entry found in cache but there is not artwork available
	  DPRINTF(E_DBG, L_ART, "Artwork found in cache but no image available for group %" PRId64 "\n", persistentid);
	  return -1;
	}
    }

  /* Image is not in the artwork cache. Try directory artwork first */
  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUP_DIRS;
  qp.persistentid = persistentid;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUP_DIRS query\n");

      /* Skip to invidual files artwork */
      goto files_art;
    }

  got_art = 0;
  while (!got_art && ((ret = db_query_fetch_string(&qp, &dir)) == 0) && (dir))
    {
      /* If item is an internet stream don't look for artwork */
      if (strncmp(dir, "http://", strlen("http://")) == 0)
	continue;

      /* If Spotify item don't look for files artwork */
      if (strncmp(dir, "spotify:", strlen("spotify:")) == 0)
	continue;

      format = artwork_get_dir_image(dir, max_w, max_h, filename, evbuf);
      got_art = (format > 0);
    }

  db_query_end(&qp);

  if (ret < 0)
    DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUP_DIRS results\n");
  else if (got_art > 0)
    {
      cache_artwork_add(persistentid, max_w, max_h, format, filename, evbuf);
      return format;
    }


  /* Then try individual files */
 files_art:
  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUP_ITEMS;
  qp.persistentid = persistentid;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUP_ITEMS query\n");

      return -1;
    }

  got_art = 0;
  while (!got_art && ((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      if ((safe_atoi32(dbmfi.artwork, &artwork) != 0) && (safe_atou32(dbmfi.data_kind, &data_kind) != 0))
	continue;

      format = artwork_get_item_path(dbmfi.path, artwork, max_w, max_h, evbuf);
      got_art = (format > 0);

      if (artwork == ARTWORK_SPOTIFY)
	got_spotifyitem = 1;

      if (got_art)
	strcpy(filename, dbmfi.path);
    }

  db_query_end(&qp);

  if (ret < 0)
    DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUP_ITEMS results\n");
  else if (got_art > 0)
    {
      cache_artwork_add(persistentid, max_w, max_h, format, filename, evbuf);
      return format;
    }

  /* Add cache entry for no artwork available */
  if (!got_spotifyitem)
    cache_artwork_add(persistentid, max_w, max_h, 0, "", evbuf);

  DPRINTF(E_DBG, L_ART, "No artwork found for group %" PRId64 "\n", persistentid);

  return -1;
}

int
artwork_get_item(int id, int max_w, int max_h, struct evbuffer *evbuf)
{
  struct media_file_info *mfi;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for item %d\n", id);

  mfi = db_file_fetch_byid(id);
  if (!mfi)
    return -1;

  /*
   * Load artwork image for the persistent id
   */
  ret = artwork_get_group_persistentid(mfi->songalbumid, max_w, max_h, evbuf);
  if (ret < 0)
    DPRINTF(E_DBG, L_ART, "No artwork found for item id %d (%s)\n", id, mfi->fname);

  free_mfi(mfi, 0);

  return ret;
}

int
artwork_get_group(int id, int max_w, int max_h, struct evbuffer *evbuf)
{
  int64_t persistentid;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %d\n", id);

  /*
   * Get the persistent id for the given group id
   */
  ret = db_group_persistentid_byid(id, &persistentid);
  if (ret < 0) {
    DPRINTF(E_LOG, L_ART, "Error fetching persistent id for group id %d\n", id);
    return -1;
  }

  /*
   * Load artwork image for the persistent id
   */
  ret = artwork_get_group_persistentid(persistentid, max_w, max_h, evbuf);
  if (ret < 0)
    DPRINTF(E_DBG, L_ART, "No artwork found for group id %d\n", id);

  return ret;
}
