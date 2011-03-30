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

#include <event.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "db.h"
#include "logger.h"
#include "artwork.h"


static const char *cover_basename[] =
  {
    "artwork", "cover",
  };

static const char *cover_extension[] =
  {
    "png", "jpg",
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
artwork_rescale(AVFormatContext *src_ctx, int s, int out_w, int out_h, int format, struct evbuffer *evbuf)
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

  int64_t pix_fmt_mask;
  const enum PixelFormat *pix_fmts;

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

  ret = avcodec_open(src, img_decoder);
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

  dst_fmt->video_codec = CODEC_ID_NONE;

  /* Try to keep same codec if possible */
  if ((src->codec_id == CODEC_ID_PNG) && (format & ART_CAN_PNG))
    dst_fmt->video_codec = CODEC_ID_PNG;
  else if ((src->codec_id == CODEC_ID_MJPEG) && (format & ART_CAN_JPEG))
    dst_fmt->video_codec = CODEC_ID_MJPEG;

  /* If not possible, select new codec */
  if (dst_fmt->video_codec == CODEC_ID_NONE)
    {
      if (format & ART_CAN_PNG)
	dst_fmt->video_codec = CODEC_ID_PNG;
      else if (format & ART_CAN_JPEG)
	dst_fmt->video_codec = CODEC_ID_MJPEG;
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

  ret = snprintf(dst_ctx->filename, sizeof(dst_ctx->filename), "evbuffer:%p", evbuf);
  if ((ret < 0) || (ret >= sizeof(dst_ctx->filename)))
    {
      DPRINTF(E_LOG, L_ART, "Output artwork URL too long\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  dst_st = av_new_stream(dst_ctx, 0);
  if (!dst_st)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for new output stream\n");

      ret = -1;
      goto out_free_dst_ctx;
    }

  dst = dst_st->codec;

  avcodec_get_context_defaults2(dst, CODEC_TYPE_VIDEO);

  if (dst_fmt->flags & AVFMT_GLOBALHEADER)
    dst->flags |= CODEC_FLAG_GLOBAL_HEADER;

  dst->codec_id = dst_fmt->video_codec;
  dst->codec_type = CODEC_TYPE_VIDEO;

  pix_fmt_mask = 0;
  pix_fmts = img_encoder->pix_fmts;
  while (pix_fmts && (*pix_fmts != -1))
    {
      pix_fmt_mask |= (1 << *pix_fmts);
      pix_fmts++;
    }

  dst->pix_fmt = avcodec_find_best_pix_fmt(pix_fmt_mask, src->pix_fmt, 1, NULL);

  if (dst->pix_fmt < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not determine best pixel format\n");

      goto out_free_dst;
      ret = -1;
    }

  DPRINTF(E_DBG, L_ART, "Selected pixel format: %d\n", dst->pix_fmt);

  dst->time_base.num = 1;
  dst->time_base.den = 25;

  dst->width = out_w;
  dst->height = out_h;

  ret = av_set_parameters(dst_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Invalid parameters for artwork output: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_free_dst;
    }

  /* Open encoder */
  ret = avcodec_open(dst, img_encoder);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not open codec for encoding: %s\n", strerror(AVUNERROR(ret)));

      ret = -1;
      goto out_free_dst;
    }

  i_frame = avcodec_alloc_frame();
  o_frame = avcodec_alloc_frame();

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
#if (LIBSWSCALE_VERSION_MAJOR == 0 && LIBSWSCALE_VERSION_MINOR >= 9)
  /* FFmpeg 0.6 */
  sws_scale(swsctx, (const uint8_t * const *)i_frame->data, i_frame->linesize, 0, src->height, o_frame->data, o_frame->linesize);
#else
  sws_scale(swsctx, i_frame->data, i_frame->linesize, 0, src->height, o_frame->data, o_frame->linesize);
#endif

  sws_freeContext(swsctx);
  av_free_packet(&pkt);

  /* Open output file */
  ret = url_fopen(&dst_ctx->pb, dst_ctx->filename, URL_WRONLY);
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

      url_fclose(dst_ctx->pb);

      ret = -1;
      goto out_free_buf;
    }

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

  ret = av_write_header(dst_ctx);
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
      case CODEC_ID_PNG:
	ret = ART_FMT_PNG;
	break;

      case CODEC_ID_MJPEG:
	ret = ART_FMT_JPEG;
	break;

      default:
	DPRINTF(E_LOG, L_ART, "Unhandled rescale output format\n");
	ret = -1;
	break;
    }

 out_fclose_dst:
  url_fclose(dst_ctx->pb);
  av_free(outbuf);

 out_free_buf:
  av_free(buf);

 out_free_frames:
  if (i_frame)
    av_free(i_frame);
  if (o_frame)
    av_free(o_frame);
  avcodec_close(dst);

 out_free_dst:
  av_free(dst_st);
  av_free(dst);

 out_free_dst_ctx:
  av_free(dst_ctx);

 out_close_src:
  avcodec_close(src);

  return ret;
}

static int
artwork_get(char *filename, int max_w, int max_h, int format, struct evbuffer *evbuf)
{
  AVFormatContext *src_ctx;
  AVCodecContext *src;
  int s;
  int target_w;
  int target_h;
  int need_rescale;
  int format_ok;
  int ret;

  ret = av_open_input_file(&src_ctx, filename, NULL, 0, NULL);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot open artwork file '%s': %s\n", filename, strerror(AVUNERROR(ret)));

      return -1;
    }

  ret = av_find_stream_info(src_ctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

      av_close_input_file(src_ctx);
      return -1;
    }

  format_ok = 0;
  for (s = 0; s < src_ctx->nb_streams; s++)
    {
      if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_PNG)
	{
	  format_ok = (format & ART_CAN_PNG) ? ART_FMT_PNG : 0;
	  break;
	}
      else if (src_ctx->streams[s]->codec->codec_id == CODEC_ID_MJPEG)
	{
	  format_ok = (format & ART_CAN_JPEG) ? ART_FMT_JPEG : 0;
	  break;
	}
    }

  if (s == src_ctx->nb_streams)
    {
      DPRINTF(E_LOG, L_ART, "Artwork file '%s' not a PNG or JPEG file\n", filename);

      av_close_input_file(src_ctx);
      return -1;
    }

  src = src_ctx->streams[s]->codec;

  DPRINTF(E_DBG, L_ART, "Original image '%s': w %d h %d\n", filename, src->width, src->height);

  need_rescale = 1;

  /* Determine width/height -- assuming max_w == max_h */
  if ((src->width <= max_w) && (src->height <= max_h))
    {
      need_rescale = 0;

      target_w = src->width;
      target_h = src->height;
    }
  else if (src->width > src->height)
    {
      target_w = max_w;
      target_h = (double)max_h * ((double)src->height / (double)src->width);
    }
  else if (src->height > src->width)
    {
      target_h = max_h;
      target_w = (double)max_w * ((double)src->width / (double)src->height);
    }
  else
    {
      target_w = max_w;
      target_h = max_h;
    }

  DPRINTF(E_DBG, L_ART, "Raw destination width %d height %d\n", target_w, target_h);

  if (target_h > max_h)
    target_h = max_h;

  /* PNG prefers even row count */
  target_w += target_w % 2;

  if (target_w > max_w)
    target_w = max_w - (max_w % 2);

  DPRINTF(E_DBG, L_ART, "Destination width %d height %d\n", target_w, target_h);

  /* Fastpath */
  if (!need_rescale && format_ok)
    {
      ret = artwork_read(filename, evbuf);
      if (ret == 0)
	ret = format_ok;
    }
  else
    ret = artwork_rescale(src_ctx, s, target_w, target_h, format, evbuf);

  av_close_input_file(src_ctx);

  if (ret < 0)
    {
      if (EVBUFFER_LENGTH(evbuf) > 0)
	evbuffer_drain(evbuf, EVBUFFER_LENGTH(evbuf));
    }

  return ret;
}


static int
artwork_get_own_image(char *path, int max_w, int max_h, int format, struct evbuffer *evbuf)
{
  char artwork[PATH_MAX];
  char *ptr;
  int len;
  int i;
  int ret;

  ret = snprintf(artwork, sizeof(artwork), "%s", path);
  if ((ret < 0) || (ret >= sizeof(artwork)))
    {
      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX\n");

      return -1;
    }

  ptr = strrchr(artwork, '.');
  if (ptr)
    *ptr = '\0';

  len = strlen(artwork);

  for (i = 0; i < (sizeof(cover_extension) / sizeof(cover_extension[0])); i++)
    {
      ret = snprintf(artwork + len, sizeof(artwork) - len, ".%s", cover_extension[i]);
      if ((ret < 0) || (ret >= sizeof(artwork) - len))
	{
	  DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (ext %s)\n", cover_extension[i]);

	  continue;
	}

      DPRINTF(E_DBG, L_ART, "Trying own artwork file %s\n", artwork);

      ret = access(artwork, F_OK);
      if (ret < 0)
	continue;

      break;
    }

  if (i == (sizeof(cover_extension) / sizeof(cover_extension[0])))
    return -1;

  return artwork_get(artwork, max_w, max_h, format, evbuf);
}

static int
artwork_get_dir_image(char *path, int isdir, int max_w, int max_h, int format, struct evbuffer *evbuf)
{
  char artwork[PATH_MAX];
  char *ptr;
  int i;
  int j;
  int len;
  int ret;

  ret = snprintf(artwork, sizeof(artwork), "%s", path);
  if ((ret < 0) || (ret >= sizeof(artwork)))
    {
      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX\n");

      return -1;
    }

  if (!isdir)
    {
      ptr = strrchr(artwork, '/');
      if (ptr)
	*ptr = '\0';
    }

  len = strlen(artwork);

  for (i = 0; i < (sizeof(cover_basename) / sizeof(cover_basename[0])); i++)
    {
      for (j = 0; j < (sizeof(cover_extension) / sizeof(cover_extension[0])); j++)
	{
	  ret = snprintf(artwork + len, sizeof(artwork) - len, "/%s.%s", cover_basename[i], cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(artwork) - len))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", cover_basename[i], cover_extension[j]);

	      continue;
	    }

	  DPRINTF(E_DBG, L_ART, "Trying directory artwork file %s\n", artwork);

	  ret = access(artwork, F_OK);
	  if (ret < 0)
	    continue;

	  break;
	}

      if (j < (sizeof(cover_extension) / sizeof(cover_extension[0])))
	break;
    }

  if (i == (sizeof(cover_basename) / sizeof(cover_basename[0])))
    return -1;

  return artwork_get(artwork, max_w, max_h, format, evbuf);
}


int
artwork_get_item(int id, int max_w, int max_h, int format, struct evbuffer *evbuf)
{
  char *filename;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for item %d, max w = %d, max h = %d\n", id, max_w, max_h);

  filename = db_file_path_byid(id);
  if (!filename)
    return -1;

  /* FUTURE: look at embedded artwork */

  /* Look for basename(filename).{png,jpg} */
  ret = artwork_get_own_image(filename, max_w, max_h, format, evbuf);
  if (ret > 0)
    goto out;

  /* Look for basedir(filename)/{artwork,cover}.{png,jpg} */
  ret = artwork_get_dir_image(filename, 0, max_w, max_h, format, evbuf);
  if (ret > 0)
    goto out;

  DPRINTF(E_DBG, L_ART, "No artwork found for item id %d\n", id);

 out:
  free(filename);
  return ret;
}

int
artwork_get_group(int id, int max_w, int max_h, int format, struct evbuffer *evbuf)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  char *dir;
  int got_art;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %d, max w = %d, max h = %d\n", id, max_w, max_h);

  /* Try directory artwork first */
  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUP_DIRS;
  qp.id = id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUP_DIRS query\n");

      /* Skip to invidual files artwork */
      goto files_art;
    }

  got_art = -1;
  while ((got_art < 0) && ((ret = db_query_fetch_string(&qp, &dir)) == 0) && (dir))
    {
      got_art = artwork_get_dir_image(dir, 1, max_w, max_h, format, evbuf);
    }

  db_query_end(&qp);

  if (ret < 0)
    DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUP_DIRS results\n");
  else if (got_art > 0)
    return got_art;


  /* Then try individual files */
 files_art:
  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUPITEMS;
  qp.id = id;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUPITEMS query\n");

      return -1;
    }

  got_art = -1;
  while ((got_art < 0) && ((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      got_art = artwork_get_own_image(dbmfi.path, max_w, max_h, format, evbuf);
    }

  db_query_end(&qp);

  if (ret < 0)
    DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUPITEMS results\n");
  else if (got_art > 0)
    return got_art;

  return -1;
}
