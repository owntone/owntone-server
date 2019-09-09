/*
 * Copyright (C) 2015-2017 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "db.h"
#include "misc.h"
#include "logger.h"
#include "conffile.h"
#include "cache.h"
#include "http.h"
#include "transcode.h"

#include "artwork.h"

#ifdef HAVE_SPOTIFY_H
# include "spotify_webapi.h"
# include "spotify.h"
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
 *   ART_FMT_JPEG (positive)  Found a jpeg
 *   ART_FMT_PNG  (positive)  Found a png
 *   ART_E_NONE (zero)        No artwork found
 *   ART_E_ERROR (negative)   An error occurred while searching for artwork
 *   ART_E_ABORT (negative)   Caller should abort artwork search (may be returned by cache)
 */
#define ART_E_NONE 0
#define ART_E_ERROR -1
#define ART_E_ABORT -2

enum artwork_cache
{
  NEVER = 0,       // No caching of any results
  ON_SUCCESS = 1,  // Cache if artwork found
  ON_FAILURE = 2,  // Cache if artwork not found (so we don't keep asking)
};

/* This struct contains the data available to the handler, as well as a char
 * buffer where the handler should output the path to the artwork (if it is
 * local - otherwise the buffer can be left empty). The purpose of supplying the
 * path is that the filescanner can then clear the cache in case the file
 * changes.
 */
struct artwork_ctx {
  // Handler should output path here if artwork is local
  char path[PATH_MAX];
  // Handler should output artwork data to this evbuffer
  struct evbuffer *evbuf;

  // Input data to handler, requested width and height
  int max_w;
  int max_h;
  // Input data to handler, did user configure to look for individual artwork
  int individual;

  // Input data for item handlers
  struct db_media_file_info *dbmfi;
  int id;
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

  // What data_kinds the handler can work with, combined with (1 << A) | (1 << B)
  int data_kinds;

  // When should results from the source be cached?
  enum artwork_cache cache;
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
static int source_item_stream_get(struct artwork_ctx *ctx);
static int source_item_pipe_get(struct artwork_ctx *ctx);
static int source_item_spotify_get(struct artwork_ctx *ctx);
static int source_item_spotifywebapi_get(struct artwork_ctx *ctx);
static int source_item_ownpl_get(struct artwork_ctx *ctx);

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
      .cache = ON_FAILURE,
    },
    {
      .name = "embedded",
      .handler = source_item_embedded_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "own",
      .handler = source_item_own_get,
      .data_kinds = (1 << DATA_KIND_FILE),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "stream",
      .handler = source_item_stream_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .cache = NEVER,
    },
    {
      .name = "pipe",
      .handler = source_item_pipe_get,
      .data_kinds = (1 << DATA_KIND_PIPE),
      .cache = NEVER,
    },
    {
      .name = "Spotify",
      .handler = source_item_spotify_get,
      .data_kinds = (1 << DATA_KIND_SPOTIFY),
      .cache = ON_SUCCESS,
    },
    {
      .name = "Spotify web api",
      .handler = source_item_spotifywebapi_get,
      .data_kinds = (1 << DATA_KIND_SPOTIFY),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = "playlist own",
      .handler = source_item_ownpl_get,
      .data_kinds = (1 << DATA_KIND_HTTP),
      .cache = ON_SUCCESS | ON_FAILURE,
    },
    {
      .name = NULL,
      .handler = NULL,
      .data_kinds = 0,
      .cache = 0,
    }
  };



/* -------------------------------- HELPERS -------------------------------- */

/* Reads an artwork file from the given url straight into an evbuf
 *
 * @out evbuf     Image data
 * @in  url       URL for the image
 * @return        0 on success, -1 on error
 */
static int
artwork_url_read(struct evbuffer *evbuf, const char *url)
{
  struct http_client_ctx client;
  struct keyval *kv;
  const char *content_type;
  int len;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying internet artwork in %s\n", url);

  ret = ART_E_NONE;

  len = strlen(url);
  if ((len < 14) || (len > PATH_MAX)) // Can't be shorter than http://a/1.jpg
    goto out_url;

  kv = keyval_alloc();
  if (!kv)
    goto out_url;

  memset(&client, 0, sizeof(struct http_client_ctx));
  client.url = url;
  client.input_headers = kv;
  client.input_body = evbuf;

  if (http_client_request(&client) < 0)
    goto out_kv;

  if (client.response_code != HTTP_OK)
    goto out_kv;

  content_type = keyval_get(kv, "Content-Type");
  if (content_type && (strcmp(content_type, "image/jpeg") == 0))
    ret = ART_FMT_JPEG;
  else if (content_type && (strcmp(content_type, "image/png") == 0))
    ret = ART_FMT_PNG;

 out_kv:
  keyval_clear(kv);
  free(kv);

 out_url:
  return ret;
}

/* Reads an artwork file from the filesystem straight into an evbuf
 * TODO Use evbuffer_add_file or evbuffer_read?
 *
 * @out evbuf     Image data
 * @in  path      Path to the artwork
 * @return        0 on success, -1 on error
 */
static int
artwork_read(struct evbuffer *evbuf, char *path)
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

/* Will the source image fit inside requested size. If not, what size should it
 * be rescaled to to maintain aspect ratio.
 *
 * @out target_w  Rescaled width
 * @out target_h  Rescaled height
 * @in  width     Actual width
 * @in  height    Actual height
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @return        -1 no rescaling needed, otherwise 0
 */
static int
rescale_calculate(int *target_w, int *target_h, int width, int height, int max_w, int max_h)
{
  DPRINTF(E_DBG, L_ART, "Original image dimensions: w %d h %d\n", width, height);

  *target_w = width;
  *target_h = height;

  if ((width == 0) || (height == 0))                   /* Unknown source size, can't rescale */
    return -1;

  if ((max_w <= 0) || (max_h <= 0))                    /* No valid target dimensions, use original */
    return -1;

  if ((width <= max_w) && (height <= max_h))           /* Smaller than target */
    return -1;

  if (width * max_h > height * max_w)                  /* Wider aspect ratio than target */
    {
      *target_w = max_w;
      *target_h = (double)max_w * ((double)height / (double)width);
    }
  else                                                 /* Taller or equal aspect ratio */
    {
      *target_w = (double)max_h * ((double)width / (double)height);
      *target_h = max_h;
    }

  if ((*target_h > max_h) && (max_h > 0))
    *target_h = max_h;

  /* PNG prefers even row count */
  *target_w += *target_w % 2;

  if ((*target_w > max_w) && (max_w > 0))
    *target_w = max_w - (max_w % 2);

  DPRINTF(E_DBG, L_ART, "Rescale required, destination width %d height %d\n", *target_w, *target_h);

  return 0;
}

/*
 * Either gets the artwork file given in "path" from the file system (rescaled if needed) or rescales the artwork given in "inbuf".
 *
 * @out evbuf        Image data (rescaled if needed)
 * @in  path         Path to the artwork file (alternative to inbuf)
 * @in  inbuf        Buffer with the artwork (alternative to path)
 * @in  max_w        Requested width
 * @in  max_h        Requested height
 * @in  is_embedded  Whether the artwork in file is embedded or raw jpeg/png
 * @return           ART_FMT_* on success, ART_E_ERROR on error
 */
static int
artwork_get(struct evbuffer *evbuf, char *path, struct evbuffer *inbuf, int max_w, int max_h, bool is_embedded)
{
  struct decode_ctx *xcode_decode;
  struct encode_ctx *xcode_encode;
  void *frame;
  int width;
  int height;
  int target_w;
  int target_h;
  int format_ok;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Getting artwork (max destination width %d height %d)\n", max_w, max_h);

  xcode_decode = transcode_decode_setup(XCODE_JPEG, NULL, DATA_KIND_FILE, path, inbuf, 0); // Covers XCODE_PNG too
  if (!xcode_decode)
    {
      if (path)
	DPRINTF(E_DBG, L_ART, "No artwork found in '%s'\n", path);
      else
	DPRINTF(E_DBG, L_ART, "No artwork provided to artwork_get()\n");
      return ART_E_NONE;
    }

  if (transcode_decode_query(xcode_decode, "is_jpeg"))
    format_ok = ART_FMT_JPEG;
  else if (transcode_decode_query(xcode_decode, "is_png"))
    format_ok = ART_FMT_PNG;
  else
    {
      if (is_embedded)
	DPRINTF(E_DBG, L_ART, "File '%s' has no PNG or JPEG artwork\n", path);
      else if (path)
	DPRINTF(E_LOG, L_ART, "Artwork file '%s' not a PNG or JPEG file\n", path);
      else
	DPRINTF(E_LOG, L_ART, "Artwork data provided to artwork_get() is not PNG or JPEG\n");

      goto fail_free_decode;
    }

  width = transcode_decode_query(xcode_decode, "width");
  height = transcode_decode_query(xcode_decode, "height");

  ret = rescale_calculate(&target_w, &target_h, width, height, max_w, max_h);
  if (ret < 0)
    {
      if (is_embedded)
	{
	  target_w = width;
	  target_h = height;
	}
      else if (path)
	{
	  // No rescaling required, just read the raw file into the evbuf
	  ret = artwork_read(evbuf, path);
	  if (ret < 0)
	    goto fail_free_decode;

	  transcode_decode_cleanup(&xcode_decode);
	  return format_ok;
	}
      else
	{
	  goto fail_free_decode;
	}
    }

  if (format_ok == ART_FMT_JPEG)
    xcode_encode = transcode_encode_setup(XCODE_JPEG, NULL, xcode_decode, NULL, target_w, target_h);
  else
    xcode_encode = transcode_encode_setup(XCODE_PNG, NULL, xcode_decode, NULL, target_w, target_h);

  if (!xcode_encode)
    {
      if (path)
	DPRINTF(E_WARN, L_ART, "Error preparing rescaling of '%s'\n", path);
      else
	DPRINTF(E_WARN, L_ART, "Error preparing rescaling of artwork data\n");
      goto fail_free_decode;
    }

  // We don't use transcode() because we just want to process one frame
  ret = transcode_decode(&frame, xcode_decode);
  if (ret < 0)
    goto fail_free_encode;

  ret = transcode_encode(evbuf, xcode_encode, frame, 1);

  transcode_encode_cleanup(&xcode_encode);
  transcode_decode_cleanup(&xcode_decode);

  if (ret < 0)
    {
      evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
      return ART_E_ERROR;
    }

  return format_ok;

 fail_free_encode:
  transcode_encode_cleanup(&xcode_encode);
 fail_free_decode:
  transcode_decode_cleanup(&xcode_decode);
  return ART_E_ERROR;
}

/* Looks for an artwork file in a directory. Will rescale if needed.
 *
 * @out evbuf     Image data
 * @in  dir       Directory to search
 * @in  max_w     Requested width
 * @in  max_h     Requested height
 * @out out_path  Path to the artwork file if found, must be a char[PATH_MAX] buffer
 * @return        ART_FMT_* on success, ART_E_NONE on nothing found, ART_E_ERROR on error
 */
static int
artwork_get_dir_image(struct evbuffer *evbuf, char *dir, int max_w, int max_h, char *out_path)
{
  char path[PATH_MAX];
  char parentdir[PATH_MAX];
  int i;
  int j;
  int len;
  int ret;
  cfg_t *lib;
  int nbasenames;
  int nextensions;
  char *ptr;

  ret = snprintf(path, sizeof(path), "%s", dir);
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_ART, "Artwork path exceeds PATH_MAX (%s)\n", dir);
      return ART_E_ERROR;
    }

  len = strlen(path);

  lib = cfg_getsec(cfg, "library");
  nbasenames = cfg_size(lib, "artwork_basenames");

  if (nbasenames == 0)
    return ART_E_NONE;

  nextensions = sizeof(cover_extension) / sizeof(cover_extension[0]);

  for (i = 0; i < nbasenames; i++)
    {
      for (j = 0; j < nextensions; j++)
	{
	  ret = snprintf(path + len, sizeof(path) - len, "/%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(path) - len))
	    {
	      DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s/%s)\n", dir, cfg_getnstr(lib, "artwork_basenames", i));
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying directory artwork file %s\n", path);

	  ret = access(path, F_OK);
	  if (ret < 0)
	    continue;

	  // If artwork file exists (ret == 0), exit the loop
	  break;
	}

      // In case the previous loop exited early, we found an existing artwork file and exit the outer loop
      if (j < nextensions)
	break;
    }

  // If the loop for directory artwork did not exit early, look for parent directory artwork
  if (i == nbasenames)
    {
      ptr = strrchr(path, '/');
      if (ptr)
	*ptr = '\0';

      ptr = strrchr(path, '/');
      if ((!ptr) || (strlen(ptr) <= 1))
	{
	  DPRINTF(E_LOG, L_ART, "Could not find parent dir name (%s)\n", path);
	  return ART_E_ERROR;
	}
      strcpy(parentdir, ptr + 1);

      len = strlen(path);

      for (i = 0; i < nextensions; i++)
	{
	  ret = snprintf(path + len, sizeof(path) - len, "/%s.%s", parentdir, cover_extension[i]);
	  if ((ret < 0) || (ret >= sizeof(path) - len))
	    {
	      DPRINTF(E_LOG, L_ART, "Artwork path will exceed PATH_MAX (%s)\n", parentdir);
	      continue;
	    }

	  DPRINTF(E_SPAM, L_ART, "Trying parent directory artwork file %s\n", path);

	  ret = access(path, F_OK);
	  if (ret < 0)
	    continue;

	  break;
	}

      if (i == nextensions)
	return ART_E_NONE;
    }

  snprintf(out_path, PATH_MAX, "%s", path);

  return artwork_get(evbuf, path, NULL, max_w, max_h, false);
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

  ret = cache_artwork_get(CACHE_ARTWORK_GROUP, ctx->persistentid, ctx->max_w, ctx->max_h, &cached, &format, ctx->evbuf);
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

  /* Image is not in the artwork cache. Try directory artwork first */
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

      ret = artwork_get_dir_image(ctx->evbuf, dir, ctx->max_w, ctx->max_h, ctx->path);
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

  ret = cache_artwork_get(CACHE_ARTWORK_INDIVIDUAL, ctx->id, ctx->max_w, ctx->max_h, &cached, &format, ctx->evbuf);
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
  DPRINTF(E_SPAM, L_ART, "Trying embedded artwork in %s\n", ctx->dbmfi->path);

  snprintf(ctx->path, sizeof(ctx->path), "%s", ctx->dbmfi->path);

  return artwork_get(ctx->evbuf, ctx->path, NULL, ctx->max_w, ctx->max_h, true);
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

  return artwork_get(ctx->evbuf, path, NULL, ctx->max_w, ctx->max_h, false);
}

/*
 * Downloads the artwork pointed to by the ICY metadata tag in an internet radio
 * stream (the StreamUrl tag). The path will be converted back to the id, which
 * is given to the player. If the id is currently being played, and there is a
 * valid ICY metadata artwork URL available, it will be returned to this
 * function, which will then use the http client to get the artwork. Notice: No
 * rescaling is done.
 */
static int
source_item_stream_get(struct artwork_ctx *ctx)
{
  struct db_queue_item *queue_item;
  char *url;
  char *ext;
  int len;
  int ret;

  DPRINTF(E_SPAM, L_ART, "Trying internet stream artwork in %s\n", ctx->dbmfi->path);

  ret = ART_E_NONE;

  queue_item = db_queue_fetch_byfileid(ctx->id);
  if (!queue_item || !queue_item->artwork_url)
    {
      free_queue_item(queue_item, 0);
      return ART_E_NONE;
    }

  url = strdup(queue_item->artwork_url);
  free_queue_item(queue_item, 0);

  len = strlen(url);
  if ((len < 14) || (len > PATH_MAX)) // Can't be shorter than http://a/1.jpg
    goto out_url;

  ext = strrchr(url, '.');
  if (!ext)
    goto out_url;
  if ((strcmp(ext, ".jpg") != 0) && (strcmp(ext, ".png") != 0))
    goto out_url;

  cache_artwork_read(ctx->evbuf, url, &ret);
  if (ret > 0)
    goto out_url;

  ret = artwork_url_read(ctx->evbuf, url);

  if (ret > 0)
    {
      DPRINTF(E_SPAM, L_ART, "Found internet stream artwork in %s (%d)\n", url, ret);
      cache_artwork_stash(ctx->evbuf, url, ret);
    }

 out_url:
  free(url);

  return ret;
}

/*
 * If we are playing a pipe and there is also a metadata pipe, then
 * input/pipe.c may have saved the incoming artwork via cache_artwork_stash()
 *
 */
static int
source_item_pipe_get(struct artwork_ctx *ctx)
{
  int ret;
  int format;

  DPRINTF(E_SPAM, L_ART, "Trying pipe metadata from %s.metadata\n", ctx->dbmfi->path);

  ret = cache_artwork_read(ctx->evbuf, "pipe://metadata", &format);
  if (ret < 0)
    return ART_E_NONE;

  return format;
}

#ifdef HAVE_SPOTIFY_H
static int
source_item_spotify_get(struct artwork_ctx *ctx)
{
  struct evbuffer *raw;
  struct evbuffer *evbuf;
  int ret;

  raw = evbuffer_new();
  evbuf = evbuffer_new();
  if (!raw || !evbuf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for Spotify evbuf\n");
      return ART_E_ERROR;
    }

  ret = spotify_artwork_get(raw, ctx->dbmfi->path, ctx->max_w, ctx->max_h);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_ART, "No artwork from Spotify for %s\n", ctx->dbmfi->path);
      evbuffer_free(raw);
      evbuffer_free(evbuf);
      return ART_E_NONE;
    }

  // Make a refbuf of raw for ffmpeg image size probing and possibly rescaling.
  // We keep raw around in case rescaling is not necessary.
#ifdef HAVE_LIBEVENT2_OLD
  uint8_t *buf = evbuffer_pullup(raw, -1);
  if (!buf)
    {
      DPRINTF(E_LOG, L_ART, "Could not pullup raw artwork\n");
      goto out_free_evbuf;
    }

  ret = evbuffer_add_reference(evbuf, buf, evbuffer_get_length(raw), NULL, NULL);
#else
  ret = evbuffer_add_buffer_reference(evbuf, raw);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not copy/ref raw image for ffmpeg\n");
      goto out_free_evbuf;
    }

  // For non-file input, artwork_get() will also fail if no rescaling is required
  ret = artwork_get(ctx->evbuf, NULL, evbuf, ctx->max_w, ctx->max_h, false);
  if (ret == ART_E_ERROR)
    {
      DPRINTF(E_DBG, L_ART, "Not rescaling Spotify image\n");
      ret = evbuffer_add_buffer(ctx->evbuf, raw);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "Could not add or rescale image to output evbuf\n");
	  goto out_free_evbuf;
	}
    }

  evbuffer_free(evbuf);
  evbuffer_free(raw);

  return ART_FMT_JPEG;

 out_free_evbuf:
  evbuffer_free(evbuf);
  evbuffer_free(raw);

  return ART_E_ERROR;
}

static int
source_item_spotifywebapi_get(struct artwork_ctx *ctx)
{
  struct evbuffer *raw;
  struct evbuffer *evbuf;
  char *artwork_url;
  int content_type;
  int ret;

  artwork_url = NULL;
  raw = evbuffer_new();
  evbuf = evbuffer_new();
  if (!raw || !evbuf)
    {
      DPRINTF(E_LOG, L_ART, "Out of memory for Spotify evbuf\n");
      return ART_E_ERROR;
    }

  artwork_url = spotifywebapi_artwork_url_get(ctx->dbmfi->path, ctx->max_w, ctx->max_h);
  if (!artwork_url)
    {
      DPRINTF(E_WARN, L_ART, "No artwork from Spotify for %s\n", ctx->dbmfi->path);
      return ART_E_NONE;
    }

  ret = artwork_url_read(raw, artwork_url);
  if (ret <= 0)
    goto out_free_evbuf;

  content_type = ret;

  // Make a refbuf of raw for ffmpeg image size probing and possibly rescaling.
  // We keep raw around in case rescaling is not necessary.
#ifdef HAVE_LIBEVENT2_OLD
  uint8_t *buf = evbuffer_pullup(raw, -1);
  if (!buf)
    {
      DPRINTF(E_LOG, L_ART, "Could not pullup raw artwork\n");
      goto out_free_evbuf;
    }

  ret = evbuffer_add_reference(evbuf, buf, evbuffer_get_length(raw), NULL, NULL);
#else
  ret = evbuffer_add_buffer_reference(evbuf, raw);
#endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_ART, "Could not copy/ref raw image for ffmpeg\n");
      goto out_free_evbuf;
    }

  // For non-file input, artwork_get() will also fail if no rescaling is required
  ret = artwork_get(ctx->evbuf, NULL, evbuf, ctx->max_w, ctx->max_h, false);
  if (ret == ART_E_ERROR)
    {
      DPRINTF(E_DBG, L_ART, "Not rescaling Spotify image\n");
      ret = evbuffer_add_buffer(ctx->evbuf, raw);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_ART, "Could not add or rescale image to output evbuf\n");
	  goto out_free_evbuf;
	}
    }

  evbuffer_free(evbuf);
  evbuffer_free(raw);
  free(artwork_url);

  return content_type;

 out_free_evbuf:
  evbuffer_free(evbuf);
  evbuffer_free(raw);
  free(artwork_url);

  return ART_E_ERROR;
}
#else
static int
source_item_spotify_get(struct artwork_ctx *ctx)
{
  return ART_E_ERROR;
}

static int
source_item_spotifywebapi_get(struct artwork_ctx *ctx)
{
  return ART_E_ERROR;
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

      ctx->dbmfi->path = dbpli.path;
      format = source_item_own_get(ctx);
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
  uint32_t data_kind;
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
            (safe_atou32(dbmfi.data_kind, &data_kind) < 0) ||
            (data_kind > 30);
      if (ret)
	{
	  DPRINTF(E_LOG, L_ART, "Error converting dbmfi id or data_kind to number\n");
	  continue;
	}

      for (i = 0; artwork_item_source[i].handler; i++)
	{
	  if ((artwork_item_source[i].data_kinds & (1 << data_kind)) == 0)
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
	      ctx->cache = (artwork_item_source[i].cache & ON_SUCCESS);
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
  int i;
  int ret;

  if (!ctx->persistentid)
    {
      DPRINTF(E_LOG, L_ART, "Bug! No persistentid in call to process_group()\n");
      ctx->cache = NEVER;
      return -1;
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
	  ctx->cache = (artwork_group_source[i].cache & ON_SUCCESS);
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

  ret = process_items(ctx, 0);

  return ret;
}


/* ------------------------------ ARTWORK API ------------------------------ */

int
artwork_get_item(struct evbuffer *evbuf, int id, int max_w, int max_h)
{
  struct artwork_ctx ctx;
  char filter[32];
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for item %d\n", id);

  if (id == DB_MEDIA_FILE_NON_PERSISTENT_ID)
    return  -1;

  memset(&ctx, 0, sizeof(struct artwork_ctx));

  ctx.qp.type = Q_ITEMS;
  ctx.qp.filter = filter;
  ctx.evbuf = evbuf;
  ctx.max_w = max_w;
  ctx.max_h = max_h;
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
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_INDIVIDUAL, id, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  ctx.qp.type = Q_GROUP_ITEMS;
  ctx.qp.persistentid = ctx.persistentid;

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for item %d\n", id);

  if (ctx.cache == ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

int
artwork_get_group(struct evbuffer *evbuf, int id, int max_w, int max_h)
{
  struct artwork_ctx ctx;
  int ret;

  DPRINTF(E_DBG, L_ART, "Artwork request for group %d\n", id);

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
  ctx.max_w = max_w;
  ctx.max_h = max_h;
  ctx.cache = ON_FAILURE;
  ctx.individual = cfg_getbool(cfg_getsec(cfg, "library"), "artwork_individual");

  ret = process_group(&ctx);
  if (ret > 0)
    {
      if (ctx.cache == ON_SUCCESS)
	cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, ret, ctx.path, evbuf);

      return ret;
    }

  DPRINTF(E_DBG, L_ART, "No artwork found for group %d\n", id);

  if (ctx.cache == ON_FAILURE)
    cache_artwork_add(CACHE_ARTWORK_GROUP, ctx.persistentid, max_w, max_h, 0, "", evbuf);

  return -1;
}

/* Checks if the file is an artwork file */
int
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
      for (j = 0; j < (sizeof(cover_extension) / sizeof(cover_extension[0])); j++)
	{
	  ret = snprintf(artwork, sizeof(artwork), "%s.%s", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	  if ((ret < 0) || (ret >= sizeof(artwork)))
	    {
	      DPRINTF(E_INFO, L_ART, "Artwork path exceeds PATH_MAX (%s.%s)\n", cfg_getnstr(lib, "artwork_basenames", i), cover_extension[j]);
	      continue;
	    }

	  if (strcmp(artwork, filename) == 0)
	    return 1;
	}

      if (j < (sizeof(cover_extension) / sizeof(cover_extension[0])))
	break;
    }

  return 0;
}
