/*
 * Copyright (C) 2015-17 Espen Jurgensen
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

#define _GNU_SOURCE // For memmem()

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>

#include "logger.h"
#include "conffile.h"
#include "db.h"
#include "misc.h"
#include "transcode.h"

// Switches for compability with ffmpeg's ever changing API
#define USE_IMAGE2PIPE (LIBAVFORMAT_VERSION_MAJOR > 58) || ((LIBAVFORMAT_VERSION_MAJOR == 58) && (LIBAVFORMAT_VERSION_MINOR > 29))
#define USE_CONST_AVFORMAT (LIBAVFORMAT_VERSION_MAJOR > 59) || ((LIBAVFORMAT_VERSION_MAJOR == 59) && (LIBAVFORMAT_VERSION_MINOR > 15))
#define USE_CONST_AVCODEC (LIBAVFORMAT_VERSION_MAJOR > 59) || ((LIBAVFORMAT_VERSION_MAJOR == 59) && (LIBAVFORMAT_VERSION_MINOR > 15))
#define USE_NO_CLEAR_AVFMT_NOFILE (LIBAVFORMAT_VERSION_MAJOR > 59) || ((LIBAVFORMAT_VERSION_MAJOR == 59) && (LIBAVFORMAT_VERSION_MINOR > 15))
#define USE_CH_LAYOUT (LIBAVCODEC_VERSION_MAJOR > 59) || ((LIBAVCODEC_VERSION_MAJOR == 59) && (LIBAVCODEC_VERSION_MINOR > 24))
#define USE_CONST_AVIO_WRITE_PACKET (LIBAVFORMAT_VERSION_MAJOR > 61) || ((LIBAVFORMAT_VERSION_MAJOR == 61) && (LIBAVFORMAT_VERSION_MINOR > 0))

// Interval between ICY metadata checks for streams, in seconds
#define METADATA_ICY_INTERVAL 5
// Maximum number of streams in a file that we will accept
#define MAX_STREAMS 64
// Maximum number of times we retry when we encounter bad packets
#define MAX_BAD_PACKETS 5
// How long to wait (in microsec) before interrupting av_read_frame
#define READ_TIMEOUT 30000000
// Buffer size for reading/writing input and output evbuffers
#define AVIO_BUFFER_SIZE 4096
// Size of the wav header that iTunes needs
#define WAV_HEADER_LEN 44
// Max filters in a filtergraph
#define MAX_FILTERS 9
// Set to same size as in httpd.c (but can be set to something else)
#define STREAM_CHUNK_SIZE (64 * 1024)

static const char *default_codecs = "mpeg,alac,wav";
static const char *roku_codecs = "mpeg,mp4a,wma,alac,wav";
static const char *itunes_codecs = "mpeg,mp4a,mp4v,alac,wav";

// Used for passing errors to DPRINTF (can't count on av_err2str being present)
static char errbuf[64];

// Used by dummy_seek to mark a seek requested by ffmpeg
static const uint8_t xcode_seek_marker[8] = { 0x0D, 0x0E, 0x0A, 0x0D, 0x0B, 0x0E, 0x0E, 0x0F };

// The settings struct will be filled out based on the profile enum
struct settings_ctx
{
  bool encode_video;
  bool encode_audio;

  // Silence some log messages
  bool silent;

  // Output format (for the muxer)
  const char *format;

  // Input format (for the demuxer)
  const char *in_format;

  // Audio settings
  enum AVCodecID audio_codec;
  int sample_rate;
#if USE_CH_LAYOUT
  AVChannelLayout channel_layout;
#else
  uint64_t channel_layout;
#endif
  int nb_channels;
  int bit_rate;
  int frame_size;
  enum AVSampleFormat sample_format;
  bool with_mp4_header;
  bool with_wav_header;
  bool without_libav_header;
  bool without_libav_trailer;
  bool with_icy;
  bool with_user_filters;

  // Video settings
  enum AVCodecID video_codec;
  const char *video_codec_name;
  enum AVPixelFormat pix_fmt;
  int height;
  int width;
};

struct stream_ctx
{
  AVStream *stream;
  AVCodecContext *codec;

  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;

  // Used for seeking
  int64_t prev_pts;
  int64_t offset_pts;
};

struct decode_ctx
{
  // Settings derived from the profile
  struct settings_ctx settings;

  // Input format context
  AVFormatContext *ifmt_ctx;

  // IO Context for non-file input
  AVIOContext *avio;

  // Stream and decoder data
  struct stream_ctx audio_stream;
  struct stream_ctx video_stream;

  // Source duration in ms as provided by caller
  uint32_t len_ms;

  // Data kind (used to determine if ICY metadata is relevant to look for)
  enum data_kind data_kind;

  // Set to true if we just seeked
  bool resume;

  // Set to true if we have reached eof
  bool eof;

  // Set to true if avcodec_receive_frame() gave us a frame
  bool got_frame;

  // Contains the most recent packet from av_read_frame()
  AVPacket *packet;

  // Contains the most recent frame from avcodec_receive_frame()
  AVFrame *decoded_frame;

  // Used to measure if av_read_frame is taking too long
  int64_t timestamp;
};

struct encode_ctx
{
  // Settings derived from the profile
  struct settings_ctx settings;

  // Output format context
  AVFormatContext *ofmt_ctx;

  // Stream, filter and decoder data
  struct stream_ctx audio_stream;
  struct stream_ctx video_stream;

  // The ffmpeg muxer writes to this buffer using the avio_evbuffer interface
  struct evbuffer *obuf;

  // IO Context for non-file output
  struct transcode_evbuf_io evbuf_io;

  // Contains the most recent packet from av_buffersink_get_frame()
  AVFrame *filt_frame;

  // Contains the most recent packet from avcodec_receive_packet()
  AVPacket *encoded_pkt;

  // How many output bytes we have processed in total
  off_t bytes_processed;

  // Estimated total size of output
  off_t bytes_total;

  // Used to check for ICY metadata changes at certain intervals
  uint32_t icy_interval;
  uint32_t icy_hash;
};

enum probe_type
{
  PROBE_TYPE_DEFAULT,
  PROBE_TYPE_QUICK,
};

struct avio_evbuffer {
  struct evbuffer *evbuf;
  uint8_t *buffer;
  transcode_seekfn seekfn;
  void *seekfn_arg;
};

struct filter_def
{
  char name[64];
  char args[512];
};

struct filters
{
  AVFilterContext *av_ctx;

  // Function that will create the filter arguments for ffmpeg
  int (*deffn)(struct filter_def *, struct stream_ctx *, struct stream_ctx *, const char *);
  const char *deffn_arg;
};


/* -------------------------- PROFILE CONFIGURATION ------------------------ */

static int
init_settings(struct settings_ctx *settings, enum transcode_profile profile, struct media_quality *quality)
{
  memset(settings, 0, sizeof(struct settings_ctx));

  switch (profile)
    {
      case XCODE_PCM_NATIVE: // Sample rate and bit depth determined by source
	settings->encode_audio = true;
	settings->with_icy = true;
	settings->with_user_filters = true;
	break;

      case XCODE_WAV:
	settings->with_wav_header = true;
	settings->with_user_filters = true;
      case XCODE_PCM16:
	settings->encode_audio = true;
	settings->format = "s16le";
	settings->audio_codec = AV_CODEC_ID_PCM_S16LE;
	settings->sample_format = AV_SAMPLE_FMT_S16;
	break;

      case XCODE_PCM24:
	settings->encode_audio = true;
	settings->format = "s24le";
	settings->audio_codec = AV_CODEC_ID_PCM_S24LE;
	settings->sample_format = AV_SAMPLE_FMT_S32;
	break;

      case XCODE_PCM32:
	settings->encode_audio = true;
	settings->format = "s32le";
	settings->audio_codec = AV_CODEC_ID_PCM_S32LE;
	settings->sample_format = AV_SAMPLE_FMT_S32;
	break;

      case XCODE_MP3:
	settings->encode_audio = true;
	settings->format = "mp3";
	settings->audio_codec = AV_CODEC_ID_MP3;
	settings->sample_format = AV_SAMPLE_FMT_S16P;
	break;

      case XCODE_OPUS:
	settings->encode_audio = true;
	settings->format = "data"; // Means we get the raw packet from the encoder, no muxing
	settings->audio_codec = AV_CODEC_ID_OPUS;
	settings->sample_format = AV_SAMPLE_FMT_S16; // Only libopus support
	break;

      case XCODE_ALAC:
	settings->encode_audio = true;
	settings->format = "data"; // Means we get the raw packet from the encoder, no muxing
	settings->audio_codec = AV_CODEC_ID_ALAC;
	settings->sample_format = AV_SAMPLE_FMT_S16P;
	settings->frame_size = 352;
	break;

      case XCODE_MP4_ALAC:
	settings->with_mp4_header = true;
	settings->encode_audio = true;
	settings->format = "data";
	settings->audio_codec = AV_CODEC_ID_ALAC;
	break;

      case XCODE_MP4_ALAC_HEADER:
	settings->without_libav_header = true;
	settings->without_libav_trailer = true;
	settings->encode_audio = true;
	settings->format = "ipod"; // ffmpeg default mp4 variant ("mp4" doesn't work with SoundBridge because of the btrt atom in the header)
	settings->audio_codec = AV_CODEC_ID_ALAC;
	break;

      case XCODE_OGG:
	settings->encode_audio = true;
	settings->in_format = "ogg";
	break;

      case XCODE_JPEG:
	settings->encode_video = true;
	settings->silent = 1;
// With ffmpeg 4.3 (> libavformet 58.29) "image2" only works for actual file
// output. It's possible we should have used "image2pipe" all along, but since
// "image2" has been working we only replace it going forward.
#if USE_IMAGE2PIPE
	settings->format = "image2pipe";
#else
	settings->format = "image2";
#endif

	settings->in_format = "mjpeg";
	settings->pix_fmt = AV_PIX_FMT_YUVJ420P;
	settings->video_codec = AV_CODEC_ID_MJPEG;
	break;

      case XCODE_PNG:
	settings->encode_video = true;
	settings->silent = true;
// See explanation above
#if USE_IMAGE2PIPE
	settings->format = "image2pipe";
#else
	settings->format = "image2";
#endif
	settings->pix_fmt = AV_PIX_FMT_RGB24;
	settings->video_codec = AV_CODEC_ID_PNG;
	break;

      case XCODE_VP8:
	settings->encode_video = true;
	settings->silent = true;
// See explanation above
#if USE_IMAGE2PIPE
	settings->format = "image2pipe";
#else
	settings->format = "image2";
#endif
	settings->pix_fmt = AV_PIX_FMT_YUVJ420P;
	settings->video_codec = AV_CODEC_ID_VP8;
	break;

      default:
	DPRINTF(E_LOG, L_XCODE, "Bug! Unknown transcoding profile\n");
	return -1;
    }

  if (quality && quality->sample_rate)
    {
      settings->sample_rate    = quality->sample_rate;
    }

  if (quality && quality->channels)
    {
#if USE_CH_LAYOUT
      av_channel_layout_default(&settings->channel_layout, quality->channels);
#else
      settings->channel_layout = av_get_default_channel_layout(quality->channels);
      settings->nb_channels    = quality->channels;
#endif
    }

  if (quality && quality->bit_rate)
    {
      settings->bit_rate    = quality->bit_rate;
    }

  if (quality && quality->bits_per_sample && (quality->bits_per_sample != 8 * av_get_bytes_per_sample(settings->sample_format)))
    {
      DPRINTF(E_LOG, L_XCODE, "Bug! Mismatch between profile (%d bps) and media quality (%d bps)\n", 8 * av_get_bytes_per_sample(settings->sample_format), quality->bits_per_sample);
      return -1;
    }

  return 0;
}

static int
init_settings_from_video(struct settings_ctx *settings, enum transcode_profile profile, struct decode_ctx *src_ctx, int width, int height)
{
  settings->width = width;
  settings->height = height;

  return 0;
}

static int
init_settings_from_audio(struct settings_ctx *settings, enum transcode_profile profile, struct decode_ctx *src_ctx, struct media_quality *quality)
{
  int src_bytes_per_sample = av_get_bytes_per_sample(src_ctx->audio_stream.codec->sample_fmt);

  // Initialize unset settings that are source-dependent, not profile-dependent
  if (!settings->sample_rate)
    settings->sample_rate = src_ctx->audio_stream.codec->sample_rate;

#if USE_CH_LAYOUT
  if (!av_channel_layout_check(&settings->channel_layout))
    av_channel_layout_copy(&settings->channel_layout, &src_ctx->audio_stream.codec->ch_layout);

  settings->nb_channels = settings->channel_layout.nb_channels;
#else
  if (settings->nb_channels == 0)
    {
      settings->nb_channels = src_ctx->audio_stream.codec->channels;
      settings->channel_layout = src_ctx->audio_stream.codec->channel_layout;
    }
#endif

  // Initialize settings that are both source-dependent and profile-dependent
  switch (profile)
    {
      case XCODE_MP4_ALAC:
      case XCODE_MP4_ALAC_HEADER:
	if (!settings->sample_format)
	  settings->sample_format = (src_bytes_per_sample == 4) ? AV_SAMPLE_FMT_S32P : AV_SAMPLE_FMT_S16P;
	break;

      case XCODE_PCM_NATIVE:
	if (!settings->sample_format)
	  settings->sample_format = (src_bytes_per_sample == 4) ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
	if (!settings->audio_codec)
	  settings->audio_codec = (src_bytes_per_sample == 4) ? AV_CODEC_ID_PCM_S32LE : AV_CODEC_ID_PCM_S16LE;
	if (!settings->format)
	  settings->format = (src_bytes_per_sample == 4) ? "s32le" : "s16le";
	break;

      default:
	if (settings->sample_format && settings->audio_codec && settings->format)
	  return 0;

	DPRINTF(E_LOG, L_XCODE, "Bug! Profile %d has unset encoding parameters\n", profile);
	return -1;
    }

  return 0;
}

static void
stream_settings_set(struct stream_ctx *s, struct settings_ctx *settings, enum AVMediaType type)
{
  if (type == AVMEDIA_TYPE_AUDIO)
    {
      s->codec->sample_rate    = settings->sample_rate;
#if USE_CH_LAYOUT
      av_channel_layout_copy(&s->codec->ch_layout, &(settings->channel_layout));
#else
      s->codec->channel_layout = settings->channel_layout;
      s->codec->channels       = settings->nb_channels;
#endif
      s->codec->sample_fmt     = settings->sample_format;
      s->codec->time_base      = (AVRational){1, settings->sample_rate};
      s->codec->bit_rate       = settings->bit_rate;
    }
  else if (type == AVMEDIA_TYPE_VIDEO)
    {
      s->codec->height         = settings->height;
      s->codec->width          = settings->width;
      s->codec->pix_fmt        = settings->pix_fmt;
      s->codec->time_base      = (AVRational){1, 25};
    }
}


/* -------------------------------- HELPERS -------------------------------- */

static enum AVSampleFormat
bitdepth2format(int bits_per_sample)
{
  if (bits_per_sample == 16)
    return AV_SAMPLE_FMT_S16;
  else if (bits_per_sample == 24)
    return AV_SAMPLE_FMT_S32;
  else if (bits_per_sample == 32)
    return AV_SAMPLE_FMT_S32;
  else
    return AV_SAMPLE_FMT_NONE;
}

static inline char *
err2str(int errnum)
{
  av_strerror(errnum, errbuf, sizeof(errbuf));
  return errbuf;
}

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

// Copies the src buffer to position pos of the dst buffer, expanding dst if
// needed to fit src. Can be called with *dst = NULL and *dst_len = 0. Returns
// the number of bytes dst was expanded with.
static int
copy_buffer_to_position(uint8_t **dst, size_t *dst_len, uint8_t *src, size_t src_len, int64_t pos)
{
  int bytes_added = 0;

  if (pos < 0 || pos > *dst_len)
    return -1; // Out of bounds
  if (src_len == 0)
    return 0; // Nothing to do

  if (pos + src_len > *dst_len)
    {
      bytes_added = pos + src_len - *dst_len;
      *dst_len += bytes_added;
      CHECK_NULL(L_XCODE, *dst = realloc(*dst, *dst_len));
    }

  memcpy(*dst + pos, src, src_len);
  return bytes_added;
}

// Doesn't actually seek, just inserts a marker in the obuf
static int64_t
dummy_seek(void *arg, int64_t offset, enum transcode_seek_type type)
{
  struct transcode_ctx *ctx = arg;
  struct encode_ctx *enc_ctx = ctx->encode_ctx;

  if (type == XCODE_SEEK_SET)
    {
      evbuffer_add(enc_ctx->obuf, xcode_seek_marker, sizeof(xcode_seek_marker));
      evbuffer_add(enc_ctx->obuf, &offset, sizeof(offset));
      return offset;
    }
  else if (type == XCODE_SEEK_SIZE)
    return enc_ctx->bytes_total;

  return -1;
}

static off_t
size_estimate(enum transcode_profile profile, int bit_rate, int sample_rate, int bytes_per_sample, int channels, int len_ms)
{
  off_t bytes;

  // If the source has a number of samples that doesn't match an even len_ms
  // then the length may have been rounded up. We prefer an estimate that is on
  // the low side, otherwise ffprobe won't trust the length from our wav header.
  if (len_ms > 0)
    len_ms -= 1;
  else
    len_ms = 3 * 60 * 1000;

  if (profile == XCODE_WAV)
    bytes = (int64_t)len_ms * channels * bytes_per_sample * sample_rate / 1000 + WAV_HEADER_LEN;
  else if (profile == XCODE_MP3)
    bytes = (int64_t)len_ms * bit_rate / 8000;
  else if (profile == XCODE_MP4_ALAC)
    bytes = (int64_t)len_ms * channels * bytes_per_sample * sample_rate / 1000 / 2; // FIXME
  else
    bytes = -1;

  return bytes;
}


/*
 * Checks if this stream index is one that we are decoding
 *
 * @in ctx        Decode context
 * @in stream_index Index of stream to check
 * @return        Type of stream, unknown if we are not decoding the stream
 */
static enum AVMediaType
stream_find(struct decode_ctx *ctx, unsigned int stream_index)
{
  if (ctx->audio_stream.stream && (stream_index == ctx->audio_stream.stream->index))
    return AVMEDIA_TYPE_AUDIO;

  if (ctx->video_stream.stream && (stream_index == ctx->video_stream.stream->index))
    return AVMEDIA_TYPE_VIDEO;

  return AVMEDIA_TYPE_UNKNOWN;
}

/*
 * Adds a stream to an output
 *
 * @out ctx       A pre-allocated stream ctx where we save stream and codec info
 * @in output     Output to add the stream to
 * @in codec_id   What kind of codec should we use
 * @return        Negative on failure, otherwise zero
 */
static int
stream_add(struct encode_ctx *ctx, struct stream_ctx *s, enum AVCodecID codec_id)
{
  const AVCodecDescriptor *codec_desc;
#if USE_CONST_AVCODEC
  const AVCodec *encoder;
#else
  // Not const before ffmpeg 5.0
  AVCodec *encoder;
#endif
  AVDictionary *options = NULL;
  int ret;

  codec_desc = avcodec_descriptor_get(codec_id);
  if (!codec_desc)
    {
      DPRINTF(E_LOG, L_XCODE, "Invalid codec ID (%d)\n", codec_id);
      return -1;
    }

  encoder = avcodec_find_encoder(codec_id);
  if (!encoder)
    {
      DPRINTF(E_LOG, L_XCODE, "Necessary encoder (%s) not found\n", codec_desc->name);
      return -1;
    }

  DPRINTF(E_DBG, L_XCODE, "Selected encoder '%s'\n", encoder->long_name);

  CHECK_NULL(L_XCODE, s->stream = avformat_new_stream(ctx->ofmt_ctx, NULL));
  CHECK_NULL(L_XCODE, s->codec = avcodec_alloc_context3(encoder));

  stream_settings_set(s, &ctx->settings, encoder->type);

  if (!s->codec->pix_fmt)
    {
      s->codec->pix_fmt = avcodec_default_get_format(s->codec, encoder->pix_fmts);
      DPRINTF(E_DBG, L_XCODE, "Pixel format set to %s (encoder is %s)\n", av_get_pix_fmt_name(s->codec->pix_fmt), codec_desc->name);
    }

  if (ctx->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    s->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  // With ffmpeg 3.4, jpeg encoding with optimal huffman tables will segfault, see issue #502
  if (codec_id == AV_CODEC_ID_MJPEG)
    av_dict_set(&options, "huffman", "default", 0);

  // 20 ms frames is the current ffmpeg default, but we set it anyway, so that
  // we don't risk issues if future versions change the default (it would become
  // an issue because outputs/cast.c relies on 20 ms frames)
  if (codec_id == AV_CODEC_ID_OPUS)
    av_dict_set(&options, "frame_duration", "20", 0);

  ret = avcodec_open2(s->codec, NULL, &options);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot open encoder (%s): %s\n", codec_desc->name, err2str(ret));
      goto error;
    }

  // airplay.c "misuses" the ffmpeg alac encoder in that it pushes frames with
  // 352 samples even though the encoder wants 4096 (and doesn't have variable
  // frame capability). This worked with no issues until ffmpeg 6, where it
  // seems a frame size check was added. The below circumvents the check, but is
  // dirty because we shouldn't be writing to this data element.
  if (ctx->settings.frame_size)
    s->codec->frame_size = ctx->settings.frame_size;

  // Copy the codec parameters we just set to the stream, so the muxer knows them
  ret = avcodec_parameters_from_context(s->stream->codecpar, s->codec);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot copy stream parameters (%s): %s\n", codec_desc->name, err2str(ret));
      goto error;
    }

  if (options)
    {
      DPRINTF(E_WARN, L_XCODE, "Encoder %s didn't recognize all options given to avcodec_open2\n", codec_desc->name);
      av_dict_free(&options);
    }

  return 0;

 error:
  if (s->codec)
    avcodec_free_context(&s->codec);
  if (options)
    av_dict_free(&options);

  return -1;
}

/*
 * Called by libavformat while demuxing. Used to interrupt/unblock av_read_frame
 * in case a source (especially a network stream) becomes unavailable.
 * 
 * @in arg        Will point to the decode context
 * @return        Non-zero if av_read_frame should be interrupted
 */
static int
decode_interrupt_cb(void *arg)
{
  struct decode_ctx *ctx;

  ctx = (struct decode_ctx *)arg;

  if (av_gettime() - ctx->timestamp > READ_TIMEOUT)
    {
      DPRINTF(E_LOG, L_XCODE, "Timeout while reading source (connection problem?)\n");

      return 1;
    }

  return 0;
}

/* Will read the next packet from the source, unless we are resuming after a
 * seek in which case the most recent packet found by transcode_seek() will be
 * returned. The packet will be put in ctx->packet.
 *
 * @out type      Media type of packet
 * @in  ctx       Decode context
 * @return        0 if OK, < 0 on error or end of file
 */
static int
read_packet(enum AVMediaType *type, struct decode_ctx *dec_ctx)
{
  int ret;

  // We just seeked, so transcode_seek() will have found a new ctx->packet and
  // we should just use start with that (if the stream is one are ok with)
  if (dec_ctx->resume)
    {
      dec_ctx->resume = 0;
      *type = stream_find(dec_ctx, dec_ctx->packet->stream_index);
      if (*type != AVMEDIA_TYPE_UNKNOWN)
	return 0;
    }

  do
    {
      dec_ctx->timestamp = av_gettime();

      av_packet_unref(dec_ctx->packet);
      ret = av_read_frame(dec_ctx->ifmt_ctx, dec_ctx->packet);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not read frame: %s\n", err2str(ret));
	  return ret;
	}

      *type = stream_find(dec_ctx, dec_ctx->packet->stream_index);
    }
  while (*type == AVMEDIA_TYPE_UNKNOWN);

  return 0;
}

// Prepares a packet from the encoder for muxing
static void
packet_prepare(AVPacket *pkt, struct stream_ctx *s)
{
  pkt->stream_index = s->stream->index;

  // This "wonderful" peace of code makes sure that the timestamp always increases,
  // even if the user seeked backwards. The muxer will not accept non-increasing
  // timestamps.
  pkt->pts += s->offset_pts;
  if (pkt->pts < s->prev_pts)
    {
      s->offset_pts += s->prev_pts - pkt->pts;
      pkt->pts = s->prev_pts;
    }
  s->prev_pts = pkt->pts;
  pkt->dts = pkt->pts; //FIXME

  av_packet_rescale_ts(pkt, s->codec->time_base, s->stream->time_base);
}

/*
 * Part 4+5 of the conversion chain: read -> decode -> filter -> encode -> write
 *
 */
static int
encode_write(struct encode_ctx *ctx, struct stream_ctx *s, AVFrame *filt_frame)
{
  int ret;

  // If filt_frame is null then flushing will be initiated by the codec
  ret = avcodec_send_frame(s->codec, filt_frame);
  if (ret < 0)
    return ret;

  while (1)
    {
      ret = avcodec_receive_packet(s->codec, ctx->encoded_pkt);
      if (ret < 0)
	{
	  if (ret == AVERROR(EAGAIN))
	    ret = 0;

	  break;
	}

      packet_prepare(ctx->encoded_pkt, s);

      ret = av_interleaved_write_frame(ctx->ofmt_ctx, ctx->encoded_pkt);
      if (ret < 0)
        {
	  DPRINTF(E_WARN, L_XCODE, "av_interleaved_write_frame() failed: %s\n", err2str(ret));
	  break;
        }
    }

  return ret;
}

/*
 * Part 3 of the conversion chain: read -> decode -> filter -> encode -> write
 *
 * transcode_encode() starts here since the caller already has a frame
 *
 */
static int
filter_encode_write(struct encode_ctx *ctx, struct stream_ctx *s, AVFrame *frame)
{
  int ret;

  // Push the decoded frame into the filtergraph
  if (frame)
    {
      ret = av_buffersrc_add_frame(s->buffersrc_ctx, frame);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error while feeding the filtergraph: %s\n", err2str(ret));
	  return -1;
	}
    }

  // Pull filtered frames from the filtergraph and pass to encoder
  while (1)
    {
      ret = av_buffersink_get_frame(s->buffersink_ctx, ctx->filt_frame);
      if (ret < 0)
	{
	  if (!frame) // We are flushing
	    ret = encode_write(ctx, s, NULL);
	  else if (ret == AVERROR(EAGAIN))
	    ret = 0;

	  break;
	}

      ret = encode_write(ctx, s, ctx->filt_frame);
      av_frame_unref(ctx->filt_frame);
      if (ret < 0)
	break;
    }

  return ret;
}

/*
 * Part 2 of the conversion chain: read -> decode -> filter -> encode -> write
 *
 * If there is no encode_ctx the chain will aborted here
 *
 */
static int
decode_filter_encode_write(struct transcode_ctx *ctx, struct stream_ctx *s, AVPacket *pkt, enum AVMediaType type)
{
  struct decode_ctx *dec_ctx = ctx->decode_ctx;
  struct stream_ctx *out_stream = NULL;
  int ret;

  ret = avcodec_send_packet(s->codec, pkt);
  if (ret < 0 && (ret != AVERROR_INVALIDDATA) && (ret != AVERROR(EAGAIN))) // We don't bail on invalid data, some streams work anyway
    {
      DPRINTF(E_LOG, L_XCODE, "Decoder error, avcodec_send_packet said '%s' (%d)\n", err2str(ret), ret);
      return ret;
    }

  if (ctx->encode_ctx)
    {
      if (type == AVMEDIA_TYPE_AUDIO)
	out_stream = &ctx->encode_ctx->audio_stream;
      else if (type == AVMEDIA_TYPE_VIDEO)
	out_stream = &ctx->encode_ctx->video_stream;
      else
	return -1;
    }

  while (1)
    {
      ret = avcodec_receive_frame(s->codec, dec_ctx->decoded_frame);
      if (ret < 0)
	{
	  if (ret == AVERROR(EAGAIN))
	    ret = 0;
	  else if (out_stream)
	    ret = filter_encode_write(ctx->encode_ctx, out_stream, NULL); // Flush

	  break;
	}

      dec_ctx->got_frame = 1;

      if (!out_stream)
	break;

      ret = filter_encode_write(ctx->encode_ctx, out_stream, dec_ctx->decoded_frame);
      if (ret < 0)
	break;
    }

  return ret;
}

/*
 * Part 1 of the conversion chain: read -> decode -> filter -> encode -> write
 *
 * Will read exactly one packet from the input and put it in the chain. You
 * cannot count on anything coming out of the other end from just one packet,
 * so you probably should loop when calling this and check the contents of
 * enc_ctx->obuf.
 *
 */
static int
read_decode_filter_encode_write(struct transcode_ctx *ctx)
{
  struct decode_ctx *dec_ctx = ctx->decode_ctx;
  struct encode_ctx *enc_ctx = ctx->encode_ctx;
  enum AVMediaType type;
  int ret;

  ret = read_packet(&type, dec_ctx);
  if (ret < 0)
    {
      if (ret == AVERROR_EOF)
	dec_ctx->eof = 1;

      if (dec_ctx->audio_stream.stream)
	decode_filter_encode_write(ctx, &dec_ctx->audio_stream, NULL, AVMEDIA_TYPE_AUDIO);
      if (dec_ctx->video_stream.stream)
	decode_filter_encode_write(ctx, &dec_ctx->video_stream, NULL, AVMEDIA_TYPE_VIDEO);

      if (enc_ctx)
	av_interleaved_write_frame(enc_ctx->ofmt_ctx, NULL); // Flush muxer
      if (enc_ctx && !enc_ctx->settings.without_libav_trailer)
	av_write_trailer(enc_ctx->ofmt_ctx);

      return ret;
    }

  if (type == AVMEDIA_TYPE_AUDIO)
    ret = decode_filter_encode_write(ctx, &dec_ctx->audio_stream, dec_ctx->packet, type);
  else if (type == AVMEDIA_TYPE_VIDEO)
    ret = decode_filter_encode_write(ctx, &dec_ctx->video_stream, dec_ctx->packet, type);

  return ret;
}

/* ------------------------------- CUSTOM I/O ------------------------------ */
/*      For using ffmpeg with evbuffer input/output instead of files         */

static int
avio_evbuffer_read(void *opaque, uint8_t *buf, int size)
{
  struct avio_evbuffer *ae = (struct avio_evbuffer *)opaque;
  int ret;

  ret = evbuffer_remove(ae->evbuf, buf, size);

  // Must return AVERROR, see avio.h: avio_alloc_context()
  return (ret > 0) ? ret : AVERROR_EOF;
}

#if USE_CONST_AVIO_WRITE_PACKET
static int
avio_evbuffer_write(void *opaque, const uint8_t *buf, int size)
#else
static int
avio_evbuffer_write(void *opaque, uint8_t *buf, int size)
#endif
{
  struct avio_evbuffer *ae = (struct avio_evbuffer *)opaque;
  int ret;

  ret = evbuffer_add(ae->evbuf, buf, size);

  return (ret == 0) ? size : -1;
}

static int64_t
avio_evbuffer_seek(void *opaque, int64_t offset, int whence)
{
  struct avio_evbuffer *ae = (struct avio_evbuffer *)opaque;
  enum transcode_seek_type seek_type;

  // Caller shouldn't need to know about ffmpeg defines
  if (whence & AVSEEK_SIZE)
    seek_type = XCODE_SEEK_SIZE;
  else if (whence == SEEK_SET)
    seek_type = XCODE_SEEK_SET;
  else if (whence == SEEK_CUR)
    seek_type = XCODE_SEEK_CUR;
  else
    return -1;

  return ae->seekfn(ae->seekfn_arg, offset, seek_type);
}

static AVIOContext *
avio_evbuffer_open(struct transcode_evbuf_io *evbuf_io, int is_output)
{
  struct avio_evbuffer *ae;
  AVIOContext *s;

  ae = calloc(1, sizeof(struct avio_evbuffer));
  if (!ae)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Out of memory for avio_evbuffer\n");

      return NULL;
    }

  ae->buffer = av_mallocz(AVIO_BUFFER_SIZE);
  if (!ae->buffer)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Out of memory for avio buffer\n");

      free(ae);
      return NULL;
    }

  ae->evbuf = evbuf_io->evbuf;
  ae->seekfn = evbuf_io->seekfn;
  ae->seekfn_arg = evbuf_io->seekfn_arg;

  if (is_output)
    s = avio_alloc_context(ae->buffer, AVIO_BUFFER_SIZE, 1, ae, NULL, avio_evbuffer_write, (evbuf_io->seekfn ? avio_evbuffer_seek : NULL));
  else
    s = avio_alloc_context(ae->buffer, AVIO_BUFFER_SIZE, 0, ae, avio_evbuffer_read, NULL, (evbuf_io->seekfn ? avio_evbuffer_seek : NULL));

  if (!s)
    {
      DPRINTF(E_LOG, L_FFMPEG, "Could not allocate AVIOContext\n");

      av_free(ae->buffer);
      free(ae);
      return NULL;
    }

  s->seekable = (evbuf_io->seekfn ? AVIO_SEEKABLE_NORMAL : 0);

  return s;
}

static void
avio_evbuffer_close(AVIOContext *s)
{
  struct avio_evbuffer *ae;

  if (!s)
    return;

  ae = (struct avio_evbuffer *)s->opaque;

  avio_flush(s);

  av_free(s->buffer);
  free(ae);

  av_free(s);
}


/* ----------------------- CUSTOM HEADER GENERATION ------------------------ */

static int
make_wav_header(struct evbuffer **wav_header, int sample_rate, int bytes_per_sample, int channels, off_t bytes_total)
{
  uint8_t header[WAV_HEADER_LEN];

  uint32_t wav_size = bytes_total - WAV_HEADER_LEN;

  memcpy(header, "RIFF", 4);
  add_le32(header + 4, 36 + wav_size);
  memcpy(header + 8, "WAVEfmt ", 8);
  add_le32(header + 16, 16);
  add_le16(header + 20, 1);
  add_le16(header + 22, channels);     /* channels */
  add_le32(header + 24, sample_rate);  /* samplerate */
  add_le32(header + 28, sample_rate * channels * bytes_per_sample); /* byte rate */
  add_le16(header + 32, channels * bytes_per_sample);               /* block align */
  add_le16(header + 34, 8 * bytes_per_sample);                      /* bits per sample */
  memcpy(header + 36, "data", 4);
  add_le32(header + 40, wav_size);

  *wav_header = evbuffer_new();
  evbuffer_add(*wav_header, header, sizeof(header));
  return 0;
}

static int
mp4_adjust_moov_stco_offset(uint8_t *moov, size_t moov_len)
{
  uint8_t stco_needle[8] = { 's', 't', 'c', 'o', 0, 0, 0, 0 };
  uint32_t be32;
  uint32_t n_entries;
  uint32_t entry;
  uint8_t *ptr;
  uint8_t *end;

  end = moov + moov_len;
  ptr = memmem(moov, moov_len, stco_needle, sizeof(stco_needle));
  if (!ptr || ptr + sizeof(stco_needle) + sizeof(be32) > end)
    return -1;

  ptr += sizeof(stco_needle);
  memcpy(&be32, ptr, sizeof(be32));
  for (n_entries = be32toh(be32); n_entries > 0; n_entries--)
    {
      ptr += sizeof(be32);
      if (ptr + sizeof(be32) > end)
	return -1;

      memcpy(&be32, ptr, sizeof(be32));
      entry = be32toh(be32);
      be32 = htobe32(entry + moov_len);
      memcpy(ptr, &be32, sizeof(be32));
    }

  return 0;
}

static int
mp4_header_trailer_from_evbuf(uint8_t **header, size_t *header_len, uint8_t **trailer, size_t *trailer_len, struct evbuffer *evbuf, int64_t start_pos)
{
  uint8_t *buf = evbuffer_pullup(evbuf, -1);
  size_t buf_len = evbuffer_get_length(evbuf);
  int64_t pos = start_pos;
  int bytes_added = 0;
  uint8_t *marker;
  size_t len;
  int ret;

  while (buf_len > 0)
    {
      marker = memmem(buf, buf_len, xcode_seek_marker, sizeof(xcode_seek_marker));
      len = marker ? marker - buf : buf_len;

      if (pos <= *header_len) // Either first write of header or seek to pos inside header
	ret = copy_buffer_to_position(header, header_len, buf, len, pos);
      else if (pos >= start_pos) // Either first write of trailer or seek to pos inside trailer
	ret = copy_buffer_to_position(trailer, trailer_len, buf, len, pos - start_pos);
      else // Unexpected seek to body (pos is before trailer but not in header)
	ret = -1;

      if (ret < 0)
	return -1;

      bytes_added += ret;
      if (!marker)
	break;

      memcpy(&pos, marker + sizeof(xcode_seek_marker), sizeof(pos));
      buf += len + sizeof(xcode_seek_marker) + sizeof(pos);
      buf_len -= len + sizeof(xcode_seek_marker) + sizeof(pos);
  }

  evbuffer_drain(evbuf, -1);
  return bytes_added;
}

// Transcodes the entire file so that we can grab the header, which will then
// have a correct moov atom. The moov atom contains elements like stco and stsz
// which can only be made when the encoding has been done, since they contain
// information about where the frames are in the file. iTunes and Soundsbrdige
// requires these to be correct, otherwise they won't play our transcoded files.
// They also require that the atom is in the beginning of the file. ffmpeg's
// "faststart" option does this, but is difficult to use with non-file output,
// instead we move the atom ourselves.
static int
make_mp4_header(struct evbuffer **mp4_header, const char *url)
{
  struct transcode_ctx ctx = { 0 };
  struct transcode_evbuf_io evbuf_io = { 0 };
  uint8_t free_tag[4] = { 'f', 'r', 'e', 'e' };
  uint8_t *header = NULL;
  uint8_t *trailer = NULL;
  size_t header_len = 0;
  size_t trailer_len = 0;
  uint8_t *ptr;
  int ret;

  if (!url || *url != '/')
    return -1;

  CHECK_NULL(L_XCODE, evbuf_io.evbuf = evbuffer_new());

  evbuf_io.seekfn = dummy_seek;
  evbuf_io.seekfn_arg = &ctx;

  ctx.decode_ctx = transcode_decode_setup(XCODE_MP4_ALAC_HEADER, NULL, DATA_KIND_FILE, url, NULL, -1);
  if (!ctx.decode_ctx)
    goto error;

  ctx.encode_ctx = transcode_encode_setup_with_io(XCODE_MP4_ALAC_HEADER, NULL, &evbuf_io, ctx.decode_ctx, 0, 0);
  if (!ctx.encode_ctx)
    goto error;

  // Save the template header, which looks something like this (note that the
  // mdate size is still unknown, so just zeroes, and there is no moov):
  //
  //  0000  00 00 00 1c 66 74 79 70 69 73 6f 6d 00 00 02 00  ....ftypisom....
  //  0010  69 73 6f 6d 69 73 6f 32 6d 70 34 31 00 00 00 08  isomiso2mp41....
  //  0020  66 72 65 65 00 00 00 00 6d 64 61 74              free....mdat
  ret = avformat_write_header(ctx.encode_ctx->ofmt_ctx, NULL);
  if (ret < 0)
    goto error;

  // Writes the obuf to the header buffer, bytes_processed is 0
  ret = mp4_header_trailer_from_evbuf(&header, &header_len, &trailer, &trailer_len, ctx.encode_ctx->obuf, ctx.encode_ctx->bytes_processed);
  if (ret < 0)
    goto error;

  ctx.encode_ctx->bytes_processed += ret;

  // Encode but discard result, this is just so that ffmpeg can create the
  // missing header data.
  while (read_decode_filter_encode_write(&ctx) == 0)
    {
      ctx.encode_ctx->bytes_processed += evbuffer_get_length(ctx.encode_ctx->obuf);
      evbuffer_drain(ctx.encode_ctx->obuf, -1);
    }

  // Here, ffmpeg will seek back and write the size to the mdat atom and then
  // seek forward again to write the trailer. Since we can't actually seek, we
  // instead look for the markers that dummy_seek() inserted.
  av_write_trailer(ctx.encode_ctx->ofmt_ctx);
  ret = mp4_header_trailer_from_evbuf(&header, &header_len, &trailer, &trailer_len, ctx.encode_ctx->obuf, ctx.encode_ctx->bytes_processed);
  if (ret < 0 || !header || !trailer)
    goto error;

  // The trailer buffer should now contain the moov atom. We need to adjust the
  // chunk offset (stco) in it because we will move it to the beginning of the
  // file.
  ret = mp4_adjust_moov_stco_offset(trailer, trailer_len);
  if (ret < 0)
    goto error;

  // Now we want to move the trailer (which has the moov atom) into the header.
  // We insert it before the free atom, because that's what ffmpeg does when
  // the "faststart" option is set.
  CHECK_NULL(L_XCODE, header = realloc(header, header_len + trailer_len));

  ptr = memmem(header, header_len, free_tag, sizeof(free_tag));
  if (!ptr || ptr - header < sizeof(uint32_t))
    goto error;

  ptr -= sizeof(uint32_t);
  memmove(ptr + trailer_len, ptr, header + header_len - ptr);
  memcpy(ptr, trailer, trailer_len);
  header_len += trailer_len;

  *mp4_header = evbuffer_new();
  evbuffer_add(*mp4_header, header, header_len);

  free(header);
  free(trailer);
  transcode_decode_cleanup(&ctx.decode_ctx);
  transcode_encode_cleanup(&ctx.encode_ctx);
  evbuffer_free(evbuf_io.evbuf);
  return 0;

 error:
  if (header)
    DHEXDUMP(E_DBG, L_XCODE, header, header_len, "MP4 header\n");
  if (trailer)
    DHEXDUMP(E_DBG, L_XCODE, trailer, trailer_len, "MP4 trailer\n");

  free(header);
  free(trailer);
  transcode_decode_cleanup(&ctx.decode_ctx);
  transcode_encode_cleanup(&ctx.encode_ctx);
  evbuffer_free(evbuf_io.evbuf);
  return -1;
}


/* --------------------------- INPUT/OUTPUT INIT --------------------------- */

static int
open_decoder(AVCodecContext **dec_ctx, unsigned int *stream_index, struct decode_ctx *ctx, enum AVMediaType type)
{
#if USE_CONST_AVCODEC
  const AVCodec *decoder;
#else
  // Not const before ffmpeg 5.0
  AVCodec *decoder;
#endif
  int ret;

  ret = av_find_best_stream(ctx->ifmt_ctx, type, -1, -1, &decoder, 0);
  if (ret < 0)
    {
      if (!ctx->settings.silent)
	DPRINTF(E_LOG, L_XCODE, "Error finding best stream: %s\n", err2str(ret));
      return ret;
    }

  *stream_index = (unsigned int)ret;

  CHECK_NULL(L_XCODE, *dec_ctx = avcodec_alloc_context3(decoder));

  // Filter creation will need the sample rate and format that the decoder is
  // giving us - however sample rate of dec_ctx will be 0 if we don't prime it
  // with the streams codecpar data.
  ret = avcodec_parameters_to_context(*dec_ctx, ctx->ifmt_ctx->streams[*stream_index]->codecpar);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to copy codecpar for stream #%d: %s\n", *stream_index, err2str(ret));
      avcodec_free_context(dec_ctx);
      return ret;
    }

  ret = avcodec_open2(*dec_ctx, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%d: %s\n", *stream_index, err2str(ret));
      avcodec_free_context(dec_ctx);
      return ret;
    }

  return 0;
}

static void
close_input(struct decode_ctx *ctx)
{
  if (!ctx->ifmt_ctx)
    return;

  avio_evbuffer_close(ctx->avio);
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avformat_close_input(&ctx->ifmt_ctx);
  ctx->ifmt_ctx = NULL;
}

static int
open_input(struct decode_ctx *ctx, const char *path, struct transcode_evbuf_io *evbuf_io, enum probe_type probe_type)
{
  AVDictionary *options = NULL;
  AVCodecContext *dec_ctx;
#if USE_CONST_AVFORMAT
  const AVInputFormat *ifmt;
#else
  // Not const before ffmpeg 5.0
  AVInputFormat *ifmt;
#endif
  unsigned int stream_index;
  const char *user_agent;
  int ret = 0;

  CHECK_NULL(L_XCODE, ctx->ifmt_ctx = avformat_alloc_context());

  // Caller can ask for small probe to start quicker + search for embedded
  // artwork quicker. Especially useful for http sources. The standard probe
  // size takes around 5 sec for an mp3, while the below only takes around a
  // second. The improved performance comes at the cost of possible inaccuracy.
  if (probe_type == PROBE_TYPE_QUICK)
    {
      ctx->ifmt_ctx->probesize = 65536;
      ctx->ifmt_ctx->format_probesize = 65536;
    }

  if (ctx->data_kind == DATA_KIND_HTTP)
    {
      av_dict_set(&options, "icy", "1", 0);

      user_agent = cfg_getstr(cfg_getsec(cfg, "general"), "user_agent");
      av_dict_set(&options, "user_agent", user_agent, 0);

      av_dict_set(&options, "reconnect", "1", 0);
      // The below option disabled because it does not work with m3u8 streams,
      // see https://lists.ffmpeg.org/pipermail/ffmpeg-user/2018-September/041109.html
//      av_dict_set(&options, "reconnect_at_eof", "1", 0);
      av_dict_set(&options, "reconnect_streamed", "1", 0);
    }

  // TODO Newest versions of ffmpeg have timeout and reconnect options we should use
  ctx->ifmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
  ctx->ifmt_ctx->interrupt_callback.opaque = ctx;
  ctx->timestamp = av_gettime();

  if (evbuf_io)
    {
      ifmt = av_find_input_format(ctx->settings.in_format);
      if (!ifmt)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not find input format: '%s'\n", ctx->settings.in_format);
	  goto out_fail;
	}

      CHECK_NULL(L_XCODE, ctx->avio = avio_evbuffer_open(evbuf_io, 0));

      ctx->ifmt_ctx->pb = ctx->avio;
      ret = avformat_open_input(&ctx->ifmt_ctx, NULL, ifmt, &options);
    }
  else
    {
      ret = avformat_open_input(&ctx->ifmt_ctx, path, NULL, &options);
    }

  if (options)
    av_dict_free(&options);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot open '%s': %s\n", path, err2str(ret));
      goto out_fail;
    }

  // If the source has REPLAYGAIN_TRACK_GAIN metadata, this will inject the
  // values into the the next packet's side data (as AV_FRAME_DATA_REPLAYGAIN),
  // which has the effect that a volume replaygain filter works. Note that
  // ffmpeg itself uses another method in process_input() in ffmpeg.c.
  av_format_inject_global_side_data(ctx->ifmt_ctx);

  ret = avformat_find_stream_info(ctx->ifmt_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot find stream information: %s\n", err2str(ret));
      goto out_fail;
    }

  if (ctx->ifmt_ctx->nb_streams > MAX_STREAMS)
    {
      DPRINTF(E_LOG, L_XCODE, "File '%s' has too many streams (%u)\n", path, ctx->ifmt_ctx->nb_streams);
      goto out_fail;
    }

  if (ctx->settings.encode_audio)
    {
      ret = open_decoder(&dec_ctx, &stream_index, ctx, AVMEDIA_TYPE_AUDIO);
      if (ret < 0)
	goto out_fail;

      ctx->audio_stream.codec = dec_ctx;
      ctx->audio_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  if (ctx->settings.encode_video)
    {
      ret = open_decoder(&dec_ctx, &stream_index, ctx, AVMEDIA_TYPE_VIDEO);
      if (ret < 0)
	goto out_fail;

      ctx->video_stream.codec = dec_ctx;
      ctx->video_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  return 0;

 out_fail:
  close_input(ctx);
  return (ret < 0 ? ret : -1); // If we got an error code from ffmpeg then return that
}

static void
close_output(struct encode_ctx *ctx)
{
  if (!ctx->ofmt_ctx)
    return;

  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avio_evbuffer_close(ctx->ofmt_ctx->pb);
  avformat_free_context(ctx->ofmt_ctx);
  ctx->ofmt_ctx = NULL;
}

static int
open_output(struct encode_ctx *ctx, struct transcode_evbuf_io *evbuf_io, struct decode_ctx *src_ctx)
{
#if USE_CONST_AVFORMAT
  const AVOutputFormat *oformat;
#else
  // Not const before ffmpeg 5.0
  AVOutputFormat *oformat;
#endif
  AVDictionary *options = NULL;
  struct evbuffer *header = NULL;
  int ret;

  oformat = av_guess_format(ctx->settings.format, NULL, NULL);
  if (!oformat)
    {
      DPRINTF(E_LOG, L_XCODE, "ffmpeg/libav could not find the '%s' output format\n", ctx->settings.format);
      return -1;
    }

#if USE_NO_CLEAR_AVFMT_NOFILE
  CHECK_ERRNO(L_XCODE, avformat_alloc_output_context2(&ctx->ofmt_ctx, oformat, NULL, NULL));
#else
  // Clear AVFMT_NOFILE bit, it is not allowed as we will set our own AVIOContext.
  // If this is not done with e.g. ffmpeg 3.4 then artwork rescaling will fail.
  oformat->flags &= ~AVFMT_NOFILE;

  CHECK_NULL(L_XCODE, ctx->ofmt_ctx = avformat_alloc_context());

  ctx->ofmt_ctx->oformat = oformat;
#endif

  CHECK_NULL(L_XCODE, ctx->ofmt_ctx->pb = avio_evbuffer_open(evbuf_io, 1));
  ctx->obuf = evbuf_io->evbuf;

  if (ctx->settings.encode_audio)
    {
      ret = stream_add(ctx, &ctx->audio_stream, ctx->settings.audio_codec);
      if (ret < 0)
	goto error;
    }

  if (ctx->settings.encode_video)
    {
      ret = stream_add(ctx, &ctx->video_stream, ctx->settings.video_codec);
      if (ret < 0)
	goto error;
    }

  ret = avformat_init_output(ctx->ofmt_ctx, &options);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error initializing output: %s\n", err2str(ret));
      goto error;
    }
  else if (options)
    {
      DPRINTF(E_WARN, L_XCODE, "Didn't recognize all options given to avformat_init_output\n");
      av_dict_free(&options);
      goto error;
    }

  // For WAV output, both avformat_write_header() and manual wav header is required
  if (!ctx->settings.without_libav_header)
    {
      ret = avformat_write_header(ctx->ofmt_ctx, NULL);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error writing header to output buffer: %s\n", err2str(ret));
	  goto error;
	}
    }
  if (ctx->settings.with_wav_header)
    {
      ret = make_wav_header(&header, ctx->settings.sample_rate, av_get_bytes_per_sample(ctx->settings.sample_format), ctx->settings.nb_channels, ctx->bytes_total);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error creating WAV header\n");
	  goto error;
	}

      evbuffer_add_buffer(ctx->obuf, header);
      evbuffer_free(header);
    }
  if (ctx->settings.with_mp4_header)
    {
      ret = make_mp4_header(&header, src_ctx->ifmt_ctx->url);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error creating MP4 header\n");
	  goto error;
	}

      evbuffer_add_buffer(ctx->obuf, header);
      evbuffer_free(header);
    }

  return 0;

 error:
  close_output(ctx);
  return -1;
}

static int
filter_def_abuffer(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
#if USE_CH_LAYOUT
  char buf[64];

  // Some AIFF files only have a channel number, not a layout
  if (in_stream->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    av_channel_layout_default(&in_stream->codec->ch_layout, in_stream->codec->ch_layout.nb_channels);

  av_channel_layout_describe(&in_stream->codec->ch_layout, buf, sizeof(buf));

  snprintf(def->args, sizeof(def->args),
           "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
           in_stream->stream->time_base.num, in_stream->stream->time_base.den,
           in_stream->codec->sample_rate, av_get_sample_fmt_name(in_stream->codec->sample_fmt),
           buf);
#else
  if (!in_stream->codec->channel_layout)
    in_stream->codec->channel_layout = av_get_default_channel_layout(in_stream->codec->channels);

  snprintf(def->args, sizeof(def->args),
           "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
           in_stream->stream->time_base.num, in_stream->stream->time_base.den,
           in_stream->codec->sample_rate, av_get_sample_fmt_name(in_stream->codec->sample_fmt),
           in_stream->codec->channel_layout);
#endif
  snprintf(def->name, sizeof(def->name), "abuffer");
  return 0;
}

static int
filter_def_aformat(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
#if USE_CH_LAYOUT
  char buf[64];

  if (out_stream->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    av_channel_layout_default(&out_stream->codec->ch_layout, out_stream->codec->ch_layout.nb_channels);

  av_channel_layout_describe(&out_stream->codec->ch_layout, buf, sizeof(buf));

  snprintf(def->args, sizeof(def->args),
           "sample_fmts=%s:sample_rates=%d:channel_layouts=%s",
           av_get_sample_fmt_name(out_stream->codec->sample_fmt), out_stream->codec->sample_rate,
           buf);
#else
  // For some AIFF files, ffmpeg (3.4.6) will not give us a channel_layout (bug in ffmpeg?)
  if (!out_stream->codec->channel_layout)
    out_stream->codec->channel_layout = av_get_default_channel_layout(out_stream->codec->channels);

  snprintf(def->args, sizeof(def->args),
           "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
           av_get_sample_fmt_name(out_stream->codec->sample_fmt), out_stream->codec->sample_rate,
           out_stream->codec->channel_layout);
#endif
  snprintf(def->name, sizeof(def->name), "aformat");
  return 0;
}

static int
filter_def_abuffersink(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  snprintf(def->name, sizeof(def->name), "abuffersink");
  *def->args = '\0';
  return 0;
}

static int
filter_def_buffer(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  snprintf(def->name, sizeof(def->name), "buffer");
  snprintf(def->args, sizeof(def->args),
           "width=%d:height=%d:pix_fmt=%s:time_base=%d/%d:sar=%d/%d",
           in_stream->codec->width, in_stream->codec->height, av_get_pix_fmt_name(in_stream->codec->pix_fmt),
           in_stream->stream->time_base.num, in_stream->stream->time_base.den,
           in_stream->codec->sample_aspect_ratio.num, in_stream->codec->sample_aspect_ratio.den);
  return 0;
}

static int
filter_def_format(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  snprintf(def->name, sizeof(def->name), "format");
  snprintf(def->args, sizeof(def->args),
           "pix_fmts=%s", av_get_pix_fmt_name(out_stream->codec->pix_fmt));
  return 0;
}

static int
filter_def_scale(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  snprintf(def->name, sizeof(def->name), "scale");
  snprintf(def->args, sizeof(def->args),
           "w=%d:h=%d", out_stream->codec->width, out_stream->codec->height);
  return 0;
}

static int
filter_def_buffersink(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  snprintf(def->name, sizeof(def->name), "buffersink");
  *def->args = '\0';
  return 0;
}

static int
filter_def_user(struct filter_def *def, struct stream_ctx *out_stream, struct stream_ctx *in_stream, const char *deffn_arg)
{
  char *ptr;

  snprintf(def->name, sizeof(def->name), "%s", deffn_arg);

  ptr = strchr(def->name, '=');
  if (ptr)
    {
      *ptr = '\0';
      snprintf(def->args, sizeof(def->args), "%s", ptr + 1);
    }
  else
    *def->args = '\0';

  return 0;
}

static int
define_audio_filters(struct filters *filters, size_t filters_len, bool with_user_filters)
{
  int num_user_filters;
  int i;

  num_user_filters = cfg_size(cfg_getsec(cfg, "library"), "decode_audio_filters");
  if (filters_len < num_user_filters + 3)
    {
      DPRINTF(E_LOG, L_XCODE, "Too many audio filters configured (%d, max is %zu)\n", num_user_filters, filters_len - 3);
      return -1;
    }

  filters[0].deffn = filter_def_abuffer;
  for (i = 0; with_user_filters && i < num_user_filters; i++)
    {
      filters[1 + i].deffn = filter_def_user;
      filters[1 + i].deffn_arg = cfg_getnstr(cfg_getsec(cfg, "library"), "decode_audio_filters", i);
    }
  filters[1 + i].deffn = filter_def_aformat;
  filters[2 + i].deffn = filter_def_abuffersink;

  return 0;
}

static int
define_video_filters(struct filters *filters, size_t filters_len, bool with_user_filters)
{
  int num_user_filters;
  int i;

  num_user_filters = cfg_size(cfg_getsec(cfg, "library"), "decode_video_filters");
  if (filters_len < num_user_filters + 3)
    {
      DPRINTF(E_LOG, L_XCODE, "Too many video filters configured (%d, max is %zu)\n", num_user_filters, filters_len - 3);
      return -1;
    }

  filters[0].deffn = filter_def_buffer;
  for (i = 0; with_user_filters && i < num_user_filters; i++)
    {
      filters[1 + i].deffn = filter_def_user;
      filters[1 + i].deffn_arg = cfg_getnstr(cfg_getsec(cfg, "library"), "decode_video_filters", i);
    }
  filters[1 + i].deffn = filter_def_format;
  filters[2 + i].deffn = filter_def_scale;
  filters[3 + i].deffn = filter_def_buffersink;

  return 0;
}

static int
add_filters(int *num_added, AVFilterGraph *filter_graph, struct filters *filters, size_t filters_len,
            struct stream_ctx *out_stream, struct stream_ctx *in_stream)
{
  const AVFilter *av_filter;
  struct filter_def def;
  int i;
  int ret;

  for (i = 0; i < filters_len && filters[i].deffn; i++)
    {
      ret = filters[i].deffn(&def, out_stream, in_stream, filters[i].deffn_arg);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error creating filter definition\n");
	  return -1;
	}

      av_filter = avfilter_get_by_name(def.name);
      if (!av_filter)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not find filter '%s'\n", def.name);
	  return -1;
	}

      ret = avfilter_graph_create_filter(&filters[i].av_ctx, av_filter, def.name, def.args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error creating filter '%s': %s\n", def.name, err2str(ret));
	  return -1;
	}

      DPRINTF(E_DBG, L_XCODE, "Created '%s' filter: '%s'\n", def.name, def.args);

      if (i == 0)
	continue;

      ret = avfilter_link(filters[i - 1].av_ctx, 0, filters[i].av_ctx, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error connecting filters: %s\n", err2str(ret));
	  return -1;
	}
    }

  *num_added = i;
  return 0;
}

static int
create_filtergraph(struct stream_ctx *out_stream, struct filters *filters, size_t filters_len, struct stream_ctx *in_stream)
{
  AVFilterGraph *filter_graph;
  int ret;
  int added;

  CHECK_NULL(L_XCODE, filter_graph = avfilter_graph_alloc());

  ret = add_filters(&added, filter_graph, filters, filters_len, out_stream, in_stream);
  if (ret < 0)
    {
      goto out_fail;
    }

  ret = avfilter_graph_config(filter_graph, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Filter graph config failed: %s\n", err2str(ret));
      goto out_fail;
    }

  out_stream->buffersrc_ctx = filters[0].av_ctx;
  out_stream->buffersink_ctx = filters[added - 1].av_ctx;
  out_stream->filter_graph = filter_graph;

  return 0;

 out_fail:
  avfilter_graph_free(&filter_graph);
  return -1;
}

static void
close_filters(struct encode_ctx *ctx)
{
  avfilter_graph_free(&ctx->audio_stream.filter_graph);
  avfilter_graph_free(&ctx->video_stream.filter_graph);
}

static int
open_filters(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  struct filters filters[MAX_FILTERS] = { 0 };
  int ret;

  if (ctx->settings.encode_audio)
    {
      ret = define_audio_filters(filters, ARRAY_SIZE(filters), ctx->settings.with_user_filters);
      if (ret < 0)
	goto out_fail;

      ret = create_filtergraph(&ctx->audio_stream, filters, ARRAY_SIZE(filters), &src_ctx->audio_stream);
      if (ret < 0)
	goto out_fail;

      // Many audio encoders require a fixed frame size. This will ensure that
      // the filt_frame from av_buffersink_get_frame has that size (except EOF).
      if (! (ctx->audio_stream.codec->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
	av_buffersink_set_frame_size(ctx->audio_stream.buffersink_ctx, ctx->audio_stream.codec->frame_size);
    }

  if (ctx->settings.encode_video)
    {
      ret = define_video_filters(filters, ARRAY_SIZE(filters), ctx->settings.with_user_filters);
      if (ret < 0)
	goto out_fail;

      ret = create_filtergraph(&ctx->video_stream, filters, ARRAY_SIZE(filters), &src_ctx->video_stream);
      if (ret < 0)
	goto out_fail;
    }

  return 0;

 out_fail:
  close_filters(ctx);
  return -1;
}


/* ----------------------------- TRANSCODE API ----------------------------- */

/*                                  Setup                                    */

struct decode_ctx *
transcode_decode_setup(enum transcode_profile profile, struct media_quality *quality, enum data_kind data_kind, const char *path, struct transcode_evbuf_io *evbuf_io, uint32_t len_ms)
{
  struct decode_ctx *ctx;
  int ret;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct decode_ctx)));
  CHECK_NULL(L_XCODE, ctx->decoded_frame = av_frame_alloc());
  CHECK_NULL(L_XCODE, ctx->packet = av_packet_alloc());

  ctx->len_ms = len_ms;
  ctx->data_kind = data_kind;

  ret = init_settings(&ctx->settings, profile, quality);
  if (ret < 0)
    goto fail_free;

  if (data_kind == DATA_KIND_HTTP)
    {
      ret = open_input(ctx, path, evbuf_io, PROBE_TYPE_QUICK);

      // Retry with a default, slower probe size
      if (ret == AVERROR_STREAM_NOT_FOUND)
	ret = open_input(ctx, path, evbuf_io, PROBE_TYPE_DEFAULT);
    }
  else
    ret = open_input(ctx, path, evbuf_io, PROBE_TYPE_DEFAULT);

  if (ret < 0)
    goto fail_free;

  return ctx;

 fail_free:
  av_packet_free(&ctx->packet);
  av_frame_free(&ctx->decoded_frame);
  free(ctx);
  return NULL;
}

struct encode_ctx *
transcode_encode_setup_with_io(enum transcode_profile profile, struct media_quality *quality, struct transcode_evbuf_io *evbuf_io, struct decode_ctx *src_ctx, int width, int height)
{
  struct encode_ctx *ctx;
  int dst_bytes_per_sample;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct encode_ctx)));
  CHECK_NULL(L_XCODE, ctx->filt_frame = av_frame_alloc());
  CHECK_NULL(L_XCODE, ctx->encoded_pkt = av_packet_alloc());
  CHECK_NULL(L_XCODE, ctx->evbuf_io.evbuf = evbuffer_new());

  // Caller didn't specify one, so use our own
  if (!evbuf_io)
    evbuf_io = &ctx->evbuf_io;

  // Initialize general settings
  if (init_settings(&ctx->settings, profile, quality) < 0)
    goto error;

  if (ctx->settings.encode_audio && init_settings_from_audio(&ctx->settings, profile, src_ctx, quality) < 0)
    goto error;

  if (ctx->settings.encode_video && init_settings_from_video(&ctx->settings, profile, src_ctx, width, height) < 0)
    goto error;

  dst_bytes_per_sample = av_get_bytes_per_sample(ctx->settings.sample_format);
  ctx->bytes_total = size_estimate(profile, ctx->settings.bit_rate, ctx->settings.sample_rate, dst_bytes_per_sample, ctx->settings.nb_channels, src_ctx->len_ms);

  if (ctx->settings.with_icy && src_ctx->data_kind == DATA_KIND_HTTP)
    ctx->icy_interval = METADATA_ICY_INTERVAL * ctx->settings.nb_channels * dst_bytes_per_sample * ctx->settings.sample_rate;

  if (open_output(ctx, evbuf_io, src_ctx) < 0)
    goto error;

  if (open_filters(ctx, src_ctx) < 0)
    goto error;

  return ctx;

 error:
  transcode_encode_cleanup(&ctx);
  return NULL;
}

struct encode_ctx *
transcode_encode_setup(enum transcode_profile profile, struct media_quality *quality, struct decode_ctx *src_ctx, int width, int height)
{
  return transcode_encode_setup_with_io(profile, quality, NULL, src_ctx, width, height);
}

struct transcode_ctx *
transcode_setup(enum transcode_profile profile, struct media_quality *quality, enum data_kind data_kind, const char *path, uint32_t len_ms)
{
  struct transcode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct transcode_ctx)));

  ctx->decode_ctx = transcode_decode_setup(profile, quality, data_kind, path, NULL, len_ms);
  if (!ctx->decode_ctx)
    {
      free(ctx);
      return NULL;
    }

  ctx->encode_ctx = transcode_encode_setup(profile, quality, ctx->decode_ctx, 0, 0);
  if (!ctx->encode_ctx)
    {
      transcode_decode_cleanup(&ctx->decode_ctx);
      free(ctx);
      return NULL;
    }

  return ctx;
}

struct decode_ctx *
transcode_decode_setup_raw(enum transcode_profile profile, struct media_quality *quality)
{
  const AVCodecDescriptor *codec_desc;
  struct decode_ctx *ctx;
#if USE_CONST_AVCODEC
  const AVCodec *decoder;
#else
  // Not const before ffmpeg 5.0
  AVCodec *decoder;
#endif
  int ret;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct decode_ctx)));

  if (init_settings(&ctx->settings, profile, quality) < 0)
    {
      goto out_free_ctx;
    }

  codec_desc = avcodec_descriptor_get(ctx->settings.audio_codec);
  if (!codec_desc)
    {
      DPRINTF(E_LOG, L_XCODE, "Invalid codec ID (%d)\n", ctx->settings.audio_codec);
      goto out_free_ctx;
    }

  // In raw mode we won't actually need to read or decode, but we still setup
  // the decode_ctx because transcode_encode_setup() gets info about the input
  // through this structure (TODO dont' do that)
  decoder = avcodec_find_decoder(ctx->settings.audio_codec);
  if (!decoder)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not find decoder for: %s\n", codec_desc->name);
      goto out_free_ctx;
    }

  CHECK_NULL(L_XCODE, ctx->ifmt_ctx = avformat_alloc_context());
  CHECK_NULL(L_XCODE, ctx->audio_stream.codec = avcodec_alloc_context3(decoder));
  CHECK_NULL(L_XCODE, ctx->audio_stream.stream = avformat_new_stream(ctx->ifmt_ctx, NULL));

  stream_settings_set(&ctx->audio_stream, &ctx->settings, decoder->type);

  // Copy the data we just set to the structs we will be querying later, e.g. in open_filter
  ctx->audio_stream.stream->time_base = ctx->audio_stream.codec->time_base;
  ret = avcodec_parameters_from_context(ctx->audio_stream.stream->codecpar, ctx->audio_stream.codec);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot copy stream parameters (%s): %s\n", codec_desc->name, err2str(ret));
      goto out_free_codec;
    }

  return ctx;

 out_free_codec:
  avcodec_free_context(&ctx->audio_stream.codec);
  avformat_free_context(ctx->ifmt_ctx);
 out_free_ctx:
  free(ctx);
  return NULL;
}

enum transcode_profile
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype)
{
  const char *codectype;
  const char *prefer_format;
  cfg_t *lib;
  bool force_xcode;
  bool supports_alac;
  bool supports_mpeg;
  bool supports_wav;
  int count;
  int i;

  if (!file_codectype)
    {
      return XCODE_UNKNOWN;
    }

  lib = cfg_getsec(cfg, "library");

  count = cfg_size(lib, "no_decode");
  for (i = 0; i < count; i++)
    {
      codectype = cfg_getnstr(lib, "no_decode", i);
      if (strcmp(file_codectype, codectype) == 0)
	return XCODE_NONE; // Codectype is in no_decode
    }

  count = cfg_size(lib, "force_decode");
  for (i = 0, force_xcode = false; i < count && !force_xcode; i++)
    {
      codectype = cfg_getnstr(lib, "force_decode", i);
      if (strcmp(file_codectype, codectype) == 0)
	force_xcode = true; // Codectype is in force_decode
    }

  if (!client_codecs && user_agent)
    {
      if (strncmp(user_agent, "iTunes", strlen("iTunes")) == 0)
	client_codecs = itunes_codecs;
      else if (strncmp(user_agent, "Music/", strlen("Music/")) == 0) // Apple Music, include slash because the name is generic
	client_codecs = itunes_codecs;
      else if (strncmp(user_agent, "QuickTime", strlen("QuickTime")) == 0)
	client_codecs = itunes_codecs; // Use iTunes codecs
      else if (strncmp(user_agent, "Front%20Row", strlen("Front%20Row")) == 0)
	client_codecs = itunes_codecs; // Use iTunes codecs
      else if (strncmp(user_agent, "AppleCoreMedia", strlen("AppleCoreMedia")) == 0)
	client_codecs = itunes_codecs; // Use iTunes codecs
      else if (strncmp(user_agent, "Roku", strlen("Roku")) == 0)
	client_codecs = roku_codecs;
      else if (strncmp(user_agent, "Hifidelio", strlen("Hifidelio")) == 0)
	/* Allegedly can't transcode for Hifidelio because their
	 * HTTP implementation doesn't honour Connection: close.
	 * At least, that's why mt-daapd didn't do it.
	 */
	return XCODE_NONE;
    }

  if (!client_codecs)
    client_codecs = default_codecs;
  else
    DPRINTF(E_SPAM, L_XCODE, "Client advertises codecs: %s\n", client_codecs);

  if (!force_xcode && strstr(client_codecs, file_codectype))
    return XCODE_NONE;

  supports_alac = strstr(client_codecs, "alac") || strstr(client_codecs, "mp4a");
  supports_mpeg = strstr(client_codecs, "mpeg") && avcodec_find_encoder(AV_CODEC_ID_MP3);
  supports_wav = strstr(client_codecs, "wav");

  prefer_format = cfg_getstr(lib, "prefer_format");
  if (prefer_format)
    {
      if (strcmp(prefer_format, "wav") == 0 && supports_wav)
	return XCODE_WAV;
      if (strcmp(prefer_format, "mpeg") == 0 && supports_mpeg)
	return XCODE_MP3;
      if (strcmp(prefer_format, "alac") == 0 && supports_alac)
	return XCODE_MP4_ALAC;
    }

  // This order determines the default if user didn't configure a preference.
  // The lossless formats are given highest preference.
  if (supports_wav)
    return XCODE_WAV;
  if (supports_mpeg)
    return XCODE_MP3;
  if (supports_alac)
    return XCODE_MP4_ALAC;

  return XCODE_UNKNOWN;
}


/*                                  Cleanup                                  */

void
transcode_decode_cleanup(struct decode_ctx **ctx)
{
  if (!(*ctx))
    return;

  close_input(*ctx);

  av_packet_free(&(*ctx)->packet);
  av_frame_free(&(*ctx)->decoded_frame);
  free(*ctx);
  *ctx = NULL;
}

void
transcode_encode_cleanup(struct encode_ctx **ctx)
{
  if (!*ctx)
    return;

  close_filters(*ctx);
  close_output(*ctx);

  evbuffer_free((*ctx)->evbuf_io.evbuf);
  av_packet_free(&(*ctx)->encoded_pkt);
  av_frame_free(&(*ctx)->filt_frame);
  free(*ctx);
  *ctx = NULL;
}

void
transcode_cleanup(struct transcode_ctx **ctx)
{
  if (!*ctx)
    return;

  transcode_encode_cleanup(&(*ctx)->encode_ctx);
  transcode_decode_cleanup(&(*ctx)->decode_ctx);
  free(*ctx);
  *ctx = NULL;
}


/*                       Encoding, decoding and transcoding                  */

int
transcode_decode(transcode_frame **frame, struct decode_ctx *dec_ctx)
{
  struct transcode_ctx ctx;
  int ret;

  if (dec_ctx->got_frame)
    DPRINTF(E_LOG, L_XCODE, "Bug! Currently no support for multiple calls to transcode_decode()\n");

  ctx.decode_ctx = dec_ctx;
  ctx.encode_ctx = NULL;

  do
    {
      // This function stops after decoding because ctx->encode_ctx is NULL
      ret = read_decode_filter_encode_write(&ctx);
    }
  while ((ret == 0) && (!dec_ctx->got_frame));

  if (ret < 0)
    return -1;

  *frame = dec_ctx->decoded_frame;

  if (dec_ctx->eof)
    return 0;

  return 1;
}

// Filters and encodes
int
transcode_encode(struct evbuffer *evbuf, struct encode_ctx *ctx, transcode_frame *frame, int eof)
{
  AVFrame *f = frame;
  struct stream_ctx *s;
  size_t start_length;
  int ret;

  start_length = evbuffer_get_length(ctx->obuf);

  // Really crappy way of detecting if frame is audio, video or something else
#if USE_CH_LAYOUT
  if (f->ch_layout.nb_channels && f->sample_rate)
#else
  if (f->channel_layout && f->sample_rate)
#endif
    s = &ctx->audio_stream;
  else if (f->width && f->height)
    s = &ctx->video_stream;
  else
    {
      DPRINTF(E_LOG, L_XCODE, "Bug! Encoder could not detect frame type\n");
      return -1;
    }

  ret = filter_encode_write(ctx, s, f);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error occurred while encoding: %s\n", err2str(ret));
      return ret;
    }

  // Flush
  if (eof)
    {
      filter_encode_write(ctx, s, NULL);
      av_write_trailer(ctx->ofmt_ctx);
    }

  ret = evbuffer_get_length(ctx->obuf) - start_length;

  evbuffer_add_buffer(evbuf, ctx->obuf);

  return ret;
}

int
transcode(struct evbuffer *evbuf, int *icy_timer, struct transcode_ctx *ctx, int want_bytes)
{
  size_t start_length;
  int processed = 0;
  int ret;

  if (icy_timer)
    *icy_timer = 0;

  if (ctx->decode_ctx->eof)
    return 0;

  start_length = evbuffer_get_length(ctx->encode_ctx->obuf);

  do
    {
      ret = read_decode_filter_encode_write(ctx);
      processed = evbuffer_get_length(ctx->encode_ctx->obuf) - start_length;
    }
  while ((ret == 0) && (!want_bytes || (processed < want_bytes)));

  evbuffer_add_buffer(evbuf, ctx->encode_ctx->obuf);

  ctx->encode_ctx->bytes_processed += processed;
  if (icy_timer && ctx->encode_ctx->icy_interval)
    *icy_timer = (ctx->encode_ctx->bytes_processed % ctx->encode_ctx->icy_interval < processed);

  if ((ret < 0) && (ret != AVERROR_EOF))
    return ret;

  return processed;
}

transcode_frame *
transcode_frame_new(void *data, size_t size, int nsamples, struct media_quality *quality)
{
  AVFrame *f;
  int ret;

  f = av_frame_alloc();
  if (!f)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for frame\n");
      return NULL;
    }

  f->format = bitdepth2format(quality->bits_per_sample);
  if (f->format == AV_SAMPLE_FMT_NONE)
    {
      DPRINTF(E_LOG, L_XCODE, "transcode_frame_new() called with unsupported bps (%d)\n", quality->bits_per_sample);
      av_frame_free(&f);
      return NULL;
    }

  f->sample_rate    = quality->sample_rate;
  f->nb_samples     = nsamples;
#if USE_CH_LAYOUT
  av_channel_layout_default(&f->ch_layout, quality->channels);
#else
  f->channel_layout = av_get_default_channel_layout(quality->channels);
# ifdef HAVE_FFMPEG
  f->channels       = quality->channels;
# endif
#endif
  f->pts            = AV_NOPTS_VALUE;

  // We don't align because the frame won't be given directly to the encoder
  // anyway, it will first go through the filter (which might align it...?)
  ret = avcodec_fill_audio_frame(f, quality->channels, f->format, data, size, 1);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error filling frame with rawbuf, size %zu, samples %d (%d/%d/%d): %s\n",
	size, nsamples, quality->sample_rate, quality->bits_per_sample, quality->channels, err2str(ret));

      av_frame_free(&f);
      return NULL;
    }

  return f;
}

void
transcode_frame_free(transcode_frame *frame)
{
  AVFrame *f = frame;

  av_frame_free(&f);
}


/*                                  Seeking                                  */

int
transcode_seek(struct transcode_ctx *ctx, int ms)
{
  struct decode_ctx *dec_ctx = ctx->decode_ctx;
  struct stream_ctx *s;
  int64_t start_time;
  int64_t target_pts;
  int64_t got_pts;
  int got_ms;
  int ret;

  s = &dec_ctx->audio_stream;
  if (!s->stream)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not seek in non-audio input\n");
      return -1;
    }

  start_time = s->stream->start_time;

  target_pts = ms;
  target_pts = target_pts * AV_TIME_BASE / 1000;
  target_pts = av_rescale_q(target_pts, AV_TIME_BASE_Q, s->stream->time_base);

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    target_pts += start_time;

  ret = av_seek_frame(dec_ctx->ifmt_ctx, s->stream->index, target_pts, AVSEEK_FLAG_BACKWARD);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not seek into stream: %s\n", err2str(ret));
      return -1;
    }

  avcodec_flush_buffers(s->codec);

  // Fast forward until first packet with a timestamp is found
  s->codec->skip_frame = AVDISCARD_NONREF;
  while (1)
    {
      dec_ctx->timestamp = av_gettime();

      av_packet_unref(dec_ctx->packet);
      ret = av_read_frame(dec_ctx->ifmt_ctx, dec_ctx->packet);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not read more data while seeking: %s\n", err2str(ret));
	  s->codec->skip_frame = AVDISCARD_DEFAULT;
	  return -1;
	}

      if (stream_find(dec_ctx, dec_ctx->packet->stream_index) == AVMEDIA_TYPE_UNKNOWN)
	continue;

      // Need a pts to return the real position
      if (dec_ctx->packet->pts == AV_NOPTS_VALUE)
	continue;

      break;
    }
  s->codec->skip_frame = AVDISCARD_DEFAULT;

  // Tell read_packet() to resume with dec_ctx->packet
  dec_ctx->resume = 1;

  // Compute position in ms from pts
  got_pts = dec_ctx->packet->pts;

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    got_pts -= start_time;

  got_pts = av_rescale_q(got_pts, s->stream->time_base, AV_TIME_BASE_Q);
  got_ms = got_pts / (AV_TIME_BASE / 1000);

  // Since negative return would mean error, we disallow it here
  if (got_ms < 0)
    got_ms = 0;

  DPRINTF(E_DBG, L_XCODE, "Seek wanted %d ms, got %d ms\n", ms, got_ms);

  return got_ms;
}

/*                                  Querying                                 */

int
transcode_decode_query(struct decode_ctx *ctx, const char *query)
{
  if (strcmp(query, "width") == 0)
    {
      if (ctx->video_stream.stream)
	return ctx->video_stream.stream->codecpar->width;
    }
  else if (strcmp(query, "height") == 0)
    {
      if (ctx->video_stream.stream)
	return ctx->video_stream.stream->codecpar->height;
    }
  else if (strcmp(query, "is_png") == 0)
    {
      if (ctx->video_stream.stream)
	return (ctx->video_stream.stream->codecpar->codec_id == AV_CODEC_ID_PNG);
    }
  else if (strcmp(query, "is_jpeg") == 0)
    {
      if (ctx->video_stream.stream)
	return (ctx->video_stream.stream->codecpar->codec_id == AV_CODEC_ID_MJPEG);
    }

  return -1;
}

int
transcode_encode_query(struct encode_ctx *ctx, const char *query)
{
  if (strcmp(query, "sample_rate") == 0)
    {
      if (ctx->audio_stream.stream)
	return ctx->audio_stream.stream->codecpar->sample_rate;
    }
  else if (strcmp(query, "bits_per_sample") == 0)
    {
      if (ctx->audio_stream.stream)
	return av_get_bits_per_sample(ctx->audio_stream.stream->codecpar->codec_id);
    }
  else if (strcmp(query, "channels") == 0)
    {
      if (ctx->audio_stream.stream)
#if USE_CH_LAYOUT
	return ctx->audio_stream.stream->codecpar->ch_layout.nb_channels;
#else
	return ctx->audio_stream.stream->codecpar->channels;
#endif
    }
  else if (strcmp(query, "samples_per_frame") == 0)
    {
      if (ctx->audio_stream.stream)
	return ctx->audio_stream.stream->codecpar->frame_size;
    }
  else if (strcmp(query, "estimated_size") == 0)
    {
      if (ctx->audio_stream.stream)
	return ctx->bytes_total;
    }

  return -1;
}


/*                                  Metadata                                 */

struct http_icy_metadata *
transcode_metadata(struct transcode_ctx *ctx, int *changed)
{
  struct http_icy_metadata *m;

  if (!ctx->decode_ctx->ifmt_ctx)
    return NULL;

  m = http_icy_metadata_get(ctx->decode_ctx->ifmt_ctx, 1);
  if (!m)
    return NULL;

  *changed = (m->hash != ctx->encode_ctx->icy_hash);

  ctx->encode_ctx->icy_hash = m->hash;

  return m;
}

void
transcode_metadata_strings_set(struct transcode_metadata_string *s, enum transcode_profile profile, struct media_quality *q, uint32_t len_ms)
{
  off_t bytes;

  memset(s, 0, sizeof(struct transcode_metadata_string));

  switch (profile)
    {
      case XCODE_WAV:
	s->type = "wav";
	s->codectype = "wav";
	s->description = "WAV audio file";

	snprintf(s->bitrate, sizeof(s->bitrate), "%d", 8 * STOB(q->sample_rate, q->bits_per_sample, q->channels) / 1000); // 44100/16/2 -> 1411

	bytes = size_estimate(profile, q->bit_rate, q->sample_rate, q->bits_per_sample / 8, q->channels, len_ms);
	snprintf(s->file_size, sizeof(s->file_size), "%d", (int)bytes);
	break;

      case XCODE_MP3:
	s->type = "mp3";
	s->codectype = "mpeg";
	s->description = "MPEG audio file";

	snprintf(s->bitrate, sizeof(s->bitrate), "%d", q->bit_rate / 1000);

	bytes = size_estimate(profile, q->bit_rate, q->sample_rate, q->bits_per_sample / 8, q->channels, len_ms);
	snprintf(s->file_size, sizeof(s->file_size), "%d", (int)bytes);
	break;

      case XCODE_MP4_ALAC:
	s->type = "m4a";
	s->codectype = "alac";
	s->description = "Apple Lossless audio file";

	snprintf(s->bitrate, sizeof(s->bitrate), "%d", 8 * STOB(q->sample_rate, q->bits_per_sample, q->channels) / 1000); // 44100/16/2 -> 1411

	bytes = size_estimate(profile, q->bit_rate, q->sample_rate, q->bits_per_sample / 8, q->channels, len_ms);
	snprintf(s->file_size, sizeof(s->file_size), "%d", (int)bytes);
	break;

      default:
	DPRINTF(E_WARN, L_XCODE, "transcode_metadata_strings_set() called with unknown profile %d\n", profile);
    }
}
