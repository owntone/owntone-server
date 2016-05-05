
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include <event2/buffer.h>
#include "db.h"
#include "http.h"

#define XCODE_WAVHEADER (1 << 14)
#define XCODE_HAS_VIDEO (1 << 15)

enum transcode_profile
{
  // Transcodes the best available audio stream into PCM16 (does not add wav header)
  XCODE_PCM16_NOHEADER = 1,
  // Transcodes the best available audio stream into PCM16 (with wav header)
  XCODE_PCM16_HEADER   = XCODE_WAVHEADER | 2,
  // Transcodes the best available audio stream into MP3
  XCODE_MP3            = 3,
  // Transcodes video + audio + subtitle streams (not tested - for future use)
  XCODE_H264_AAC       = XCODE_HAS_VIDEO | 4,
};

struct decode_ctx;
struct encode_ctx;
struct transcode_ctx;
struct decoded_frame;

// Setting up
struct decode_ctx *
transcode_decode_setup(struct media_file_info *mfi, int decode_video);

struct encode_ctx *
transcode_encode_setup(struct decode_ctx *src_ctx, enum transcode_profile profile, off_t *est_size);

struct transcode_ctx *
transcode_setup(struct media_file_info *mfi, enum transcode_profile profile, off_t *est_size);

struct decode_ctx *
transcode_decode_setup_raw(void);

int
transcode_needed(const char *user_agent, const char *client_codecs, char *file_codectype);

// Cleaning up
void
transcode_decode_cleanup(struct decode_ctx *ctx);

void
transcode_encode_cleanup(struct encode_ctx *ctx);

void
transcode_cleanup(struct transcode_ctx *ctx);

void
transcode_decoded_free(struct decoded_frame *decoded);

// Transcoding

/* Demuxes and decodes the next packet from the input.
 *
 * @out decoded   A newly allocated struct with a pointer to the frame and the
 *                stream. Must be freed with transcode_decoded_free().
 * @in  ctx       Decode context
 * @return        Positive if OK, negative if error, 0 if EOF
 */
int
transcode_decode(struct decoded_frame **decoded, struct decode_ctx *ctx);

/* Encodes and remuxes a frame. Also resamples if needed.
 *
 * @out evbuf     An evbuffer filled with remuxed data
 * @in  frame     The frame to encode, e.g. from transcode_decode
 * @in  wanted    Bytes that the caller wants processed
 * @in  ctx       Encode context
 * @return        Length of evbuf if OK, negative if error
 */
int
transcode_encode(struct evbuffer *evbuf, struct decoded_frame *decoded, struct encode_ctx *ctx);

/* Demuxes, decodes, encodes and remuxes the next packet from the input.
 *
 * @out evbuf     An evbuffer filled with remuxed data
 * @in  wanted    Bytes that the caller wants processed
 * @in  ctx       Transcode context
 * @out icy_timer True if METADATA_ICY_INTERVAL has elapsed
 * @return        Bytes processed if OK, negative if error, 0 if EOF
 */
int
transcode(struct evbuffer *evbuf, int wanted, struct transcode_ctx *ctx, int *icy_timer);

struct decoded_frame *
transcode_raw2frame(uint8_t *data, size_t size);

// Seeking
int
transcode_seek(struct transcode_ctx *ctx, int ms);

// Metadata
struct http_icy_metadata *
transcode_metadata(struct transcode_ctx *ctx, int *changed);

char *
transcode_metadata_artwork_url(struct transcode_ctx *ctx);

#endif /* !__TRANSCODE_H__ */
