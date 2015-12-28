/*
 * Copyright (C) 2015-2016 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include "player.h"
#include "http.h"

#include "avio_evbuffer.h"
#include "artwork.h"

#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

/* Old ffmpeg compability
 */
#if LIBAVCODEC_VERSION_MAJOR <= 53 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR <= 34)
# define AV_CODEC_ID_MJPEG CODEC_ID_MJPEG
# define AV_CODEC_ID_PNG CODEC_ID_PNG
# define AV_CODEC_ID_NONE CODEC_ID_NONE
#endif

/* This artwork module will look for artwork by consulting a set of sources one
 * at a time. A source is for instance the local library, the cache or a cover
 * art database. For each source there is a handler function, which will do the
 * actual work of getting the artwork.
 *
 * There are two types of handlers: item and group. Item handlers are capable of
 * finding artwork for a single item (a dbmfi), while group handlers can get for
 * an album or artist (a persistentid).
 *
 * An artwork source handler must return one of the following:
 *
 *   ART_FMT_JPEG (positive)  Found a jpeg
 *   ART_FMT_PNG  (positive)  Found a png
 *   ART_E_NONE (zero)        No artwork found
 *   ART_E_ERROR (negative)   An error occurred while searching for artwork
 *   ART_E_ABORT (negative)   Caller should abort artwork search (may be returned by cache)
 */
#define ART_E_NONE 0
#define ART_E_ERROR -1
#define ART_E_ABORT -2

enum artwork_cache
{
  NEVER = 0,       // No caching of any results
  ON_SUCCESS = 1,  // Cache if artwork found
  ON_FAILURE = 2,  // Cache if artwork not found (so we don't keep asking)
};

/* This struct contains the data available to the handler, as well as a char
 * buffer where the handler should output the path to the artwork (if it is
 * local - otherwise the buffer can be left empty). The purpose of supplying the
 * path is that the filescanner can then clear the cache in case the file
 * changes.
 */
struct artwork_ctx {
  // Handler should output path here if artwork is local
  char path[PATH_MAX];
  // Handler should output artwork data to this evbuffer
  struct evbuffer *evbuf;

  // Input data for item handlers
  struct db_media_file_info *dbmfi;
  int id;

  // Input data for group handlers
  int64_t persistentid;

  // Input data for both (requested width and height)
  int max_w;
  int max_h;

  // Not to be used by handler - query for item or group
  struct query_params qp;

  // Not to be used by handler - should the result be cached
  enum artwork_cache cache;
};

/* Definition of an artwork source. Covers both item and group sources.
 */
struct artwork_source {
  // Name of the source, e.g. "cache"
  const char *name;

  // The handler
  int (*handler)(struct artwork_ctx *ctx);

  // What data_kinds the handler can work with, combined with (1 << A) | (1 << B)
  int data_kinds;

  // When should results from the source be cached?
  enum artwork_cache cache;
};

/* File extensions that we look for or accept
 */
static const char *cover_extension[] =
  {
    "jpg", "png",
  };



/* -------------------------------- HELPERS -------------------------------- */

/* Reads an artwork file from the filesystem straight into an evbuf
 * TODO Use evbuffer_add_file or evbuffer_read?
 *
 * @out evbuf     Image data
 * @in  path      Path to the artwork
 * @return        0 on success, -1 on error
 */
static int
artwork_read(struct evbuffer *evbuf, char *path)
{
  uint8_t buf[4096];
  struct stat sb;
  int fd;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not open artwork file '%s': %s\n", path, strerror(errno));

      return -1;
    }

  ret = fstat(fd, &sb);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not stat() artwork file '%s': %s\n", path, strerror(errno));

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

/* Will the source image fit inside requested size. If not, what size should it
 * be rescaled to to maintain aspect ratio.
 *
 * @in  src       Image source
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @out target_w  Rescaled width
 * @out target_h  Rescaled height
 * @return        0 no rescaling needed, 1 rescaling needed
 */
static int
rescale_needed(AVCodecContext *src, int max_w, int max_h, int *target_w, int *target_h)
{
  DPRINTF(E_DBG, L_ART, "Original image dimensions: w %d h %d\n", src->width, src->height);

  *target_w = src->width;
  *target_h = src->height;

  if ((src->width == 0) || (src->height == 0))         /* Unknown source size, can't rescale */
    return 0;

  if ((max_w <= 0) || (max_h <= 0))                    /* No valid target dimensions, use original */
    return 0;

  if ((src->width <= max_w) && (src->height <= max_h)) /* Smaller than target */
    return 0;

  if (src->width * max_h > src->height * max_w)        /* Wider aspect ratio than target */
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

  return 1;
}

/* Rescale an image
 *
 * @out evbuf     Rescaled image data
 * @in  src_ctx   Image source
 * @in  s         Index of stream containing image
 * @in  out_w     Rescaled width
 * @in  out_h     Rescaled height
 * @return        ART_FMT_* on success, -1 on error
 */
static int
artwork_rescale(struct evbuffer *evbuf, AVFormatContext *src_ctx, int s, int out_w, int out_h)
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

  // Avoids threading issue in both ffmpeg and libav that prevents decoding embedded png's
  src->thread_count = 1;

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

  if (src->pix_fmt < 0)
    {
      DPRINTF(E_LOG, L_ART, "Unknown pixel format for artwork %s\n", src_ctx->filename);

      ret = -1;
      goto out_close_src;
    }

  /* Set up output */
  dst_fmt = av_guess_format("image2", NULL, NULL);
  if (!dst_fmt)
    {
      DPRINTF(E_LOG, L_ART, "ffmpeg image2 muxer not available\n");

      ret = -1;
      goto out_close_src;
    }

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
    }

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

  dst_fmt->flags &= ~AVFMT_NOFILE;

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
# ifndef HAVE_FFMPEG
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
  dst_ctx->pb = avio_evbuffer_open(evbuf);
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

      avio_evbuffer_close(dst_ctx->pb);

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
      case AV_CODEC_ID_PNG:
	ret = ART_FMT_PNG;
	break;

      case AV_CODEC_ID_MJPEG:
	ret = ART_FMT_JPEG;
	break;

      default:
	DPRINTF(E_LOG, L_ART, "Unhandled rescale output format\n");
	ret = -1;
	break;
    }

 out_fclose_dst:
  avio_evbuffer_close(dst_ctx->pb);
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

/* Get an artwork file from the filesystem. Will rescale if needed.
 *
 * @out evbuf     Image data
 * @in  path      Path to the artwork
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @return        ART_FMT_* on success, -1 on error
 */
static int
artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h)
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
  ret = avformat_open_input(&src_ctx, path, NULL, NULL);
#else
  ret = av_open_input_file(&src_ctx, path, NULL, 0, NULL);
#endif
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open artwork file '%s': %s\n", path, strerror(AVUNERROR(ret)));

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
      if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_PNG)
	{
	  format_ok = ART_FMT_PNG;
	  break;
	}
      else if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_MJPEG)
	{
	  format_ok = ART_FMT_JPEG;
	  break;
	}
    }

  if (s == src_ctx->nb_streams)
    {
      DPRINTF(E_LOG, L_ART, "Artwork file '%s' not a PNG or JPEG file\n", path);

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
      ret = artwork_read(evbuf, path);
      if (ret == 0)
	ret = format_ok;
    }
  else
    ret = artwork_rescale(evbuf, src_ctx, s, target_w, target_h);

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
/* Get an embedded artwork file from a media file. Will rescale if needed.
 *
 * @out evbuf     Image data
 * @in  path      Path to the artwork
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @return        ART_FMT_* on success, -1 on error
 */
static int
artwork_get_embedded_image(struct evbuffer *evbuf, char *path, int max_w, int max_h)
{
  AVFormatContext *src_ctx;
  AVStream *src_st;
  int s;
  int target_w;
  int target_h;
  int format_ok;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying embedded artwork in %s\n", path);

  src_ctx = NULL;

  ret = avformat_open_input(&src_ctx, path, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open media file '%s': %s\n", path, strerror(AVUNERROR(ret)));

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
	  if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_PNG)
	    {
	      format_ok = ART_FMT_PNG;
	      break;
	    }
	  else if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_MJPEG)
	    {
	      format_ok = ART_FMT_JPEG;
	      break;
	    }
	}
    }

  if (s == src_ctx->nb_streams)
    {
      DPRINTF(E_DBG, L_ART, "Did not find embedded artwork in '%s'\n", path);

      avformat_close_input(&src_ctx);
      return 0;
    }
  else
    DPRINTF(E_DBG, L_ART, "Found embedded artwork in '%s'\n", path);

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

      ret = artwork_rescale(evbuf, src_ctx, s, target_w, target_h);
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

/*
 * Looks for basename(in_path).{png,jpg}, so if in_path is /foo/bar.mp3 it
 * will look for /foo/bar.png and /foo/bar.jpg
 *
 * @param evbuf the event buffer that will contain the (scaled) image
 * @param in_path path to the item we are getting artwork for
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param out_path path to artwork, input must be either NULL or char[PATH_MAX]
 * @return ART_FMT_* on success, 0 on nothing found, -1 on error
 */
static int
artwork_get_own_image(struct evbuffer *evbuf, char *in_path, int max_w, int max_h, char *out_path)
{
  char path[PATH_MAX];
  char *ptr;
  int len;
  int nextensions;
  int i;
  int ret;

  ret = snprintf(path, sizeof(path), "%s", in_path);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", in_path);
      return -1;
    }

  ptr = strrchr(path, '.');
  if (ptr)
    *ptr = '\0';

  len = strlen(path);

  nextensions = sizeof(cover_extension) / sizeof(cover_extension[0]);

  for (i = 0; i < nextensions; i++)
    {
      ret = snprintf(path + len, sizeof(path) - len, ".%s", cover_extension[i]);
      if ((ret < 0) || (ret >= sizeof(path) - len))
	{
	  DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", in_path);
	  continue;
	}

      DPRINTF(E_SPAM, L_ART, "Trying own artwork file %s\n", path);

      ret = access(path, F_OK);
      if (ret < 0)
	continue;

      break;
    }

  if (i == nextensions)
    return 0;

  DPRINTF(E_DBG, L_ART, "Found own artwork file %s\n", path);

  if (out_path)
    strncpy(out_path, path, PATH_MAX);

  return artwork_get(evbuf, path, max_w, max_h);
}

/*
 * Looks for cover files in a directory, so if dir is /foo/bar and the user has
 * configured the cover file names "cover" and "artwork" it will look for
 * /foo/bar/cover.{png,jpg}, /foo/bar/artwork.{png,jpg} and also
 * /foo/bar/bar.{png,jpg} (so called parentdir artwork)
 *
 * @param evbuf the event buffer that will contain the (scaled) image
 * @param dir the directory to search
 * @param max_w maximum image width
 * @param max_h maximum image height
 * @param out_path path to artwork, input must be either NULL or char[PATH_MAX]
 * @return ART_FMT_* on success, 0 on nothing found, -1 on error
 */
static int
artwork_get_dir_image(struct evbuffer *evbuf, char *dir, int max_w, int max_h, char *out_path)
{
  char path[PATH_MAX];
  char parentdir[PATH_MAX];
  int i;
  int j;
  int len;
  int ret;
  cfg_t *lib;
  int nbasenames;
  int nextensions;
  char *ptr;

  ret = snprintf(path, sizeof(path), "%s", dir);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", dir);
      return -1;
    }

  len = strlen(path);

  lib = cfg_getsec(cfg, "library");
  nbasenames = cfg_size(lib, "artwork_basenames");

  if (nbasenames == 0)
    return 0;

  nextensions = sizeof(cover_extension) / sizeof(cover_extension[0]);

  for (i = 0; i < nbasenames; i++)
    {
      for (j = 0; j < nextensions; j++)
	{
	  ret = snprintf(path + len, sizeof(path) - len, "/%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(path) - len))
	    {
	      DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s/%s)\n", dir, cfg_getnstr(lib, "artwork_basenames", i));
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying directory artwork file %s\n", path);

	  ret = access(path, F_OK);
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
      ptr = strrchr(path, '/');
      if (ptr)
	*ptr = '\0';

      ptr = strrchr(path, '/');
      if ((!ptr) || (strlen(ptr) <= 1))
	{
	  DPRINTF(E_LOG, L_ART, "Could not find parent dir name (%s)\n", path);
	  return -1;
	}
      strcpy(parentdir, ptr + 1);

      len = strlen(path);

      for (i = 0; i < nextensions; i++)
	{
	  ret = snprintf(path + len, sizeof(path) - len, "/%s.%s", parentdir, cover_extension[i]);
	  if ((ret < 0) || (ret >= sizeof(path) - len))
	    {
	      DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", parentdir);
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying parent directory artwork file %s\n", path);

	  ret = access(path, F_OK);
	  if (ret < 0)
	    continue;

	  break;
	}

      if (i == nextensions)
	return 0;
    }

  DPRINTF(E_DBG, L_ART, "Found directory artwork file %s\n", path);

  if (out_path)
    strncpy(out_path, path, PATH_MAX);

  return artwork_get(evbuf, path, max_w, max_h);
}


/* -------------------- SOURCE HANDLERS AND DEFINITIONS -------------------- */

static int
source_group_cache_get(struct artwork_ctx *ctx)
{
  int format;
  int cached;
  int ret;

  ret = cache_artwork_get(CACHE_ARTWORK_GROUP, ctx->persistentid, ctx->max_w, ctx->max_h, &cached, &format, ctx->evbuf);
  if (ret < 0)
    return ART_E_ERROR;

  if (!cached)
    return ART_E_NONE;

  DPRINTF(E_DBG, L_ART, "Group %" PRIi64 " found in cache with format %d\n", ctx->persistentid, format);

  if (!format)
    return ART_E_ABORT;

  return format;
}

static int
source_group_dir_get(struct artwork_ctx *ctx)
{
  struct query_params qp;
  char *dir;
  int ret;

  /* Image is not in the artwork cache. Try directory artwork first */
  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUP_DIRS;
  qp.persistentid = ctx->persistentid;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUP_DIRS query\n");
      return ART_E_ERROR;
    }

  while (((ret = db_query_fetch_string(&qp, &dir)) == 0) && (dir))
    {
      /* The db query may return non-directories (eg if item is an internet stream or Spotify) */
      if (access(dir, F_OK) < 0)
	continue;

      ret = artwork_get_dir_image(ctx->evbuf, dir, ctx->max_w, ctx->max_h, ctx->path);
      if (ret > 0)
	{
	  db_query_end(&qp);
	  return ret;
	}
    }

  db_query_end(&qp);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUP_DIRS results\n");
      return ART_E_ERROR;
    }

  return ART_E_NONE;
}

static int
source_item_cache_get(struct artwork_ctx *ctx)
{
  int format;
  int cached;
  int ret;

  ret = cache_artwork_get(CACHE_ARTWORK_INDIVIDUAL, ctx->id, ctx->max_w, ctx->max_h, &cached, &format, ctx->evbuf);
  if (ret < 0)
    return ART_E_ERROR;

  if (!cached)
    return ART_E_NONE;

  DPRINTF(E_DBG, L_ART, "Item %d found in cache with format %d\n", ctx->id, format);

  if (!format)
    return ART_E_ABORT;

  return format;
}

static int
source_item_embedded_get(struct artwork_ctx *ctx)
{
  int ret;

  ret = artwork_get_embedded_image(ctx->evbuf, ctx->dbmfi->path, ctx->max_w, ctx->max_h);
  if (ret < 0)
    return ART_E_ERROR;

  if (ret == 0)
    return ART_E_NONE;

  // TODO Use a more efficient way, and something that will always NULL-terminate
  if (ret > 0)
    strncpy(ctx->path, ctx->dbmfi->path, PATH_MAX);

  return ret;
}

static int
source_item_own_get(struct artwork_ctx *ctx)
{
  int ret;

  ret = artwork_get_own_image(ctx->evbuf, ctx->dbmfi->path, ctx->max_w, ctx->max_h, ctx->path);
  if (ret < 0)
    return ART_E_ERROR;

  if (ret == 0)
    return ART_E_NONE;

  return ret;
}

/*
 * Downloads the artwork pointed to by the ICY metadata tag in an internet radio
 * stream (the StreamUrl tag). The path will be converted back to the id, which
 * is given to the player. If the id is currently being played, and there is a
 * valid ICY metadata artwork URL available, it will be returned to this
 * function, which will then use the http client to get the artwork. Notice: No
 * rescaling is done.
 */
static int
source_item_stream_get(struct artwork_ctx *ctx)
{
  struct http_client_ctx client;
  struct keyval *kv;
  const char *content_type;
  char *url;
  char *ext;
  int len;
  int ret;

  DPRINTF(E_DBG, L_ART, "Trying internet stream artwork in %s\n", ctx->dbmfi->path);

  ret = ART_E_NONE;

  url = player_get_icy_artwork_url(ctx->id);
  if (!url)
    return ART_E_NONE;

  len = strlen(url);
  if ((len < 14) || (len > PATH_MAX)) // Can't be shorter than http://a/1.jpg
    goto out_url;

  ext = strrchr(url, '.');
  if (!ext)
    goto out_url;
  if ((strcmp(ext, ".jpg") != 0) && (strcmp(ext, ".png") != 0))
    goto out_url;

  cache_artwork_read(ctx->evbuf, url, &ret);
  if (ret > 0)
    goto out_url;

  kv = keyval_alloc();
  if (!kv)
    goto out_url;

  memset(&client, 0, sizeof(struct http_client_ctx));
  client.url = url;
  client.headers = kv;
  client.body = ctx->evbuf;

  if (http_client_request(&client) < 0)
    goto out_kv;

  content_type = keyval_get(kv, "Content-Type");
  if (content_type && (strcmp(content_type, "image/jpeg") == 0))
    ret = ART_FMT_JPEG;
  else if (content_type && (strcmp(content_type, "image/png") == 0))
    ret = ART_FMT_PNG;

  if (ret > 0)
    {
      DPRINTF(E_DBG, L_ART, "Found internet stream artwork in %s (%s)\n", url, content_type);
      strncpy(ctx->path, ctx->dbmfi->path, PATH_MAX); // TODO
      cache_artwork_stash(ctx->evbuf, url, ret);
    }

 out_kv:
  keyval_clear(kv);
  free(kv);

 out_url:
  free(url);

  return ret;
}

#ifdef HAVE_SPOTIFY_H
static int
source_item_spotify_get(struct artwork_ctx *ctx)
{
  int ret;

  ret = spotify_artwork_get(ctx->evbuf, ctx->dbmfi->path, ctx->max_w, ctx->max_h);
  if (ret < 0)
    return ART_E_NONE;

  return ART_FMT_JPEG;
}
#else
static int
source_item_spotify_get(struct artwork_ctx *ctx)
{
  return ART_E_ERROR;
}
#endif

/* List of sources that can provide artwork for a group (i.e. usually an album
 * identified by a persistentid). The source handlers will be called in the
 * order of this list. Must be terminated by a NULL struct.
 */
static struct artwork_source artwork_group_source[] =
  {
    {
      .name = "cache",
      .handler = source_group_cache_get,
      .cache = ON_FAILURE,
    },
    {
      .name = "directory",
      .handler = source_group_dir_get,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    { 
      .name = NULL,
      .handler = NULL,
      .cache = 0,
    }
  };

/* List of sources that can provide artwork for an item (a track characterized
 * by a dbmfi). The source handlers will be called in the order of this list.
 * The handler will only be called if the data_kind matches. Must be terminated
 * by a NULL struct.
 */
static struct artwork_source artwork_item_source[] =
  {
    {
      .name = "cache",
      .handler = source_item_cache_get,
      .data_kinds = (1 << DATA_KIND_FILE) | (1 << DATA_KIND_SPOTIFY),
      .cache = ON_FAILURE,
    },
    {
      .name = "embedded",
      .handler = source_item_embedded_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "own",
      .handler = source_item_own_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "stream",
      .handler = source_item_stream_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .cache = NEVER,
    },
    {
      .name = "Spotify",
      .handler = source_item_spotify_get,
      .data_kinds = (1 << DATA_KIND_SPOTIFY),
      .cache = ON_SUCCESS,
    },
    { 
      .name = NULL,
      .handler = NULL,
      .data_kinds = 0,
      .cache = 0,
    }
  };


/* --------------------------- SOURCE PROCESSING --------------------------- */

static int
process_items(struct artwork_ctx *ctx, int item_mode)
{
  struct db_media_file_info dbmfi;
  uint32_t data_kind;
  int artwork_individual;
  int i;
  int ret;

  artwork_individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

  ret = db_query_start(&ctx->qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start query (type=%d)\n", ctx->qp.type);
      ctx->cache = NEVER;
      return -1;
    }

  while (((ret = db_query_fetch_file(&ctx->qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      // Save the first songalbumid, might need it for process_group() if this search doesn't give anything
      if (!ctx->persistentid)
	safe_atoi64(dbmfi.songalbumid, &ctx->persistentid);

      if (item_mode && !artwork_individual)
	goto no_artwork;

      ret = (safe_atoi32(dbmfi.id, &ctx->id) < 0) ||
            (safe_atou32(dbmfi.data_kind, &data_kind) < 0) ||
            (data_kind > 30);
      if (ret)
	{
	  DPRINTF(E_LOG, L_ART, "Error converting dbmfi id or data_kind to number\n");
	  continue;
	}

      for (i = 0; artwork_item_source[i].handler; i++)
	{
	  if ((artwork_item_source[i].data_kinds & (1 << data_kind)) == 0)
	    continue;

	  // If just one handler says we should not cache a negative result then we obey that
	  if ((artwork_item_source[i].cache & ON_FAILURE) == 0)
	    ctx->cache = NEVER;

	  DPRINTF(E_DBG, L_ART, "Checking item source '%s'\n", artwork_item_source[i].name);

	  ctx->dbmfi = &dbmfi;
	  ret = artwork_item_source[i].handler(ctx);
	  ctx->dbmfi = NULL;

	  if (ret > 0)
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork for '%s' found in source '%s'\n", dbmfi.title, artwork_item_source[i].name);
	      ctx->cache = (artwork_item_source[i].cache & ON_SUCCESS);
	      db_query_end(&ctx->qp);
	      return ret;
	    }
	  else if (ret == ART_E_ABORT)
	    {
	      DPRINTF(E_INFO, L_ART, "Source '%s' stopped search for artwork for '%s'\n", artwork_item_source[i].name, dbmfi.title);
	      ctx->cache = NEVER;
	      break;
	    }
	  else if (ret == ART_E_ERROR)
	    {
	      DPRINTF(E_LOG, L_ART, "Source '%s' returned an error for '%s'\n", artwork_item_source[i].name, dbmfi.title);
	      ctx->cache = NEVER;
	    }
	}
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching results\n");
      ctx->cache = NEVER;
    }

 no_artwork:
  db_query_end(&ctx->qp);

  return -1;
}

static int
process_group(struct artwork_ctx *ctx)
{
  int i;
  int ret;

  if (!ctx->persistentid)
    {
      DPRINTF(E_LOG, L_ART, "Bug! No persistentid in call to process_group()\n");
      ctx->cache = NEVER;
      return -1;
    }

  for (i = 0; artwork_group_source[i].handler; i++)
    {
      // If just one handler says we should not cache a negative result then we obey that
      if ((artwork_group_source[i].cache & ON_FAILURE) == 0)
	ctx->cache = NEVER;

      DPRINTF(E_DBG, L_ART, "Checking group source '%s'\n", artwork_group_source[i].name);

      ret = artwork_group_source[i].handler(ctx);
      if (ret > 0)
	{
	  DPRINTF(E_INFO, L_ART, "Artwork for group %" PRIi64 " found in source '%s'\n", ctx->persistentid, artwork_group_source[i].name);
	  ctx->cache = (artwork_group_source[i].cache & ON_SUCCESS);
	  return ret;
	}
      else if (ret == ART_E_ABORT)
	{
	  DPRINTF(E_INFO, L_ART, "Source '%s' stopped search for artwork for group %" PRIi64 "\n", artwork_group_source[i].name, ctx->persistentid);
	  ctx->cache = NEVER;
	  return -1;
	}
      else if (ret == ART_E_ERROR)
	{
	  DPRINTF(E_LOG, L_ART, "Source '%s' returned an error for group %" PRIi64 "\n", artwork_group_source[i].name, ctx->persistentid);
	  ctx->cache = NEVER;
	}
    }

  ret = process_items(ctx, 0);

  return ret;
}


/* ------------------------------ ARTWORK API ------------------------------ */

int
artwork_get_item(struct evbuffer *evbuf, int id, int max_w, int max_h)
{
  struct artwork_ctx ctx;
  char filter[32];
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for item %d\n", id);

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  ctx.qp.type = Q_ITEMS;
  ctx.qp.filter = filter;
  ctx.evbuf = evbuf;
  ctx.max_w = max_w;
  ctx.max_h = max_h;
  ctx.cache = ON_FAILURE;

  ret = snprintf(filter, sizeof(filter), "id = %d", id);
  if ((ret < 0) || (ret >= sizeof(filter)))
    {
      DPRINTF(E_LOG, L_ART, "Could not build filter for file id %d; no artwork will be sent\n", id);
      return -1;
    }

  // Note: process_items will set ctx.persistentid for the following process_group()
  ret = process_items(&ctx, 1);
  if (ret > 0)
    {
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_INDIVIDUAL, id, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  ctx.qp.type = Q_GROUP_ITEMS;
  ctx.qp.persistentid = ctx.persistentid;

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for item %d\n", id);

  if (ctx.cache == ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

int
artwork_get_group(struct evbuffer *evbuf, int id, int max_w, int max_h)
{
  struct artwork_ctx ctx;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %d\n", id);

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  /* Get the persistent id for the given group id */
  ret = db_group_persistentid_byid(id, &ctx.persistentid);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching persistent id for group id %d\n", id);
      return -1;
    }

  ctx.qp.type = Q_GROUP_ITEMS;
  ctx.qp.persistentid = ctx.persistentid;
  ctx.evbuf = evbuf;
  ctx.max_w = max_w;
  ctx.max_h = max_h;
  ctx.cache = ON_FAILURE;

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for group %d\n", id);

  if (ctx.cache == ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

/* Checks if the file is an artwork file */
int
artwork_file_is_artwork(const char *filename)
{
  cfg_t *lib;
  int n;
  int i;
  int j;
  int ret;
  char artwork[PATH_MAX];

  lib = cfg_getsec(cfg, "library");
  n = cfg_size(lib, "artwork_basenames");

  for (i = 0; i < n; i++)
    {
      for (j = 0; j < (sizeof(cover_extension) / sizeof(cover_extension[0])); j++)
	{
	  ret = snprintf(artwork, sizeof(artwork), "%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(artwork)))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	      continue;
	    }

	  if (strcmp(artwork, filename) == 0)
	    return 1;
	}

      if (j < (sizeof(cover_extension) / sizeof(cover_extension[0])))
	break;
    }

  return 0;
}
