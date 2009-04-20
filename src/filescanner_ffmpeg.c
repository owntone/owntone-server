/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <stddef.h>
#include <stdarg.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "daapd.h"
#include "err.h"
#include "filescanner.h"


/* Legacy format-specific scanners */
extern int scan_get_wmainfo(char *filename, MP3FILE *pmp3);
#ifdef FLAC
extern int scan_get_flacinfo(char *filename, MP3FILE *pmp3);
#endif
#ifdef MUSEPACK
extern int scan_get_mpcinfo(char *filename, MP3FILE *pmp3);
#endif


/* Mapping between the metadata name(s) and the offset
 * of the equivalent metadata field in struct media_file_info */
struct metadata_map {
  char *key;
  int as_int;
  size_t offset;
};

#define mfi_offsetof(field) offsetof(struct media_file_info, field)

/* Lookup is case-insensitive, first occurrence takes precedence */
static struct metadata_map md_map[] =
  {
    { "title",        0, mfi_offsetof(title) },
    { "artist",       0, mfi_offsetof(artist) },
    { "author",       0, mfi_offsetof(artist) },
    { "albumartist",  0, mfi_offsetof(album_artist) },
    { "album",        0, mfi_offsetof(album) },
    { "genre",        0, mfi_offsetof(genre) },
    { "composer",     0, mfi_offsetof(composer) },
    { "grouping",     0, mfi_offsetof(grouping) },
    { "orchestra",    0, mfi_offsetof(orchestra) },
    { "conductor",    0, mfi_offsetof(conductor) },
    { "comment",      0, mfi_offsetof(comment) },
    { "description",  0, mfi_offsetof(comment) },
    { "totaltracks",  1, mfi_offsetof(total_tracks) },
    { "track",        1, mfi_offsetof(track) },
    { "tracknumber",  1, mfi_offsetof(track) },
    { "totaldiscs",   1, mfi_offsetof(total_discs) },
    { "disc",         1, mfi_offsetof(disc) },
    { "discnumber",   1, mfi_offsetof(disc) },
    { "year",         1, mfi_offsetof(year) },
    { "date",         1, mfi_offsetof(year) },
    { NULL,           0, 0 }
  };


int
scan_metadata_ffmpeg(char *file, struct media_file_info *mfi)
{
  AVFormatContext *ctx;
  AVMetadataTag *mdt;
  enum CodecID codec_id;
  enum CodecID video_codec_id;
  enum CodecID audio_codec_id;
  int video_stream;
  int audio_stream;
  char **strval;
  uint32_t *intval;
  char *endptr;
  long tmpval;
  int mdcount;
  int i;
  int ret;

  ret = av_open_input_file(&ctx, file, NULL, 0, NULL);
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Cannot open media file '%s': %s\n", file, strerror(AVUNERROR(ret)));

      return -1;
    }

  ret = av_find_stream_info(ctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

      av_close_input_file(ctx);
      return -1;
    }

#if 0
  /* Dump input format as determined by ffmpeg */
  dump_format(ctx, 0, file, FALSE);
#endif

  DPRINTF(E_DBG, L_SCAN, "File has %d streams\n", ctx->nb_streams);

  /* Extract codec IDs, check for video */
  video_codec_id = CODEC_ID_NONE;
  video_stream = -1;

  audio_codec_id = CODEC_ID_NONE;
  audio_stream = -1;

  for (i = 0; i < ctx->nb_streams; i++)
    {
      switch (ctx->streams[i]->codec->codec_type)
	{
	  case CODEC_TYPE_VIDEO:
	    if (video_stream == -1)
	      {
		DPRINTF(E_DBG, L_SCAN, "File has video (stream %d)\n", i);

		mfi->has_video = 1;
		video_codec_id = ctx->streams[i]->codec->codec_id;
		video_stream = i;
	      }
	    break;

	  case CODEC_TYPE_AUDIO:
	    if (audio_stream == -1)
	      {
		audio_codec_id = ctx->streams[i]->codec->codec_id;
		audio_stream = i;
	      } 
	    break;

	  default:
	    break;
	}
    }

  /* Common media information */
  if (ctx->duration > 0)
    mfi->song_length = ctx->duration / (AV_TIME_BASE / 1000); /* ms */

  if (ctx->bit_rate > 0)
    mfi->bitrate = ctx->bit_rate / 1000;
  else if (ctx->duration > 0) /* guesstimate */
    mfi->bitrate = ((mfi->file_size * 8) / (ctx->duration / AV_TIME_BASE)) / 1000;

  DPRINTF(E_DBG, L_SCAN, "Duration %d ms, bitrate %d kbps\n", mfi->song_length, mfi->bitrate);

  /* Get some more information on the audio stream */
  if (audio_stream != -1)
    {
      if (ctx->streams[audio_stream]->codec->sample_rate != 0)
	mfi->samplerate = ctx->streams[audio_stream]->codec->sample_rate;

      /* Try sample format first */
      mfi->bits_per_sample = av_get_bits_per_sample_format(ctx->streams[audio_stream]->codec->sample_fmt);
      if (mfi->bits_per_sample == 0)
	{
	  /* Try codec */
	  mfi->bits_per_sample = av_get_bits_per_sample(audio_codec_id);
	}

      DPRINTF(E_DBG, L_SCAN, "samplerate %d, bps %d\n", mfi->samplerate, mfi->bits_per_sample);
    }

  /* Check codec */
  codec_id = (mfi->has_video) ? video_codec_id : audio_codec_id;
  switch (codec_id)
    {
      case CODEC_ID_AAC:
	DPRINTF(E_DBG, L_SCAN, "AAC\n");
	mfi->type = strdup("m4a");
	mfi->codectype = strdup("mp4a");
	mfi->description = strdup("AAC audio file");
	break;

      case CODEC_ID_ALAC:
	DPRINTF(E_DBG, L_SCAN, "ALAC\n");
	mfi->type = strdup("m4a");
	mfi->codectype = strdup("alac");
	mfi->description = strdup("AAC audio file");
	break;

      case CODEC_ID_FLAC:
	DPRINTF(E_DBG, L_SCAN, "FLAC\n");
	mfi->type = strdup("flac");
	mfi->codectype = strdup("flac");
	mfi->description = strdup("FLAC audio file");
	break;

      case CODEC_ID_MUSEPACK7:
      case CODEC_ID_MUSEPACK8:
	DPRINTF(E_DBG, L_SCAN, "Musepack\n");
	mfi->type = strdup("mpc");
	mfi->codectype = strdup("mpc");
	mfi->description = strdup("Musepack audio file");
	break;

      case CODEC_ID_MPEG4: /* Video */
      case CODEC_ID_H264:
	DPRINTF(E_DBG, L_SCAN, "MPEG4 video\n");
	mfi->type = strdup("m4v");
	mfi->codectype = strdup("mp4v");
	mfi->description = strdup("MPEG-4 video file");
	break;

      case CODEC_ID_MP3:
	DPRINTF(E_DBG, L_SCAN, "MP3\n");
	mfi->type = strdup("mp3");
	mfi->codectype = strdup("mpeg");
	mfi->description = strdup("MPEG audio file");
	break;

      case CODEC_ID_VORBIS:
	DPRINTF(E_DBG, L_SCAN, "VORBIS\n");
	mfi->type = strdup("ogg");
	mfi->codectype = strdup("ogg");
	mfi->description = strdup("Ogg Vorbis audio file");
	break;

      case CODEC_ID_WMAVOICE:
	DPRINTF(E_DBG, L_SCAN, "WMA Voice\n");
	mfi->type = strdup("wma");
	mfi->codectype = strdup("wmav");
	mfi->description = strdup("WMA audio file");
	break;

      case CODEC_ID_WMAPRO:
	DPRINTF(E_DBG, L_SCAN, "WMA Pro\n");
	mfi->type = strdup("wmap");
	mfi->codectype = strdup("wma");
	mfi->description = strdup("WMA audio file");
	break;

      case CODEC_ID_WMALOSSLESS:
	DPRINTF(E_DBG, L_SCAN, "WMA Lossless\n");
	mfi->type = strdup("wma");
	mfi->codectype = strdup("wmal");
	mfi->description = strdup("WMA audio file");
	break;

      case CODEC_ID_PCM_S16LE ... CODEC_ID_PCM_F64LE:
	if (strcmp(ctx->iformat->name, "aiff") == 0)
	  {
	    DPRINTF(E_DBG, L_SCAN, "AIFF\n");
	    mfi->type = strdup("aif");
	    mfi->codectype = strdup("aif");
	    mfi->description = strdup("AIFF audio file");
	    break;
	  }
	else if (strcmp(ctx->iformat->name, "wav") == 0)
	  {
	    DPRINTF(E_DBG, L_SCAN, "WAV\n");
	    mfi->type = strdup("wav");
	    mfi->codectype = strdup("wav");
	    mfi->description = strdup("WAV audio file");
	    break;
	  }
	/* WARNING: will fallthrough to default case, don't move */
	/* FALLTHROUGH */

      default:
	DPRINTF(E_DBG, L_SCAN, "Unknown codec 0x%x (video: %s), format %s (%s)\n",
		codec_id, (mfi->has_video) ? "yes" : "no", ctx->iformat->name, ctx->iformat->long_name);
	mfi->type = strdup("unkn");
	mfi->codectype = strdup("unkn");
	if (mfi->has_video)
	  mfi->description = strdup("Unknown video file format");
	else
	  mfi->description = strdup("Unknown audio file format");
	break;
    }

  mdcount = 0;

  if (ctx->metadata == NULL)
    {
      DPRINTF(E_WARN, L_SCAN, "ffmpeg reports no metadata\n");

      goto skip_extract;
    }

#if 0
  /* Dump all the metadata reported by ffmpeg */
  mdt = NULL;
  while ((mdt = av_metadata_get(ctx->metadata, "", mdt, AV_METADATA_IGNORE_SUFFIX)) != NULL)
    fprintf(stderr, " -> %s = %s\n", mdt->key, mdt->value);
#endif

  /* Extract actual metadata */
  for (i = 0; md_map[i].key != NULL; i++)
    {
      mdt = av_metadata_get(ctx->metadata, md_map[i].key, NULL, 0);
      if (mdt == NULL)
	continue;

      if ((mdt->value == NULL) || (strlen(mdt->value) == 0))
	continue;

      mdcount++;

      if (!md_map[i].as_int)
	{
	  strval = (char **) ((char *) mfi + md_map[i].offset);

	  if (*strval == NULL)
	    *strval = strdup(mdt->value);
	}
      else
	{
	  intval = (uint32_t *) ((char *) mfi + md_map[i].offset);

	  if (*intval == 0)
	    {
	      errno = 0;
	      tmpval = strtol(mdt->value, &endptr, 10);

	      if (((errno == ERANGE) && ((tmpval == LONG_MAX) || (tmpval == LONG_MIN)))
		  || ((errno != 0) && (tmpval == 0)))
		continue;

	      if (endptr == mdt->value)
		continue;

	      if (tmpval > UINT32_MAX)
		continue;

	      *intval = (uint32_t) tmpval;
	    }
	}
    }

 skip_extract:
  if (mdcount == 0)
    {
      /* ffmpeg doesn't support FLAC nor Musepack metadata,
       * and is buggy for some WMA variants, so fall back to the
       * legacy format-specific parsers until it gets fixed */
      if ((codec_id == CODEC_ID_WMAPRO)
	  || (codec_id == CODEC_ID_WMAVOICE)
	  || (codec_id == CODEC_ID_WMALOSSLESS))
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy WMA scanner\n");

	  av_close_input_file(ctx);
	  return (scan_get_wmainfo(file, mfi) ? 0 : -1);
	}
#ifdef FLAC
      else if (codec_id == CODEC_ID_FLAC)
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy FLAC scanner\n");

	  av_close_input_file(ctx);
	  return (scan_get_flacinfo(file, mfi) ? 0 : -1);
	}
#endif /* FLAC */
#ifdef MUSEPACK
      else if ((codec_id == CODEC_ID_MUSEPACK7)
	       || (codec_id == CODEC_ID_MUSEPACK8))
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy Musepack scanner\n");

	  av_close_input_file(ctx);
	  return (scan_get_mpcinfo(file, mfi) ? 0 : -1);
	}
#endif /* MUSEPACK */
      else
	DPRINTF(E_WARN, L_SCAN, "Could not extract any metadata\n");
    }

  /* Just in case there's no title set ... */
  if (mfi->title == NULL)
    mfi->title = strdup(mfi->fname);

  /* All done */
  av_close_input_file(ctx);

  return 0;
}
