
#ifndef __TRANSCODE_H__
#define __TRANSCODE_H__

#include <event2/buffer.h>
#include "db.h"
#include "http.h"

enum transcode_profile
{
  // Transcodes the best audio stream into PCM16 (does not add wav header)
  XCODE_PCM16_NOHEADER,
  // Transcodes the best audio stream into PCM16 (with wav header)
  XCODE_PCM16_HEADER,
  // Transcodes the best audio stream into MP3
  XCODE_MP3,
  // Transcodes the best video stream into JPEG/PNG
  XCODE_JPEG,
  XCODE_PNG,
};

struct decode_ctx;
struct encode_ctx;
struct transcode_ctx;
struct decoded_frame;

// Setting up
struct decode_ctx *
transcode_decode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length);

struct encode_ctx *
transcode_encode_setup(enum transcode_profile profile, struct decode_ctx *src_ctx, off_t *est_size);

struct transcode_ctx *
transcode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length, off_t *est_size);

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

#endif /* !__TRANSCODE_H__ */
