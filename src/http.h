
#ifndef __HTTP_H__
#define __HTTP_H__

#include <event2/buffer.h>
#include <event2/http.h>
#include "misc.h"

#include <libavformat/avformat.h>

struct http_client_ctx
{
  const char *url;

  /* A keyval/evbuf to store response headers and body.
   * Can be set to NULL to ignore that part of the response.
   */
  struct keyval *headers;
  struct evbuffer *body;

  /* Cut the connection after the headers have been received
   * Used for getting Shoutcast/ICY headers for old versions of libav/ffmpeg
   * (requires libevent 1 or 2.1.4+)
   */
  int headers_only;

  /* Private */
  int ret;
  void *evbase;
};

struct http_icy_metadata
{
  uint32_t id;

  /* Static stream metadata from icy_metadata_headers */
  char *name;
  char *description;
  char *genre;

  /* Track specific, comes from icy_metadata_packet */
  char *title;
  char *artist;
  char *artwork_url;

  uint32_t hash;
};


/* Generic HTTP client. No support for https.
 *
 * @param ctx HTTP request params, see above
 * @return 0 if successful, -1 if an error occurred
 */
int
http_client_request(struct http_client_ctx *ctx);


/* Returns a newly allocated string with the first stream in the m3u given in
 * url. If url is not a m3u, the string will be a copy of url.
 *
 * @param stream the newly allocated string with link to stream (NULL on error)
 * @param url link to either stream or m3u
 * @return 0 if successful, -1 if an error occurred
 */
int
http_stream_setup(char **stream, const char *url);


/* Extracts ICY header and packet metadata (requires libav 10)
 *
 *   example header metadata (standard http header format):
 *     icy-name: Rock On Radio
 *   example packet metadata (track currently being played):
 *     StreamTitle='Robert Miles - Black Rubber';StreamUrl='';
 *
 * The extraction is straight from the stream and done in the player thread, so
 * it must not produce significant delay.
 *
 * @param fmtctx the libav/ffmpeg AVFormatContext containing the stream
 * @param packet_only only get currently playing info (see struct above)
 * @return metadata struct if successful, NULL on error or nothing found
 */
struct http_icy_metadata *
http_icy_metadata_get(AVFormatContext *fmtctx, int packet_only);


/* Frees an ICY metadata struct
 *
 * @param metadata struct to free
 * @param content_only just free content, not the struct
 */
void
http_icy_metadata_free(struct http_icy_metadata *metadata, int content_only);

#endif /* !__HTTP_H__ */
