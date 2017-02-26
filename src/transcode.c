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
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>

#include "ffmpeg-compat.h"

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

  // Output format (for the muxer)
  const char *format;

  // Audio settings
  enum AVCodecID audio_codec;
  const char *audio_codec_name;
  int sample_rate;
  uint64_t channel_layout;
  int channels;
  enum AVSampleFormat sample_format;
  int byte_depth;
  bool wavheader;

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
};

struct decode_ctx
{
  // Settings derived from the profile
  struct settings_ctx settings;

  // Input format context
  AVFormatContext *ifmt_ctx;

  // Stream and decoder data
  struct stream_ctx audio_stream;
  struct stream_ctx video_stream;

  // Duration (used to make wav header)
  uint32_t duration;

  // Data kind (used to determine if ICY metadata is relevant to look for)
  enum data_kind data_kind;

  // Contains the most recent packet from av_read_frame
  // Used for resuming after seek and for freeing correctly
  // in transcode_decode()
  AVPacket packet;
  int resume;
  int resume_offset;

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

  // Used for seeking
  int64_t prev_pts[MAX_STREAMS];
  int64_t offset_pts[MAX_STREAMS];

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

struct decoded_frame
{
  AVFrame *frame;
  enum AVMediaType type;
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
	settings->format = "image2";
	settings->video_codec = AV_CODEC_ID_MJPEG;
	break;

      case XCODE_PNG:
	settings->encode_video = 1;
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
  else if (type == AVMEDIA_TYPE_AUDIO)
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

  if (ctx->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
    s->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

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

/* Will read the next packet from the source, unless we are in resume mode, in
 * which case the most recent packet will be returned, but with an adjusted data
 * pointer. Use ctx->resume and ctx->resume_offset to make the function resume
 * from the most recent packet.
 *
 * @out packet    Pointer to an already allocated AVPacket. The content of the
 *                packet will be updated, and packet->data is pointed to the data
 *                returned by av_read_frame(). The packet struct is owned by the
 *                caller, but *not* packet->data, so don't free the packet with
 *                av_free_packet()/av_packet_unref()
 * @out type      Media type of packet
 * @in  ctx       Decode context
 * @return        0 if OK, < 0 on error or end of file
 */
static int
read_packet(AVPacket *packet, enum AVMediaType *type, struct decode_ctx *ctx)
{
  int ret;

  do
    {
      if (ctx->resume)
	{
	  // Copies packet struct, but not actual packet payload, and adjusts
	  // data pointer to somewhere inside the payload if resume_offset is set
	  *packet = ctx->packet;
	  packet->data += ctx->resume_offset;
	  packet->size -= ctx->resume_offset;
	  ctx->resume = 0;
        }
      else
	{
	  // We are going to read a new packet from source, so now it is safe to
	  // discard the previous packet and reset resume_offset
	  av_packet_unref(&ctx->packet);

	  ctx->resume_offset = 0;
	  ctx->timestamp = av_gettime();

	  ret = av_read_frame(ctx->ifmt_ctx, &ctx->packet);
	  if (ret < 0)
	    {
	      DPRINTF(E_WARN, L_XCODE, "Could not read frame: %s\n", err2str(ret));
              return ret;
	    }

	  *packet = ctx->packet;
	}

      *type = stream_find(ctx, packet->stream_index);
    }
  while (*type == AVMEDIA_TYPE_UNKNOWN);

  return 0;
}

static int
encode_write_frame(struct encode_ctx *ctx, struct stream_ctx *s, AVFrame *filt_frame, int *got_frame)
{
  AVPacket enc_pkt;
  unsigned int stream_index;
  int ret;
  int got_frame_local;

  if (!got_frame)
    got_frame = &got_frame_local;

  stream_index = s->stream->index;

  // Encode filtered frame
  enc_pkt.data = NULL;
  enc_pkt.size = 0;
  av_init_packet(&enc_pkt);

  if (s->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    ret = avcodec_encode_audio2(s->codec, &enc_pkt, filt_frame, got_frame);
  else if (s->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    ret = avcodec_encode_video2(s->codec, &enc_pkt, filt_frame, got_frame);
  else
    return -1;

  if (ret < 0)
    return -1;
  if (!(*got_frame))
    return 0;

  // Prepare packet for muxing
  enc_pkt.stream_index = stream_index;

  // This "wonderful" peace of code makes sure that the timestamp never decreases,
  // even if the user seeked backwards. The muxer will not accept decreasing
  // timestamps
  enc_pkt.pts += ctx->offset_pts[stream_index];
  if (enc_pkt.pts < ctx->prev_pts[stream_index])
    {
      ctx->offset_pts[stream_index] += ctx->prev_pts[stream_index] - enc_pkt.pts;
      enc_pkt.pts = ctx->prev_pts[stream_index];
    }
  ctx->prev_pts[stream_index] = enc_pkt.pts;
  enc_pkt.dts = enc_pkt.pts; //FIXME

  av_packet_rescale_ts(&enc_pkt, s->codec->time_base, s->stream->time_base);

  // Mux encoded frame
  ret = av_interleaved_write_frame(ctx->ofmt_ctx, &enc_pkt);
  return ret;
}

#if HAVE_DECL_AV_BUFFERSRC_ADD_FRAME_FLAGS && HAVE_DECL_AV_BUFFERSINK_GET_FRAME
static int
filter_encode_write_frame(struct encode_ctx *ctx, struct stream_ctx *s, AVFrame *frame)
{
  AVFrame *filt_frame;
  int ret;

  // Push the decoded frame into the filtergraph
  if (frame)
    {
      ret = av_buffersrc_add_frame_flags(s->buffersrc_ctx, frame, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error while feeding the filtergraph: %s\n", err2str(ret));
	  return -1;
	}
    }

  // Pull filtered frames from the filtergraph
  while (1)
    {
      filt_frame = av_frame_alloc();
      if (!filt_frame)
	{
	  DPRINTF(E_LOG, L_XCODE, "Out of memory for filt_frame\n");
	  return -1;
	}

      ret = av_buffersink_get_frame(s->buffersink_ctx, filt_frame);
      if (ret < 0)
	{
	  /* if no more frames for output - returns AVERROR(EAGAIN)
	   * if flushed and no more frames for output - returns AVERROR_EOF
	   * rewrite retcode to 0 to show it as normal procedure completion
	   */
	  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	    ret = 0;
	  av_frame_free(&filt_frame);
	  break;
	}

      filt_frame->pict_type = AV_PICTURE_TYPE_NONE;
      ret = encode_write_frame(ctx, s, filt_frame, NULL);
      av_frame_free(&filt_frame);
      if (ret < 0)
	break;
    }

  return ret;
}
#else
static int
filter_encode_write_frame(struct encode_ctx *ctx, struct stream_ctx *s, AVFrame *frame)
{
  AVFilterBufferRef *picref;
  AVFrame *filt_frame;
  int ret;

  // Push the decoded frame into the filtergraph
  if (frame)
    {
      ret = av_buffersrc_write_frame(s->buffersrc_ctx, frame);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error while feeding the filtergraph: %s\n", err2str(ret));
	  return -1;
	}
    }

  // Pull filtered frames from the filtergraph
  while (1)
    {
      filt_frame = av_frame_alloc();
      if (!filt_frame)
	{
	  DPRINTF(E_LOG, L_XCODE, "Out of memory for filt_frame\n");
	  return -1;
	}

      if (s->codec->codec_type == AVMEDIA_TYPE_AUDIO && !(s->codec->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE))
	ret = av_buffersink_read_samples(s->buffersink_ctx, &picref, s->codec->frame_size);
      else
	ret = av_buffersink_read(s->buffersink_ctx, &picref);

      if (ret < 0)
	{
	  /* if no more frames for output - returns AVERROR(EAGAIN)
	   * if flushed and no more frames for output - returns AVERROR_EOF
	   * rewrite retcode to 0 to show it as normal procedure completion
	   */
	  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	    ret = 0;
	  av_frame_free(&filt_frame);
	  break;
	}

      avfilter_copy_buf_props(filt_frame, picref);
      ret = encode_write_frame(ctx, s, filt_frame, NULL);
      av_frame_free(&filt_frame);
      avfilter_unref_buffer(picref);
      if (ret < 0)
	break;
    }

  return ret;
}
#endif

/* Will step through each stream and feed the stream decoder with empty packets
 * to see if the decoder has more frames lined up. Will return non-zero if a
 * frame is found. Should be called until it stops returning anything.
 *
 * @out frame     AVFrame if there was anything to flush, otherwise undefined
 * @out stream    Set to the AVStream where a decoder returned a frame
 * @in  ctx       Decode context
 * @return        Non-zero (true) if frame found, otherwise 0 (false)
 */
static int
flush_decoder(AVFrame *frame, enum AVMediaType *type, struct decode_ctx *ctx)
{
  AVPacket dummypacket = { 0 };
  int got_frame = 0;

  if (ctx->audio_stream.codec)
    {
      *type = AVMEDIA_TYPE_AUDIO;
      avcodec_decode_audio4(ctx->audio_stream.codec, frame, &got_frame, &dummypacket);
    }

  if (!got_frame && ctx->video_stream.codec)
    {
      *type = AVMEDIA_TYPE_VIDEO;
      avcodec_decode_video2(ctx->video_stream.codec, frame, &got_frame, &dummypacket);
    }

  return got_frame;
}

static void
flush_encoder(struct encode_ctx *ctx, struct stream_ctx *s)
{
  int ret;
  int got_frame;

  DPRINTF(E_DBG, L_XCODE, "Flushing output stream #%u encoder\n", s->stream->index);

  if (!(s->codec->codec->capabilities & CODEC_CAP_DELAY))
    return;

  do
    {
      ret = encode_write_frame(ctx, s, NULL, &got_frame);
    }
  while ((ret == 0) && got_frame);
}


/* --------------------------- INPUT/OUTPUT INIT --------------------------- */

static int
open_input(struct decode_ctx *ctx, const char *path)
{
  AVDictionary *options = NULL;
  AVCodec *decoder;
  AVCodecContext *dec_ctx;
  int stream_index;
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

  ret = avformat_open_input(&ctx->ifmt_ctx, path, NULL, &options);

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
      // Find audio stream and open decoder
      stream_index = av_find_best_stream(ctx->ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
      if ((stream_index < 0) || (!decoder))
	{
	  DPRINTF(E_LOG, L_XCODE, "Did not find audio stream or suitable decoder for %s\n", path);
	  goto out_fail;
	}

      CHECK_NULL(L_XCODE, dec_ctx = avcodec_alloc_context3(decoder));

      // In open_filter() we need to tell the sample rate and format that the decoder
      // is giving us - however sample rate of dec_ctx will be 0 if we don't prime it
      // with the streams codecpar data.
      ret = avcodec_parameters_to_context(dec_ctx, ctx->ifmt_ctx->streams[stream_index]->codecpar);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Failed to copy codecpar for stream #%d: %s\n", stream_index, err2str(ret));
	  goto out_fail;
	}

      dec_ctx->request_sample_fmt = ctx->settings.sample_format;
      dec_ctx->request_channel_layout = ctx->settings.channel_layout;

      ret = avcodec_open2(dec_ctx, NULL, NULL);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%d: %s\n", stream_index, err2str(ret));
	  goto out_fail;
	}

      ctx->audio_stream.codec = dec_ctx;
      ctx->audio_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  if (ctx->settings.encode_video)
    {
      // Find video stream and open decoder
      stream_index = av_find_best_stream(ctx->ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
      if ((stream_index < 0) || (!decoder))
	{
	  DPRINTF(E_LOG, L_XCODE, "Did not find video stream or suitable decoder for '%s': %s\n", path, err2str(ret));
	  goto out_fail;
	}

      CHECK_NULL(L_XCODE, dec_ctx = avcodec_alloc_context3(decoder));

      ret = avcodec_open2(dec_ctx, NULL, NULL);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%d: %s\n", stream_index, err2str(ret));
	  goto out_fail;
	}

      ctx->video_stream.codec = dec_ctx;
      ctx->video_stream.stream = ctx->ifmt_ctx->streams[stream_index];
    }

  return 0;

 out_fail:
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avformat_close_input(&ctx->ifmt_ctx);

  return -1;
}

static void
close_input(struct decode_ctx *ctx)
{
  avcodec_free_context(&ctx->audio_stream.codec);
  avcodec_free_context(&ctx->video_stream.codec);
  avformat_close_input(&ctx->ifmt_ctx);
}

static int
open_output(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  int ret;

  ctx->ofmt_ctx = NULL;
  avformat_alloc_output_context2(&ctx->ofmt_ctx, NULL, ctx->settings.format, NULL);
  if (!ctx->ofmt_ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output context\n");
      return -1;
    }

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
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer source: %s\n", err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
               av_get_sample_fmt_name(out_stream->codec->sample_fmt), out_stream->codec->sample_rate,
               out_stream->codec->channel_layout);

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio format filter: %s\n", err2str(ret));
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
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
               in_stream->codec->width, in_stream->codec->height, in_stream->codec->pix_fmt,
               in_stream->stream->time_base.num, in_stream->stream->time_base.den,
               in_stream->codec->sample_aspect_ratio.num, in_stream->codec->sample_aspect_ratio.den);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer source: %s\n", err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "pix_fmt=%d", out_stream->codec->pix_fmt);

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create format filter: %s\n", err2str(ret));
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "width=%d:height=%d", out_stream->codec->width, out_stream->codec->height);

      ret = avfilter_graph_create_filter(&scale_ctx, scale, "scale", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create scale filter: %s\n", err2str(ret));
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
transcode_decode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length)
{
  struct decode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct decode_ctx)));

  ctx->duration = song_length;
  ctx->data_kind = data_kind;

  if ((init_settings(&ctx->settings, profile) < 0) || (open_input(ctx, path) < 0))
    {
      free(ctx);
      return NULL;
    }

  av_init_packet(&ctx->packet);

  return ctx;
}

struct encode_ctx *
transcode_encode_setup(enum transcode_profile profile, struct decode_ctx *src_ctx, off_t *est_size)
{
  struct encode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = calloc(1, sizeof(struct encode_ctx)));

  if ((init_settings(&ctx->settings, profile) < 0) || (open_output(ctx, src_ctx) < 0))
    {
      free(ctx);
      return NULL;
    }

  if (open_filters(ctx, src_ctx) < 0)
    {
      close_output(ctx);
      free(ctx);
      return NULL;
    }

  if (src_ctx->data_kind == DATA_KIND_HTTP)
    ctx->icy_interval = METADATA_ICY_INTERVAL * ctx->settings.channels * ctx->settings.byte_depth * ctx->settings.sample_rate;

  if (ctx->settings.wavheader)
    make_wav_header(ctx, src_ctx, est_size);

  return ctx;
}

struct transcode_ctx *
transcode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length, off_t *est_size)
{
  struct transcode_ctx *ctx;

  CHECK_NULL(L_XCODE, ctx = malloc(sizeof(struct transcode_ctx)));

  ctx->decode_ctx = transcode_decode_setup(profile, data_kind, path, song_length);
  if (!ctx->decode_ctx)
    {
      free(ctx);
      return NULL;
    }

  ctx->encode_ctx = transcode_encode_setup(profile, ctx->decode_ctx, est_size);
  if (!ctx->encode_ctx)
    {
      transcode_decode_cleanup(ctx->decode_ctx);
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
    DPRINTF(E_DBG, L_XCODE, "Client advertises codecs: %s\n", client_codecs);

  if (!client_codecs)
    {
      DPRINTF(E_DBG, L_XCODE, "Could not identify client, using default codectype set\n");
      client_codecs = default_codecs;
    }

  if (strstr(client_codecs, file_codectype))
    {
      DPRINTF(E_DBG, L_XCODE, "Codectype supported by client, no decoding needed\n");
      return 0;
    }

  DPRINTF(E_DBG, L_XCODE, "Will decode\n");
  return 1;
}


/*                                  Cleanup                                  */

void
transcode_decode_cleanup(struct decode_ctx *ctx)
{
  av_packet_unref(&ctx->packet);
  close_input(ctx);
  free(ctx);
}

void
transcode_encode_cleanup(struct encode_ctx *ctx)
{
  if (ctx->audio_stream.stream)
    {
      if (ctx->audio_stream.filter_graph)
	filter_encode_write_frame(ctx, &ctx->audio_stream, NULL);
      flush_encoder(ctx, &ctx->audio_stream);
    }

  if (ctx->video_stream.stream)
    {
      if (ctx->video_stream.filter_graph)
	filter_encode_write_frame(ctx, &ctx->video_stream, NULL);
      flush_encoder(ctx, &ctx->video_stream);
    }

  av_write_trailer(ctx->ofmt_ctx);

  close_filters(ctx);
  close_output(ctx);
  free(ctx);
}

void
transcode_cleanup(struct transcode_ctx *ctx)
{
  transcode_encode_cleanup(ctx->encode_ctx);
  transcode_decode_cleanup(ctx->decode_ctx);
  free(ctx);
}

void
transcode_decoded_free(struct decoded_frame *decoded)
{
  av_frame_free(&decoded->frame);
  free(decoded);
}


/*                       Encoding, decoding and transcoding                  */


int
transcode_decode(struct decoded_frame **decoded, struct decode_ctx *ctx)
{
  AVPacket packet;
  AVFrame *frame;
  enum AVMediaType type;
  int got_frame;
  int retry;
  int ret;
  int used;

  // Alloc the frame we will return on success
  frame = av_frame_alloc();
  if (!frame)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decode frame\n");

      return -1;
    }

  // Loop until we either fail or get a frame
  retry = 0;
  do
    {
      ret = read_packet(&packet, &type, ctx);
      if (ret < 0)
	{
	  // Some decoders need to be flushed, meaning the decoder is to be called
	  // with empty input until no more frames are returned
	  DPRINTF(E_DBG, L_XCODE, "Could not read packet, will flush decoders\n");

	  got_frame = flush_decoder(frame, &type, ctx);
	  if (got_frame)
	    break;

	  av_frame_free(&frame);
	  if (ret == AVERROR_EOF)
	    return 0;
	  else
	    return -1;
	}

      // "used" will tell us how much of the packet was decoded. We may
      // not get a frame because of insufficient input, in which case we loop to
      // read another packet.
      if (type == AVMEDIA_TYPE_AUDIO)
	used = avcodec_decode_audio4(ctx->audio_stream.codec, frame, &got_frame, &packet);
      else
	used = avcodec_decode_video2(ctx->video_stream.codec, frame, &got_frame, &packet);

      // decoder returned an error, but maybe the packet was just a bad apple,
      // so let's try MAX_BAD_PACKETS times before giving up
      if (used < 0)
	{
	  DPRINTF(E_DBG, L_XCODE, "Couldn't decode packet: %s\n", err2str(used));

	  retry += 1;
	  if (retry < MAX_BAD_PACKETS)
	    continue;

	  DPRINTF(E_LOG, L_XCODE, "Couldn't decode packet after %i retries: %s\n", MAX_BAD_PACKETS, err2str(used));

	  av_frame_free(&frame);
	  return -1;
	}

      // decoder didn't process the entire packet, so flag a resume, meaning
      // that the next read_packet() will return this same packet, but where the
      // data pointer is adjusted with an offset
      if (used < packet.size)
	{
	  DPRINTF(E_SPAM, L_XCODE, "Decoder did not finish packet, packet will be resumed\n");

	  ctx->resume_offset += used;
	  ctx->resume = 1;
	}
    }
  while (!got_frame);

  if (got_frame > 0)
    {
      // Return the decoded frame and stream index
      *decoded = malloc(sizeof(struct decoded_frame));
      if (!(*decoded))
	{
	  DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded result\n");

	  av_frame_free(&frame);
	  return -1;
	}

      (*decoded)->frame = frame;
      (*decoded)->type = type;
    }

  return got_frame;
}

// Filters and encodes
int
transcode_encode(struct evbuffer *evbuf, struct decoded_frame *decoded, struct encode_ctx *ctx)
{
  struct stream_ctx *s;
  int encoded_length;
  int ret;

  encoded_length = 0;

  if (decoded->type == AVMEDIA_TYPE_AUDIO)
    s = &ctx->audio_stream;
  else if (decoded->type == AVMEDIA_TYPE_VIDEO)
    s = &ctx->video_stream;
  else
    return -1;

  if (ctx->settings.wavheader)
    {
      encoded_length += sizeof(ctx->header);
      evbuffer_add(evbuf, ctx->header, sizeof(ctx->header));
      ctx->settings.wavheader = 0;
    }

  ret = filter_encode_write_frame(ctx, s, decoded->frame);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error occurred: %s\n", err2str(ret));
      return ret;
    }

  encoded_length += evbuffer_get_length(ctx->obuf);
  evbuffer_add_buffer(evbuf, ctx->obuf);

  return encoded_length;
}

int
transcode(struct evbuffer *evbuf, int wanted, struct transcode_ctx *ctx, int *icy_timer)
{
  struct decoded_frame *decoded;
  int processed;
  int ret;

  *icy_timer = 0;

  processed = 0;
  while (processed < wanted)
    {
      ret = transcode_decode(&decoded, ctx->decode_ctx);
      if (ret <= 0)
	return ret;

      ret = transcode_encode(evbuf, decoded, ctx->encode_ctx);
      transcode_decoded_free(decoded);
      if (ret < 0)
	return -1;

      processed += ret;
    }

  ctx->encode_ctx->total_bytes += processed;
  if (ctx->encode_ctx->icy_interval)
    *icy_timer = (ctx->encode_ctx->total_bytes % ctx->encode_ctx->icy_interval < processed);

  return processed;
}

struct decoded_frame *
transcode_raw2frame(uint8_t *data, size_t size)
{
  struct decoded_frame *decoded;
  AVFrame *frame;
  int ret;

  decoded = malloc(sizeof(struct decoded_frame));
  if (!decoded)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded struct\n");
      return NULL;
    }

  frame = av_frame_alloc();
  if (!frame)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for frame\n");
      free(decoded);
      return NULL;
    }

  decoded->type         = AVMEDIA_TYPE_AUDIO;
  decoded->frame        = frame;

  frame->nb_samples     = size / 4;
  frame->format         = AV_SAMPLE_FMT_S16;
  frame->channel_layout = AV_CH_LAYOUT_STEREO;
#ifdef HAVE_FFMPEG
  frame->channels       = 2;
#endif
  frame->pts            = AV_NOPTS_VALUE;
  frame->sample_rate    = 44100;

  ret = avcodec_fill_audio_frame(frame, 2, frame->format, data, size, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error filling frame with rawbuf: %s\n", err2str(ret));
      transcode_decoded_free(decoded);
      return NULL;
    }

  return decoded;
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
      av_packet_unref(&dec_ctx->packet);

      dec_ctx->timestamp = av_gettime();

      ret = av_read_frame(dec_ctx->ifmt_ctx, &dec_ctx->packet);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not read more data while seeking: %s\n", err2str(ret));
	  s->codec->skip_frame = AVDISCARD_DEFAULT;
	  return -1;
	}

      if (stream_find(dec_ctx, dec_ctx->packet.stream_index) == AVMEDIA_TYPE_UNKNOWN)
	continue;

      // Need a pts to return the real position
      if (dec_ctx->packet.pts == AV_NOPTS_VALUE)
	continue;

      break;
    }
  s->codec->skip_frame = AVDISCARD_DEFAULT;

  // Tell transcode_decode() to resume with ctx->packet
  dec_ctx->resume = 1;
  dec_ctx->resume_offset = 0;

  // Compute position in ms from pts
  got_pts = dec_ctx->packet.pts;

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

