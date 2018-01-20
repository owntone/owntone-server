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
#include "avio_evbuffer.h"
#include "transcode.h"

// Interval between ICY metadata checks for streams, in seconds
#define METADATA_ICY_INTERVAL 5
// Maximum number of streams in a file that we will accept
#define MAX_STREAMS 64
// Maximum number of times we retry when we encounter bad packets
#define MAX_BAD_PACKETS 5
// How long to wait (in microsec) before interrupting av_read_frame
#define READ_TIMEOUT 15000000

static const char *default_codecs = "mpeg,wav";
static const char *roku_codecs = "mpeg,mp4a,wma,wav";
static const char *itunes_codecs = "mpeg,mp4a,mp4v,alac,wav";

// Used for passing errors to DPRINTF (can't count on av_err2str being present)
static char errbuf[64];

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
  const char *audio_codec_name;
  int sample_rate;
  uint64_t channel_layout;
  int channels;
  enum AVSampleFormat sample_format;
  int byte_depth;
  bool wavheader;
  bool icy;

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

  // Duration (used to make wav header)
  uint32_t duration;

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

  // Contains the most recent packet from av_buffersink_get_frame()
  AVFrame *filt_frame;

  // Contains the most recent packet from avcodec_receive_packet()
  AVPacket *encoded_pkt;

  // How many output bytes we have processed in total
  off_t total_bytes;

  // Used to check for ICY metadata changes at certain intervals
  uint32_t icy_interval;
  uint32_t icy_hash;

  // WAV header
  uint8_t header[44];
};

struct transcode_ctx
{
  struct decode_ctx *decode_ctx;
  struct encode_ctx *encode_ctx;
};


/* -------------------------- PROFILE CONFIGURATION ------------------------ */

static int
init_settings(struct settings_ctx *settings, enum transcode_profile profile)
{
  const AVCodecDescriptor *codec_desc;

  memset(settings, 0, sizeof(struct settings_ctx));

  switch (profile)
    {
      case XCODE_PCM16_HEADER:
	settings->wavheader = 1;
      case XCODE_PCM16_NOHEADER:
	settings->encode_audio = 1;
	settings->format = "s16le";
	settings->audio_codec = AV_CODEC_ID_PCM_S16LE;
	settings->sample_rate = 44100;
	settings->channel_layout = AV_CH_LAYOUT_STEREO;
	settings->channels = 2;
	settings->sample_format = AV_SAMPLE_FMT_S16;
	settings->byte_depth = 2; // Bytes per sample = 16/8
	settings->icy = 1;
	break;

      case XCODE_MP3:
	settings->encode_audio = 1;
	settings->format = "mp3";
	settings->audio_codec = AV_CODEC_ID_MP3;
	settings->sample_rate = 44100;
	settings->channel_layout = AV_CH_LAYOUT_STEREO;
	settings->channels = 2;
	settings->sample_format = AV_SAMPLE_FMT_S16P;
	settings->byte_depth = 2; // Bytes per sample = 16/8
	break;

      case XCODE_JPEG:
	settings->encode_video = 1;
	settings->silent = 1;
	settings->format = "image2";
	settings->in_format = "mjpeg";
	settings->video_codec = AV_CODEC_ID_MJPEG;
	break;

      case XCODE_PNG:
	settings->encode_video = 1;
	settings->silent = 1;
	settings->format = "image2";
	settings->video_codec = AV_CODEC_ID_PNG;
	break;

      default:
	DPRINTF(E_LOG, L_XCODE, "Bug! Unknown transcoding profile\n");
	return -1;
    }

  if (settings->audio_codec)
    {
      codec_desc = avcodec_descriptor_get(settings->audio_codec);
      settings->audio_codec_name = codec_desc->name;
    }

  if (settings->video_codec)
    {
      codec_desc = avcodec_descriptor_get(settings->video_codec);
      settings->video_codec_name = codec_desc->name;
    }

  return 0;
}

static void
stream_settings_set(struct stream_ctx *s, struct settings_ctx *settings, enum AVMediaType type)
{
  if (type == AVMEDIA_TYPE_AUDIO)
    {
      s->codec->sample_rate    = settings->sample_rate;
      s->codec->channel_layout = settings->channel_layout;
      s->codec->channels       = settings->channels;
      s->codec->sample_fmt     = settings->sample_format;
      s->codec->time_base      = (AVRational){1, settings->sample_rate};
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

static void
make_wav_header(struct encode_ctx *ctx, struct decode_ctx *src_ctx, off_t *est_size)
{
  uint32_t wav_len;
  int duration;

  if (src_ctx->duration)
    duration = src_ctx->duration;
  else
    duration = 3 * 60 * 1000; /* 3 minutes, in ms */

  wav_len = ctx->settings.channels * ctx->settings.byte_depth * ctx->settings.sample_rate * (duration / 1000);

  *est_size = wav_len + sizeof(ctx->header);

  memcpy(ctx->header, "RIFF", 4);
  add_le32(ctx->header + 4, 36 + wav_len);
  memcpy(ctx->header + 8, "WAVEfmt ", 8);
  add_le32(ctx->header + 16, 16);
  add_le16(ctx->header + 20, 1);
  add_le16(ctx->header + 22, ctx->settings.channels);     /* channels */
  add_le32(ctx->header + 24, ctx->settings.sample_rate);  /* samplerate */
  add_le32(ctx->header + 28, ctx->settings.sample_rate * ctx->settings.channels * ctx->settings.byte_depth); /* byte rate */
  add_le16(ctx->header + 32, ctx->settings.channels * ctx->settings.byte_depth);                             /* block align */
  add_le16(ctx->header + 34, ctx->settings.byte_depth * 8);                                                  /* bits per sample */
  memcpy(ctx->header + 36, "data", 4);
  add_le32(ctx->header + 40, wav_len);
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
 * @in codec_name Name of codec (only used for logging)
 * @return        Negative on failure, otherwise zero
 */
static int
stream_add(struct encode_ctx *ctx, struct stream_ctx *s, enum AVCodecID codec_id, const char *codec_name)
{
  AVCodec *encoder;
  int ret;

  encoder = avcodec_find_encoder(codec_id);
  if (!encoder)
    {
      DPRINTF(E_LOG, L_XCODE, "Necessary encoder (%s) not found\n", codec_name);
      return -1;
    }

  CHECK_NULL(L_XCODE, s->stream = avformat_new_stream(ctx->ofmt_ctx, NULL));
  CHECK_NULL(L_XCODE, s->codec = avcodec_alloc_context3(encoder));

  stream_settings_set(s, &ctx->settings, encoder->type);

  if (!s->codec->pix_fmt)
    {
      s->codec->pix_fmt = avcodec_default_get_format(s->codec, encoder->pix_fmts);
      DPRINTF(E_DBG, L_XCODE, "Pixel format set to %s (encoder is %s)\n", av_get_pix_fmt_name(s->codec->pix_fmt), codec_name);
    }

  if (ctx->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    s->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

  ret = avcodec_open2(s->codec, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot open encoder (%s): %s\n", codec_name, err2str(ret));
      avcodec_free_context(&s->codec);
      return -1;
    }

  // Copy the codec parameters we just set to the stream, so the muxer knows them
  ret = avcodec_parameters_from_context(s->stream->codecpar, s->codec);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot copy stream parameters (%s): %s\n", codec_name, err2str(ret));
      avcodec_free_context(&s->codec);
      return -1;
    }

  return 0;
}

/*
 * Called by libavformat while demuxing. Used to interrupt/unblock av_read_frame
 * in case a source (especially a network stream) becomes unavailable.
 * 
 * @in arg        Will point to the decode context
 * @return        Non-zero if av_read_frame should be interrupted
 */
static int decode_interrupt_cb(void *arg)
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
	break;
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

      // Flush muxer
      if (ctx->encode_ctx)
	{
	  av_interleaved_write_frame(ctx->encode_ctx->ofmt_ctx, NULL);
	  av_write_trailer(ctx->encode_ctx->ofmt_ctx);
	}

      return ret;
    }

  if (type == AVMEDIA_TYPE_AUDIO)
    ret = decode_filter_encode_write(ctx, &dec_ctx->audio_stream, dec_ctx->packet, type);
  else if (type == AVMEDIA_TYPE_VIDEO)
    ret = decode_filter_encode_write(ctx, &dec_ctx->video_stream, dec_ctx->packet, type);

  return ret;
}


/* --------------------------- INPUT/OUTPUT INIT --------------------------- */

static AVCodecContext *
open_decoder(unsigned int *stream_index, struct decode_ctx *ctx, enum AVMediaType type)
{
  AVCodecContext *dec_ctx;
  AVCodec *decoder;
  int ret;

  *stream_index = av_find_best_stream(ctx->ifmt_ctx, type, -1, -1, &decoder, 0);
  if ((*stream_index < 0) || (!decoder))
    {
      if (!ctx->settings.silent)
	DPRINTF(E_LOG, L_XCODE, "No stream data or decoder for '%s'\n", ctx->ifmt_ctx->filename);
      return NULL;
    }

  CHECK_NULL(L_XCODE, dec_ctx = avcodec_alloc_context3(decoder));

  // In open_filter() we need to tell the sample rate and format that the decoder
  // is giving us - however sample rate of dec_ctx will be 0 if we don't prime it
  // with the streams codecpar data.
  ret = avcodec_parameters_to_context(dec_ctx, ctx->ifmt_ctx->streams[*stream_index]->codecpar);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to copy codecpar for stream #%d: %s\n", *stream_index, err2str(ret));
      avcodec_free_context(&dec_ctx);
      return NULL;
    }

  if (type == AVMEDIA_TYPE_AUDIO)
    {
      dec_ctx->request_sample_fmt = ctx->settings.sample_format;
      dec_ctx->request_channel_layout = ctx->settings.channel_layout;
    }

  ret = avcodec_open2(dec_ctx, NULL, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%d: %s\n", *stream_index, err2str(ret));
      avcodec_free_context(&dec_ctx);
      return NULL;
    }

  return dec_ctx;
}

static int
open_input(struct decode_ctx *ctx, const char *path, struct evbuffer *evbuf)
{
  AVDictionary *options = NULL;
  AVCodecContext *dec_ctx;
  AVInputFormat *ifmt;
  unsigned int stream_index;
  int ret;

  CHECK_NULL(L_XCODE, ctx->ifmt_ctx = avformat_alloc_context());

  if (ctx->data_kind == DATA_KIND_HTTP)
    {
# ifndef HAVE_FFMPEG
      // Without this, libav is slow to probe some internet streams, which leads to RAOP timeouts
      ctx->ifmt_ctx->probesize = 64000;
# endif
      av_dict_set(&options, "icy", "1", 0);
    }

  // TODO Newest versions of ffmpeg have timeout and reconnect options we should use
  ctx->ifmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
  ctx->ifmt_ctx->interrupt_callback.opaque = ctx;
  ctx->timestamp = av_gettime();

  if (evbuf)
    {
      ifmt = av_find_input_format(ctx->settings.in_format);
      if (!ifmt)
	{
	  DPRINTF(E_LOG, L_XCODE, "Could not find input format: '%s'\n", ctx->settings.in_format);
	  return -1;
	}

      CHECK_NULL(L_XCODE, ctx->avio = avio_input_evbuffer_open(evbuf));

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
      return -1;
    }

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
      dec_ctx = open_decoder(&stream_index, ctx, AVMEDIA_TYPE_AUDIO);
      if (!dec_ctx)
	goto out_fail;

      ctx->audio_stream.codec = dec_ctx;
      ctx->audio_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  if (ctx->settings.encode_video)
    {
      dec_ctx = open_decoder(&stream_index, ctx, AVMEDIA_TYPE_VIDEO);
      if (!dec_ctx)
	goto out_fail;

      ctx->video_stream.codec = dec_ctx;
      ctx->video_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  return 0;

 out_fail:
  avio_evbuffer_close(ctx->avio);
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avformat_close_input(&ctx->ifmt_ctx);

  return -1;
}

static void
close_input(struct decode_ctx *ctx)
{
  avio_evbuffer_close(ctx->avio);
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avformat_close_input(&ctx->ifmt_ctx);
}

static int
open_output(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  AVOutputFormat *oformat;
  int ret;

  oformat = av_guess_format(ctx->settings.format, NULL, NULL);
  if (!oformat)
    {
      DPRINTF(E_LOG, L_XCODE, "ffmpeg/libav could not find the '%s' output format\n", ctx->settings.format);
      return -1;
    }

  // Clear AVFMT_NOFILE bit, it is not allowed as we will set our own AVIOContext
  oformat->flags = ~AVFMT_NOFILE;

  CHECK_NULL(L_XCODE, ctx->ofmt_ctx = avformat_alloc_context());

  ctx->ofmt_ctx->oformat = oformat;

  ctx->obuf = evbuffer_new();
  if (!ctx->obuf)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output evbuffer\n");
      goto out_free_output;
    }

  ctx->ofmt_ctx->pb = avio_output_evbuffer_open(ctx->obuf);
  if (!ctx->ofmt_ctx->pb)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output avio pb\n");
      goto out_free_evbuf;
    }

  if (ctx->settings.encode_audio)
    {
      ret = stream_add(ctx, &ctx->audio_stream, ctx->settings.audio_codec, ctx->settings.audio_codec_name);
      if (ret < 0)
	goto out_free_streams;
    }

  if (ctx->settings.encode_video)
    {
      ret = stream_add(ctx, &ctx->video_stream, ctx->settings.video_codec, ctx->settings.video_codec_name);
      if (ret < 0)
	goto out_free_streams;
    }

  // Notice, this will not write WAV header (so we do that manually)
  ret = avformat_write_header(ctx->ofmt_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error writing header to output buffer: %s\n", err2str(ret));
      goto out_free_streams;
    }

  if (ctx->settings.wavheader)
    {
      evbuffer_add(ctx->obuf, ctx->header, sizeof(ctx->header));
    }

  return 0;

 out_free_streams:
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);

  avio_evbuffer_close(ctx->ofmt_ctx->pb);
 out_free_evbuf:
  evbuffer_free(ctx->obuf);
 out_free_output:
  avformat_free_context(ctx->ofmt_ctx);

  return -1;
}

static void
close_output(struct encode_ctx *ctx)
{
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);

  avio_evbuffer_close(ctx->ofmt_ctx->pb);
  evbuffer_free(ctx->obuf);

  avformat_free_context(ctx->ofmt_ctx);
}

static int
open_filter(struct stream_ctx *out_stream, struct stream_ctx *in_stream)
{
  AVFilter *buffersrc;
  AVFilter *format;
  AVFilter *scale;
  AVFilter *buffersink;
  AVFilterContext *buffersrc_ctx;
  AVFilterContext *format_ctx;
  AVFilterContext *scale_ctx;
  AVFilterContext *buffersink_ctx;
  AVFilterGraph *filter_graph;
  char args[512];
  int ret;

  CHECK_NULL(L_XCODE, filter_graph = avfilter_graph_alloc());

  if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      buffersrc = avfilter_get_by_name("abuffer");
      format = avfilter_get_by_name("aformat");
      buffersink = avfilter_get_by_name("abuffersink");
      if (!buffersrc || !format || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source, format or sink element not found\n");
	  goto out_fail;
	}

      if (!in_stream->codec->channel_layout)
	in_stream->codec->channel_layout = av_get_default_channel_layout(in_stream->codec->channels);

      snprintf(args, sizeof(args),
               "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
               in_stream->stream->time_base.num, in_stream->stream->time_base.den,
               in_stream->codec->sample_rate, av_get_sample_fmt_name(in_stream->codec->sample_fmt),
               in_stream->codec->channel_layout);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer source (%s): %s\n", args, err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
               av_get_sample_fmt_name(out_stream->codec->sample_fmt), out_stream->codec->sample_rate,
               out_stream->codec->channel_layout);

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio format filter (%s): %s\n", args, err2str(ret));
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer sink: %s\n", err2str(ret));
	  goto out_fail;
	}

      if ( (ret = avfilter_link(buffersrc_ctx, 0, format_ctx, 0)) < 0 ||
           (ret = avfilter_link(format_ctx, 0, buffersink_ctx, 0)) < 0 )
	{
	  DPRINTF(E_LOG, L_XCODE, "Error connecting audio filters: %s\n", err2str(ret));
	  goto out_fail;
	}
    }
  else if (in_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      buffersrc = avfilter_get_by_name("buffer");
      format = avfilter_get_by_name("format");
      scale = avfilter_get_by_name("scale");
      buffersink = avfilter_get_by_name("buffersink");
      if (!buffersrc || !format || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source, format, scale or sink element not found\n");
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "width=%d:height=%d:pix_fmt=%s:time_base=%d/%d:sar=%d/%d",
               in_stream->codec->width, in_stream->codec->height, av_get_pix_fmt_name(in_stream->codec->pix_fmt),
               in_stream->stream->time_base.num, in_stream->stream->time_base.den,
               in_stream->codec->sample_aspect_ratio.num, in_stream->codec->sample_aspect_ratio.den);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer source (%s): %s\n", args, err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "pix_fmts=%s", av_get_pix_fmt_name(out_stream->codec->pix_fmt));

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create format filter (%s): %s\n", args, err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "w=%d:h=%d", out_stream->codec->width, out_stream->codec->height);

      ret = avfilter_graph_create_filter(&scale_ctx, scale, "scale", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create scale filter (%s): %s\n", args, err2str(ret));
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer sink: %s\n", err2str(ret));
	  goto out_fail;
	}

      if ( (ret = avfilter_link(buffersrc_ctx, 0, format_ctx, 0)) < 0 ||
           (ret = avfilter_link(format_ctx, 0, scale_ctx, 0)) < 0 ||
           (ret = avfilter_link(scale_ctx, 0, buffersink_ctx, 0)) < 0 )
	{
	  DPRINTF(E_LOG, L_XCODE, "Error connecting video filters: %s\n", err2str(ret));
	  goto out_fail;
	}
    }
  else
    {
      DPRINTF(E_LOG, L_XCODE, "Bug! Unknown type passed to filter graph init\n");
      goto out_fail;
    }

  ret = avfilter_graph_config(filter_graph, NULL);
  if (ret < 0)
    goto out_fail;

  /* Fill filtering context */
  out_stream->buffersrc_ctx = buffersrc_ctx;
  out_stream->buffersink_ctx = buffersink_ctx;
  out_stream->filter_graph = filter_graph;

  return 0;

 out_fail:
  avfilter_graph_free(&filter_graph);

  return -1;
}

static int
open_filters(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  int ret;

  if (ctx->settings.encode_audio)
    {
      ret = open_filter(&ctx->audio_stream, &src_ctx->audio_stream);
      if (ret < 0)
	goto out_fail;
    }

  if (ctx->settings.encode_video)
    {
      ret = open_filter(&ctx->video_stream, &src_ctx->video_stream);
      if (ret < 0)
	goto out_fail;
    }

  return 0;

 out_fail:
  avfilter_graph_free(&ctx->audio_stream.filter_graph);
  avfilter_graph_free(&ctx->video_stream.filter_graph);

  return -1;
}

static void
close_filters(struct encode_ctx *ctx)
{
  avfilter_graph_free(&ctx->audio_stream.filter_graph);
  avfilter_graph_free(&ctx->video_stream.filter_graph);
}


/* ----------------------------- TRANSCODE API ----------------------------- */

/*                                  Setup                                    */

struct decode_ctx *
transcode_decode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, struct evbuffer *evbuf, uint32_t song_length)
{
  struct decode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct decode_ctx)));
  CHECK_NULL(L_XCODE, ctx->decoded_frame = av_frame_alloc());
  CHECK_NULL(L_XCODE, ctx->packet = av_packet_alloc());

  ctx->duration = song_length;
  ctx->data_kind = data_kind;

  if ((init_settings(&ctx->settings, profile) < 0) || (open_input(ctx, path, evbuf) < 0))
    goto fail_free;

  return ctx;

 fail_free:
  av_packet_free(&ctx->packet);
  av_frame_free(&ctx->decoded_frame);
  free(ctx);
  return NULL;
}

struct encode_ctx *
transcode_encode_setup(enum transcode_profile profile, struct decode_ctx *src_ctx, off_t *est_size, int width, int height)
{
  struct encode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct encode_ctx)));
  CHECK_NULL(L_XCODE, ctx->filt_frame = av_frame_alloc());
  CHECK_NULL(L_XCODE, ctx->encoded_pkt = av_packet_alloc());

  if (init_settings(&ctx->settings, profile) < 0)
    goto fail_free;

  ctx->settings.width = width;
  ctx->settings.height = height;

  if (ctx->settings.wavheader)
    make_wav_header(ctx, src_ctx, est_size);

  if (open_output(ctx, src_ctx) < 0)
    goto fail_free;

  if (open_filters(ctx, src_ctx) < 0)
    goto fail_close;

  if (ctx->settings.icy && src_ctx->data_kind == DATA_KIND_HTTP)
    ctx->icy_interval = METADATA_ICY_INTERVAL * ctx->settings.channels * ctx->settings.byte_depth * ctx->settings.sample_rate;

  return ctx;

 fail_close:
  close_output(ctx);
 fail_free:
  av_packet_free(&ctx->encoded_pkt);
  av_frame_free(&ctx->filt_frame);
  free(ctx);
  return NULL;
}

struct transcode_ctx *
transcode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length, off_t *est_size)
{
  struct transcode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct transcode_ctx)));

  ctx->decode_ctx = transcode_decode_setup(profile, data_kind, path, NULL, song_length);
  if (!ctx->decode_ctx)
    {
      free(ctx);
      return NULL;
    }

  ctx->encode_ctx = transcode_encode_setup(profile, ctx->decode_ctx, est_size, 0, 0);
  if (!ctx->encode_ctx)
    {
      transcode_decode_cleanup(&ctx->decode_ctx);
      free(ctx);
      return NULL;
    }

  return ctx;
}

struct decode_ctx *
transcode_decode_setup_raw(void)
{
  struct decode_ctx *ctx;
  AVCodec *decoder;
  int ret;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct decode_ctx)));

  if (init_settings(&ctx->settings, XCODE_PCM16_NOHEADER) < 0)
    {
      goto out_free_ctx;
    }

  // In raw mode we won't actually need to read or decode, but we still setup
  // the decode_ctx because transcode_encode_setup() gets info about the input
  // through this structure (TODO dont' do that)
  decoder = avcodec_find_decoder(ctx->settings.audio_codec);
  if (!decoder)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not find decoder for: %s\n", ctx->settings.audio_codec_name);
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
      DPRINTF(E_LOG, L_XCODE, "Cannot copy stream parameters (%s): %s\n", ctx->settings.audio_codec_name, err2str(ret));
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

int
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype)
{
  char *codectype;
  cfg_t *lib;
  int size;
  int i;

  if (!file_codectype)
    {
      DPRINTF(E_LOG, L_XCODE, "Can't determine decode status, codec type is unknown\n");
      return -1;
    }

  lib = cfg_getsec(cfg, "library");

  size = cfg_size(lib, "no_decode");
  if (size > 0)
    {
      for (i = 0; i < size; i++)
	{
	  codectype = cfg_getnstr(lib, "no_decode", i);

	  if (strcmp(file_codectype, codectype) == 0)
	    return 0; // Codectype is in no_decode
	}
    }

  size = cfg_size(lib, "force_decode");
  if (size > 0)
    {
      for (i = 0; i < size; i++)
	{
	  codectype = cfg_getnstr(lib, "force_decode", i);

	  if (strcmp(file_codectype, codectype) == 0)
	    return 1; // Codectype is in force_decode
	}
    }

  if (!client_codecs)
    {
      if (user_agent)
	{
	  if (strncmp(user_agent, "iTunes", strlen("iTunes")) == 0)
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
	    return 0;
	}
    }
  else
    DPRINTF(E_SPAM, L_XCODE, "Client advertises codecs: %s\n", client_codecs);

  if (!client_codecs)
    {
      DPRINTF(E_SPAM, L_XCODE, "Could not identify client, using default codectype set\n");
      client_codecs = default_codecs;
    }

  if (strstr(client_codecs, file_codectype))
    {
      DPRINTF(E_SPAM, L_XCODE, "Codectype supported by client, no decoding needed\n");
      return 0;
    }

  DPRINTF(E_SPAM, L_XCODE, "Will decode\n");
  return 1;
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

  av_packet_free(&(*ctx)->encoded_pkt);
  av_frame_free(&(*ctx)->filt_frame);
  free(*ctx);
  *ctx = NULL;
}

void
transcode_cleanup(struct transcode_ctx **ctx)
{
  transcode_encode_cleanup(&(*ctx)->encode_ctx);
  transcode_decode_cleanup(&(*ctx)->decode_ctx);
  free(*ctx);
  *ctx = NULL;
}


/*                       Encoding, decoding and transcoding                  */

int
transcode_decode(void **frame, struct decode_ctx *dec_ctx)
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
transcode_encode(struct evbuffer *evbuf, struct encode_ctx *ctx, void *frame, int eof)
{
  AVFrame *f = frame;
  struct stream_ctx *s;
  size_t start_length;
  int ret;

  start_length = evbuffer_get_length(ctx->obuf);

  // Really crappy way of detecting if frame is audio, video or something else
  if (f->channel_layout && f->sample_rate)
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

  ctx->encode_ctx->total_bytes += processed;
  if (icy_timer && ctx->encode_ctx->icy_interval)
    *icy_timer = (ctx->encode_ctx->total_bytes % ctx->encode_ctx->icy_interval < processed);

  if ((ret < 0) && (ret != AVERROR_EOF))
    return ret;

  return processed;
}

void *
transcode_frame_new(enum transcode_profile profile, uint8_t *data, size_t size)
{
  AVFrame *f;
  int ret;

  f = av_frame_alloc();
  if (!f)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for frame\n");
      return NULL;
    }

  f->nb_samples     = size / 4;
  f->format         = AV_SAMPLE_FMT_S16;
  f->channel_layout = AV_CH_LAYOUT_STEREO;
#ifdef HAVE_FFMPEG
  f->channels       = 2;
#endif
  f->pts            = AV_NOPTS_VALUE;
  f->sample_rate    = 44100;

  ret = avcodec_fill_audio_frame(f, 2, f->format, data, size, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error filling frame with rawbuf: %s\n", err2str(ret));
      av_frame_free(&f);
      return NULL;
    }

  return f;
}

void
transcode_frame_free(void *frame)
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

