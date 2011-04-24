/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <inttypes.h>

#if defined(HAVE_SYS_EVENTFD_H) && defined(HAVE_EVENTFD)
# define USE_EVENTFD
# include <sys/eventfd.h>
#endif

#include <zlib.h>
#include <event.h>
#include "evhttp/evhttp.h"

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "httpd.h"
#include "httpd_rsp.h"
#include "httpd_daap.h"
#include "httpd_dacp.h"
#include "transcode.h"


/*
 * HTTP client quirks by User-Agent, from mt-daapd
 *
 * - iTunes:
 *   + Connection: Keep-Alive on HTTP error 401
 * - Hifidelio:
 *   + Connection: Keep-Alive for streaming (Connection: close not honoured)
 *
 * These quirks are not implemented. Implement as needed.
 *
 * Implemented quirks:
 *
 * - Roku:
 *   + Does not encode space as + in query string
 * - iTunes:
 *   + Does not encode space as + in query string
 */


#define STREAM_CHUNK_SIZE (64 * 1024)
#define WEBFACE_ROOT   DATADIR "/webface/"

struct content_type_map {
  char *ext;
  char *ctype;
};

struct stream_ctx {
  struct evhttp_request *req;
  uint8_t *buf;
  struct evbuffer *evbuf;
  struct event ev;
  int id;
  int fd;
  off_t size;
  off_t stream_size;
  off_t offset;
  off_t start_offset;
  off_t end_offset;
  int marked;
  struct transcode_ctx *xcode;
};


static const struct content_type_map ext2ctype[] =
  {
    { ".html", "text/html; charset=utf-8" },
    { ".xml",  "text/xml; charset=utf-8" },
    { ".css",  "text/css; charset=utf-8" },
    { ".txt",  "text/plain; charset=utf-8" },
    { ".js",   "application/javascript; charset=utf-8" },
    { ".gif",  "image/gif" },
    { ".ico",  "image/x-ico" },
    { ".png",  "image/png" },
    { NULL, NULL }
  };

struct event_base *evbase_httpd;

#ifdef USE_EVENTFD
static int exit_efd;
#else
static int exit_pipe[2];
#endif
static int httpd_exit;
static struct event exitev;
static struct evhttp *evhttpd;
static pthread_t tid_httpd;


static void
stream_end(struct stream_ctx *st, int failed)
{
  if (st->req->evcon)
    evhttp_connection_set_closecb(st->req->evcon, NULL, NULL);

  if (!failed)
    evhttp_send_reply_end(st->req);

  evbuffer_free(st->evbuf);

  if (st->xcode)
    transcode_cleanup(st->xcode);
  else
    {
      free(st->buf);
      close(st->fd);
    }

  free(st);
}

static void
stream_up_playcount(struct stream_ctx *st)
{
  if (!st->marked
      && (st->stream_size > ((st->size * 50) / 100))
      && (st->offset > ((st->size * 80) / 100)))
    {
      st->marked = 1;
      db_file_inc_playcount(st->id);
    }
}

static void
stream_chunk_resched_cb(struct evhttp_connection *evcon, void *arg)
{
  struct stream_ctx *st;
  struct timeval tv;
  int ret;

  st = (struct stream_ctx *)arg;

  evutil_timerclear(&tv);
  ret = event_add(&st->ev, &tv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not re-add one-shot event for streaming\n");

      stream_end(st, 0);
    }
}

static void
stream_chunk_xcode_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st;
  struct timeval tv;
  int xcoded;
  int ret;

  st = (struct stream_ctx *)arg;

  xcoded = transcode(st->xcode, st->evbuf, STREAM_CHUNK_SIZE);
  if (xcoded <= 0)
    {
      if (xcoded == 0)
	DPRINTF(E_LOG, L_HTTPD, "Done streaming transcoded file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Transcoding error, file id %d\n", st->id);

      stream_end(st, 0);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Got %d bytes from transcode; streaming file id %d\n", xcoded, st->id);

  /* Consume transcoded data until we meet start_offset */
  if (st->start_offset > st->offset)
    {
      ret = st->start_offset - st->offset;

      if (ret < xcoded)
	{
	  evbuffer_drain(st->evbuf, ret);
	  st->offset += ret;

	  ret = xcoded - ret;
	}
      else
	{
	  evbuffer_drain(st->evbuf, xcoded);
	  st->offset += xcoded;

	  goto consume;
	}
    }
  else
    ret = xcoded;

  evhttp_send_reply_chunk_with_cb(st->req, st->evbuf, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_up_playcount(st);

  return;

 consume: /* reschedule immediately - consume up to start_offset */
  evutil_timerclear(&tv);
  ret = event_add(&st->ev, &tv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not re-add one-shot event for streaming (xcode)\n");

      stream_end(st, 0);
      return;
    }
}

static void
stream_chunk_raw_cb(int fd, short event, void *arg)
{
  struct stream_ctx *st;
  size_t chunk_size;
  int ret;

  st = (struct stream_ctx *)arg;

  if (st->end_offset && (st->offset > st->end_offset))
    {
      stream_end(st, 0);
      return;
    }

  if (st->end_offset && ((st->offset + STREAM_CHUNK_SIZE) > (st->end_offset + 1)))
    chunk_size = st->end_offset + 1 - st->offset;
  else
    chunk_size = STREAM_CHUNK_SIZE;  

  ret = read(st->fd, st->buf, chunk_size);
  if (ret <= 0)
    {
      if (ret == 0)
	DPRINTF(E_LOG, L_HTTPD, "Done streaming file id %d\n", st->id);
      else
	DPRINTF(E_LOG, L_HTTPD, "Streaming error, file id %d\n", st->id);

      stream_end(st, 0);
      return;
    }

  DPRINTF(E_DBG, L_HTTPD, "Read %d bytes; streaming file id %d\n", ret, st->id);

  evbuffer_add(st->evbuf, st->buf, ret);

  evhttp_send_reply_chunk_with_cb(st->req, st->evbuf, stream_chunk_resched_cb, st);

  st->offset += ret;

  stream_up_playcount(st);
}

static void
stream_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct stream_ctx *st;

  st = (struct stream_ctx *)arg;

  DPRINTF(E_LOG, L_HTTPD, "Connection failed; stopping streaming of file ID %d\n", st->id);

  /* Stop streaming */
  event_del(&st->ev);

  stream_end(st, 1);
}


/* Thread: httpd */
void
httpd_stream_file(struct evhttp_request *req, int id)
{
  struct media_file_info *mfi;
  struct stream_ctx *st;
  void (*stream_cb)(int fd, short event, void *arg);
  struct stat sb;
  struct timeval tv;
  const char *param;
  const char *param_end;
  char buf[64];
  int64_t offset;
  int64_t end_offset;
  off_t pos;
  int transcode;
  int ret;

  offset = 0;
  end_offset = 0;
  param = evhttp_find_header(req->input_headers, "Range");
  if (param)
    {
      DPRINTF(E_DBG, L_HTTPD, "Found Range header: %s\n", param);

      /* Start offset */
      ret = safe_atoi64(param + strlen("bytes="), &offset);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Invalid start offset, will stream whole file (%s)\n", param);
	  offset = 0;
	}
      /* End offset, if any */
      else
	{
	  param_end = strchr(param, '-');
	  if (param_end)
	    {
	      ret = safe_atoi64(param_end + 1, &end_offset);
	      if (ret < 0)
		{
		  DPRINTF(E_LOG, L_HTTPD, "Invalid end offset, will stream to end of file (%s)\n", param);
		  end_offset = 0;
		}

	      if (end_offset < offset)
		{
		  DPRINTF(E_LOG, L_HTTPD, "End offset < start offset, will stream to end of file (%" PRIi64 " < %" PRIi64 ")\n", end_offset, offset);
		  end_offset = 0;
		}
	    }
	}
    }

  mfi = db_file_fetch_byid(id);
  if (!mfi)
    {
      DPRINTF(E_LOG, L_HTTPD, "Item %d not found\n", id);

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return;
    }

  if (mfi->data_kind != 0)
    {
      evhttp_send_error(req, 500, "Cannot stream radio station");

      goto out_free_mfi;
    }

  st = (struct stream_ctx *)malloc(sizeof(struct stream_ctx));
  if (!st)
    {
      DPRINTF(E_LOG, L_HTTPD, "Out of memory for struct stream_ctx\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_free_mfi;
    }
  memset(st, 0, sizeof(struct stream_ctx));
  st->fd = -1;

  transcode = transcode_needed(req->input_headers, mfi->codectype);

  if (transcode)
    {
      DPRINTF(E_INFO, L_HTTPD, "Preparing to transcode %s\n", mfi->path);

      stream_cb = stream_chunk_xcode_cb;

      st->xcode = transcode_setup(mfi, &st->size, 1);
      if (!st->xcode)
	{
	  DPRINTF(E_WARN, L_HTTPD, "Transcoding setup failed, aborting streaming\n");

	  evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

	  goto out_free_st;
	}

      if (!evhttp_find_header(req->output_headers, "Content-Type"))
	evhttp_add_header(req->output_headers, "Content-Type", "audio/wav");
    }
  else
    {
      /* Stream the raw file */
      DPRINTF(E_INFO, L_HTTPD, "Preparing to stream %s\n", mfi->path);

      st->buf = (uint8_t *)malloc(STREAM_CHUNK_SIZE);
      if (!st->buf)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Out of memory for raw streaming buffer\n");

	  evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

	  goto out_free_st;
	}

      stream_cb = stream_chunk_raw_cb;

      st->fd = open(mfi->path, O_RDONLY);
      if (st->fd < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  goto out_cleanup;
	}

      ret = stat(mfi->path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  goto out_cleanup;
	}
      st->size = sb.st_size;

      pos = lseek(st->fd, offset, SEEK_SET);
      if (pos == (off_t) -1)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not seek into %s: %s\n", mfi->path, strerror(errno));

	  evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

	  goto out_cleanup;
	}
      st->offset = offset;
      st->end_offset = end_offset;

      /* Content-Type for video files is different than for audio files
       * and overrides whatever may have been set previously, like
       * application/x-dmap-tagged when we're speaking DAAP.
       */
      if (mfi->has_video)
	{
	  /* Front Row and others expect video/<type> */
	  ret = snprintf(buf, sizeof(buf), "video/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    {
	      evhttp_remove_header(req->output_headers, "Content-Type");
	      evhttp_add_header(req->output_headers, "Content-Type", buf);
	    }
	}
      /* If no Content-Type has been set and we're streaming audio, add a proper
       * Content-Type for the file we're streaming. Remember DAAP streams audio
       * with application/x-dmap-tagged as the Content-Type (ugh!).
       */
      else if (!evhttp_find_header(req->output_headers, "Content-Type") && mfi->type)
	{
	  ret = snprintf(buf, sizeof(buf), "audio/%s", mfi->type);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Type too large for buffer, dropping\n");
	  else
	    evhttp_add_header(req->output_headers, "Content-Type", buf);
	}
    }

  st->evbuf = evbuffer_new();
  if (!st->evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not allocate an evbuffer for streaming\n");

      evhttp_clear_headers(req->output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  ret = evbuffer_expand(st->evbuf, STREAM_CHUNK_SIZE);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not expand evbuffer for streaming\n");

      evhttp_clear_headers(req->output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  evutil_timerclear(&tv);
  event_set(&st->ev, -1, EV_TIMEOUT, stream_cb, st);
  event_base_set(evbase_httpd, &st->ev);
  ret = event_add(&st->ev, &tv);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not add one-shot event for streaming\n");

      evhttp_clear_headers(req->output_headers);
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      goto out_cleanup;
    }

  st->id = mfi->id;
  st->start_offset = offset;
  st->stream_size = st->size;
  st->req = req;

  if ((offset == 0) && (end_offset == 0))
    {
      /* If we are not decoding, send the Content-Length. We don't do
       * that if we are decoding because we can only guesstimate the
       * size in this case and the error margin is unknown and variable.
       */
      if (!transcode)
	{
	  ret = snprintf(buf, sizeof(buf), "%" PRIi64, (int64_t)st->size);
	  if ((ret < 0) || (ret >= sizeof(buf)))
	    DPRINTF(E_LOG, L_HTTPD, "Content-Length too large for buffer, dropping\n");
	  else
	    evhttp_add_header(req->output_headers, "Content-Length", buf);
	}

      evhttp_send_reply_start(req, HTTP_OK, "OK");
    }
  else
    {
      if (offset > 0)
	st->stream_size -= offset;
      if (end_offset > 0)
	st->stream_size -= (st->size - end_offset);

      DPRINTF(E_DBG, L_HTTPD, "Stream request with range %" PRIi64 "-%" PRIi64 "\n", offset, end_offset);

      ret = snprintf(buf, sizeof(buf), "bytes %" PRIi64 "-%" PRIi64 "/%" PRIi64,
		     offset, (end_offset) ? end_offset : (int64_t)st->size, (int64_t)st->size);
      if ((ret < 0) || (ret >= sizeof(buf)))
	DPRINTF(E_LOG, L_HTTPD, "Content-Range too large for buffer, dropping\n");
      else
	evhttp_add_header(req->output_headers, "Content-Range", buf);

      ret = snprintf(buf, sizeof(buf), "%" PRIi64, ((end_offset) ? end_offset + 1 : (int64_t)st->size) - offset);
      if ((ret < 0) || (ret >= sizeof(buf)))
	DPRINTF(E_LOG, L_HTTPD, "Content-Length too large for buffer, dropping\n");
      else
	evhttp_add_header(req->output_headers, "Content-Length", buf);

      evhttp_send_reply_start(req, 206, "Partial Content");
    }

#ifdef HAVE_POSIX_FADVISE
  if (!transcode)
    {
      /* Hint the OS */
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_WILLNEED);
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_SEQUENTIAL);
      posix_fadvise(st->fd, st->start_offset, st->stream_size, POSIX_FADV_NOREUSE);
    }
#endif

  evhttp_connection_set_closecb(req->evcon, stream_fail_cb, st);

  DPRINTF(E_INFO, L_HTTPD, "Kicking off streaming for %s\n", mfi->path);

  free_mfi(mfi, 0);

  return;

 out_cleanup:
  if (st->evbuf)
    evbuffer_free(st->evbuf);
  if (st->xcode)
    transcode_cleanup(st->xcode);
  if (st->buf)
    free(st->buf);
  if (st->fd > 0)
    close(st->fd);
 out_free_st:
  free(st);
 out_free_mfi:
  free_mfi(mfi, 0);
}

/* Thread: httpd */
void
httpd_send_reply(struct evhttp_request *req, int code, const char *reason, struct evbuffer *evbuf)
{
  unsigned char outbuf[128 * 1024];
  z_stream strm;
  struct evbuffer *gzbuf;
  const char *param;
  int flush;
  int zret;
  int ret;

  if (!evbuf || (EVBUFFER_LENGTH(evbuf) == 0))
    {
      DPRINTF(E_DBG, L_HTTPD, "Not gzipping body-less reply\n");

      goto no_gzip;
    }

  param = evhttp_find_header(req->input_headers, "Accept-Encoding");
  if (!param)
    {
      DPRINTF(E_DBG, L_HTTPD, "Not gzipping; no Accept-Encoding header\n");

      goto no_gzip;
    }
  else if (!strstr(param, "gzip") && !strstr(param, "*"))
    {
      DPRINTF(E_DBG, L_HTTPD, "Not gzipping; gzip not in Accept-Encoding (%s)\n", param);

      goto no_gzip;
    }

  gzbuf = evbuffer_new();
  if (!gzbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not allocate evbuffer for gzipped reply\n");

      goto no_gzip;
    }

  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;

  /* Set up a gzip stream (the "+ 16" in 15 + 16), instead of a zlib stream (default) */
  zret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (zret != Z_OK)
    {
      DPRINTF(E_DBG, L_HTTPD, "zlib setup failed: %s\n", zError(zret));

      goto out_fail_init;
    }

  strm.next_in = EVBUFFER_DATA(evbuf);
  strm.avail_in = EVBUFFER_LENGTH(evbuf);

  flush = Z_NO_FLUSH;

  /* 2 iterations: Z_NO_FLUSH until input is consumed, then Z_FINISH */
  for (;;)
    {
      do
	{
	  strm.next_out = outbuf;
	  strm.avail_out = sizeof(outbuf);

	  zret = deflate(&strm, flush);
	  if (zret == Z_STREAM_ERROR)
	    {
	      DPRINTF(E_LOG, L_HTTPD, "Could not deflate data: %s\n", strm.msg);

	      goto out_fail_gz;
	    }

	  ret = evbuffer_add(gzbuf, outbuf, sizeof(outbuf) - strm.avail_out);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_HTTPD, "Out of memory adding gzipped data to evbuffer\n");

	      goto out_fail_gz;
	    }
	}
      while (strm.avail_out == 0);

      if (flush == Z_FINISH)
	break;

      flush = Z_FINISH;
    }

  if (zret != Z_STREAM_END)
    {
      DPRINTF(E_LOG, L_HTTPD, "Compressed data not finalized!\n");

      goto out_fail_gz;
    }

  deflateEnd(&strm);

  evhttp_add_header(req->output_headers, "Content-Encoding", "gzip");
  evhttp_send_reply(req, code, reason, gzbuf);

  evbuffer_free(gzbuf);

  /* Drain original buffer, as would be after evhttp_send_reply() */
  evbuffer_drain(evbuf, EVBUFFER_LENGTH(evbuf));

  return;

 out_fail_gz:
  deflateEnd(&strm);
 out_fail_init:
  evbuffer_free(gzbuf);
 no_gzip:
  evhttp_send_reply(req, code, reason, evbuf);
}

/* Thread: httpd */
static int
path_is_legal(char *path)
{
  return strncmp(WEBFACE_ROOT, path, strlen(WEBFACE_ROOT));
}

/* Thread: httpd */
static void
redirect_to_index(struct evhttp_request *req, char *uri)
{
  char buf[256];
  int slashed;
  int ret;

  slashed = (uri[strlen(uri) - 1] == '/');

  ret = snprintf(buf, sizeof(buf), "%s%sindex.html", uri, (slashed) ? "" : "/");
  if ((ret < 0) || (ret >= sizeof(buf)))
    {
      DPRINTF(E_LOG, L_HTTPD, "Redirection URL exceeds buffer length\n");

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return;
    }

  evhttp_add_header(req->output_headers, "Location", buf);
  evhttp_send_reply(req, HTTP_MOVETEMP, "Moved", NULL);
}

/* Thread: httpd */
static void
serve_file(struct evhttp_request *req, char *uri)
{
  char *ext;
  char path[PATH_MAX];
  char *deref;
  char *ctype;
  char *passwd;
  struct evbuffer *evbuf;
  struct stat sb;
  int fd;
  int i;
  int ret;

  /* Check authentication */
  passwd = cfg_getstr(cfg_getsec(cfg, "general"), "admin_password");
  if (passwd)
    {
      DPRINTF(E_DBG, L_HTTPD, "Checking web interface authentication\n");

      ret = httpd_basic_auth(req, "admin", passwd, PACKAGE " web interface");
      if (ret != 0)
	return;

      DPRINTF(E_DBG, L_HTTPD, "Authentication successful\n");
    }
  else
    {
      if ((strcmp(req->remote_host, "::1") != 0)
	  && (strcmp(req->remote_host, "127.0.0.1") != 0))
	{
	  DPRINTF(E_LOG, L_HTTPD, "Remote web interface request denied; no password set\n");

	  evhttp_send_error(req, 403, "Forbidden");
	  return;
	}
    }

  ret = snprintf(path, sizeof(path), "%s%s", WEBFACE_ROOT, uri + 1); /* skip starting '/' */
  if ((ret < 0) || (ret >= sizeof(path)))
    {
      DPRINTF(E_LOG, L_HTTPD, "Request exceeds PATH_MAX: %s\n", uri);

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

      return;
    }

  ret = lstat(path, &sb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not lstat() %s: %s\n", path, strerror(errno));

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

      return;
    }

  if (S_ISDIR(sb.st_mode))
    {
      redirect_to_index(req, uri);

      return;
    }
  else if (S_ISLNK(sb.st_mode))
    {
      deref = m_realpath(path);
      if (!deref)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not dereference %s: %s\n", path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  return;
	}

      if (strlen(deref) + 1 > PATH_MAX)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Dereferenced path exceeds PATH_MAX: %s\n", path);

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  free(deref);
	  return;
	}

      strcpy(path, deref);
      free(deref);

      ret = stat(path, &sb);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Could not stat() %s: %s\n", path, strerror(errno));

	  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");

	  return;
	}

      if (S_ISDIR(sb.st_mode))
	{
	  redirect_to_index(req, uri);

	  return;
	}
    }

  if (path_is_legal(path) != 0)
    {
      evhttp_send_error(req, 403, "Forbidden");

      return;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not create evbuffer\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal error");
      return;
    }

  fd = open(path, O_RDONLY);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not open %s: %s\n", path, strerror(errno));

      evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
      return;
    }

  /* FIXME: this is broken, if we ever need to serve files here,
   * this must be fixed.
   */
  ret = evbuffer_read(evbuf, fd, sb.st_size);
  close(fd);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not read file into evbuffer\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal error");
      return;
    }

  ctype = "application/octet-stream";
  ext = strrchr(path, '.');
  if (ext)
    {
      for (i = 0; ext2ctype[i].ext; i++)
	{
	  if (strcmp(ext, ext2ctype[i].ext) == 0)
	    {
	      ctype = ext2ctype[i].ctype;
	      break;
	    }
	}
    }

  evhttp_add_header(req->output_headers, "Content-Type", ctype);

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  evbuffer_free(evbuf);
}

/* Thread: httpd */
static void
httpd_gen_cb(struct evhttp_request *req, void *arg)
{
  const char *req_uri;
  char *uri;
  char *ptr;

  req_uri = evhttp_request_uri(req);
  if (!req_uri)
    {
      redirect_to_index(req, "/");

      return;
    }

  uri = strdup(req_uri);
  ptr = strchr(uri, '?');
  if (ptr)
    {
      DPRINTF(E_DBG, L_HTTPD, "Found query string\n");

      *ptr = '\0';
    }

  ptr = uri;
  uri = evhttp_decode_uri(uri);
  free(ptr);

  /* Dispatch protocol-specific URIs */
  if (rsp_is_request(req, uri))
    {
      rsp_request(req);

      goto out;
    }
  else if (daap_is_request(req, uri))
    {
      daap_request(req);

      goto out;
    }
  else if (dacp_is_request(req, uri))
    {
      dacp_request(req);

      goto out;
    }

  DPRINTF(E_DBG, L_HTTPD, "HTTP request: %s\n", uri);

  /* Serve web interface files */
  serve_file(req, uri);

 out:
  free(uri);
}

/* Thread: httpd */
static void *
httpd(void *arg)
{
  int ret;

  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Error: DB init failed\n");

      pthread_exit(NULL);
    }

  event_base_dispatch(evbase_httpd);

  if (!httpd_exit)
    DPRINTF(E_FATAL, L_HTTPD, "HTTPd event loop terminated ahead of time!\n");

  db_perthread_deinit();

  pthread_exit(NULL);
}

/* Thread: httpd */
static void
exit_cb(int fd, short event, void *arg)
{
  event_base_loopbreak(evbase_httpd);

  httpd_exit = 1;
}

char *
httpd_fixup_uri(struct evhttp_request *req)
{
  const char *ua;
  const char *uri;
  const char *u;
  const char *q;
  char *fixed;
  char *f;
  int len;

  uri = evhttp_request_uri(req);
  if (!uri)
    return NULL;

  /* No query string, nothing to do */
  q = strchr(uri, '?');
  if (!q)
    return strdup(uri);

  ua = evhttp_find_header(req->input_headers, "User-Agent");
  if (!ua)
    return strdup(uri);

  if ((strncmp(ua, "iTunes", strlen("iTunes")) != 0)
      && (strncmp(ua, "Remote", strlen("Remote")) != 0)
      && (strncmp(ua, "Roku", strlen("Roku")) != 0))
    return strdup(uri);

  /* Reencode + as %2B and space as + in the query,
     which iTunes and Roku devices don't do */
  len = strlen(uri);

  u = q;
  while (*u)
    {
      if (*u == '+')
	len += 2;

      u++;
    }

  fixed = (char *)malloc(len + 1);
  if (!fixed)
    return NULL;

  strncpy(fixed, uri, q - uri);

  f = fixed + (q - uri);
  while (*q)
    {
      switch (*q)
	{
	  case '+':
	    *f = '%';
	    f++;
	    *f = '2';
	    f++;
	    *f = 'B';
	    break;

	  case ' ':
	    *f = '+';
	    break;

	  default:
	    *f = *q;
	    break;
	}

      q++;
      f++;
    }

  *f = '\0';

  return fixed;
}

static const char *http_reply_401 = "<html><head><title>401 Unauthorized</title></head><body>Authorization required</body></html>";

int
httpd_basic_auth(struct evhttp_request *req, char *user, char *passwd, char *realm)
{
  struct evbuffer *evbuf;
  char *header;
  const char *auth;
  char *authuser;
  char *authpwd;
  int len;
  int ret;

  auth = evhttp_find_header(req->input_headers, "Authorization");
  if (!auth)
    {
      DPRINTF(E_DBG, L_HTTPD, "No Authorization header\n");

      goto need_auth;
    }

  if (strncmp(auth, "Basic ", strlen("Basic ")) != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Bad Authentication header\n");

      goto need_auth;
    }

  auth += strlen("Basic ");

  authuser = b64_decode(auth);
  if (!authuser)
    {
      DPRINTF(E_LOG, L_HTTPD, "Could not decode Authentication header\n");

      goto need_auth;
    }

  authpwd = strchr(authuser, ':');
  if (!authpwd)
    {
      DPRINTF(E_LOG, L_HTTPD, "Malformed Authentication header\n");

      free(authuser);
      goto need_auth;
    }

  *authpwd = '\0';
  authpwd++;

  if (user)
    {
      if (strcmp(user, authuser) != 0)
	{
	  DPRINTF(E_LOG, L_HTTPD, "Username mismatch\n");

	  free(authuser);
	  goto need_auth;
	}
    }

  if (strcmp(passwd, authpwd) != 0)
    {
      DPRINTF(E_LOG, L_HTTPD, "Bad password\n");

      free(authuser);
      goto need_auth;
    }

  free(authuser);

  return 0;

 need_auth:
  len = strlen(realm) + strlen("Basic realm=") + 3;
  header = (char *)malloc(len);
  if (!header)
    {
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  ret = snprintf(header, len, "Basic realm=\"%s\"", realm);
  if ((ret < 0) || (ret >= len))
    {
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return -1;
    }

  evhttp_add_header(req->output_headers, "WWW-Authenticate", header);
  evbuffer_add(evbuf, http_reply_401, strlen(http_reply_401));
  evhttp_send_reply(req, 401, "Unauthorized", evbuf);

  free(header);
  evbuffer_free(evbuf);

  return -1;
}

/* Thread: main */
int
httpd_init(void)
{
  unsigned short port;
  int v6enabled;
  int ret;

  httpd_exit = 0;

  v6enabled = cfg_getbool(cfg_getsec(cfg, "general"), "ipv6");

  evbase_httpd = event_base_new();
  if (!evbase_httpd)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create an event base\n");

      return -1;
    }

  ret = rsp_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "RSP protocol init failed\n");

      goto rsp_fail;
    }

  ret = daap_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "DAAP protocol init failed\n");

      goto daap_fail;
    }

  ret = dacp_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "DACP protocol init failed\n");

      goto dacp_fail;
    }

#ifdef USE_EVENTFD
  exit_efd = eventfd(0, EFD_CLOEXEC);
  if (exit_efd < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create eventfd: %s\n", strerror(errno));

      goto pipe_fail;
    }
#else
# if defined(__linux__)
  ret = pipe2(exit_pipe, O_CLOEXEC);
# else
  ret = pipe(exit_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create pipe: %s\n", strerror(errno));

      goto pipe_fail;
    }
#endif /* USE_EVENTFD */

#ifdef USE_EVENTFD
  event_set(&exitev, exit_efd, EV_READ, exit_cb, NULL);
#else
  event_set(&exitev, exit_pipe[0], EV_READ, exit_cb, NULL);
#endif
  event_base_set(evbase_httpd, &exitev);
  event_add(&exitev, NULL);

  evhttpd = evhttp_new(evbase_httpd);
  if (!evhttpd)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not create HTTP server\n");

      goto evhttp_fail;
    }

  port = cfg_getint(cfg_getsec(cfg, "library"), "port");

  /* We are binding v6 and v4 separately, and we allow v6 to fail
   * as IPv6 might not be supported on the system.
   * We still warn about the failure, in case there's another issue.
   */
  ret = evhttp_bind_socket(evhttpd, "0.0.0.0", port);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not bind INADDR_ANY:%d\n", port);

      goto bind_fail;
    }

  if (v6enabled)
    {
      ret = evhttp_bind_socket(evhttpd, "::", port);
      if (ret < 0)
	DPRINTF(E_WARN, L_HTTPD, "Could not bind IN6ADDR_ANY:%d (that's OK)\n", port);
    }

  evhttp_set_gencb(evhttpd, httpd_gen_cb, NULL);

  ret = pthread_create(&tid_httpd, NULL, httpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not spawn HTTPd thread: %s\n", strerror(errno));

      goto thread_fail;
    }

  return 0;

 thread_fail:
 bind_fail:
  evhttp_free(evhttpd);
 evhttp_fail:
#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
 pipe_fail:
  dacp_deinit();
 dacp_fail:
  daap_deinit();
 daap_fail:
  rsp_deinit();
 rsp_fail:
  event_base_free(evbase_httpd);

  return -1;
}

/* Thread: main */
void
httpd_deinit(void)
{
  int ret;

#ifdef USE_EVENTFD
  ret = eventfd_write(exit_efd, 1);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not send exit event: %s\n", strerror(errno));

      return;
    }
#else
  int dummy = 42;

  ret = write(exit_pipe[1], &dummy, sizeof(dummy));
  if (ret != sizeof(dummy))
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not write to exit fd: %s\n", strerror(errno));

      return;
    }
#endif

  ret = pthread_join(tid_httpd, NULL);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_HTTPD, "Could not join HTTPd thread: %s\n", strerror(errno));

      return;
    }

  rsp_deinit();
  dacp_deinit();
  daap_deinit();

#ifdef USE_EVENTFD
  close(exit_efd);
#else
  close(exit_pipe[0]);
  close(exit_pipe[1]);
#endif
  evhttp_free(evhttpd);
  event_base_free(evbase_httpd);
}
