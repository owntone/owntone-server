/*
 * Copyright (C) 2015-2020 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2010-2011 Julien BLACHE <jb@jblache.org>
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
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>

#include "db.h"
#include "misc.h"
#include "misc_json.h"
#include "logger.h"
#include "conffile.h"
#include "settings.h"
#include "cache.h"
#include "http.h"
#include "transcode.h"

#include "artwork.h"

#ifdef SPOTIFY
# include "library/spotify_webapi.h"
#endif

/* This artwork module will look for artwork by consulting a set of sources one
 * at a time. A source is for instance the local library, the cache or a cover
 * art database. For each source there is a handler function, which will do the
 * actual work of getting the artwork.
 *
 * There are two types of handlers: item and group. Item handlers are capable of
 * finding artwork for a single item (a dbmfi), while group handlers can get for
 * an album or artist (a persistentid).
 *
 * An artwork source handler must return one of the following:
 *
 *   ART_FMT_XXXX (positive)  An image, see possible formats in artwork.h
 *   ART_E_NONE (zero)        No artwork found
 *   ART_E_ERROR (negative)   An error occurred while searching for artwork
 *   ART_E_ABORT (negative)   Caller should abort artwork search (may be returned by cache)
 */
#define ART_E_NONE 0
#define ART_E_ERROR -1
#define ART_E_ABORT -2

// See online_source_is_failing()
#define ONLINE_SEARCH_COOLDOWN_TIME 3600
#define ONLINE_SEARCH_FAILURES_MAX 3

enum artwork_cache
{
  NEVER = 0,       // No caching of any results
  ON_SUCCESS = 1,  // Cache if artwork found
  ON_FAILURE = 2,  // Cache if artwork not found (so we don't keep asking)
};

// Input data to handlers, requested width, height and format. Can be set to
// zero then source is returned.
struct artwork_req_params {
  int max_w;
  int max_h;
  int format;
};

/* This struct contains the data available to the handler, as well as a char
 * buffer where the handler should output the path to the artwork (if it is
 * local - otherwise the buffer can be left empty). The purpose of supplying the
 * path is that the filescanner can then clear the cache in case the file
 * changes.
 */
struct artwork_ctx {
  // Handler should output path or URL to artwork here
  char path[PATH_MAX];
  // Handler should output artwork data to this evbuffer
  struct evbuffer *evbuf;

  // Requested size and format
  struct artwork_req_params req_params;

  // Input data to handler, did user configure to look for individual artwork
  int individual;

  // Input data for item handlers
  struct db_media_file_info *dbmfi;
  int id;
  uint32_t data_kind;
  uint32_t media_kind;
  // Input data for group handlers
  int64_t persistentid;

  // Not to be used by handler - query for item or group
  struct query_params qp;
  // Not to be used by handler - should the result be cached
  enum artwork_cache cache;
};

/* Definition of an artwork source. Covers both item and group sources.
 */
struct artwork_source {
  // Name of the source, e.g. "cache"
  const char *name;

  // The handler
  int (*handler)(struct artwork_ctx *ctx);

  // data_kinds the handler can work with, combined with (1 << A) | (1 << B)
  uint32_t data_kinds;

  // media_kinds the handler supports, combined with A | B
  uint32_t media_kinds;

  // When should results from the source be cached?
  enum artwork_cache cache;
};

/* Since online sources of artwork have similar characteristics there generic
 * callers for them. They use the below info to request artwork.
 */
enum parse_result {
  ONLINE_SOURCE_PARSE_OK,
  ONLINE_SOURCE_PARSE_INVALID,
  ONLINE_SOURCE_PARSE_NOT_FOUND,
  ONLINE_SOURCE_PARSE_NO_PARSER,
};

struct online_source {
  // Name of online source
  const char *name;
  const char *setting_name;

  // How to authorize (using the Authorize http header)
  const char *auth_header;
  const char *auth_key;
  const char *auth_secret;

  // How to search for artwork
  const char *search_endpoint;
  const char *search_param;
  struct {
    const char *key;
    const char *template;
  } query_parts[8];

  // Remember previous artwork searches, used to avoid futile requests
  struct {
    pthread_mutex_t mutex;
    int last_id;
    uint32_t last_hash;
    int last_max_w;
    int last_max_h;
    int last_response_code;
    char *last_artwork_url;
    time_t last_timestamp;
    int count_failures;
  } search_history;

  // Function that can extract the artwork url from the parsed json response
  enum parse_result (*response_jparse)(char **artwork_url, json_object *response, int max_w, int max_h);
};

/* File extensions that we look for or accept
 */
static const char *cover_extension[] =
  {
    "jpg", "png",
  };

/* ----------------- DECLARE AND CONFIGURE SOURCE HANDLERS ----------------- */

/* Forward - group handlers */
static int source_group_cache_get(struct artwork_ctx *ctx);
static int source_group_dir_get(struct artwork_ctx *ctx);
/* Forward - item handlers */
static int source_item_cache_get(struct artwork_ctx *ctx);
static int source_item_embedded_get(struct artwork_ctx *ctx);
static int source_item_own_get(struct artwork_ctx *ctx);
static int source_item_artwork_url_get(struct artwork_ctx *ctx);
static int source_item_pipe_get(struct artwork_ctx *ctx);
static int source_item_spotifywebapi_track_get(struct artwork_ctx *ctx);
static int source_item_ownpl_get(struct artwork_ctx *ctx);
static int source_item_spotifywebapi_search_get(struct artwork_ctx *ctx);
static int source_item_discogs_get(struct artwork_ctx *ctx);
static int source_item_coverartarchive_get(struct artwork_ctx *ctx);

/* List of sources that can provide artwork for a group (i.e. usually an album
 * identified by a persistentid). The source handlers will be called in the
 * order of this list. Must be terminated by a NULL struct.
 */
static struct artwork_source artwork_group_source[] =
  {
    {
      .name = "cache",
      .handler = source_group_cache_get,
      .cache = ON_FAILURE,
    },
    {
      .name = "directory",
      .handler = source_group_dir_get,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = NULL,
      .handler = NULL,
      .cache = 0,
    }
  };

/* List of sources that can provide artwork for an item (a track characterized
 * by a dbmfi). The source handlers will be called in the order of this list.
 * The handler will only be called if the data_kind matches. Must be terminated
 * by a NULL struct.
 */
static struct artwork_source artwork_item_source[] =
  {
    {
      .name = "cache",
      .handler = source_item_cache_get,
      .data_kinds = (1 << DATA_KIND_FILE) | (1 << DATA_KIND_SPOTIFY),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = ON_FAILURE,
    },
    {
      .name = "embedded",
      .handler = source_item_embedded_get,
      .data_kinds = (1 << DATA_KIND_FILE) | (1 << DATA_KIND_HTTP),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "own",
      .handler = source_item_own_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "stream",
      .handler = source_item_artwork_url_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = NEVER,
    },
    {
      .name = "pipe",
      .handler = source_item_pipe_get,
      .data_kinds = (1 << DATA_KIND_PIPE),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = NEVER,
    },
    {
      .name = "Spotify track web api",
      .handler = source_item_spotifywebapi_track_get,
      .data_kinds = (1 << DATA_KIND_SPOTIFY),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      // Note that even though caching is set for this handler, it will in most
      // cases not happen because source_item_artwork_url_get() comes before
      // and has NEVER. This is intentional because this handler is also a
      // backup for when we don't get anything from the stream.
      .name = "playlist own",
      .handler = source_item_ownpl_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .media_kinds = MEDIA_KIND_ALL,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "Spotify search web api (files)",
      .handler = source_item_spotifywebapi_search_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "Spotify search web api (streams)",
      .handler = source_item_spotifywebapi_search_get,
      .data_kinds = (1 << DATA_KIND_HTTP) | (1 << DATA_KIND_PIPE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = NEVER,
    },
    {
      .name = "Discogs (files)",
      .handler = source_item_discogs_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "Discogs (streams)",
      .handler = source_item_discogs_get,
      .data_kinds = (1 << DATA_KIND_HTTP) | (1 << DATA_KIND_PIPE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = NEVER,
    },
    {
      // The Cover Art Archive seems rather slow, so low priority
      .name = "Cover Art Archive (files)",
      .handler = source_item_coverartarchive_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      // The Cover Art Archive seems rather slow, so low priority
      .name = "Cover Art Archive (streams)",
      .handler = source_item_coverartarchive_get,
      .data_kinds = (1 << DATA_KIND_HTTP) | (1 << DATA_KIND_PIPE),
      .media_kinds = MEDIA_KIND_MUSIC,
      .cache = NEVER,
    },
    {
      .name = NULL,
      .handler = NULL,
      .data_kinds = 0,
      .cache = 0,
    }
  };

/* Forward - parsers of online source responses */
static enum parse_result response_jparse_spotify(char **artwork_url, json_object *response, int max_w, int max_h);
static enum parse_result response_jparse_discogs(char **artwork_url, json_object *response, int max_w, int max_h);
static enum parse_result response_jparse_musicbrainz(char **artwork_url, json_object *response, int max_w, int max_h);

/* Definitions of online sources */
static struct online_source spotify_source =
  {
    .name = "Spotify",
    .setting_name = "use_artwork_source_spotify",
    .auth_header = "Bearer $SECRET$",
    .search_endpoint = "https://api.spotify.com/v1/search",
    .search_param = "type=track&limit=1&$QUERY$",
    .query_parts =
      {
	{ "q", "artist:$ARTIST$ album:$ALBUM$" },
	{ "q", "artist:$ARTIST$ track:$TITLE$" },
	{ NULL, NULL },
      },
    .response_jparse = response_jparse_spotify,
    .search_history = { .mutex = PTHREAD_MUTEX_INITIALIZER },
  };

static struct online_source discogs_source =
  {
    .name = "Discogs",
    .setting_name = "use_artwork_source_discogs",
    .auth_header = "Discogs key=$KEY$, secret=$SECRET$",
    .auth_key = "ivzUxlkUiwpptDKpSCHF",
    .auth_secret = "CYLZyExtlznKCupoIIhTpHVDReLunhUo",
    .search_endpoint = "https://api.discogs.com/database/search",
    .search_param = "type=release&per_page=1&$QUERY$",
    .query_parts =
      {
	{ "artist", "$ARTIST$" },
	{ "release_title", "$ALBUM$" },
	{ "track", "$TITLE$" },
	{ NULL, NULL },
      },
    .response_jparse = response_jparse_discogs,
    .search_history = { .mutex = PTHREAD_MUTEX_INITIALIZER },
  };

static struct online_source musicbrainz_source =
  {
    .name = "Musicbrainz",
    .setting_name = "use_artwork_source_coverartarchive",
    .search_endpoint = "http://musicbrainz.org/ws/2/release-group/",
    .search_param = "limit=1&fmt=json&$QUERY$",
    .query_parts =
      {
	{ "query", "artist:$ARTIST$ AND release:$ALBUM$ AND status:Official" },
	{ "query", "artist:$ARTIST$ AND title:$TITLE$ AND status:Official" },
	{ NULL, NULL },
      },
    .response_jparse = response_jparse_musicbrainz,
    .search_history = { .mutex = PTHREAD_MUTEX_INITIALIZER },
  };



/* -------------------------------- HELPERS -------------------------------- */

/* Reads an artwork file from the given http url straight into an evbuf
 *
 * @out evbuf     Image data
 * @in  url       URL for the image
 * @return        ART_FMT_* on success, ART_E_NONE on 404, ART_E_ERROR otherwise
 */
static int
artwork_read_byurl(struct evbuffer *evbuf, const char *url)
{
  struct http_client_ctx client;
  struct keyval *kv;
  const char *content_type;
  size_t len;
  int format;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying internet artwork in %s\n", url);

  format = ART_E_ERROR;
  CHECK_NULL(L_ART, kv = keyval_alloc());

  len = strlen(url);
  if ((len < 14) || (len > PATH_MAX)) // Can't be shorter than http://a/1.jpg
    {
      DPRINTF(E_LOG, L_ART, "Artwork request URL is invalid (len=%zu): '%s'\n", len, url);
      goto out;
    }

  memset(&client, 0, sizeof(struct http_client_ctx));
  client.url = url;
  client.input_headers = kv;
  client.input_body = evbuf;

  ret = http_client_request(&client);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Request to '%s' failed with return value %d\n", url, ret);
      goto out;
    }

  if (client.response_code == HTTP_NOTFOUND)
    {
      DPRINTF(E_INFO, L_ART, "No artwork found at '%s' (code %d)\n", url, client.response_code);
      format = ART_E_NONE;
      goto out;
    }
  else if (client.response_code != HTTP_OK)
    {
      DPRINTF(E_LOG, L_ART, "Request to '%s' failed with code %d\n", url, client.response_code);
      goto out;
    }

  content_type = keyval_get(kv, "Content-Type");
  if (content_type && (strcasecmp(content_type, "image/jpeg") == 0 || strcasecmp(content_type, "image/jpg") == 0))
    format = ART_FMT_JPEG;
  else if (content_type && strcasecmp(content_type, "image/png") == 0)
    format = ART_FMT_PNG;
  else
    DPRINTF(E_LOG, L_ART, "Artwork from '%s' has no known content type\n", url);

 out:
  keyval_clear(kv);
  free(kv);
  return format;
}

/* Reads an artwork file from the filesystem straight into an evbuf
 * TODO Use evbuffer_add_file or evbuffer_read?
 *
 * @out evbuf     Image data
 * @in  path      Path to the artwork
 * @return        0 on success, -1 on error
 */
static int
artwork_read_bypath(struct evbuffer *evbuf, char *path)
{
  uint8_t buf[4096];
  struct stat sb;
  int fd;
  int ret;

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not open artwork file '%s': %s\n", path, strerror(errno));

      return -1;
    }

  ret = fstat(fd, &sb);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Could not stat() artwork file '%s': %s\n", path, strerror(errno));

      goto out_fail;
    }

  ret = evbuffer_expand(evbuf, sb.st_size);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for artwork\n");

      goto out_fail;
    }

  while ((ret = read(fd, buf, sizeof(buf))) > 0)
    evbuffer_add(evbuf, buf, ret);

  close(fd);

  return 0;

 out_fail:
  close(fd);
  return -1;
}

/* Calculates new size if the source image will not fit inside the requested
 * size. If the original fits then dst_w/h will equal src_w/h.
 *
 * @out dst_w  Rescaled width
 * @out dst_h  Rescaled height
 * @in  src_w  Actual width
 * @in  src_h  Actual height
 * @in  max_w  Requested width
 * @in  max_h  Requested height
 */
static void
size_calculate(int *dst_w, int *dst_h, int src_w, int src_h, int max_w, int max_h)
{
  DPRINTF(E_DBG, L_ART, "Original image dimensions: w %d h %d\n", src_w, src_h);

  *dst_w = src_w;
  *dst_h = src_h;

  // No valid target dimensions, use original
  if ((max_w <= 0) || (max_h <= 0))
    return;

  // Smaller than target, use original
  if ((src_w <= max_w) && (src_h <= max_h))
    return;

  // Wider aspect ratio than target
  if (src_w * max_h > src_h * max_w)
    {
      *dst_w = max_w;
      *dst_h = (double)max_w * ((double)src_h / (double)src_w);
    }
  // Taller or equal aspect ratio
  else
    {
      *dst_w = (double)max_h * ((double)src_w / (double)src_h);
      *dst_h = max_h;
    }

  if (*dst_h > max_h)
    *dst_h = max_h;

  // PNG prefers even row count
  *dst_w += *dst_w % 2;

  if (*dst_w > max_w)
    *dst_w = max_w - (max_w % 2);

  DPRINTF(E_DBG, L_ART, "Rescale required, destination width %d height %d\n", *dst_w, *dst_h);
}

#ifdef HAVE_LIBEVENT2_OLD
// This is not how this function is actually defined in libevent 2.1+, but it
// works as a less optimal stand-in
int
evbuffer_add_buffer_reference(struct evbuffer *outbuf, struct evbuffer *inbuf)
{
  uint8_t *buf = evbuffer_pullup(inbuf, -1);
  if (!buf)
    return -1;

  return evbuffer_add_reference(outbuf, buf, evbuffer_get_length(inbuf), NULL, NULL);
}
#endif

/*
 * Either gets the artwork file given in "path" (rescaled if needed) or rescales
 * the artwork given in "inbuf".
 *
 * @out evbuf        Image data (rescaled if needed)
 * @in  path         Path to the artwork file (alternative to inbuf)
 * @in  in_buf       Buffer with the artwork (alternative to path)
 * @in  is_embedded  Whether the artwork in file is embedded or raw jpeg/png
 * @in  data_kind    Used by the transcode module to determine e.g. probe size
 * @in  req_params   Requested max size/format
 * @return           ART_FMT_* on success, ART_E_ERROR on error
 */
static int
artwork_get(struct evbuffer *evbuf, char *path, struct evbuffer *in_buf, bool is_embedded, enum data_kind data_kind, struct artwork_req_params req_params)
{
  struct decode_ctx *xcode_decode = NULL;
  struct encode_ctx *xcode_encode = NULL;
  struct evbuffer *xcode_buf = NULL;
  void *frame;
  int src_width;
  int src_height;
  int src_format;
  int dst_width;
  int dst_height;
  int dst_format;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Getting artwork (max destination width %d height %d)\n", req_params.max_w, req_params.max_h);

  // At this point we don't know if we will need to rescale/reformat, and we
  // won't know until probing the source (which the transcode module does). The
  // act of probing uses evbuffer_remove(), thus consuming some of the buffer.
  // So that means that if rescaling/reformating turns out not to be required,
  // we could no longer just add in_buf to the evbuf buffer and return to the
  // caller. The below makes that possible (with no copying).
  if (in_buf)
    {
      CHECK_NULL(L_ART, xcode_buf = evbuffer_new());
      ret = evbuffer_add_buffer_reference(xcode_buf, in_buf);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "Could not copy/ref raw image for rescaling (ret=%d)\n", ret);
	  ret = ART_E_ERROR;
	  goto out;
	}
    }

  xcode_decode = transcode_decode_setup(XCODE_JPEG, NULL, data_kind, path, xcode_buf, 0); // Covers XCODE_PNG too
  if (!xcode_decode)
    {
      if (path)
	DPRINTF(E_DBG, L_ART, "No artwork found in '%s'\n", path);
      else
	DPRINTF(E_DBG, L_ART, "No artwork provided to artwork_get()\n");

      ret = ART_E_NONE;
      goto out;
    }

  // Determine source and destination format
  if (transcode_decode_query(xcode_decode, "is_jpeg"))
    src_format = ART_FMT_JPEG;
  else if (transcode_decode_query(xcode_decode, "is_png"))
    src_format = ART_FMT_PNG;
  else
    {
      if (path)
	DPRINTF(E_DBG, L_ART, "File '%s' has no PNG or JPEG artwork\n", path);
      else
	DPRINTF(E_LOG, L_ART, "Artwork data provided to artwork_get() is not PNG or JPEG\n");

      ret = ART_E_ERROR;
      goto out;
    }

  dst_format = req_params.format ? req_params.format : src_format;

  // Determine source and destination size
  src_width = transcode_decode_query(xcode_decode, "width");
  src_height = transcode_decode_query(xcode_decode, "height");
  if (src_width <= 0 || src_height <= 0)
    {
      if (path)
	DPRINTF(E_DBG, L_ART, "File '%s' has unknown artwork dimensions\n", path);
      else
	DPRINTF(E_LOG, L_ART, "Artwork data provided to artwork_get() has unknown dimensions\n");

      ret = ART_E_ERROR;
      goto out;
    }

  size_calculate(&dst_width, &dst_height, src_width, src_height, req_params.max_w, req_params.max_h);

  // Fast path. Won't work for embedded, since we need to extract the image from
  // the file.
  if (!is_embedded && dst_format == src_format && dst_width == src_width && dst_height == src_height)
    {
      if (path)
	ret = artwork_read_bypath(evbuf, path);
      else
        ret = evbuffer_add_buffer(evbuf, in_buf);

      ret = (ret < 0) ? ART_E_ERROR : src_format;
      goto out;
    }

  if (dst_format == ART_FMT_JPEG)
    xcode_encode = transcode_encode_setup(XCODE_JPEG, NULL, xcode_decode, NULL, dst_width, dst_height);
  else if (dst_format == ART_FMT_PNG)
    xcode_encode = transcode_encode_setup(XCODE_PNG, NULL, xcode_decode, NULL, dst_width, dst_height);
  else if (dst_format == ART_FMT_VP8)
    xcode_encode = transcode_encode_setup(XCODE_VP8, NULL, xcode_decode, NULL, dst_width, dst_height);
  else
    xcode_encode = transcode_encode_setup(XCODE_JPEG, NULL, xcode_decode, NULL, dst_width, dst_height);

  if (!xcode_encode)
    {
      if (path)
	DPRINTF(E_WARN, L_ART, "Error preparing rescaling of '%s'\n", path);
      else
	DPRINTF(E_WARN, L_ART, "Error preparing rescaling of artwork data\n");

      ret = ART_E_ERROR;
      goto out;
    }

  // We don't use transcode() because we just want to process one frame
  ret = transcode_decode(&frame, xcode_decode);
  if (ret < 0)
    {
      ret = ART_E_ERROR;
      goto out;
    }

  ret = transcode_encode(evbuf, xcode_encode, frame, 1);
  if (ret < 0)
    {
      evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
      ret = ART_E_ERROR;
      goto out;
    }

  ret = dst_format;

 out:
  transcode_encode_cleanup(&xcode_encode);
  transcode_decode_cleanup(&xcode_decode);

  if (xcode_buf)
    evbuffer_free(xcode_buf);

  return ret;
}

/*
 * Checks if an image file with one of the configured artwork_basenames exists in
 * the given directory "dir". Returns 0 if an image exists, -1 if no image was
 * found or an error occurred.
 *
 * If an image exists, "out_path" will contain the absolute path to this image.
 *
 * @param out_path If return value is 0, contains the absolute path to the image
 * @param len If return value is 0, contains the length of the absolute path
 * @param dir The directory to search
 * @return 0 if image exists, -1 otherwise
 */
static int
dir_image_find(char *out_path, size_t len, const char *dir)
{
  char path[PATH_MAX];
  int i;
  int j;
  int path_len;
  int ret;
  cfg_t *lib;
  int nbasenames;
  int nextensions;

  ret = snprintf(path, sizeof(path), "%s", dir);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", dir);
      return -1;
    }

  path_len = strlen(path);

  lib = cfg_getsec(cfg, "library");
  nbasenames = cfg_size(lib, "artwork_basenames");

  if (nbasenames == 0)
    return -1;

  nextensions = ARRAY_SIZE(cover_extension);

  for (i = 0; i < nbasenames; i++)
    {
      for (j = 0; j < nextensions; j++)
	{
	  ret = snprintf(path + path_len, sizeof(path) - path_len, "/%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(path) - path_len))
	    {
	      DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s/%s)\n", dir, cfg_getnstr(lib, "artwork_basenames", i));
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying directory artwork file %s\n", path);

	  ret = access(path, F_OK);
	  if (ret == 0)
	    {
	      snprintf(out_path, len, "%s", path);
	      return 0;
	    }
	}
    }

  return -1;
}

/*
 * Checks if an image file exists in the given directory "dir" with the basename
 * equal to the directory name. Returns 0 if an image exists, -1 if no image was
 * found or an error occurred.
 *
 * If an image exists, "out_path" will contain the absolute path to this image.
 *
 * @param out_path If return value is 0, contains the absolute path to the image
 * @param len If return value is 0, contains the length of the absolute path
 * @param dir The directory to search
 * @return 0 if image exists, -1 otherwise
 */
static int
parent_dir_image_find(char *out_path, size_t len, const char *dir)
{
  char path[PATH_MAX];
  char parentdir[PATH_MAX];
  char *ptr;
  int i;
  int nextensions;
  int path_len;
  int ret;

  ret = snprintf(path, sizeof(path), "%s", dir);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", dir);
      return -1;
    }

  ptr = strrchr(path, '/');
  if ((!ptr) || (strlen(ptr) <= 1))
    {
      DPRINTF(E_LOG, L_ART, "Could not find parent dir name (%s)\n", path);
      return -1;
    }
  strcpy(parentdir, ptr + 1);

  path_len = strlen(path);
  nextensions = ARRAY_SIZE(cover_extension);

  for (i = 0; i < nextensions; i++)
    {
      ret = snprintf(path + path_len, sizeof(path) - path_len, "/%s.%s", parentdir, cover_extension[i]);
      if ((ret < 0) || (ret >= sizeof(path) - path_len))
        {
	  DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", parentdir);
	  continue;
	}

      DPRINTF(E_SPAM, L_ART, "Trying parent directory artwork file %s\n", path);

      ret = access(path, F_OK);
      if (ret == 0)
	{
	  snprintf(out_path, len, "%s", path);
	  return 0;
	}
    }

  return -1;
}

/* Looks for an artwork file in a directory. Will rescale if needed.
 *
 * @out evbuf     Image data
 * @out out_path  Path to the artwork file if found, must be a char[PATH_MAX] buffer
 * @in  len       Max size of "out_path"
 * @in  dir       Directory to search
 * @in  req_params Requested max size/format
 * @return        ART_FMT_* on success, ART_E_NONE on nothing found, ART_E_ERROR on error
 */
static int
artwork_get_bydir(struct evbuffer *evbuf, char *out_path, size_t len, char *dir, struct artwork_req_params req_params)
{
  int ret;

  ret = dir_image_find(out_path, len, dir);
  if (ret >= 0)
    {
      return artwork_get(evbuf, out_path, NULL, false, DATA_KIND_FILE, req_params);
    }

  ret = parent_dir_image_find(out_path, len, dir);
  if (ret >= 0)
    {
      return artwork_get(evbuf, out_path, NULL, false, DATA_KIND_FILE, req_params);
    }

  return ART_E_NONE;
}

/* Retrieves artwork from an URL, will rescale if needed. Checks the cache stash
 * before making a request. Stashes result in cache, also if negative.
 *
 * @out artwork   Image data
 * @in  url       URL of the artwork
 * @in  req_params Requested max size/format
 * @return        ART_FMT_* on success, ART_E_NONE or ART_E_ERROR
 */
static int
artwork_get_byurl(struct evbuffer *artwork, const char *url, struct artwork_req_params req_params)
{
  struct evbuffer *raw;
  int format;
  int ret;

  CHECK_NULL(L_ART, raw = evbuffer_new());
  format = ART_E_ERROR;

  ret = cache_artwork_read(raw, url, &format);
  if (ret < 0)
    {
      format = artwork_read_byurl(raw, url);
      cache_artwork_stash(raw, url, format);
    }

  // If we couldn't read, or we have cached a negative result from the last attempt, we stop now
  if (format <= 0)
    goto out;

  // Takes care of resizing
  ret = artwork_get(artwork, NULL, raw, false, 0, req_params);
  if (ret < 0)
    format = ART_E_ERROR;

 out:
  evbuffer_free(raw);
  return format;
}

/* ------------------------- ONLINE SOURCE HANDLING  ----------------------- */

static enum parse_result
response_jparse_discogs(char **artwork_url, json_object *response, int max_w, int max_h)
{
  json_object *image;
  const char *s;
  const char *key;

  if ((max_w > 0 && max_w <= 150) || (max_h > 0 && max_h <= 150))
    key = "thumb";
  else
    key = "cover_image";

  image = JPARSE_SELECT(response, "results", key);
  if (!image || json_object_get_type(image) != json_type_string)
    return ONLINE_SOURCE_PARSE_NOT_FOUND;

  s = json_object_get_string(image);
  if (!s)
    return ONLINE_SOURCE_PARSE_INVALID;

  *artwork_url = strdup(s);

  return ONLINE_SOURCE_PARSE_OK;
}

static enum parse_result
response_jparse_musicbrainz(char **artwork_url, json_object *response, int max_w, int max_h)
{
  json_object *id;
  const char *s;

  id = JPARSE_SELECT(response, "release-groups", "id");
  if (!id || json_object_get_type(id) != json_type_string)
    return ONLINE_SOURCE_PARSE_NOT_FOUND;

  s = json_object_get_string(id);
  if (!s)
    return ONLINE_SOURCE_PARSE_INVALID;

  // We will request 500 as a default. The use of https is not just for privacy
  // it is also because the http client only supports redirects for https.
  if ((max_w > 0 && max_w <= 250) || (max_h > 0 && max_h <= 250))
    *artwork_url = safe_asprintf("https://coverartarchive.org/release-group/%s/front-250", s);
  else if ((max_w == 0 && max_h == 0) || (max_w <= 500 && max_h <= 500))
    *artwork_url = safe_asprintf("https://coverartarchive.org/release-group/%s/front-500", s);
  else
    *artwork_url = safe_asprintf("https://coverartarchive.org/release-group/%s/front-1200", s);

  return ONLINE_SOURCE_PARSE_OK;
}

static enum parse_result
response_jparse_spotify(char **artwork_url, json_object *response, int max_w, int max_h)
{
  json_object *images;
  json_object *image;
  const char *s;
  int image_count;
  int i;

  images = JPARSE_SELECT(response, "tracks", "items", "album", "images");
  if (!images || json_object_get_type(images) != json_type_array)
    return ONLINE_SOURCE_PARSE_NOT_FOUND;

  // Find first image that has a smaller width than the given max_w (this should
  // avoid the need for resizing and improve performance at the cost of some
  // quality loss). Note that Spotify returns the images ordered descending by
  // width (widest image first). Special case is if no max width (max_w = 0) is
  // given, then the widest images will be used.
  s = NULL;
  image_count = json_object_array_length(images);
  for (i = 0; i < image_count; i++)
    {
      image = json_object_array_get_idx(images, i);
      if (image)
	{
	  s = jparse_str_from_obj(image, "url");

	  if (max_w <= 0  || jparse_int_from_obj(image, "width") <= max_w)
	    {
	      // We have the first image that has a smaller width than the given max_w
	      break;
	    }
	}
    }

  if (!s)
    return ONLINE_SOURCE_PARSE_NOT_FOUND;

  *artwork_url = strdup(s);
  return ONLINE_SOURCE_PARSE_OK;
}

static enum parse_result
online_source_response_parse(char **artwork_url, struct online_source *src, struct evbuffer *response, int max_w, int max_h)
{
  json_object *jresponse;
  char *body;
  int ret;

  // 0-terminate for safety
  evbuffer_add(response, "", 1);
  body = (char *)evbuffer_pullup(response, -1);

  DPRINTF(E_SPAM, L_ART, "Response from '%s': %s\n", src->name, body);

  if (src->response_jparse)
    {
      jresponse = json_tokener_parse(body);
      if (!jresponse)
	return ONLINE_SOURCE_PARSE_INVALID;

      ret = src->response_jparse(artwork_url, jresponse, max_w, max_h);
      jparse_free(jresponse);
    }
  else
    ret = ONLINE_SOURCE_PARSE_NO_PARSER;

  return ret;
}

static int
online_source_request_url_make(char *url, size_t url_size, struct online_source *src, struct artwork_ctx *ctx)
{
  struct db_queue_item *queue_item;
  struct keyval query = { 0 };
  const char *artist = NULL;
  const char *album = NULL;
  const char *title = NULL;
  char param[512];
  char *encoded_query = NULL;
  int ret;
  int i;

  // First check if the item is in the queue. When searching for artwork, it is
  // better to use queue_item metadata. For stream items the queue metadata will
  // for instance be updated with icy metadata. It is also possible we are asked
  // for artwork for a non-library item.
  queue_item = db_queue_fetch_byfileid(ctx->id);
  if (queue_item && ctx->data_kind == DATA_KIND_HTTP)
    {
      // Normally we prefer searching by artist and album, but for streams we
      // take the below approach, since they have no album information, and the
      // title is in the album field
      artist = queue_item->artist;
      title = queue_item->album;
    }
  else if (queue_item)
    {
      artist = queue_item->artist;
      album = queue_item->album;
    }
  else
    {
      // We will just search for artist and album
      artist = ctx->dbmfi->artist;
      album = ctx->dbmfi->album;
    }

  if (!artist || (!album && !title))
    {
      DPRINTF(E_DBG, L_ART, "Cannot construct query to %s, missing input data (artist=%s, album=%s, title=%s)\n", src->name, artist, album, title);
      goto error;
    }

  for (i = 0; src->query_parts[i].key; i++)
    {
      if (!album && strstr(src->query_parts[i].template, "$ALBUM$"))
	continue;
      if (!title && strstr(src->query_parts[i].template, "$TITLE$"))
	continue;

      snprintf(param, sizeof(param), "%s", src->query_parts[i].template);
      if ((safe_snreplace(param, sizeof(param), "$ARTIST$", artist) < 0) ||
	  (safe_snreplace(param, sizeof(param), "$ALBUM$", album) < 0) ||
	  (safe_snreplace(param, sizeof(param), "$TITLE$", title) < 0))
	{
	  DPRINTF(E_WARN, L_ART, "Cannot make request for online artwork, query string is too long\n");
	  goto error;
	}

      ret = keyval_add(&query, src->query_parts[i].key, param);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "keyval_add() failed in request_url_make()\n");
	  goto error;
	}
    }

  encoded_query = http_form_urlencode(&query);
  if (!encoded_query)
    goto error;

  snprintf(url, url_size, "%s?%s", src->search_endpoint, src->search_param);
  if (safe_snreplace(url, url_size, "$QUERY$", encoded_query) < 0)
    {
      DPRINTF(E_WARN, L_ART, "Cannot make request for online artwork, url is too long (%zu)\n", strlen(encoded_query));
      goto error;
    }

  free(encoded_query);
  keyval_clear(&query);
  free_queue_item(queue_item, 0);

  return 0;

 error:
  free(encoded_query);
  keyval_clear(&query);
  free_queue_item(queue_item, 0);
  return -1;
}

static int
online_source_search_check_last(char **last_artwork_url, struct online_source *src, uint32_t hash, int max_w, int max_h)
{
  bool is_same;

  pthread_mutex_lock(&src->search_history.mutex);

  is_same = (hash == src->search_history.last_hash) &&
            (max_w == src->search_history.last_max_w) &&
            (max_h == src->search_history.last_max_h);

  // Copy this to the caller while we have the lock anyway
  if (is_same)
    *last_artwork_url = safe_strdup(src->search_history.last_artwork_url);

  pthread_mutex_unlock(&src->search_history.mutex);

  return is_same ? 0 : -1;
}

static bool
online_source_is_failing(struct online_source *src, int id)
{
  bool is_failing;

  pthread_mutex_lock(&src->search_history.mutex);

  // If the last request was more than ONLINE_SEARCH_COOLDOWN_TIME ago we will always try again
  if (time(NULL) > src->search_history.last_timestamp + ONLINE_SEARCH_COOLDOWN_TIME)
    is_failing = false;
  // We won't try again if the source was not replying as expected
  else if (src->search_history.last_response_code != HTTP_OK)
    is_failing = true;
  // The playback source has changed since the last search, let's give it a chance
  // (internet streams can feed us with garbage search metadata, but will not change id)
  else if (id != src->search_history.last_id)
    is_failing = false;
  // We allow up to ONLINE_SEARCH_FAILURES_MAX for the same track id before declaring failure
  else if (src->search_history.count_failures < ONLINE_SEARCH_FAILURES_MAX)
    is_failing = false;
  else
    is_failing = true;

  pthread_mutex_unlock(&src->search_history.mutex);

  return is_failing;
}

static void
online_source_history_update(struct online_source *src, int id, uint32_t request_hash, int response_code, const char *artwork_url)
{
  pthread_mutex_lock(&src->search_history.mutex);

  src->search_history.last_id = id;
  src->search_history.last_hash = request_hash;
  src->search_history.last_response_code = response_code;
  src->search_history.last_timestamp = time(NULL);

  free(src->search_history.last_artwork_url);
  src->search_history.last_artwork_url = safe_strdup(artwork_url); // FIXME should free this on exit

  if (artwork_url)
    src->search_history.count_failures = 0;
  else
    src->search_history.count_failures++;

  pthread_mutex_unlock(&src->search_history.mutex);
}

static char *
online_source_search(struct online_source *src, struct artwork_ctx *ctx)
{
  char *artwork_url;
  struct http_client_ctx client = { 0 };
  struct keyval output_headers = { 0 };
  uint32_t hash;
  char url[2048];
  char auth_header[256];
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying %s for %s\n", src->name, ctx->dbmfi->path);

  ret = online_source_request_url_make(url, sizeof(url), src, ctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "Skipping artwork source %s, could not construct a request URL\n", src->name);
      return NULL;
    }

  // Be nice to our peer + improve response times by not repeating search requests
  hash = djb_hash(url, strlen(url));
  ret = online_source_search_check_last(&artwork_url, src, hash, ctx->req_params.max_w, ctx->req_params.max_h);
  if (ret == 0)
    {
      return artwork_url; // Will be NULL if we are repeating a search that failed
    }

  // If our recent searches have been futile we may give the source a break
  if (online_source_is_failing(src, ctx->id))
    {
      DPRINTF(E_DBG, L_ART, "Skipping artwork source %s, too many failed requests\n", src->name);
      return NULL;
    }

  if (src->auth_header)
    {
      snprintf(auth_header, sizeof(auth_header), "%s", src->auth_header);
      if ((safe_snreplace(auth_header, sizeof(auth_header), "$KEY$", src->auth_key) < 0) ||
	  (safe_snreplace(auth_header, sizeof(auth_header), "$SECRET$", src->auth_secret) < 0))
	{
	  DPRINTF(E_WARN, L_ART, "Cannot make request for online artwork, auth header is too long\n");
	  return NULL;
	}

      keyval_add(&output_headers, "Authorization", auth_header);
    }

  CHECK_NULL(L_ART, client.input_body = evbuffer_new());
  client.url = url;
  client.output_headers = &output_headers;

  ret = http_client_request(&client);
  keyval_clear(&output_headers);
  if (ret < 0 || client.response_code != HTTP_OK)
    {
      DPRINTF(E_WARN, L_ART, "Artwork request to '%s' failed, response code %d\n", url, client.response_code);
      goto error;
    }

  ret = online_source_response_parse(&artwork_url, src, client.input_body, ctx->req_params.max_w, ctx->req_params.max_h);
  if (ret == ONLINE_SOURCE_PARSE_NOT_FOUND)
    DPRINTF(E_DBG, L_ART, "No image tag found in response from source '%s'\n", src->name);
  else if (ret == ONLINE_SOURCE_PARSE_INVALID)
    DPRINTF(E_WARN, L_ART, "Response from source '%s' was in an unexpected format\n", src->name);
  else if (ret == ONLINE_SOURCE_PARSE_NO_PARSER)
    DPRINTF(E_LOG, L_ART, "Bug! Cannot parse response from source '%s', parser missing\n", src->name);
  else if (ret != ONLINE_SOURCE_PARSE_OK)
    DPRINTF(E_LOG, L_ART, "Bug! Cannot parse response from source '%s', unknown error\n", src->name);

  if (ret != ONLINE_SOURCE_PARSE_OK)
    goto error;

  online_source_history_update(src, ctx->id, hash, client.response_code, artwork_url);
  evbuffer_free(client.input_body);
  return artwork_url;

 error:
  online_source_history_update(src, ctx->id, hash, client.response_code, NULL);
  evbuffer_free(client.input_body);
  return NULL;
}

static bool
online_source_is_enabled(struct online_source *src)
{
  struct settings_category *category;
  bool enabled;

  CHECK_NULL(L_ART, category = settings_category_get("artwork"));
  enabled = settings_option_getbool(settings_option_get(category, src->setting_name));

  if (!enabled)
    DPRINTF(E_SPAM, L_ART, "Source %s is disabled\n", src->name);

  return enabled;
}


/* ---------------------- SOURCE HANDLER IMPLEMENTATION -------------------- */

/* Looks in the cache for group artwork
 */
static int
source_group_cache_get(struct artwork_ctx *ctx)
{
  int format;
  int cached;
  int ret;

  ret = cache_artwork_get(CACHE_ARTWORK_GROUP, ctx->persistentid, ctx->req_params.max_w, ctx->req_params.max_h, &cached, &format, ctx->evbuf);
  if (ret < 0)
    return ART_E_ERROR;

  if (!cached)
    return ART_E_NONE;

  if (!format)
    return ART_E_ABORT;

  return format;
}

/* Looks for cover files in a directory, so if dir is /foo/bar and the user has
 * configured the cover file names "cover" and "artwork" it will look for
 * /foo/bar/cover.{png,jpg}, /foo/bar/artwork.{png,jpg} and also
 * /foo/bar/bar.{png,jpg} (so-called parentdir artwork)
 */
static int
source_group_dir_get(struct artwork_ctx *ctx)
{
  struct query_params qp;
  char *dir;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));

  qp.type = Q_GROUP_DIRS;
  qp.persistentid = ctx->persistentid;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start Q_GROUP_DIRS query\n");
      return ART_E_ERROR;
    }

  while (((ret = db_query_fetch_string(&qp, &dir)) == 0) && (dir))
    {
      /* The db query may return non-directories (eg if item is an internet stream or Spotify) */
      if (access(dir, F_OK) < 0)
	continue;

      ret = artwork_get_bydir(ctx->evbuf, ctx->path, sizeof(ctx->path), dir, ctx->req_params);
      if (ret > 0)
	{
	  db_query_end(&qp);
	  return ret;
	}
    }

  db_query_end(&qp);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching Q_GROUP_DIRS results\n");
      return ART_E_ERROR;
    }

  return ART_E_NONE;
}

/* Looks in the cache for item artwork. Only relevant if configured to look for
 * individual artwork.
 */
static int
source_item_cache_get(struct artwork_ctx *ctx)
{
  int format;
  int cached;
  int ret;

  if (!ctx->individual)
    return ART_E_NONE;

  ret = cache_artwork_get(CACHE_ARTWORK_INDIVIDUAL, ctx->id, ctx->req_params.max_w, ctx->req_params.max_h, &cached, &format, ctx->evbuf);
  if (ret < 0)
    return ART_E_ERROR;

  if (!cached)
    return ART_E_NONE;

  if (!format)
    return ART_E_ABORT;

  return format;
}

/* Get an embedded artwork file from a media file. Will rescale if needed.
 */
static int
source_item_embedded_get(struct artwork_ctx *ctx)
{
  int artwork;

  DPRINTF(E_SPAM, L_ART, "Trying embedded artwork in %s\n", ctx->dbmfi->path);

  if (safe_atoi32(ctx->dbmfi->artwork, &artwork) < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error converting dbmfi artwork to number for '%s'\n", ctx->dbmfi->path);
      return ART_E_ERROR;
    }

  if (artwork != ARTWORK_EMBEDDED)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", ctx->dbmfi->path);

  return artwork_get(ctx->evbuf, ctx->path, NULL, true, ctx->data_kind, ctx->req_params);
}

/* Looks for basename(in_path).{png,jpg}, so if in_path is /foo/bar.mp3 it
 * will look for /foo/bar.png and /foo/bar.jpg
 */
static int
source_item_own_get(struct artwork_ctx *ctx)
{
  char path[PATH_MAX];
  char *ptr;
  int len;
  int nextensions;
  int i;
  int ret;

  ret = snprintf(path, sizeof(path), "%s", ctx->dbmfi->path);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", ctx->dbmfi->path);
      return ART_E_ERROR;
    }

  ptr = strrchr(path, '.');
  if (ptr)
    *ptr = '\0';

  len = strlen(path);

  nextensions = sizeof(cover_extension) / sizeof(cover_extension[0]);

  for (i = 0; i < nextensions; i++)
    {
      ret = snprintf(path + len, sizeof(path) - len, ".%s", cover_extension[i]);
      if ((ret < 0) || (ret >= sizeof(path) - len))
	{
	  DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", ctx->dbmfi->path);
	  continue;
	}

      DPRINTF(E_SPAM, L_ART, "Trying own artwork file %s\n", path);

      ret = access(path, F_OK);
      if (ret < 0)
	continue;

      break;
    }

  if (i == nextensions)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", path);

  return artwork_get(ctx->evbuf, path, NULL, false, ctx->data_kind, ctx->req_params);
}

/*
 * Downloads the artwork from the location pointed to by queue_item->artwork_url
 */
static int
source_item_artwork_url_get(struct artwork_ctx *ctx)
{
  struct db_queue_item *queue_item;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying artwork url for %s\n", ctx->dbmfi->path);

  queue_item = db_queue_fetch_byfileid(ctx->id);
  if (!queue_item || !queue_item->artwork_url)
    {
      free_queue_item(queue_item, 0);
      return ART_E_NONE;
    }

  ret = artwork_get_byurl(ctx->evbuf, queue_item->artwork_url, ctx->req_params);

  snprintf(ctx->path, sizeof(ctx->path), "%s", queue_item->artwork_url);

  free_queue_item(queue_item, 0);

  return ret;
}

/*
 * If we are playing a pipe and there is also a metadata pipe, then input/pipe.c
 * may have saved the incoming artwork in a tmp file
 *
 */
static int
source_item_pipe_get(struct artwork_ctx *ctx)
{
  struct db_queue_item *queue_item;
  const char *proto = "file:";
  char *path;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying pipe metadata from %s.metadata\n", ctx->dbmfi->path);

  queue_item = db_queue_fetch_byfileid(ctx->id);
  if (!queue_item || !queue_item->artwork_url || strncmp(queue_item->artwork_url, proto, strlen(proto)) != 0)
    {
      free_queue_item(queue_item, 0);
      return ART_E_NONE;
    }

  path = queue_item->artwork_url + strlen(proto);

  // Sometimes the file has been replaced, but queue_item->artwork_url hasn't
  // been updated yet. In that case just stop now.
  ret = access(path, F_OK);
  if (ret != 0)
    {
      return ART_E_NONE;
    }

  snprintf(ctx->path, sizeof(ctx->path), "%s", path);

  free_queue_item(queue_item, 0);

  return artwork_get(ctx->evbuf, ctx->path, NULL, false, ctx->data_kind, ctx->req_params);
}

static int
source_item_discogs_get(struct artwork_ctx *ctx)
{
  char *url;
  int ret;

  if (!online_source_is_enabled(&discogs_source))
    return ART_E_NONE;

  url = online_source_search(&discogs_source, ctx);
  if (!url)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", url);

  ret = artwork_get_byurl(ctx->evbuf, url, ctx->req_params);

  free(url);
  return ret;
}

static int
source_item_coverartarchive_get(struct artwork_ctx *ctx)
{
  char *url;
  int ret;

  if (!online_source_is_enabled(&musicbrainz_source))
    return ART_E_NONE;

  // We search Musicbrainz to get the Musicbrainz ID, which we need to get the
  // artwork from the Cover Art Archive
  url = online_source_search(&musicbrainz_source, ctx);
  if (!url)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", url);

  ret = artwork_get_byurl(ctx->evbuf, url, ctx->req_params);

  free(url);
  return ret;
}

#ifdef SPOTIFY
static int
source_item_spotifywebapi_track_get(struct artwork_ctx *ctx)
{
  char *artwork_url;
  int ret;

  artwork_url = spotifywebapi_artwork_url_get(ctx->dbmfi->path, ctx->req_params.max_w, ctx->req_params.max_h);
  if (!artwork_url)
    {
      DPRINTF(E_WARN, L_ART, "No artwork from Spotify for %s\n", ctx->dbmfi->path);
      return ART_E_NONE;
    }

  ret = artwork_get_byurl(ctx->evbuf, artwork_url, ctx->req_params);

  free(artwork_url);
  return ret;
}

static int
source_item_spotifywebapi_search_get(struct artwork_ctx *ctx)
{
  struct spotifywebapi_status_info webapi_info;
  struct spotifywebapi_access_token webapi_token;
  char *url;
  int ret;

  if (!online_source_is_enabled(&spotify_source))
    return ART_E_NONE;

  spotifywebapi_status_info_get(&webapi_info);
  if (!webapi_info.token_valid)
    return ART_E_NONE; // Not logged in

  spotifywebapi_access_token_get(&webapi_token);
  if (!webapi_token.token)
    return ART_E_ERROR;

  spotify_source.auth_secret = webapi_token.token;

  url = online_source_search(&spotify_source, ctx);
  free(webapi_token.token);
  if (!url)
    return ART_E_NONE;

  snprintf(ctx->path, sizeof(ctx->path), "%s", url);

  ret = artwork_get_byurl(ctx->evbuf, url, ctx->req_params);

  free(url);
  return ret;
}
#else
static int
source_item_spotifywebapi_track_get(struct artwork_ctx *ctx)
{
  return ART_E_ERROR;
}

static int
source_item_spotifywebapi_search_get(struct artwork_ctx *ctx)
{
  // Silence compiler warning about spotify_source being unused
  (void)spotify_source;

  return ART_E_NONE;
}
#endif

/* First looks of the mfi->path is in any playlist, and if so looks in the dir
 * of the playlist file (m3u et al) to see if there is any artwork. So if the
 * playlist is /foo/bar.m3u it will look for /foo/bar.png and /foo/bar.jpg.
 */
static int
source_item_ownpl_get(struct artwork_ctx *ctx)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  char filter[PATH_MAX + 64];
  char *mfi_path;
  int format;
  int ret;

  ret = db_snprintf(filter, sizeof(filter), "filepath = '%q'", ctx->dbmfi->path);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Artwork path is too long: '%s'\n", ctx->dbmfi->path);
      return ART_E_ERROR;
    }

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_FIND_PL;
  qp.filter = filter;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start ownpl query\n");
      return ART_E_ERROR;
    }

  mfi_path = ctx->dbmfi->path;

  format = ART_E_NONE;
  while (((ret = db_query_fetch_pl(&qp, &dbpli)) == 0) && (dbpli.id) && (format == ART_E_NONE))
    {
      if (!dbpli.path)
	continue;

      if (dbpli.artwork_url)
	{
	  format = artwork_get_byurl(ctx->evbuf, dbpli.artwork_url, ctx->req_params);
	  if (format > 0)
	    break;
	}

      // Only handle non-remote paths with source_item_own_get()
      if (dbpli.path && dbpli.path[0] == '/')
	{
	  ctx->dbmfi->path = dbpli.path;
	  format = source_item_own_get(ctx);
	}
    }

  ctx->dbmfi->path = mfi_path;

  if ((ret < 0) || (format < 0))
    format = ART_E_ERROR;

  db_query_end(&qp);

  return format;
}


/* --------------------------- SOURCE PROCESSING --------------------------- */

static int
process_items(struct artwork_ctx *ctx, int item_mode)
{
  struct db_media_file_info dbmfi;
  int i;
  int ret;

  ret = db_query_start(&ctx->qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start query (type=%d)\n", ctx->qp.type);
      ctx->cache = NEVER;
      return -1;
    }

  while (((ret = db_query_fetch_file(&ctx->qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      // Save the first songalbumid, might need it for process_group() if this search doesn't give anything
      if (!ctx->persistentid)
	safe_atoi64(dbmfi.songalbumid, &ctx->persistentid);

      if (item_mode && !ctx->individual)
	goto no_artwork;

      ret = (safe_atoi32(dbmfi.id, &ctx->id) < 0) ||
            (safe_atou32(dbmfi.data_kind, &ctx->data_kind) < 0) ||
            (safe_atou32(dbmfi.media_kind, &ctx->media_kind) < 0) ||
            (ctx->data_kind > 30);
      if (ret)
	{
	  DPRINTF(E_LOG, L_ART, "Error converting dbmfi id, data_kind or media_kind to number for '%s'\n", dbmfi.path);
	  continue;
	}

      for (i = 0; artwork_item_source[i].handler; i++)
	{
	  if ((artwork_item_source[i].data_kinds & (1 << ctx->data_kind)) == 0)
	    continue;

	  if ((artwork_item_source[i].media_kinds & ctx->media_kind) == 0)
	    continue;

	  // If just one handler says we should not cache a negative result then we obey that
	  if ((artwork_item_source[i].cache & ON_FAILURE) == 0)
	    ctx->cache = NEVER;

	  DPRINTF(E_SPAM, L_ART, "Checking item source '%s'\n", artwork_item_source[i].name);

	  ctx->dbmfi = &dbmfi;
	  ret = artwork_item_source[i].handler(ctx);
	  ctx->dbmfi = NULL;

	  if (ret > 0)
	    {
	      DPRINTF(E_DBG, L_ART, "Artwork for '%s' found in source '%s'\n", dbmfi.title, artwork_item_source[i].name);
	      ctx->cache = artwork_item_source[i].cache;
	      db_query_end(&ctx->qp);
	      return ret;
	    }
	  else if (ret == ART_E_ABORT)
	    {
	      DPRINTF(E_DBG, L_ART, "Source '%s' stopped search for artwork for '%s'\n", artwork_item_source[i].name, dbmfi.title);
	      ctx->cache = NEVER;
	      break;
	    }
	  else if (ret == ART_E_ERROR)
	    {
	      DPRINTF(E_LOG, L_ART, "Source '%s' returned an error for '%s'\n", artwork_item_source[i].name, dbmfi.title);
	      ctx->cache = NEVER;
	    }
	}
    }

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching results\n");
      ctx->cache = NEVER;
    }

 no_artwork:
  db_query_end(&ctx->qp);

  return -1;
}

static int
process_group(struct artwork_ctx *ctx)
{
  struct db_media_file_info dbmfi;
  bool is_valid;
  int i;
  int ret;

  if (!ctx->persistentid)
    {
      DPRINTF(E_LOG, L_ART, "Bug! No persistentid in call to process_group()\n");
      ctx->cache = NEVER;
      return -1;
    }

  // Check if the group is valid (exists and is not e.g. "Unknown album")
  ret = db_query_start(&ctx->qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not start query to check if group is valid (persistentid = %" PRIi64 ")\n", ctx->qp.persistentid);
      goto invalid_group;
    }

  is_valid = (db_query_fetch_file(&ctx->qp, &dbmfi) == 0 && dbmfi.id && strcmp(dbmfi.album, CFG_NAME_UNKNOWN_ALBUM) != 0 && strcmp(dbmfi.album_artist, CFG_NAME_UNKNOWN_ARTIST) != 0);
  db_query_end(&ctx->qp);
  if (!is_valid)
    {
      DPRINTF(E_SPAM, L_ART, "Skipping group sources due to unknown album or artist\n");
      goto invalid_group;
    }

  for (i = 0; artwork_group_source[i].handler; i++)
    {
      // If just one handler says we should not cache a negative result then we obey that
      if ((artwork_group_source[i].cache & ON_FAILURE) == 0)
	ctx->cache = NEVER;

      DPRINTF(E_SPAM, L_ART, "Checking group source '%s'\n", artwork_group_source[i].name);

      ret = artwork_group_source[i].handler(ctx);
      if (ret > 0)
	{
	  DPRINTF(E_DBG, L_ART, "Artwork for group %" PRIi64 " found in source '%s'\n", ctx->persistentid, artwork_group_source[i].name);
	  ctx->cache = artwork_group_source[i].cache;
	  return ret;
	}
      else if (ret == ART_E_ABORT)
	{
	  DPRINTF(E_DBG, L_ART, "Source '%s' stopped search for artwork for group %" PRIi64 "\n", artwork_group_source[i].name, ctx->persistentid);
	  ctx->cache = NEVER;
	  return -1;
	}
      else if (ret == ART_E_ERROR)
	{
	  DPRINTF(E_LOG, L_ART, "Source '%s' returned an error for group %" PRIi64 "\n", artwork_group_source[i].name, ctx->persistentid);
	  ctx->cache = NEVER;
	}
    }

 invalid_group:
  return process_items(ctx, 0);
}


/* ------------------------------ ARTWORK API ------------------------------ */

int
artwork_get_item(struct evbuffer *evbuf, int id, int max_w, int max_h, int format)
{
  struct artwork_ctx ctx;
  char filter[32];
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for item %d (max_w=%d, max_h=%d)\n", id, max_w, max_h);

  if (id == DB_MEDIA_FILE_NON_PERSISTENT_ID)
    return  -1;

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  ctx.qp.type = Q_ITEMS;
  ctx.qp.filter = filter;
  ctx.evbuf = evbuf;
  ctx.req_params.max_w = max_w;
  ctx.req_params.max_h = max_h;
  ctx.req_params.format = format;
  ctx.cache = ON_FAILURE;
  ctx.individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

  ret = db_snprintf(filter, sizeof(filter), "id = %d", id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not build filter for file id %d; no artwork will be sent\n", id);
      return -1;
    }

  // Note: process_items will set ctx.persistentid for the following process_group()
  // - and do nothing else if artwork_individual is not configured by user
  ret = process_items(&ctx, 1);
  if (ret > 0)
    {
      if (ctx.cache & ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_INDIVIDUAL, id, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  ctx.qp.type = Q_GROUP_ITEMS;
  ctx.qp.persistentid = ctx.persistentid;

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache & ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for item %d\n", id);

  if (ctx.cache & ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

int
artwork_get_group(struct evbuffer *evbuf, int id, int max_w, int max_h, int format)
{
  struct artwork_ctx ctx;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %d (max_w=%d, max_h=%d)\n", id, max_w, max_h);

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  /* Get the persistent id for the given group id */
  ret = db_group_persistentid_byid(id, &ctx.persistentid);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Error fetching persistent id for group id %d\n", id);
      return -1;
    }

  ctx.qp.type = Q_GROUP_ITEMS;
  ctx.qp.persistentid = ctx.persistentid;
  ctx.evbuf = evbuf;
  ctx.req_params.max_w = max_w;
  ctx.req_params.max_h = max_h;
  ctx.req_params.format = format;
  ctx.cache = ON_FAILURE;
  ctx.individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache & ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for group %d\n", id);

  if (ctx.cache & ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

/* Checks if the file is an artwork file */
bool
artwork_file_is_artwork(const char *filename)
{
  cfg_t *lib;
  int n;
  int i;
  int j;
  int ret;
  char artwork[PATH_MAX];

  lib = cfg_getsec(cfg, "library");
  n = cfg_size(lib, "artwork_basenames");

  for (i = 0; i < n; i++)
    {
      for (j = 0; j < ARRAY_SIZE(cover_extension); j++)
	{
	  ret = snprintf(artwork, sizeof(artwork), "%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(artwork)))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	      continue;
	    }

	  if (strcmp(artwork, filename) == 0)
	    return true;
	}
    }

  return false;
}

bool
artwork_extension_is_artwork(const char *path)
{
  char *ext;
  int len;
  int i;

  ext = strrchr(path, '.');
  if (!ext)
    return false;

  ext++;

  for (i = 0; i < ARRAY_SIZE(cover_extension); i++)
    {
      len = strlen(cover_extension[i]);

      if (strncasecmp(cover_extension[i], ext, len) != 0)
        continue;

      // Check that after the extension we either have the end or "?"
      if (ext[len] != '\0' && ext[len] != '?')
        continue;

      return true;
    }

  return false;
}
