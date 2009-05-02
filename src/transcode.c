/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
#include <endian.h>
#include <byteswap.h>
#include <stdint.h>

#include <event.h>
#include "evhttp/evhttp.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "daapd.h"
#include "err.h"
#include "conffile.h"
#include "ff-dbstruct.h"
#include "transcode.h"


#define XCODE_BUFFER_SIZE ((AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2)
#define RAW_BUFFER_SIZE   256

struct transcode_ctx {
  AVFormatContext *fmtctx;
  /* Audio stream */
  int astream;
  AVCodecContext *acodec; /* pCodecCtx */
  AVCodec *adecoder; /* pCodec */
  AVPacket apacket;
  int apacket_size;
  uint8_t *apacket_data;
  uint8_t *abuffer;

  off_t offset;

  uint32_t duration;
  uint64_t samples;

  /* WAV header */
  uint8_t header[44];

  /* Raw mode */
  int fd;
  uint8_t *rawbuffer;
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
make_wav_header(struct transcode_ctx *ctx, size_t *est_size)
{
  uint32_t samplerate;
  uint32_t byte_rate;
  uint32_t wav_len;
  int duration;
  uint16_t channels;
  uint16_t block_align;
  uint16_t bits_per_sample;

  switch (ctx->acodec->sample_fmt)
    {
      case SAMPLE_FMT_S16:
	bits_per_sample = 16;
	break;
      case SAMPLE_FMT_S32:
	/* BROKEN */
	bits_per_sample = 32;
	break;
      default:
	bits_per_sample = 16;
	break;
    }

  if (ctx->duration)
    duration = ctx->duration;
  else
    duration = 3 * 60 * 1000; /* 3 minutes, in ms */

  channels = ctx->acodec->channels;
  samplerate = ctx->acodec->sample_rate;

  if (ctx->samples)
    wav_len = ((bits_per_sample * channels / 8) * ctx->samples);
  else
    wav_len = ((bits_per_sample * samplerate * channels / 8) * (duration/1000));

  *est_size = wav_len + sizeof(ctx->header);

  byte_rate = samplerate * channels * bits_per_sample / 8;
  block_align = channels * bits_per_sample / 8;

  DPRINTF(E_DBG, L_XCODE, "WAV parameters: %d channels, %d kHz, %d bps\n",
	  channels, samplerate, bits_per_sample);

  memcpy(ctx->header, "RIFF", 4);
  add_le32(ctx->header + 4, 36 + wav_len);
  memcpy(ctx->header + 8, "WAVEfmt ", 8);
  add_le32(ctx->header + 16, 16);
  add_le16(ctx->header + 20, 1);
  add_le16(ctx->header + 22, channels);
  add_le32(ctx->header + 24, samplerate);
  add_le32(ctx->header + 28, byte_rate);
  add_le16(ctx->header + 32, block_align);
  add_le16(ctx->header + 34, bits_per_sample);
  memcpy(ctx->header + 36, "data", 4);
  add_le32(ctx->header + 40, wav_len);
}


int
transcode(struct transcode_ctx *ctx, struct evbuffer *evbuf, int wanted)
{
  int processed;
  int buflen;
  int used;
  int stop;
  int ret;
#if BYTE_ORDER == BIG_ENDIAN
  int i;
  uint16_t *buf;
#endif

  processed = 0;

  if (ctx->offset == 0)
    {
      evbuffer_add(evbuf, ctx->header, sizeof(ctx->header));
      processed += sizeof(ctx->header);
      ctx->offset += sizeof(ctx->header);
    }

  stop = 0;
  while ((processed < wanted) && !stop)
    {
      /* Decode data */
      while (ctx->apacket_size > 0)
	{
	  buflen = XCODE_BUFFER_SIZE;

	  used = avcodec_decode_audio2(ctx->acodec,
				       (int16_t *)ctx->abuffer, &buflen,
				       ctx->apacket_data, ctx->apacket_size);

	  if (used < 0)
	    {
	      /* Something happened, skip this packet */
	      ctx->apacket_size = 0;
	      break;
	    }

	  ctx->apacket_data += used;
	  ctx->apacket_size -= used;

	  /* No frame decoded this time around */
	  if (buflen == 0)
	    continue;

#if BYTE_ORDER == BIG_ENDIAN
	  /* swap buffer, le16 */
	  buf = (uint16_t *)ctx->abuffer;
	  for (i = 0; i < (buflen / 2); i++)
	    {
	      buf[i] = htole16(buf[i]);
	    }
#endif

	  ret = evbuffer_add(evbuf, ctx->abuffer, buflen);
	  if (ret != 0)
	    {
	      DPRINTF(E_WARN, L_XCODE, "Could not copy WAV data to buffer\n");

	      return -1;
	    }

	  processed += buflen;
	}

      /* Read more data */
      if (ctx->fd != -1)
	{
	  /* Raw mode */
	  ret = read(ctx->fd, ctx->rawbuffer, RAW_BUFFER_SIZE);
	  if (ret <= 0)
	    {
	      DPRINTF(E_WARN, L_XCODE, "Could not read more raw data\n");

	      stop = 1;
	      break;
	    }

	  ctx->apacket_data = ctx->rawbuffer;
	  ctx->apacket_size = ret;
	}
      else
	{
	  /* ffmpeg mode */
	  do
	    {
	      if (ctx->apacket.data)
		av_free_packet(&ctx->apacket);

	      ret = av_read_packet(ctx->fmtctx, &ctx->apacket);
	      if (ret < 0)
		{
		  DPRINTF(E_WARN, L_XCODE, "Could not read more data\n");

		  stop = 1;
		  break;
		}
	    }
	  while (ctx->apacket.stream_index != ctx->astream);

	  /* Copy apacket data & size and do not mess with them */
	  ctx->apacket_data = ctx->apacket.data;
	  ctx->apacket_size = ctx->apacket.size;
	}
    }

  ctx->offset += processed;

  return processed;
}

struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, size_t *est_size)
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
  ctx->fd = -1;

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

  /* FLAC needs raw mode, ffmpeg sucks */
  if (ctx->acodec->codec_id == CODEC_ID_FLAC)
    {
      ctx->rawbuffer = (uint8_t *)malloc(RAW_BUFFER_SIZE);
      if (!ctx->rawbuffer)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not allocate raw buffer\n");

	  avcodec_close(ctx->acodec);
	  goto setup_fail;
	}

      ctx->fd = open(mfi->path, O_RDONLY);
      if (ctx->fd < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not open %s: %s\n", mfi->fname, strerror(errno));

	  free(ctx->rawbuffer);
	  avcodec_close(ctx->acodec);
	  goto setup_fail;
	}
    }

  if (ctx->fd != -1)
    DPRINTF(E_DBG, L_XCODE, "Set up raw mode for transcoding input\n");

  ctx->abuffer = (uint8_t *)malloc(XCODE_BUFFER_SIZE);
  if (!ctx->abuffer)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not allocate transcode buffer\n");

      if (ctx->fd != -1)
	{
	  close(ctx->fd);
	  free(ctx->rawbuffer);
	}
      avcodec_close(ctx->acodec);
      goto setup_fail;
    }

  ctx->duration = mfi->song_length;
  ctx->samples = mfi->sample_count;

  make_wav_header(ctx, est_size);

  return ctx;

 setup_fail:
  av_close_input_file(ctx->fmtctx);
  free(ctx);
  return NULL;
}

void
transcode_cleanup(struct transcode_ctx *ctx)
{
  if (ctx->fd != -1)
    {
      close(ctx->fd);
      free(ctx->rawbuffer);
    }
  if (ctx->apacket.data)
    av_free_packet(&ctx->apacket);
  avcodec_close(ctx->acodec);
  av_close_input_file(ctx->fmtctx);

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

  lib = cfg_getnsec(cfg, "library", 0);

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
