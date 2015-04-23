/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
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
#include <string.h>

#include <errno.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>

#include "logger.h"
#include "filescanner.h"
#include "misc.h"
#include "http.h"


/* Legacy format-specific scanners */
extern int scan_get_wmainfo(char *filename, struct media_file_info *pmp3);
#ifdef FLAC
extern int scan_get_flacinfo(char *filename, struct media_file_info *pmp3);
#endif
#ifdef MUSEPACK
extern int scan_get_mpcinfo(char *filename, struct media_file_info *pmp3);
#endif


/* Mapping between the metadata name(s) and the offset
 * of the equivalent metadata field in struct media_file_info */
struct metadata_map {
  char *key;
  int as_int;
  size_t offset;
  int (*handler_function)(struct media_file_info *, char *);
};

static int
parse_slash_separated_ints(char *string, uint32_t *firstval, uint32_t *secondval)
{
  int numvals = 0;
  char *ptr;

  ptr = strchr(string, '/');
  if (ptr)
    {
      *ptr = '\0';
      if (safe_atou32(ptr + 1, secondval) == 0)
        numvals++;
    }

  if (safe_atou32(string, firstval) == 0)
    numvals++;

  return numvals;
}

static int
parse_track(struct media_file_info *mfi, char *track_string)
{
  uint32_t *track = (uint32_t *) ((char *) mfi + mfi_offsetof(track));
  uint32_t *total_tracks = (uint32_t *) ((char *) mfi + mfi_offsetof(total_tracks));

  return parse_slash_separated_ints(track_string, track, total_tracks);
}

static int
parse_disc(struct media_file_info *mfi, char *disc_string)
{
  uint32_t *disc = (uint32_t *) ((char *) mfi + mfi_offsetof(disc));
  uint32_t *total_discs = (uint32_t *) ((char *) mfi + mfi_offsetof(total_discs));

  return parse_slash_separated_ints(disc_string, disc, total_discs);
}

/* Lookup is case-insensitive, first occurrence takes precedence */
static const struct metadata_map md_map_generic[] =
  {
    { "title",        0, mfi_offsetof(title),              NULL },
    { "artist",       0, mfi_offsetof(artist),             NULL },
    { "author",       0, mfi_offsetof(artist),             NULL },
    { "album_artist", 0, mfi_offsetof(album_artist),       NULL },
    { "album",        0, mfi_offsetof(album),              NULL },
    { "genre",        0, mfi_offsetof(genre),              NULL },
    { "composer",     0, mfi_offsetof(composer),           NULL },
    { "grouping",     0, mfi_offsetof(grouping),           NULL },
    { "orchestra",    0, mfi_offsetof(orchestra),          NULL },
    { "conductor",    0, mfi_offsetof(conductor),          NULL },
    { "comment",      0, mfi_offsetof(comment),            NULL },
    { "description",  0, mfi_offsetof(comment),            NULL },
    { "track",        1, mfi_offsetof(track),              parse_track },
    { "disc",         1, mfi_offsetof(disc),               parse_disc },
    { "year",         1, mfi_offsetof(year),               NULL },
    { "date",         1, mfi_offsetof(year),               NULL },
    { "title-sort",   0, mfi_offsetof(title_sort),         NULL },
    { "artist-sort",  0, mfi_offsetof(artist_sort),        NULL },
    { "album-sort",   0, mfi_offsetof(album_sort),         NULL },
    { "compilation",  1, mfi_offsetof(compilation),        NULL },

    { NULL,           0, 0,                                NULL }
  };

static const struct metadata_map md_map_tv[] =
  {
    { "stik",         1, mfi_offsetof(media_kind),         NULL },
    { "show",         0, mfi_offsetof(tv_series_name),     NULL },
    { "episode_id",   0, mfi_offsetof(tv_episode_num_str), NULL },
    { "network",      0, mfi_offsetof(tv_network_name),    NULL },
    { "episode_sort", 1, mfi_offsetof(tv_episode_sort),    NULL },
    { "season_number",1, mfi_offsetof(tv_season_num),      NULL },

    { NULL,           0, 0,                                NULL }
  };

/* NOTE about VORBIS comments:
 *  Only a small set of VORBIS comment fields are officially designated. Most
 *  common tags are at best de facto standards. Currently, metadata conversion
 *  functionality in ffmpeg only adds support for a couple of tags. Specifically,
 *  ALBUMARTIST and TRACKNUMBER are handled as of Feb 1, 2010 (rev 21587). Tags
 *  with names that already match the generic ffmpeg scheme--TITLE and ARTIST,
 *  for example--are of course handled. The rest of these tags are reported to
 *  have been used by various programs in the wild.
 */
static const struct metadata_map md_map_vorbis[] =
  {
    { "albumartist",  0, mfi_offsetof(album_artist),      NULL },
    { "album artist", 0, mfi_offsetof(album_artist),      NULL },
    { "tracknumber",  1, mfi_offsetof(track),             NULL },
    { "tracktotal",   1, mfi_offsetof(total_tracks),      NULL },
    { "totaltracks",  1, mfi_offsetof(total_tracks),      NULL },
    { "discnumber",   1, mfi_offsetof(disc),              NULL },
    { "disctotal",    1, mfi_offsetof(total_discs),       NULL },
    { "totaldiscs",   1, mfi_offsetof(total_discs),       NULL },

    { NULL,           0, 0,                               NULL }
  };

/* NOTE about ID3 tag names:
 *  metadata conversion for ID3v2 tags was added in ffmpeg in september 2009
 *  (rev 20073) for ID3v2.3; support for ID3v2.2 tag names was added in december
 *  2009 (rev 20839).
 *
 * ID3v2.x tags will be removed from the map once a version of ffmpeg containing
 * the changes listed above will be generally available. The more entries in the
 * map, the slower the filescanner gets.
 */
static const struct metadata_map md_map_id3[] =
  {
    { "TT2",                 0, mfi_offsetof(title),                 NULL },              /* ID3v2.2 */
    { "TIT2",                0, mfi_offsetof(title),                 NULL },              /* ID3v2.3 */
    { "TP1",                 0, mfi_offsetof(artist),                NULL },              /* ID3v2.2 */
    { "TPE1",                0, mfi_offsetof(artist),                NULL },              /* ID3v2.3 */
    { "TP2",                 0, mfi_offsetof(album_artist),          NULL },              /* ID3v2.2 */
    { "TPE2",                0, mfi_offsetof(album_artist),          NULL },              /* ID3v2.3 */
    { "TAL",                 0, mfi_offsetof(album),                 NULL },              /* ID3v2.2 */
    { "TALB",                0, mfi_offsetof(album),                 NULL },              /* ID3v2.3 */
    { "TCO",                 0, mfi_offsetof(genre),                 NULL },              /* ID3v2.2 */
    { "TCON",                0, mfi_offsetof(genre),                 NULL },              /* ID3v2.3 */
    { "TCM",                 0, mfi_offsetof(composer),              NULL },              /* ID3v2.2 */
    { "TCOM",                0, mfi_offsetof(composer),              NULL },              /* ID3v2.3 */
    { "TRK",                 1, mfi_offsetof(track),                 parse_track },       /* ID3v2.2 */
    { "TRCK",                1, mfi_offsetof(track),                 parse_track },       /* ID3v2.3 */
    { "TPA",                 1, mfi_offsetof(disc),                  parse_disc },        /* ID3v2.2 */
    { "TPOS",                1, mfi_offsetof(disc),                  parse_disc },        /* ID3v2.3 */
    { "TYE",                 1, mfi_offsetof(year),                  NULL },              /* ID3v2.2 */
    { "TYER",                1, mfi_offsetof(year),                  NULL },              /* ID3v2.3 */
    { "TDRC",                1, mfi_offsetof(year),                  NULL },              /* ID3v2.4 */
    { "TSOA",                0, mfi_offsetof(album_sort),            NULL },              /* ID3v2.4 */
    { "XSOA",                0, mfi_offsetof(album_sort),            NULL },              /* ID3v2.3 */
    { "TSOP",                0, mfi_offsetof(artist_sort),           NULL },              /* ID3v2.4 */
    { "XSOP",                0, mfi_offsetof(artist_sort),           NULL },              /* ID3v2.3 */
    { "TSOT",                0, mfi_offsetof(title_sort),            NULL },              /* ID3v2.4 */
    { "XSOT",                0, mfi_offsetof(title_sort),            NULL },              /* ID3v2.3 */
    { "TS2",                 0, mfi_offsetof(album_artist_sort),     NULL },              /* ID3v2.2 */
    { "TSO2",                0, mfi_offsetof(album_artist_sort),     NULL },              /* ID3v2.3 */
    { "ALBUMARTISTSORT",     0, mfi_offsetof(album_artist_sort),     NULL },              /* ID3v2.x */
    { "TSC",                 0, mfi_offsetof(composer_sort),         NULL },              /* ID3v2.2 */
    { "TSOC",                0, mfi_offsetof(composer_sort),         NULL },              /* ID3v2.3 */

    { NULL,                  0, 0,                                   NULL }
  };


static int
#if LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 5)
extract_metadata_core(struct media_file_info *mfi, AVDictionary *md, const struct metadata_map *md_map)
#else
extract_metadata_core(struct media_file_info *mfi, AVMetadata *md, const struct metadata_map *md_map)
#endif
{
#if LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 5)
  AVDictionaryEntry *mdt;
#else
  AVMetadataTag *mdt;
#endif
  char **strval;
  uint32_t *intval;
  int mdcount;
  int i;
  int ret;

#if 0
  /* Dump all the metadata reported by ffmpeg */
  mdt = NULL;
#if LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 5)
  while ((mdt = av_dict_get(md, "", mdt, AV_DICT_IGNORE_SUFFIX)) != NULL)
#else
  while ((mdt = av_metadata_get(md, "", mdt, AV_METADATA_IGNORE_SUFFIX)) != NULL)
#endif
    fprintf(stderr, " -> %s = %s\n", mdt->key, mdt->value);
#endif

  mdcount = 0;

  /* Extract actual metadata */
  for (i = 0; md_map[i].key != NULL; i++)
    {
#if LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 5)
      mdt = av_dict_get(md, md_map[i].key, NULL, 0);
#else
      mdt = av_metadata_get(md, md_map[i].key, NULL, 0);
#endif
      if (mdt == NULL)
	continue;

      if ((mdt->value == NULL) || (strlen(mdt->value) == 0))
	continue;

      if (md_map[i].handler_function)
	{
	  mdcount += md_map[i].handler_function(mfi, mdt->value);
	  continue;
	}

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
	      ret = safe_atou32(mdt->value, intval);
	      if (ret < 0)
		continue;
	    }
	}
    }

  return mdcount;
}

static int
extract_metadata(struct media_file_info *mfi, AVFormatContext *ctx, AVStream *audio_stream, AVStream *video_stream, const struct metadata_map *md_map)
{
  int mdcount;
  int ret;

  mdcount = 0;

  if (ctx->metadata)
    {
      ret = extract_metadata_core(mfi, ctx->metadata, md_map);
      mdcount += ret;

      DPRINTF(E_DBG, L_SCAN, "Picked up %d tags from file metadata\n", ret);
    }

  if (audio_stream->metadata)
    {
      ret = extract_metadata_core(mfi, audio_stream->metadata, md_map);
      mdcount += ret;

      DPRINTF(E_DBG, L_SCAN, "Picked up %d tags from audio stream metadata\n", ret);
    }

  if (video_stream && video_stream->metadata)
    {
      ret = extract_metadata_core(mfi, video_stream->metadata, md_map);
      mdcount += ret;

      DPRINTF(E_DBG, L_SCAN, "Picked up %d tags from video stream metadata\n", ret);
    }

  return mdcount;
}

int
scan_metadata_ffmpeg(char *file, struct media_file_info *mfi)
{
  AVFormatContext *ctx;
  AVDictionary *options;
  const struct metadata_map *extra_md_map;
  struct http_icy_metadata *icy_metadata;
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  enum AVCodecID codec_id;
  enum AVCodecID video_codec_id;
  enum AVCodecID audio_codec_id;
#else
  enum CodecID codec_id;
  enum CodecID video_codec_id;
  enum CodecID audio_codec_id;
#endif
  AVStream *video_stream;
  AVStream *audio_stream;
  char *path;
  int mdcount;
  int i;
  int ret;

  ctx = NULL;
  options = NULL;
  path = strdup(file);

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
# ifndef HAVE_FFMPEG
  // Without this, libav is slow to probe some internet streams
  if (mfi->data_kind == DATA_KIND_URL)
    {
      ctx = avformat_alloc_context();
      ctx->probesize = 64000;
    }
# endif

  if (mfi->data_kind == DATA_KIND_URL)
    {
      free(path);
      ret = http_stream_setup(&path, file);
      if (ret < 0)
	return -1;

      av_dict_set(&options, "icy", "1", 0);
      mfi->artwork = ARTWORK_HTTP;
    }

  ret = avformat_open_input(&ctx, path, NULL, &options);

  if (options)
    av_dict_free(&options);
#else
  ret = av_open_input_file(&ctx, path, NULL, 0, NULL);
#endif
  if (ret != 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Cannot open media file '%s': %s\n", path, strerror(AVUNERROR(ret)));

      free(path);
      return -1;
    }

  free(path);

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 3)
  ret = avformat_find_stream_info(ctx, NULL);
#else
  ret = av_find_stream_info(ctx);
#endif
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_SCAN, "Cannot get stream info: %s\n", strerror(AVUNERROR(ret)));

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
      avformat_close_input(&ctx);
#else
      av_close_input_file(ctx);
#endif
      return -1;
    }

#if 0
  /* Dump input format as determined by ffmpeg */
  av_dump_format(ctx, 0, file, 0);
#endif

  DPRINTF(E_DBG, L_SCAN, "File has %d streams\n", ctx->nb_streams);

  /* Extract codec IDs, check for video */
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  video_codec_id = AV_CODEC_ID_NONE;
  video_stream = NULL;

  audio_codec_id = AV_CODEC_ID_NONE;
  audio_stream = NULL;
#else
  video_codec_id = CODEC_ID_NONE;
  video_stream = NULL;

  audio_codec_id = CODEC_ID_NONE;
  audio_stream = NULL;
#endif

  for (i = 0; i < ctx->nb_streams; i++)
    {
      switch (ctx->streams[i]->codec->codec_type)
	{
#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 64)
	  case AVMEDIA_TYPE_VIDEO:
#else
	  case CODEC_TYPE_VIDEO:
#endif
#if LIBAVFORMAT_VERSION_MAJOR >= 55 || (LIBAVFORMAT_VERSION_MAJOR == 54 && LIBAVFORMAT_VERSION_MINOR >= 6)
	    if (ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC)
	      {
		DPRINTF(E_DBG, L_SCAN, "Found embedded artwork (stream %d)\n", i);
		mfi->artwork = ARTWORK_EMBEDDED;

		break;
	      }
#endif
	    if (!video_stream)
	      {
		DPRINTF(E_DBG, L_SCAN, "File has video (stream %d)\n", i);

		mfi->has_video = 1;
		video_stream = ctx->streams[i];
		video_codec_id = video_stream->codec->codec_id;
	      }
	    break;

#if LIBAVCODEC_VERSION_MAJOR >= 53 || (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR >= 64)
	  case AVMEDIA_TYPE_AUDIO:
#else
	  case CODEC_TYPE_AUDIO:
#endif
	    if (!audio_stream)
	      {
		audio_stream = ctx->streams[i];
		audio_codec_id = audio_stream->codec->codec_id;
	      } 
	    break;

	  default:
	    break;
	}
    }

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
  if (audio_codec_id == AV_CODEC_ID_NONE)
#else
  if (audio_codec_id == CODEC_ID_NONE)
#endif
    {
      DPRINTF(E_DBG, L_SCAN, "File has no audio streams, discarding\n");

#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
      avformat_close_input(&ctx);
#else
      av_close_input_file(ctx);
#endif
      return -1;
    }

  /* Common media information */
  if (ctx->duration > 0)
    mfi->song_length = ctx->duration / (AV_TIME_BASE / 1000); /* ms */

  if (ctx->bit_rate > 0)
    mfi->bitrate = ctx->bit_rate / 1000;
  else if (ctx->duration > AV_TIME_BASE) /* guesstimate */
    mfi->bitrate = ((mfi->file_size * 8) / (ctx->duration / AV_TIME_BASE)) / 1000;

  DPRINTF(E_DBG, L_SCAN, "Duration %d ms, bitrate %d kbps\n", mfi->song_length, mfi->bitrate);

  /* Try to extract ICY metadata if url/stream */
  if (mfi->data_kind == DATA_KIND_URL)
    {
      icy_metadata = http_icy_metadata_get(ctx, 0);
      if (icy_metadata && icy_metadata->name)
	{
	  DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, name is '%s'\n", icy_metadata->name);

	  if (mfi->title)
	    free(mfi->title);
	  if (mfi->artist)
	    free(mfi->artist);
	  if (mfi->album_artist)
	    free(mfi->album_artist);

	  mfi->title = strdup(icy_metadata->name);
	  mfi->artist = strdup(icy_metadata->name);
	  mfi->album_artist = strdup(icy_metadata->name);
	}
      if (icy_metadata && icy_metadata->description)
	{
	  DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, description is '%s'\n", icy_metadata->description);

	  if (mfi->album)
	    free(mfi->album);

	  mfi->album = strdup(icy_metadata->description);
	}
      if (icy_metadata && icy_metadata->genre)
	{
	  DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, genre is '%s'\n", icy_metadata->genre);

	  if (mfi->genre)
	    free(mfi->genre);

	  mfi->genre = strdup(icy_metadata->genre);
	}
      if (icy_metadata)
	http_icy_metadata_free(icy_metadata, 0);
    }

  /* Get some more information on the audio stream */
  if (audio_stream)
    {
      if (audio_stream->codec->sample_rate != 0)
	mfi->samplerate = audio_stream->codec->sample_rate;

      /* Try sample format first */
#if LIBAVUTIL_VERSION_MAJOR >= 52 || (LIBAVUTIL_VERSION_MAJOR == 51 && LIBAVUTIL_VERSION_MINOR >= 4)
      mfi->bits_per_sample = 8 * av_get_bytes_per_sample(audio_stream->codec->sample_fmt);
#elif LIBAVCODEC_VERSION_MAJOR >= 53
      mfi->bits_per_sample = av_get_bits_per_sample_fmt(audio_stream->codec->sample_fmt);
#else
      mfi->bits_per_sample = av_get_bits_per_sample_format(audio_stream->codec->sample_fmt);
#endif
      if (mfi->bits_per_sample == 0)
	{
	  /* Try codec */
	  mfi->bits_per_sample = av_get_bits_per_sample(audio_codec_id);
	}

      DPRINTF(E_DBG, L_SCAN, "samplerate %d, bps %d\n", mfi->samplerate, mfi->bits_per_sample);
    }

  /* Check codec */
  extra_md_map = NULL;
  codec_id = (mfi->has_video) ? video_codec_id : audio_codec_id;
  switch (codec_id)
    {
#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_AAC:
#else
      case CODEC_ID_AAC:
#endif
	DPRINTF(E_DBG, L_SCAN, "AAC\n");
	mfi->type = strdup("m4a");
	mfi->codectype = strdup("mp4a");
	mfi->description = strdup("AAC audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_ALAC:
#else
      case CODEC_ID_ALAC:
#endif
	DPRINTF(E_DBG, L_SCAN, "ALAC\n");
	mfi->type = strdup("m4a");
	mfi->codectype = strdup("alac");
	mfi->description = strdup("Apple Lossless audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_FLAC:
#else
      case CODEC_ID_FLAC:
#endif
	DPRINTF(E_DBG, L_SCAN, "FLAC\n");
	mfi->type = strdup("flac");
	mfi->codectype = strdup("flac");
	mfi->description = strdup("FLAC audio file");

	extra_md_map = md_map_vorbis;
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_MUSEPACK7:
      case AV_CODEC_ID_MUSEPACK8:
#else
      case CODEC_ID_MUSEPACK7:
      case CODEC_ID_MUSEPACK8:
#endif
	DPRINTF(E_DBG, L_SCAN, "Musepack\n");
	mfi->type = strdup("mpc");
	mfi->codectype = strdup("mpc");
	mfi->description = strdup("Musepack audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_MPEG4: /* Video */
      case AV_CODEC_ID_H264:
#else
      case CODEC_ID_MPEG4: /* Video */
      case CODEC_ID_H264:
#endif
	DPRINTF(E_DBG, L_SCAN, "MPEG4 video\n");
	mfi->type = strdup("m4v");
	mfi->codectype = strdup("mp4v");
	mfi->description = strdup("MPEG-4 video file");

	extra_md_map = md_map_tv;
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_MP3:
#else
      case CODEC_ID_MP3:
#endif
	DPRINTF(E_DBG, L_SCAN, "MP3\n");
	mfi->type = strdup("mp3");
	mfi->codectype = strdup("mpeg");
	mfi->description = strdup("MPEG audio file");

	extra_md_map = md_map_id3;
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_VORBIS:
#else
      case CODEC_ID_VORBIS:
#endif
	DPRINTF(E_DBG, L_SCAN, "VORBIS\n");
	mfi->type = strdup("ogg");
	mfi->codectype = strdup("ogg");
	mfi->description = strdup("Ogg Vorbis audio file");

	extra_md_map = md_map_vorbis;
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_WMAV1:
      case AV_CODEC_ID_WMAV2:
      case AV_CODEC_ID_WMAVOICE:
#else
      case CODEC_ID_WMAV1:
      case CODEC_ID_WMAV2:
      case CODEC_ID_WMAVOICE:
#endif
	DPRINTF(E_DBG, L_SCAN, "WMA Voice\n");
	mfi->type = strdup("wma");
	mfi->codectype = strdup("wmav");
	mfi->description = strdup("WMA audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_WMAPRO:
#else
      case CODEC_ID_WMAPRO:
#endif
	DPRINTF(E_DBG, L_SCAN, "WMA Pro\n");
	mfi->type = strdup("wmap");
	mfi->codectype = strdup("wma");
	mfi->description = strdup("WMA audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_WMALOSSLESS:
#else
      case CODEC_ID_WMALOSSLESS:
#endif
	DPRINTF(E_DBG, L_SCAN, "WMA Lossless\n");
	mfi->type = strdup("wma");
	mfi->codectype = strdup("wmal");
	mfi->description = strdup("WMA audio file");
	break;

#if LIBAVCODEC_VERSION_MAJOR >= 55 || (LIBAVCODEC_VERSION_MAJOR == 54 && LIBAVCODEC_VERSION_MINOR >= 35)
      case AV_CODEC_ID_PCM_S16LE ... AV_CODEC_ID_PCM_F64LE:
#else
      case CODEC_ID_PCM_S16LE ... CODEC_ID_PCM_F64LE:
#endif
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
	  {
	    mfi->description = strdup("Unknown video file format");
	    extra_md_map = md_map_tv;
	  }
	else
	  mfi->description = strdup("Unknown audio file format");
	break;
    }

  mdcount = 0;

  if ((!ctx->metadata) && (!audio_stream->metadata)
      && (video_stream && !video_stream->metadata))
    {
      DPRINTF(E_WARN, L_SCAN, "ffmpeg reports no metadata\n");

      goto skip_extract;
    }

  if (extra_md_map)
    {
      ret = extract_metadata(mfi, ctx, audio_stream, video_stream, extra_md_map);
      mdcount += ret;

      DPRINTF(E_DBG, L_SCAN, "Picked up %d tags with extra md_map\n", ret);
    }

  ret = extract_metadata(mfi, ctx, audio_stream, video_stream, md_map_generic);
  mdcount += ret;

  DPRINTF(E_DBG, L_SCAN, "Picked up %d tags with generic md_map, %d tags total\n", ret, mdcount);

  /* fix up TV metadata */
  if (mfi->media_kind == 10)
    {
      /* I have no idea why this is, but iTunes reports a media kind of 64 for stik==10 (?!) */
      mfi->media_kind = MEDIA_KIND_TVSHOW;
    }
  /* Unspecified video files are "Movies", media_kind 2 */
  else if (mfi->has_video == 1)
    {
      mfi->media_kind = MEDIA_KIND_MOVIE;
    }

 skip_extract:
#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 21)
  avformat_close_input(&ctx);
#else
  av_close_input_file(ctx);
#endif

  if (mdcount == 0)
    {
#if LIBAVFORMAT_VERSION_MAJOR < 54 || (LIBAVFORMAT_VERSION_MAJOR == 54 && LIBAVFORMAT_VERSION_MINOR < 35)
      /* ffmpeg doesn't support FLAC nor Musepack metadata,
       * and is buggy for some WMA variants, so fall back to the
       * legacy format-specific parsers until it gets fixed */
      if ((codec_id == CODEC_ID_WMAPRO)
	  || (codec_id == CODEC_ID_WMAVOICE)
	  || (codec_id == CODEC_ID_WMALOSSLESS))
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy WMA scanner\n");
	  return (scan_get_wmainfo(file, mfi) ? 0 : -1);
	}
#ifdef FLAC
      else if (codec_id == CODEC_ID_FLAC)
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy FLAC scanner\n");
	  return (scan_get_flacinfo(file, mfi) ? 0 : -1);
	}
#endif /* FLAC */
#ifdef MUSEPACK
      else if ((codec_id == CODEC_ID_MUSEPACK7)
	       || (codec_id == CODEC_ID_MUSEPACK8))
	{
	  DPRINTF(E_WARN, L_SCAN, "Falling back to legacy Musepack scanner\n");
	  return (scan_get_mpcinfo(file, mfi) ? 0 : -1);
	}
#endif /* MUSEPACK */
      else
#endif /* LIBAVFORMAT */
	DPRINTF(E_WARN, L_SCAN, "ffmpeg/libav could not extract any metadata\n");
    }

  /* Just in case there's no title set ... */
  if (mfi->title == NULL)
    mfi->title = strdup(mfi->fname);

  /* All done */

  return 0;
}
