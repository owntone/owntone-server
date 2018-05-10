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
#include <libavutil/imgutils.h>

#include "db.h"
#include "misc.h"
#include "logger.h"
#include "conffile.h"
#include "cache.h"
#include "http.h"

#include "avio_evbuffer.h"
#include "artwork.h"

#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

#include "ffmpeg-compat.h"

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

  // Input data to handler, requested width and height
  int max_w;
  int max_h;
  // Input data to handler, did user configure to look for individual artwork
  int individual;

  // Input data for item handlers
  struct db_media_file_info *dbmfi;
  int id;
  // Input data for group handlers
  int64_t persistentid;

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


/* ----------------- DECLARE AND CONFIGURE SOURCE HANDLERS ----------------- */

/* Forward - group handlers */
static int source_group_cache_get(struct artwork_ctx *ctx);
static int source_group_dir_get(struct artwork_ctx *ctx);
/* Forward - item handlers */
static int source_item_cache_get(struct artwork_ctx *ctx);
static int source_item_embedded_get(struct artwork_ctx *ctx);
static int source_item_own_get(struct artwork_ctx *ctx);
static int source_item_stream_get(struct artwork_ctx *ctx);
static int source_item_spotify_get(struct artwork_ctx *ctx);
static int source_item_ownpl_get(struct artwork_ctx *ctx);

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
      .name = "playlist own",
      .handler = source_item_ownpl_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = NULL,
      .handler = NULL,
      .data_kinds = 0,
      .cache = 0,
    }
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

  ret = avcodec_open2(src, img_decoder, NULL);
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

  dst_st = avformat_new_stream(dst_ctx, NULL);
  if (!dst_st)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for new output stream\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  dst = dst_st->codec;

  avcodec_get_context_defaults3(dst, NULL);

  if (dst_fmt->flags & AVFMT_GLOBALHEADER)
    dst->flags |= CODEC_FLAG_GLOBAL_HEADER;

  dst->codec_id = dst_fmt->video_codec;
  dst->codec_type = AVMEDIA_TYPE_VIDEO;

  dst->pix_fmt = avcodec_default_get_format(dst, img_encoder->pix_fmts);
  if (dst->pix_fmt < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not determine best pixel format\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  dst->time_base.num = 1;
  dst->time_base.den = 25;

  dst->width = out_w;
  dst->height = out_h;

  /* Open encoder */
  ret = avcodec_open2(dst, img_encoder, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open codec for encoding: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_free_dst_ctx;
    }

  i_frame = av_frame_alloc();
  o_frame = av_frame_alloc();
  if (!i_frame || !o_frame)
    {
      DPRINTF(E_LOG, L_ART, "Could not allocate input/output frame\n");

      ret = -1;
      goto out_free_frames;
    }

  ret = av_image_get_buffer_size(dst->pix_fmt, src->width, src->height, 1);

  DPRINTF(E_DBG, L_ART, "Artwork buffer size: %d\n", ret);

  buf = (uint8_t *)av_malloc(ret);
  if (!buf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for artwork buffer\n");

      ret = -1;
      goto out_free_frames;
    }

#if HAVE_DECL_AV_IMAGE_FILL_ARRAYS
  av_image_fill_arrays(o_frame->data, o_frame->linesize, buf, dst->pix_fmt, src->width, src->height, 1);
#else
  avpicture_fill((AVPicture *)o_frame, buf, dst->pix_fmt, src->width, src->height);
#endif

  o_frame->height = dst->height;
  o_frame->width  = dst->width;
  o_frame->format = dst->pix_fmt;

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
	  av_packet_unref(&pkt);
	  continue;
	}

      avcodec_decode_video2(src, i_frame, &have_frame, &pkt);
      break;
    }

  if (!have_frame)
    {
      DPRINTF(E_LOG, L_ART, "Could not decode artwork\n");

      av_packet_unref(&pkt);
      sws_freeContext(swsctx);

      ret = -1;
      goto out_free_buf;
    }

  /* Scale */
  sws_scale(swsctx, (const uint8_t * const *)i_frame->data, i_frame->linesize, 0, src->height, o_frame->data, o_frame->linesize);

  sws_freeContext(swsctx);
  av_packet_unref(&pkt);

  /* Open output file */
  dst_ctx->pb = avio_output_evbuffer_open(evbuf);
  if (!dst_ctx->pb)
    {
      DPRINTF(E_LOG, L_ART, "Could not open artwork destination buffer\n");

      ret = -1;
      goto out_free_buf;
    }

  /* Encode frame */
  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  ret = avcodec_encode_video2(dst, &pkt, o_frame, &have_frame);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not encode artwork\n");

      ret = -1;
      goto out_fclose_dst;
    }

  ret = avformat_write_header(dst_ctx, NULL);
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
  av_packet_unref(&pkt);

 out_free_buf:
  av_free(buf);

 out_free_frames:
  if (i_frame)
    av_frame_free(&i_frame);
  if (o_frame)
    av_frame_free(&o_frame);
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
 * @return        ART_FMT_* on success, ART_E_ERROR on error
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

  DPRINTF(E_SPAM, L_ART, "Getting artwork (max destination width %d height %d)\n", max_w, max_h);

  src_ctx = NULL;

  ret = avformat_open_input(&src_ctx, path, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open artwork file '%s': %s\n", path, strerror(AVUNERROR(ret)));

      return ART_E_ERROR;
    }

  ret = avformat_find_stream_info(src_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

      avformat_close_input(&src_ctx);
      return ART_E_ERROR;
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

      avformat_close_input(&src_ctx);
      return ART_E_ERROR;
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

  avformat_close_input(&src_ctx);

  if (ret < 0)
    {
      if (evbuffer_get_length(evbuf) > 0)
	evbuffer_drain(evbuf, evbuffer_get_length(evbuf));

      ret = ART_E_ERROR;
    }

  return ret;
}

/* Looks for an artwork file in a directory. Will rescale if needed.
 *
 * @out evbuf     Image data
 * @in  dir       Directory to search
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @out out_path  Path to the artwork file if found, must be a char[PATH_MAX] buffer
 * @return        ART_FMT_* on success, ART_E_NONE on nothing found, ART_E_ERROR on error
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
      return ART_E_ERROR;
    }

  len = strlen(path);

  lib = cfg_getsec(cfg, "library");
  nbasenames = cfg_size(lib, "artwork_basenames");

  if (nbasenames == 0)
    return ART_E_NONE;

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
	  return ART_E_ERROR;
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
	return ART_E_NONE;
    }

  snprintf(out_path, PATH_MAX, "%s", path);

  return artwork_get(evbuf, path, max_w, max_h);
}


/* ---------------------- SOURCE HANDLER IMPLEMENTATION -------------------- */

/* Looks in the cache for group artwork
 */
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

  if (!format)
    return ART_E_ABORT;

  return format;
}

/* Looks for cover files in a directory, so if dir is /foo/bar and the user has
 * configured the cover file names "cover" and "artwork" it will look for
 * /foo/bar/cover.{png,jpg}, /foo/bar/artwork.{png,jpg} and also
 * /foo/bar/bar.{png,jpg} (so-called parentdir artwork)
 */
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

/* Looks in the cache for item artwork. Only relevant if configured to look for
 * individual artwork.
 */
static int
source_item_cache_get(struct artwork_ctx *ctx)
{
  int format;
  int cached;
  int ret;

  if (!ctx->individual)
    return ART_E_NONE;

  ret = cache_artwork_get(CACHE_ARTWORK_INDIVIDUAL, ctx->id, ctx->max_w, ctx->max_h, &cached, &format, ctx->evbuf);
  if (ret < 0)
    return ART_E_ERROR;

  if (!cached)
    return ART_E_NONE;

  if (!format)
    return ART_E_ABORT;

  return format;
}

/* Get an embedded artwork file from a media file. Will rescale if needed.
 */
static int
source_item_embedded_get(struct artwork_ctx *ctx)
{
  AVFormatContext *src_ctx;
  AVStream *src_st;
  int s;
  int target_w;
  int target_h;
  int format;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying embedded artwork in %s\n", ctx->dbmfi->path);

  src_ctx = NULL;

  ret = avformat_open_input(&src_ctx, ctx->dbmfi->path, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open media file '%s': %s\n", ctx->dbmfi->path, strerror(AVUNERROR(ret)));
      return ART_E_ERROR;
    }

  ret = avformat_find_stream_info(src_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));
      avformat_close_input(&src_ctx);
      return ART_E_ERROR;
    }

  format = 0;
  for (s = 0; s < src_ctx->nb_streams; s++)
    {
      if (src_ctx->streams[s]->disposition & AV_DISPOSITION_ATTACHED_PIC)
	{
	  if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_PNG)
	    {
	      format = ART_FMT_PNG;
	      break;
	    }
	  else if (src_ctx->streams[s]->codec->codec_id == AV_CODEC_ID_MJPEG)
	    {
	      format = ART_FMT_JPEG;
	      break;
	    }
	}
    }

  if (s == src_ctx->nb_streams)
    {
      avformat_close_input(&src_ctx);
      return ART_E_NONE;
    }

  src_st = src_ctx->streams[s];

  ret = rescale_needed(src_st->codec, ctx->max_w, ctx->max_h, &target_w, &target_h);

  /* Fastpath */
  if (!ret && format)
    {
      DPRINTF(E_SPAM, L_ART, "Artwork not too large, using original image\n");

      ret = evbuffer_add(ctx->evbuf, src_st->attached_pic.data, src_st->attached_pic.size);
      if (ret < 0)
	DPRINTF(E_LOG, L_ART, "Could not add embedded image to event buffer\n");
      else
        ret = format;
    }
  else
    {
      DPRINTF(E_SPAM, L_ART, "Artwork too large, rescaling image\n");

      ret = artwork_rescale(ctx->evbuf, src_ctx, s, target_w, target_h);
    }

  avformat_close_input(&src_ctx);

  if (ret < 0)
    {
      if (evbuffer_get_length(ctx->evbuf) > 0)
	evbuffer_drain(ctx->evbuf, evbuffer_get_length(ctx->evbuf));

      ret = ART_E_ERROR;
    }
  else
    snprintf(ctx->path, sizeof(ctx->path), "%s", ctx->dbmfi->path);

  return ret;
}

/* Looks for basename(in_path).{png,jpg}, so if in_path is /foo/bar.mp3 it
 * will look for /foo/bar.png and /foo/bar.jpg
 */
static int
source_item_own_get(struct artwork_ctx *ctx)
{
  char path[PATH_MAX];
  char *ptr;
  int len;
  int nextensions;
  int i;
  int ret;

  ret = snprintf(path, sizeof(path), "%s", ctx->dbmfi->path);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", ctx->dbmfi->path);
      return ART_E_ERROR;
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
	  DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", ctx->dbmfi->path);
	  continue;
	}

      DPRINTF(E_SPAM, L_ART, "Trying own artwork file %s\n", path);

      ret = access(path, F_OK);
      if (ret < 0)
	continue;

      break;
    }

  if (i == nextensions)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", path);

  return artwork_get(ctx->evbuf, path, ctx->max_w, ctx->max_h);
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
  struct db_queue_item *queue_item;
  struct keyval *kv;
  const char *content_type;
  char *url;
  char *ext;
  int len;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying internet stream artwork in %s\n", ctx->dbmfi->path);

  ret = ART_E_NONE;

  queue_item = db_queue_fetch_byfileid(ctx->id);
  if (!queue_item || !queue_item->artwork_url)
    {
      free_queue_item(queue_item, 0);
      return ART_E_NONE;
    }

  url = strdup(queue_item->artwork_url);
  free_queue_item(queue_item, 0);

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
  client.input_headers = kv;
  client.input_body = ctx->evbuf;

  if (http_client_request(&client) < 0)
    goto out_kv;

  content_type = keyval_get(kv, "Content-Type");
  if (content_type && (strcmp(content_type, "image/jpeg") == 0))
    ret = ART_FMT_JPEG;
  else if (content_type && (strcmp(content_type, "image/png") == 0))
    ret = ART_FMT_PNG;

  if (ret > 0)
    {
      DPRINTF(E_SPAM, L_ART, "Found internet stream artwork in %s (%s)\n", url, content_type);
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
  AVFormatContext *src_ctx;
  AVIOContext *avio;
  AVInputFormat *ifmt;
  struct evbuffer *raw;
  struct evbuffer *evbuf;
  int target_w;
  int target_h;
  int ret;

  raw = evbuffer_new();
  evbuf = evbuffer_new();
  if (!raw || !evbuf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for Spotify evbuf\n");
      return ART_E_ERROR;
    }

  ret = spotify_artwork_get(raw, ctx->dbmfi->path, ctx->max_w, ctx->max_h);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "No artwork from Spotify for %s\n", ctx->dbmfi->path);
      evbuffer_free(raw);
      evbuffer_free(evbuf);
      return ART_E_NONE;
    }

  // Make a refbuf of raw for ffmpeg image size probing and possibly rescaling.
  // We keep raw around in case rescaling is not necessary.
#ifdef HAVE_LIBEVENT2_OLD
  uint8_t *buf = evbuffer_pullup(raw, -1);
  if (!buf)
    {
      DPRINTF(E_LOG, L_ART, "Could not pullup raw artwork\n");
      goto out_free_evbuf;
    }

  ret = evbuffer_add_reference(evbuf, buf, evbuffer_get_length(raw), NULL, NULL);
#else
  ret = evbuffer_add_buffer_reference(evbuf, raw);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not copy/ref raw image for ffmpeg\n");
      goto out_free_evbuf;
    }

  // Now evbuf will be processed by ffmpeg, since it probably needs to be rescaled
  src_ctx = avformat_alloc_context();
  if (!src_ctx)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for source context\n");
      goto out_free_evbuf;
    }

  avio = avio_input_evbuffer_open(evbuf);
  if (!avio)
    {
      DPRINTF(E_LOG, L_ART, "Could not alloc input evbuffer\n");
      goto out_free_ctx;
    }

  src_ctx->pb = avio;

  ifmt = av_find_input_format("mjpeg");
  if (!ifmt)
    {
      DPRINTF(E_LOG, L_ART, "Could not find mjpeg input format\n");
      goto out_close_avio;
    }

  ret = avformat_open_input(&src_ctx, NULL, ifmt, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open input\n");
      goto out_close_avio;
    }

  ret = avformat_find_stream_info(src_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not find stream info\n");
      goto out_close_input;
    }

  ret = rescale_needed(src_ctx->streams[0]->codec, ctx->max_w, ctx->max_h, &target_w, &target_h);
  if (!ret)
    ret = evbuffer_add_buffer(ctx->evbuf, raw);
  else
    ret = artwork_rescale(ctx->evbuf, src_ctx, 0, target_w, target_h);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not add or rescale image to output evbuf\n");
      goto out_close_input;
    }

  avformat_close_input(&src_ctx);
  avio_evbuffer_close(avio);
  evbuffer_free(evbuf);
  evbuffer_free(raw);

  return ART_FMT_JPEG;

 out_close_input:
  avformat_close_input(&src_ctx);
 out_close_avio:
  avio_evbuffer_close(avio);
 out_free_ctx:
  if (src_ctx)
    avformat_free_context(src_ctx);
 out_free_evbuf:
  evbuffer_free(evbuf);
  evbuffer_free(raw);

  return ART_E_ERROR;

}
#else
static int
source_item_spotify_get(struct artwork_ctx *ctx)
{
  return ART_E_ERROR;
}
#endif

/* First looks of the mfi->path is in any playlist, and if so looks in the dir
 * of the playlist file (m3u et al) to see if there is any artwork. So if the
 * playlist is /foo/bar.m3u it will look for /foo/bar.png and /foo/bar.jpg.
 */
static int
source_item_ownpl_get(struct artwork_ctx *ctx)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  char filter[PATH_MAX + 64];
  char *mfi_path;
  int format;
  int ret;

  ret = snprintf(filter, sizeof(filter), "(filepath = '%s')", ctx->dbmfi->path);
  if ((ret < 0) || (ret >= sizeof(filter)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", ctx->dbmfi->path);
      return ART_E_ERROR;
    }

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_FIND_PL;
  qp.filter = filter;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start ownpl query\n");
      return ART_E_ERROR;
    }

  mfi_path = ctx->dbmfi->path;

  format = ART_E_NONE;
  while (((ret = db_query_fetch_pl(&qp, &dbpli, 0)) == 0) && (dbpli.id) && (format == ART_E_NONE))
    {
      if (!dbpli.path)
	continue;

      ctx->dbmfi->path = dbpli.path;
      format = source_item_own_get(ctx);
    }

  ctx->dbmfi->path = mfi_path;

  if ((ret < 0) || (format < 0))
    format = ART_E_ERROR;

  db_query_end(&qp);

  return format;
}


/* --------------------------- SOURCE PROCESSING --------------------------- */

static int
process_items(struct artwork_ctx *ctx, int item_mode)
{
  struct db_media_file_info dbmfi;
  uint32_t data_kind;
  int i;
  int ret;

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

      if (item_mode && !ctx->individual)
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

	  DPRINTF(E_SPAM, L_ART, "Checking item source '%s'\n", artwork_item_source[i].name);

	  ctx->dbmfi = &dbmfi;
	  ret = artwork_item_source[i].handler(ctx);
	  ctx->dbmfi = NULL;

	  if (ret > 0)
	    {
	      DPRINTF(E_DBG, L_ART, "Artwork for '%s' found in source '%s'\n", dbmfi.title, artwork_item_source[i].name);
	      ctx->cache = (artwork_item_source[i].cache & ON_SUCCESS);
	      db_query_end(&ctx->qp);
	      return ret;
	    }
	  else if (ret == ART_E_ABORT)
	    {
	      DPRINTF(E_DBG, L_ART, "Source '%s' stopped search for artwork for '%s'\n", artwork_item_source[i].name, dbmfi.title);
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

      DPRINTF(E_SPAM, L_ART, "Checking group source '%s'\n", artwork_group_source[i].name);

      ret = artwork_group_source[i].handler(ctx);
      if (ret > 0)
	{
	  DPRINTF(E_DBG, L_ART, "Artwork for group %" PRIi64 " found in source '%s'\n", ctx->persistentid, artwork_group_source[i].name);
	  ctx->cache = (artwork_group_source[i].cache & ON_SUCCESS);
	  return ret;
	}
      else if (ret == ART_E_ABORT)
	{
	  DPRINTF(E_DBG, L_ART, "Source '%s' stopped search for artwork for group %" PRIi64 "\n", artwork_group_source[i].name, ctx->persistentid);
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

  if (id == DB_MEDIA_FILE_NON_PERSISTENT_ID)
    return  -1;

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  ctx.qp.type = Q_ITEMS;
  ctx.qp.filter = filter;
  ctx.evbuf = evbuf;
  ctx.max_w = max_w;
  ctx.max_h = max_h;
  ctx.cache = ON_FAILURE;
  ctx.individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

  ret = snprintf(filter, sizeof(filter), "id = %d", id);
  if ((ret < 0) || (ret >= sizeof(filter)))
    {
      DPRINTF(E_LOG, L_ART, "Could not build filter for file id %d; no artwork will be sent\n", id);
      return -1;
    }

  // Note: process_items will set ctx.persistentid for the following process_group()
  // - and do nothing else if artwork_individual is not configured by user
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
  ctx.individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

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
