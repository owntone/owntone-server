/*
 * Copyright (C) 2015 Espen Jurgensen
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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>

#ifdef HAVE_LIBAVFILTER
# include <libavfilter/avcodec.h>
#else
# include "ffmpeg-compat.c"
#endif

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
#define READ_TIMEOUT 10000000

static char *default_codecs = "mpeg,wav";
static char *roku_codecs = "mpeg,mp4a,wma,wav";
static char *itunes_codecs = "mpeg,mp4a,mp4v,alac,wav";

struct filter_ctx {
  AVFilterContext *buffersink_ctx;
  AVFilterContext *buffersrc_ctx;
  AVFilterGraph *filter_graph;
};

struct decode_ctx {
  // Input format context
  AVFormatContext *ifmt_ctx;

  // Will point to the max 3 streams that we will transcode
  AVStream *audio_stream;
  AVStream *video_stream;
  AVStream *subtitle_stream;

  // Duration (used to make wav header)
  uint32_t duration;

  // Contains the most recent packet from av_read_frame
  // Used for resuming after seek and for freeing correctly
  // in transcode_decode()
  AVPacket packet;
  int resume;
  int resume_offset;

  // Used to measure if av_read_frame is taking too long
  int64_t timestamp;
};

struct encode_ctx {
  // Output format context
  AVFormatContext *ofmt_ctx;

  // We use filters to resample
  struct filter_ctx *filter_ctx;

  // The ffmpeg muxer writes to this buffer using the avio_evbuffer interface
  struct evbuffer *obuf;

  // Maps input stream number -> output stream number
  // So if we are decoding audio stream 3 and encoding it to 0, then
  // out_stream_map[3] is 0. A value of -1 means the stream is ignored.
  int out_stream_map[MAX_STREAMS];

  // Maps output stream number -> input stream number
  unsigned int in_stream_map[MAX_STREAMS];

  // Used for seeking
  int64_t prev_pts[MAX_STREAMS];
  int64_t offset_pts[MAX_STREAMS];

  // Settings for encoding and muxing
  const char *format;
  int encode_video;

  // Audio settings
  enum AVCodecID audio_codec;
  int sample_rate;
  uint64_t channel_layout;
  int channels;
  enum AVSampleFormat sample_format;
  int byte_depth;

  // Video settings
  enum AVCodecID video_codec;
  int video_height;
  int video_width;

  // How many output bytes we have processed in total
  off_t total_bytes;

  // Used to check for ICY metadata changes at certain intervals
  uint32_t icy_interval;
  uint32_t icy_hash;

  // WAV header
  int wavhdr;
  uint8_t header[44];
};

struct transcode_ctx {
  struct decode_ctx *decode_ctx;
  struct encode_ctx *encode_ctx;
};

struct decoded_frame
{
  AVFrame *frame;
  unsigned int stream_index;
};


/* -------------------------- PROFILE CONFIGURATION ------------------------ */

static int
init_profile(struct encode_ctx *ctx, enum transcode_profile profile)
{
  switch (profile)
    {
      case XCODE_PCM16_NOHEADER:
      case XCODE_PCM16_HEADER:
        ctx->encode_video = 0;
	ctx->format = "s16le";
	ctx->audio_codec = AV_CODEC_ID_PCM_S16LE;
	ctx->sample_rate = 44100;
	ctx->channel_layout = AV_CH_LAYOUT_STEREO;
	ctx->channels = 2;
	ctx->sample_format = AV_SAMPLE_FMT_S16;
	ctx->byte_depth = 2; // Bytes per sample = 16/8
	return 0;

      case XCODE_MP3:
        ctx->encode_video = 0;
	ctx->format = "mp3";
	ctx->audio_codec = AV_CODEC_ID_MP3;
	ctx->sample_rate = 44100;
	ctx->channel_layout = AV_CH_LAYOUT_STEREO;
	ctx->channels = 2;
	ctx->sample_format = AV_SAMPLE_FMT_S16P;
	ctx->byte_depth = 2; // Bytes per sample = 16/8
	return 0;

      case XCODE_H264_AAC:
        ctx->encode_video = 1;
	return 0;

      default:
	DPRINTF(E_LOG, L_XCODE, "Bug! Unknown transcoding profile\n");
	return -1;
    }
}


/* -------------------------------- HELPERS -------------------------------- */

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

  wav_len = ctx->channels * ctx->byte_depth * ctx->sample_rate * (duration / 1000);

  *est_size = wav_len + sizeof(ctx->header);

  memcpy(ctx->header, "RIFF", 4);
  add_le32(ctx->header + 4, 36 + wav_len);
  memcpy(ctx->header + 8, "WAVEfmt ", 8);
  add_le32(ctx->header + 16, 16);
  add_le16(ctx->header + 20, 1);
  add_le16(ctx->header + 22, ctx->channels);     /* channels */
  add_le32(ctx->header + 24, ctx->sample_rate);  /* samplerate */
  add_le32(ctx->header + 28, ctx->sample_rate * ctx->channels * ctx->byte_depth); /* byte rate */
  add_le16(ctx->header + 32, ctx->channels * ctx->byte_depth);                        /* block align */
  add_le16(ctx->header + 34, ctx->byte_depth * 8);                                        /* bits per sample */
  memcpy(ctx->header + 36, "data", 4);
  add_le32(ctx->header + 40, wav_len);
}

/*
 * Returns true if in_stream is a stream we should decode, otherwise false
 *
 * @in ctx        Decode context
 * @in in_stream  Pointer to AVStream
 * @return        True if stream should be decoded, otherwise false
 */
static int
decode_stream(struct decode_ctx *ctx, AVStream *in_stream)
{
  return ((in_stream == ctx->audio_stream) ||
          (in_stream == ctx->video_stream) ||
          (in_stream == ctx->subtitle_stream));
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
 *                av_free_packet()
 * @out stream    Set to the input AVStream corresponding to the packet
 * @out stream_index
 *                Set to the input stream index corresponding to the packet
 * @in  ctx       Decode context
 * @return        0 if OK, < 0 on error or end of file
 */
static int
read_packet(AVPacket *packet, AVStream **stream, unsigned int *stream_index, struct decode_ctx *ctx)
{
  AVStream *in_stream;
  char *errmsg;
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
	  av_free_packet(&ctx->packet);

	  ctx->resume_offset = 0;
	  ctx->timestamp = av_gettime();

	  ret = av_read_frame(ctx->ifmt_ctx, &ctx->packet);
	  if (ret < 0)
	    {
	      errmsg = malloc(128);
	      av_strerror(ret, errmsg, 128);
	      DPRINTF(E_WARN, L_XCODE, "Could not read frame: %s\n", errmsg);
	      free(errmsg);
              return ret;
	    }

	  *packet = ctx->packet;
	}

      in_stream = ctx->ifmt_ctx->streams[packet->stream_index];
    }
  while (!decode_stream(ctx, in_stream));

  av_packet_rescale_ts(packet, in_stream->time_base, in_stream->codec->time_base);

  *stream = in_stream;
  *stream_index = packet->stream_index;

  return 0;
}

static int
encode_write_frame(struct encode_ctx *ctx, AVFrame *filt_frame, unsigned int stream_index, int *got_frame)
{
  AVStream *out_stream;
  AVPacket enc_pkt;
  int ret;
  int got_frame_local;

  if (!got_frame)
    got_frame = &got_frame_local;

  out_stream = ctx->ofmt_ctx->streams[stream_index];

  // Encode filtered frame
  enc_pkt.data = NULL;
  enc_pkt.size = 0;
  av_init_packet(&enc_pkt);

  if (out_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
    ret = avcodec_encode_audio2(out_stream->codec, &enc_pkt, filt_frame, got_frame);
  else if (out_stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
    ret = avcodec_encode_video2(out_stream->codec, &enc_pkt, filt_frame, got_frame);
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

  av_packet_rescale_ts(&enc_pkt, out_stream->codec->time_base, out_stream->time_base);

  // Mux encoded frame
  ret = av_interleaved_write_frame(ctx->ofmt_ctx, &enc_pkt);
  return ret;
}

#if defined(HAVE_LIBAV_BUFFERSRC_ADD_FRAME_FLAGS) && defined(HAVE_LIBAV_BUFFERSINK_GET_FRAME)
static int
filter_encode_write_frame(struct encode_ctx *ctx, AVFrame *frame, unsigned int stream_index)
{
  AVFrame *filt_frame;
  int ret;

  // Push the decoded frame into the filtergraph
  if (frame)
    {
      ret = av_buffersrc_add_frame_flags(ctx->filter_ctx[stream_index].buffersrc_ctx, frame, 0);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error while feeding the filtergraph\n");
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

      ret = av_buffersink_get_frame(ctx->filter_ctx[stream_index].buffersink_ctx, filt_frame);
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
      ret = encode_write_frame(ctx, filt_frame, stream_index, NULL);
      av_frame_free(&filt_frame);
      if (ret < 0)
	break;
    }

  return ret;
}
#else
static int
filter_encode_write_frame(struct encode_ctx *ctx, AVFrame *frame, unsigned int stream_index)
{
  AVFilterBufferRef *picref;
  AVCodecContext *enc_ctx;
  AVFrame *filt_frame;
  int ret;

  enc_ctx = ctx->ofmt_ctx->streams[stream_index]->codec;

  // Push the decoded frame into the filtergraph
  if (frame)
    {
      ret = av_buffersrc_write_frame(ctx->filter_ctx[stream_index].buffersrc_ctx, frame);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Error while feeding the filtergraph\n");
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

      if (enc_ctx->codec_type == AVMEDIA_TYPE_AUDIO && !(enc_ctx->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE))
	ret = av_buffersink_read_samples(ctx->filter_ctx[stream_index].buffersink_ctx, &picref, enc_ctx->frame_size);
      else
	ret = av_buffersink_read(ctx->filter_ctx[stream_index].buffersink_ctx, &picref);

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
      ret = encode_write_frame(ctx, filt_frame, stream_index, NULL);
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
 * @out stream_index
 *                Set to the stream index of the stream returning a frame
 * @in  ctx       Decode context
 * @return        Non-zero (true) if frame found, otherwise 0 (false)
 */
static int
flush_decoder(AVFrame *frame, AVStream **stream, unsigned int *stream_index, struct decode_ctx *ctx)
{
  AVStream *in_stream;
  AVPacket dummypacket;
  int got_frame;
  int i;

  memset(&dummypacket, 0, sizeof(AVPacket));

  for (i = 0; i < ctx->ifmt_ctx->nb_streams; i++)
    {
      in_stream = ctx->ifmt_ctx->streams[i];
      if (!decode_stream(ctx, in_stream))
	continue;

      if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
	avcodec_decode_audio4(in_stream->codec, frame, &got_frame, &dummypacket);
      else
	avcodec_decode_video2(in_stream->codec, frame, &got_frame, &dummypacket);

      if (!got_frame)
	continue;

      DPRINTF(E_DBG, L_XCODE, "Flushing decoders produced a frame from stream %d\n", i);

      *stream = in_stream;
      *stream_index = i;
      return got_frame;
    }

  return 0;
}

static void
flush_encoder(struct encode_ctx *ctx, unsigned int stream_index)
{
  int ret;
  int got_frame;

  DPRINTF(E_DBG, L_XCODE, "Flushing output stream #%u encoder\n", stream_index);

  if (!(ctx->ofmt_ctx->streams[stream_index]->codec->codec->capabilities & CODEC_CAP_DELAY))
    return;

  do
    {
      ret = encode_write_frame(ctx, NULL, stream_index, &got_frame);
    }
  while ((ret == 0) && got_frame);
}


/* --------------------------- INPUT/OUTPUT INIT --------------------------- */

static int
open_input(struct decode_ctx *ctx, struct media_file_info *mfi, int decode_video)
{
  AVDictionary *options;
  AVCodec *decoder;
  unsigned int stream_index;
  int ret;

  options = NULL;
  ctx->ifmt_ctx = avformat_alloc_context();;
  if (!ctx->ifmt_ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for input format context\n");
      return -1;
    }

# ifndef HAVE_FFMPEG
  // Without this, libav is slow to probe some internet streams, which leads to RAOP timeouts
  if (mfi->data_kind == DATA_KIND_HTTP)
    ctx->ifmt_ctx->probesize = 64000;
# endif
  if (mfi->data_kind == DATA_KIND_HTTP)
    av_dict_set(&options, "icy", "1", 0);

  // TODO Newest versions of ffmpeg have timeout and reconnect options we should use
  ctx->ifmt_ctx->interrupt_callback.callback = decode_interrupt_cb;
  ctx->ifmt_ctx->interrupt_callback.opaque = ctx;
  ctx->timestamp = av_gettime();

  ret = avformat_open_input(&ctx->ifmt_ctx, mfi->path, NULL, &options);

  if (options)
    av_dict_free(&options);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot open input path\n");
      return -1;
    }

  ret = avformat_find_stream_info(ctx->ifmt_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Cannot find stream information\n");
      goto out_fail;
    }

  if (ctx->ifmt_ctx->nb_streams > MAX_STREAMS)
    {
      DPRINTF(E_LOG, L_XCODE, "File '%s' has too many streams (%u)\n", mfi->path, ctx->ifmt_ctx->nb_streams);
      goto out_fail;
    }

  // Find audio stream and open decoder    
  stream_index = av_find_best_stream(ctx->ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
  if ((stream_index < 0) || (!decoder))
    {
      DPRINTF(E_LOG, L_XCODE, "Did not find audio stream or suitable decoder for %s\n", mfi->path);
      goto out_fail;
    }

  ctx->ifmt_ctx->streams[stream_index]->codec->request_sample_fmt = AV_SAMPLE_FMT_S16;
  ctx->ifmt_ctx->streams[stream_index]->codec->request_channel_layout = AV_CH_LAYOUT_STEREO;

// Disabled to see if it is still required
//  if (decoder->capabilities & CODEC_CAP_TRUNCATED)
//    ctx->ifmt_ctx->streams[stream_index]->codec->flags |= CODEC_FLAG_TRUNCATED;

  ret = avcodec_open2(ctx->ifmt_ctx->streams[stream_index]->codec, decoder, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%u\n", stream_index);
      goto out_fail;
    }

  ctx->audio_stream = ctx->ifmt_ctx->streams[stream_index];

  // If no video then we are all done
  if (!decode_video)
    return 0;

  // Find video stream and open decoder    
  stream_index = av_find_best_stream(ctx->ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
  if ((stream_index < 0) || (!decoder))
    {
      DPRINTF(E_LOG, L_XCODE, "Did not find video stream or suitable decoder for %s\n", mfi->path);
      return 0;
    }

  ret = avcodec_open2(ctx->ifmt_ctx->streams[stream_index]->codec, decoder, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Failed to open decoder for stream #%u\n", stream_index);
      return 0;
    }

  ctx->video_stream = ctx->ifmt_ctx->streams[stream_index];

  // Find a (random) subtitle stream which will be remuxed
  stream_index = av_find_best_stream(ctx->ifmt_ctx, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
  if (stream_index >= 0)
    {
      ctx->subtitle_stream = ctx->ifmt_ctx->streams[stream_index];
    }

  return 0;

 out_fail:
  avformat_close_input(&ctx->ifmt_ctx);

  return -1;
}

static void
close_input(struct decode_ctx *ctx)
{
  if (ctx->audio_stream)
    avcodec_close(ctx->audio_stream->codec);
  if (ctx->video_stream)
    avcodec_close(ctx->video_stream->codec);

  avformat_close_input(&ctx->ifmt_ctx);
}

static int
open_output(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  AVStream *out_stream;
  AVStream *in_stream;
  AVCodecContext *dec_ctx;
  AVCodecContext *enc_ctx;
  AVCodec *encoder;
  const AVCodecDescriptor *codec_desc;
  enum AVCodecID codec_id;
  int ret;
  int i;

  ctx->ofmt_ctx = NULL;
  avformat_alloc_output_context2(&ctx->ofmt_ctx, NULL, ctx->format, NULL);
  if (!ctx->ofmt_ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output context\n");
      return -1;
    }

  ctx->obuf = evbuffer_new();
  if (!ctx->obuf)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output evbuffer\n");
      goto out_fail_evbuf;
    }

  ctx->ofmt_ctx->pb = avio_evbuffer_open(ctx->obuf);
  if (!ctx->ofmt_ctx->pb)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create output avio pb\n");
      goto out_fail_pb;
    }

  for (i = 0; i < src_ctx->ifmt_ctx->nb_streams; i++)
    { 
      in_stream = src_ctx->ifmt_ctx->streams[i];
      if (!decode_stream(src_ctx, in_stream))
	{
	  ctx->out_stream_map[i] = -1;
	  continue;
	}

      out_stream = avformat_new_stream(ctx->ofmt_ctx, NULL);
      if (!out_stream)
	{
	  DPRINTF(E_LOG, L_XCODE, "Failed allocating output stream\n");
	  goto out_fail_stream;
        }

      ctx->out_stream_map[i] = out_stream->index;
      ctx->in_stream_map[out_stream->index] = i;

      dec_ctx = in_stream->codec;
      enc_ctx = out_stream->codec;

      // TODO Enough to just remux subtitles?
      if (dec_ctx->codec_type == AVMEDIA_TYPE_SUBTITLE)
	{
	  avcodec_copy_context(enc_ctx, dec_ctx);
	  continue;
	}

      if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
	codec_id = ctx->audio_codec;
      else if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
	codec_id = ctx->video_codec;
      else
	continue;

      codec_desc = avcodec_descriptor_get(codec_id);
      encoder = avcodec_find_encoder(codec_id);
      if (!encoder)
	{
	  if (codec_desc)
	    DPRINTF(E_LOG, L_XCODE, "Necessary encoder (%s) for input stream %u not found\n", codec_desc->name, i);
	  else
	    DPRINTF(E_LOG, L_XCODE, "Necessary encoder (unknown) for input stream %u not found\n", i);
	  goto out_fail_stream;
	}

      if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
	{
	  enc_ctx->sample_rate = ctx->sample_rate;
	  enc_ctx->channel_layout = ctx->channel_layout;
	  enc_ctx->channels = ctx->channels;
	  enc_ctx->sample_fmt = ctx->sample_format;
	  enc_ctx->time_base = (AVRational){1, ctx->sample_rate};
	}
      else
	{
	  enc_ctx->height = ctx->video_height;
	  enc_ctx->width = ctx->video_width;
	  enc_ctx->sample_aspect_ratio = dec_ctx->sample_aspect_ratio; //FIXME
	  enc_ctx->pix_fmt = avcodec_find_best_pix_fmt_of_list(encoder->pix_fmts, dec_ctx->pix_fmt, 1, NULL);
	  enc_ctx->time_base = dec_ctx->time_base;
	}

      ret = avcodec_open2(enc_ctx, encoder, NULL);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot open encoder (%s) for input stream #%u\n", codec_desc->name, i);
	  goto out_fail_codec;
	}

      if (ctx->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
	enc_ctx->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

  // Notice, this will not write WAV header (so we do that manually)
  ret = avformat_write_header(ctx->ofmt_ctx, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error occurred when writing header to output buffer\n");
      goto out_fail_write;
    }

  return 0;

 out_fail_write:
 out_fail_codec:
  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      enc_ctx = ctx->ofmt_ctx->streams[i]->codec;
      if (enc_ctx)
	avcodec_close(enc_ctx);
    }
 out_fail_stream:
  avio_evbuffer_close(ctx->ofmt_ctx->pb);
 out_fail_pb:
  evbuffer_free(ctx->obuf);
 out_fail_evbuf:
  avformat_free_context(ctx->ofmt_ctx);

  return -1;
}

static void
close_output(struct encode_ctx *ctx)
{
  int i;

  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      if (ctx->ofmt_ctx->streams[i]->codec)
	avcodec_close(ctx->ofmt_ctx->streams[i]->codec);
    }

  avio_evbuffer_close(ctx->ofmt_ctx->pb);
  evbuffer_free(ctx->obuf);
  avformat_free_context(ctx->ofmt_ctx);
}

#ifdef HAVE_LIBAV_GRAPH_PARSE_PTR
static int
open_filter(struct filter_ctx *filter_ctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec)
{
  AVFilter *buffersrc = NULL;
  AVFilter *buffersink = NULL;
  AVFilterContext *buffersrc_ctx = NULL;
  AVFilterContext *buffersink_ctx = NULL;
  AVFilterInOut *outputs = avfilter_inout_alloc();
  AVFilterInOut *inputs  = avfilter_inout_alloc();
  AVFilterGraph *filter_graph = avfilter_graph_alloc();
  char args[512];
  int ret;

  if (!outputs || !inputs || !filter_graph)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for filter_graph, input or output\n");
      goto out_fail;
    }

  if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      buffersrc = avfilter_get_by_name("buffer");
      buffersink = avfilter_get_by_name("buffersink");
      if (!buffersrc || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source or sink element not found\n");
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
               dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
               dec_ctx->time_base.num, dec_ctx->time_base.den,
               dec_ctx->sample_aspect_ratio.num,
               dec_ctx->sample_aspect_ratio.den);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer source\n");
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer sink\n");
	  goto out_fail;
	}

      ret = av_opt_set_bin(buffersink_ctx, "pix_fmts", (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt), AV_OPT_SEARCH_CHILDREN);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot set output pixel format\n");
	  goto out_fail;
	}
    }
  else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      buffersrc = avfilter_get_by_name("abuffer");
      buffersink = avfilter_get_by_name("abuffersink");
      if (!buffersrc || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source or sink element not found\n");
	  ret = AVERROR_UNKNOWN;
	  goto out_fail;
	}

      if (!dec_ctx->channel_layout)
	dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);

      snprintf(args, sizeof(args),
               "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
               dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
               av_get_sample_fmt_name(dec_ctx->sample_fmt),
               dec_ctx->channel_layout);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer source\n");
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer sink\n");
	  goto out_fail;
	}

      ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                           (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt), AV_OPT_SEARCH_CHILDREN);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot set output sample format\n");
	  goto out_fail;
	}

      ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                           (uint8_t*)&enc_ctx->channel_layout, sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot set output channel layout\n");
	  goto out_fail;
	}

      ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                           (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate), AV_OPT_SEARCH_CHILDREN);
      if (ret < 0)
	{
          DPRINTF(E_LOG, L_XCODE, "Cannot set output sample rate\n");
	  goto out_fail;
	}
    }
  else
    {
      DPRINTF(E_LOG, L_XCODE, "Bug! Unknown type passed to filter graph init\n");
      goto out_fail;
    }

  /* Endpoints for the filter graph. */
  outputs->name       = av_strdup("in");
  outputs->filter_ctx = buffersrc_ctx;
  outputs->pad_idx    = 0;
  outputs->next       = NULL;
  inputs->name       = av_strdup("out");
  inputs->filter_ctx = buffersink_ctx;
  inputs->pad_idx    = 0;
  inputs->next       = NULL;
  if (!outputs->name || !inputs->name)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for outputs/inputs\n");
      goto out_fail;
    }

  ret = avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, NULL);
  if (ret < 0)
    goto out_fail;

  ret = avfilter_graph_config(filter_graph, NULL);
  if (ret < 0)
    goto out_fail;

  /* Fill filtering context */
  filter_ctx->buffersrc_ctx = buffersrc_ctx;
  filter_ctx->buffersink_ctx = buffersink_ctx;
  filter_ctx->filter_graph = filter_graph;

  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  return 0;

 out_fail:
  avfilter_graph_free(&filter_graph);
  avfilter_inout_free(&inputs);
  avfilter_inout_free(&outputs);

  return -1;
}
#else
static int
open_filter(struct filter_ctx *filter_ctx, AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, const char *filter_spec)
{

  AVFilter *buffersrc = NULL;
  AVFilter *format = NULL;
  AVFilter *buffersink = NULL;
  AVFilterContext *buffersrc_ctx = NULL;
  AVFilterContext *format_ctx = NULL;
  AVFilterContext *buffersink_ctx = NULL;
  AVFilterGraph *filter_graph = avfilter_graph_alloc();
  char args[512];
  int ret;

  if (!filter_graph)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for filter_graph\n");
      goto out_fail;
    }

  if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
      buffersrc = avfilter_get_by_name("buffer");
      format = avfilter_get_by_name("format");
      buffersink = avfilter_get_by_name("buffersink");
      if (!buffersrc || !format || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source, format or sink element not found\n");
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
               dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
               dec_ctx->time_base.num, dec_ctx->time_base.den,
               dec_ctx->sample_aspect_ratio.num,
               dec_ctx->sample_aspect_ratio.den);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer source\n");
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "pix_fmt=%d",
               enc_ctx->pix_fmt);

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create format filter\n");
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create buffer sink\n");
	  goto out_fail;
	}
    }
  else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      buffersrc = avfilter_get_by_name("abuffer");
      format = avfilter_get_by_name("aformat");
      buffersink = avfilter_get_by_name("abuffersink");
      if (!buffersrc || !format || !buffersink)
	{
	  DPRINTF(E_LOG, L_XCODE, "Filtering source, format or sink element not found\n");
	  ret = AVERROR_UNKNOWN;
	  goto out_fail;
	}

      if (!dec_ctx->channel_layout)
	dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);

      snprintf(args, sizeof(args),
               "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
               dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
               av_get_sample_fmt_name(dec_ctx->sample_fmt),
               dec_ctx->channel_layout);

      ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer source\n");
	  goto out_fail;
	}

      snprintf(args, sizeof(args),
               "sample_fmts=%s:sample_rates=%d:channel_layouts=0x%"PRIx64,
               av_get_sample_fmt_name(enc_ctx->sample_fmt), enc_ctx->sample_rate,
               enc_ctx->channel_layout);

      ret = avfilter_graph_create_filter(&format_ctx, format, "format", args, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio format filter\n");
	  goto out_fail;
	}

      ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", NULL, NULL, filter_graph);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_XCODE, "Cannot create audio buffer sink\n");
	  goto out_fail;
	}
    }
  else
    {
      DPRINTF(E_LOG, L_XCODE, "Bug! Unknown type passed to filter graph init\n");
      goto out_fail;
    }

  ret = avfilter_link(buffersrc_ctx, 0, format_ctx, 0);
  if (ret >= 0)
    ret = avfilter_link(format_ctx, 0, buffersink_ctx, 0);
  if (ret < 0)
    DPRINTF(E_LOG, L_XCODE, "Error connecting filters\n");

  ret = avfilter_graph_config(filter_graph, NULL);
  if (ret < 0)
    goto out_fail;

  /* Fill filtering context */
  filter_ctx->buffersrc_ctx = buffersrc_ctx;
  filter_ctx->buffersink_ctx = buffersink_ctx;
  filter_ctx->filter_graph = filter_graph;

  return 0;

 out_fail:
  avfilter_graph_free(&filter_graph);

  return -1;
}
#endif

static int
open_filters(struct encode_ctx *ctx, struct decode_ctx *src_ctx)
{
  AVCodecContext *enc_ctx;
  AVCodecContext *dec_ctx;
  const char *filter_spec;
  unsigned int stream_index;
  int i;
  int ret;

  ctx->filter_ctx = av_malloc_array(ctx->ofmt_ctx->nb_streams, sizeof(*ctx->filter_ctx));
  if (!ctx->filter_ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for outputs/inputs\n");
      return -1;
    }

  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      ctx->filter_ctx[i].buffersrc_ctx  = NULL;
      ctx->filter_ctx[i].buffersink_ctx = NULL;
      ctx->filter_ctx[i].filter_graph   = NULL;

      stream_index = ctx->in_stream_map[i];

      enc_ctx = ctx->ofmt_ctx->streams[i]->codec;
      dec_ctx = src_ctx->ifmt_ctx->streams[stream_index]->codec;

      if (enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO)
	filter_spec = "null"; /* passthrough (dummy) filter for video */
      else if (enc_ctx->codec_type == AVMEDIA_TYPE_AUDIO)
	filter_spec = "anull"; /* passthrough (dummy) filter for audio */
      else
	continue;

      ret = open_filter(&ctx->filter_ctx[i], dec_ctx, enc_ctx, filter_spec);
      if (ret < 0)
	goto out_fail;
    }

  return 0;

 out_fail:
  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      if (ctx->filter_ctx && ctx->filter_ctx[i].filter_graph)
	avfilter_graph_free(&ctx->filter_ctx[i].filter_graph);
    }
  av_free(ctx->filter_ctx);

  return -1;
}

static void
close_filters(struct encode_ctx *ctx)
{
  int i;

  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      if (ctx->filter_ctx && ctx->filter_ctx[i].filter_graph)
	avfilter_graph_free(&ctx->filter_ctx[i].filter_graph);
    }
  av_free(ctx->filter_ctx);
}


/* ----------------------------- TRANSCODE API ----------------------------- */

/*                                  Setup                                    */

struct decode_ctx *
transcode_decode_setup(struct media_file_info *mfi, int decode_video)
{
  struct decode_ctx *ctx;

  ctx = calloc(1, sizeof(struct decode_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decode ctx\n");
      return NULL;
    }

  if (open_input(ctx, mfi, decode_video) < 0)
    {
      free(ctx);
      return NULL;
    }

  ctx->duration = mfi->song_length;

  av_init_packet(&ctx->packet);

  return ctx;
}

struct encode_ctx *
transcode_encode_setup(struct decode_ctx *src_ctx, enum transcode_profile profile, off_t *est_size)
{
  struct encode_ctx *ctx;

  ctx = calloc(1, sizeof(struct encode_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for encode ctx\n");
      return NULL;
    }

  if ((init_profile(ctx, profile) < 0) || (open_output(ctx, src_ctx) < 0))
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

  ctx->icy_interval = METADATA_ICY_INTERVAL * ctx->channels * ctx->byte_depth * ctx->sample_rate;

  if (profile == XCODE_PCM16_HEADER)
    {
      ctx->wavhdr = 1;
      make_wav_header(ctx, src_ctx, est_size);
    }

  return ctx;
}

struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, enum transcode_profile profile, off_t *est_size)
{
  struct transcode_ctx *ctx;

  ctx = malloc(sizeof(struct transcode_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for transcode ctx\n");
      return NULL;
    }

  ctx->decode_ctx = transcode_decode_setup(mfi, profile & XCODE_HAS_VIDEO);
  if (!ctx->decode_ctx)
    {
      free(ctx);
      return NULL;
    }

  ctx->encode_ctx = transcode_encode_setup(ctx->decode_ctx, profile, est_size);
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
  struct AVCodec *decoder;

  ctx = calloc(1, sizeof(struct decode_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decode ctx\n");
      return NULL;
    }

  ctx->ifmt_ctx = avformat_alloc_context();
  if (!ctx->ifmt_ctx)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decode format ctx\n");
      return NULL;
    }

  decoder = avcodec_find_decoder(AV_CODEC_ID_PCM_S16LE);

  ctx->audio_stream = avformat_new_stream(ctx->ifmt_ctx, decoder);
  if (!ctx->audio_stream)
    {
      DPRINTF(E_LOG, L_XCODE, "Could not create stream with PCM16 decoder\n");
      return NULL;
    }

  ctx->audio_stream->codec->time_base.num  = 1;
  ctx->audio_stream->codec->time_base.den  = 44100;
  ctx->audio_stream->codec->sample_rate    = 44100;
  ctx->audio_stream->codec->sample_fmt     = AV_SAMPLE_FMT_S16;
  ctx->audio_stream->codec->channel_layout = AV_CH_LAYOUT_STEREO;

  return ctx;
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
  av_free_packet(&ctx->packet);
  close_input(ctx);
  free(ctx);
}

void
transcode_encode_cleanup(struct encode_ctx *ctx)
{
  int i;

  // Flush filters and encoders
  for (i = 0; i < ctx->ofmt_ctx->nb_streams; i++)
    {
      if (!ctx->filter_ctx[i].filter_graph)
	continue;
      filter_encode_write_frame(ctx, NULL, i);
      flush_encoder(ctx, i);
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
  AVStream *in_stream;
  AVFrame *frame;
  unsigned int stream_index;
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
      ret = read_packet(&packet, &in_stream, &stream_index, ctx);
      if (ret < 0)
	{
	  // Some decoders need to be flushed, meaning the decoder is to be called
	  // with empty input until no more frames are returned
	  DPRINTF(E_DBG, L_XCODE, "Could not read packet, will flush decoders\n");

	  used = 1;
	  got_frame = flush_decoder(frame, &in_stream, &stream_index, ctx);
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
      if (in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO)
	used = avcodec_decode_audio4(in_stream->codec, frame, &got_frame, &packet);
      else
	used = avcodec_decode_video2(in_stream->codec, frame, &got_frame, &packet);

      // decoder returned an error, but maybe the packet was just a bad apple,
      // so let's try MAX_BAD_PACKETS times before giving up
      if (used < 0)
	{
	  DPRINTF(E_DBG, L_XCODE, "Couldn't decode packet\n");

	  retry += 1;
	  if (retry < MAX_BAD_PACKETS)
	    continue;

	  DPRINTF(E_LOG, L_XCODE, "Couldn't decode packet after %i retries\n", MAX_BAD_PACKETS);

	  av_frame_free(&frame);
	  return -1;
	}

      // decoder didn't process the entire packet, so flag a resume, meaning
      // that the next read_packet() will return this same packet, but where the
      // data pointer is adjusted with an offset
      if (used < packet.size)
	{
	  DPRINTF(E_DBG, L_XCODE, "Decoder did not finish packet, packet will be resumed\n");

	  ctx->resume_offset += used;
	  ctx->resume = 1;
	}
    }
  while (!got_frame);

  // Return the decoded frame and stream index
  frame->pts = av_frame_get_best_effort_timestamp(frame);

  *decoded = malloc(sizeof(struct decoded_frame));
  if (!*decoded)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded result\n");

      av_frame_free(&frame);
      return -1;
    }

  (*decoded)->frame = frame;
  (*decoded)->stream_index = stream_index;

  return used;
}

// Filters and encodes
int
transcode_encode(struct evbuffer *evbuf, struct decoded_frame *decoded, struct encode_ctx *ctx)
{
  char *errmsg;
  int stream_index;
  int encoded_length;
  int ret;

  encoded_length = 0;

  stream_index = ctx->out_stream_map[decoded->stream_index];
  if (stream_index < 0)
    return -1;

  if (ctx->wavhdr)
    {
      encoded_length += sizeof(ctx->header);
      evbuffer_add(evbuf, ctx->header, sizeof(ctx->header));
      ctx->wavhdr = 0;
    }

  ret = filter_encode_write_frame(ctx, decoded->frame, stream_index);
  if (ret < 0)
    {
      errmsg = malloc(128);
      av_strerror(ret, errmsg, 128);
      DPRINTF(E_LOG, L_XCODE, "Error occurred: %s\n", errmsg);
      free(errmsg);
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
  *icy_timer = (ctx->encode_ctx->total_bytes % ctx->encode_ctx->icy_interval < processed);

  return processed;
}

struct decoded_frame *
transcode_raw2frame(uint8_t *data, size_t size)
{
  struct decoded_frame *decoded;
  AVFrame *frame;

  decoded = malloc(sizeof(struct decoded_frame));
  frame = av_frame_alloc();
  if (!decoded || !frame)
    {
      DPRINTF(E_LOG, L_XCODE, "Out of memory for decoded struct or frame\n");
      return NULL;
    }

  decoded->stream_index = 0;
  decoded->frame = frame;

  frame->nb_samples     = size / 4;
  frame->format         = AV_SAMPLE_FMT_S16;
  frame->channel_layout = AV_CH_LAYOUT_STEREO;
  frame->pts            = AV_NOPTS_VALUE;
  frame->sample_rate    = 44100;

  if (avcodec_fill_audio_frame(frame, 2, frame->format, data, size, 0) < 0)
    {
      DPRINTF(E_LOG, L_XCODE, "Error filling frame with rawbuf\n");
      return NULL;
    }

  return decoded;
}


/* TODO remux this frame without reencoding
	  av_packet_rescale_ts(&packet, in_stream->time_base, out_stream->time_base);

	  ret = av_interleaved_write_frame(ctx->ofmt_ctx, &packet);
	  if (ret < 0)
	    goto end;*/


/*                                  Seeking                                  */

int
transcode_seek(struct transcode_ctx *ctx, int ms)
{
  struct decode_ctx *decode_ctx;
  AVStream *in_stream;
  int64_t start_time;
  int64_t target_pts;
  int64_t got_pts;
  int got_ms;
  int ret;
  int i;

  decode_ctx = ctx->decode_ctx;
  in_stream = ctx->decode_ctx->audio_stream;
  start_time = in_stream->start_time;

  target_pts = ms;
  target_pts = target_pts * AV_TIME_BASE / 1000;
  target_pts = av_rescale_q(target_pts, AV_TIME_BASE_Q, in_stream->time_base);

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    target_pts += start_time;

  ret = av_seek_frame(decode_ctx->ifmt_ctx, in_stream->index, target_pts, AVSEEK_FLAG_BACKWARD);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_XCODE, "Could not seek into stream: %s\n", strerror(AVUNERROR(ret)));
      return -1;
    }

  for (i = 0; i < decode_ctx->ifmt_ctx->nb_streams; i++)
    {
      if (decode_stream(decode_ctx, decode_ctx->ifmt_ctx->streams[i]))
	avcodec_flush_buffers(decode_ctx->ifmt_ctx->streams[i]->codec);
//      avcodec_flush_buffers(ctx->ofmt_ctx->streams[stream_nb]->codec);
    }

  // Fast forward until first packet with a timestamp is found
  in_stream->codec->skip_frame = AVDISCARD_NONREF;
  while (1)
    {
      av_free_packet(&decode_ctx->packet);

      decode_ctx->timestamp = av_gettime();

      ret = av_read_frame(decode_ctx->ifmt_ctx, &decode_ctx->packet);
      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_XCODE, "Could not read more data while seeking\n");
	  in_stream->codec->skip_frame = AVDISCARD_DEFAULT;
	  return -1;
	}

      if (decode_ctx->packet.stream_index != in_stream->index)
	continue;

      // Need a pts to return the real position
      if (decode_ctx->packet.pts == AV_NOPTS_VALUE)
	continue;

      break;
    }
  in_stream->codec->skip_frame = AVDISCARD_DEFAULT;

  // Tell transcode_decode() to resume with ctx->packet
  decode_ctx->resume = 1;
  decode_ctx->resume_offset = 0;

  // Compute position in ms from pts
  got_pts = decode_ctx->packet.pts;

  if ((start_time != AV_NOPTS_VALUE) && (start_time > 0))
    got_pts -= start_time;

  got_pts = av_rescale_q(got_pts, in_stream->time_base, AV_TIME_BASE_Q);
  got_ms = got_pts / (AV_TIME_BASE / 1000);

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

char *
transcode_metadata_artwork_url(struct transcode_ctx *ctx)
{
  struct http_icy_metadata *m;
  char *artwork_url;

  if (!ctx->decode_ctx->ifmt_ctx || !ctx->decode_ctx->ifmt_ctx->filename)
    return NULL;

  artwork_url = NULL;

  m = http_icy_metadata_get(ctx->decode_ctx->ifmt_ctx, 1);
  if (m && m->artwork_url)
    artwork_url = strdup(m->artwork_url);

  if (m)
    http_icy_metadata_free(m, 0);

  return artwork_url;
}

