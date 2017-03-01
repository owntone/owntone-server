
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

// Setting up
struct decode_ctx *
transcode_decode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, struct evbuffer *evbuf, uint32_t song_length);

struct encode_ctx *
transcode_encode_setup(enum transcode_profile profile, struct decode_ctx *src_ctx, off_t *est_size, int width, int height);

struct transcode_ctx *
transcode_setup(enum transcode_profile profile, enum data_kind data_kind, const char *path, uint32_t song_length, off_t *est_size);

struct decode_ctx *
transcode_decode_setup_raw(void);

int
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
transcode_decode(void **frame, struct decode_ctx *ctx);

/* Encodes and remuxes a frame. Also resamples if needed.
 *
 * @out evbuf      An evbuffer filled with remuxed data
 * @in  ctx        Encode context
 * @in  frame      The decoded frame to encode, e.g. from transcode_decode
 * @in  eof        If true the muxer will write a trailer to the output
 * @return         Bytes added if OK, negative if error
 */
int
transcode_encode(struct evbuffer *evbuf, struct encode_ctx *ctx, void *frame, int eof);

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
 * transcode_encode() function
 *
 * @in  profile    Tells the function what kind of frame to create
 * @in  data       Buffer with raw data
 * @in  size       Size of buffer
 * @return         Opaque pointer to frame if OK, otherwise NULL
 */
void *
transcode_frame_new(enum transcode_profile profile, uint8_t *data, size_t size);
void
transcode_frame_free(void *frame);

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

// Metadata
struct http_icy_metadata *
transcode_metadata(struct transcode_ctx *ctx, int *changed);

#endif /* !__TRANSCODE_H__ */
