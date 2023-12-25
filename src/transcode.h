
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include <event2/buffer.h>
#include "http.h"
#include "misc.h"

enum transcode_profile
{
  // Used for errors
  XCODE_UNKNOWN,
  // No transcoding, send as-is
  XCODE_NONE,
  // Decodes the best audio stream into PCM16 or PCM24, no resampling (does not add wav header)
  XCODE_PCM_NATIVE,
  // Decodes/resamples the best audio stream into PCM16 (with wav header)
  XCODE_WAV,
  // Decodes/resamples the best audio stream into PCM16/24/32 (no wav headers)
  XCODE_PCM16,
  XCODE_PCM24,
  XCODE_PCM32,
  // Transcodes the best audio stream to MP3
  XCODE_MP3,
  // Transcodes the best audio stream to raw OPUS (no container)
  XCODE_OPUS,
  // Transcodes the best audio stream to raw ALAC (no container)
  XCODE_ALAC,
  // Transcodes the best audio stream to ALAC in a MP4 container
  XCODE_MP4_ALAC,
  // Produces just the header for a MP4 container with ALAC
  XCODE_MP4_ALAC_HEADER,
  // Transcodes the best audio stream from OGG
  XCODE_OGG,
  // Transcodes the best video stream to JPEG/PNG/VP8
  XCODE_JPEG,
  XCODE_PNG,
  XCODE_VP8,
};

enum transcode_seek_type
{
  XCODE_SEEK_SIZE,
  XCODE_SEEK_SET,
  XCODE_SEEK_CUR,
};

typedef void transcode_frame;
typedef int64_t(*transcode_seekfn)(void *arg, int64_t offset, enum transcode_seek_type seek_type);

struct decode_ctx;
struct encode_ctx;
struct transcode_ctx
{
  struct decode_ctx *decode_ctx;
  struct encode_ctx *encode_ctx;
};

struct transcode_evbuf_io
{
  struct evbuffer *evbuf;

  // Set to null if no seek support required
  transcode_seekfn seekfn;
  void *seekfn_arg;
};

struct transcode_decode_setup_args
{
  enum transcode_profile profile;
  struct media_quality *quality;
  bool is_http;
  uint32_t len_ms;

  // Source must be either of these
  const char *path;
  struct transcode_evbuf_io *evbuf_io;
};

struct transcode_encode_setup_args
{
  enum transcode_profile profile;
  struct media_quality *quality;
  struct decode_ctx *src_ctx;
  struct transcode_evbuf_io *evbuf_io;
  struct evbuffer *prepared_header;
  int width;
  int height;
};

struct transcode_metadata_string
{
  char *type;
  char *codectype;
  char *description;
  char file_size[64];
  char bitrate[32];
};


// Setting up
struct decode_ctx *
transcode_decode_setup(struct transcode_decode_setup_args args);

struct encode_ctx *
transcode_encode_setup(struct transcode_encode_setup_args args);

struct transcode_ctx *
transcode_setup(struct transcode_decode_setup_args decode_args, struct transcode_encode_setup_args encode_args);

struct decode_ctx *
transcode_decode_setup_raw(enum transcode_profile profile, struct media_quality *quality);

enum transcode_profile
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype);

// Cleaning up
void
transcode_decode_cleanup(struct decode_ctx **ctx);

void
transcode_encode_cleanup(struct encode_ctx **ctx);

void
transcode_cleanup(struct transcode_ctx **ctx);

// Transcoding

/* Demuxes and decodes the next packet from the input.
 *
 * @out frame      A pointer to the frame. Caller should not free it, that will
 *                 be done by the next call to the function or by the cleanup
 *                 function.
 * @in  ctx        Decode context
 * @return         Positive if OK, negative if error, 0 if EOF
 */
int
transcode_decode(transcode_frame **frame, struct decode_ctx *ctx);

/* Encodes and remuxes a frame. Also resamples if needed.
 *
 * @out evbuf      An evbuffer filled with remuxed data
 * @in  ctx        Encode context
 * @in  frame      The decoded frame to encode, e.g. from transcode_decode
 * @in  eof        If true the muxer will write a trailer to the output
 * @return         Bytes added if OK, negative if error
 */
int
transcode_encode(struct evbuffer *evbuf, struct encode_ctx *ctx, transcode_frame *frame, int eof);

/* Demuxes, decodes, encodes and remuxes from the input.
 *
 * @out evbuf      An evbuffer filled with remuxed data
 * @out icy_timer  True if METADATA_ICY_INTERVAL has elapsed
 * @in  ctx        Transcode context
 * @in  want_bytes Minimum number of bytes the caller wants added to the evbuffer
 *                 - set want_bytes to 0 to transcode everything until EOF/error
 *                 - set want_bytes to 1 to get one encoded packet
 * @return         Bytes added if OK, negative if error, 0 if EOF
 */
int
transcode(struct evbuffer *evbuf, int *icy_timer, struct transcode_ctx *ctx, int want_bytes);

/* Converts a buffer with raw data to a frame that can be passed directly to the
 * transcode_encode() function. It does not copy, so if you free the data the
 * frame will become invalid.
 *
 * @in  data       Buffer with raw data
 * @in  size       Size of buffer
 * @in  nsamples   Number of samples in the buffer
 * @in  quality    Sample rate, bits per sample and channels
 * @return         Opaque pointer to frame if OK, otherwise NULL
 */
transcode_frame *
transcode_frame_new(void *data, size_t size, int nsamples, struct media_quality *quality);
void
transcode_frame_free(transcode_frame *frame);

/* Seek to the specified position - next transcode() will return this packet
 *
 * @in  ctx        Transcode context
 * @in  seek       Requested seek position in ms
 * @return         Negative if error, otherwise actual seek position
 */
int
transcode_seek(struct transcode_ctx *ctx, int ms);

/* Query for information about a media file opened by transcode_decode_setup()
 *
 * @in  ctx        Decode context
 * @in  query      Query - see implementation for supported queries
 * @return         Negative if error, otherwise query dependent
 */
int
transcode_decode_query(struct decode_ctx *ctx, const char *query);

/* Query for information (e.g. sample rate) about the output being produced by
 * the transcoding
 *
 * @in  ctx        Encode context
 * @in  query      Query - see implementation for supported queries
 * @return         Negative if error, otherwise query dependent
 */
int
transcode_encode_query(struct encode_ctx *ctx, const char *query);

// Metadata
struct http_icy_metadata *
transcode_metadata(struct transcode_ctx *ctx, int *changed);

/* When transcoding, we are in essence serving a different source file than the
 * original to the client. So we can't serve some of the file metadata from the
 * filescanner. This function creates strings to be used for override.
 *
 * @out s          Structure with (non-allocated) strings
 * @in  profile    Transcoding profile
 * @in  q          Transcoding quality
 * @in  len_ms     Length of source track
 */
void
transcode_metadata_strings_set(struct transcode_metadata_string *s, enum transcode_profile profile, struct media_quality *q, uint32_t len_ms);

/* Creates a header for later transcoding of a source file. This header can be
 * given to transcode_encode_setup which in some cases will make it faster (MP4)
 *
 * @out header     An evbuffer with the header
 * @in  profile    Transcoding profile
 * @in  path       Path to the source file
 * @return         Negative if error, otherwise zero
 */
int
transcode_prepare_header(struct evbuffer **header, enum transcode_profile profile, const char *path);

#endif /* !__TRANSCODE_H__ */
