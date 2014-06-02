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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <netinet/in.h>
#endif

#include <event.h>
#if defined HAVE_LIBEVENT2
# include <event2/http.h>
#else
# include "evhttp/evhttp_compat.h"
#endif

#include <libavformat/avformat.h>

#include "logger.h"
#include "filescanner.h"
#include "misc.h"

#define ICY_TIMEOUT 3

enum icy_request_status { ICY_INIT, ICY_WAITING, ICY_DONE };

static enum icy_request_status status;

/* TODO why doesn't evbase_scan work... */
extern struct event_base *evbase_main;

struct icy_ctx
{
  char *url;
  char address[INET6_ADDRSTRLEN];
  char hostname[PATH_MAX];
  char path[PATH_MAX];
  int port;

  char *icy_name;
  char *icy_description;
  char *icy_genre;

  pthread_mutex_t lck;
  pthread_cond_t cond;
};

#ifndef HAVE_LIBEVENT2
static int
resolve_address(char *hostname, char *s, size_t maxlen)
{
  struct addrinfo *result;
  int ret;

  ret = getaddrinfo(hostname, NULL, NULL, &result);
  if (ret != 0)
    return -1;
 
  switch(result->ai_addr->sa_family)
    {
      case AF_INET:
	inet_ntop(AF_INET, &(((struct sockaddr_in *)result->ai_addr)->sin_addr), s, maxlen);
	break;

      case AF_INET6:
	inet_ntop(AF_INET6, &(((struct sockaddr_in6 *)result->ai_addr)->sin6_addr), s, maxlen);
	break;

      default:
	strncpy(s, "Unknown AF", maxlen);
	freeaddrinfo(result);
	return -1;
    }

  freeaddrinfo(result);
  return 0;
}
#endif

#ifndef HAVE_LIBEVENT2_OLD
static void
scan_icy_request_cb(struct evhttp_request *req, void *arg)
{
  struct icy_ctx *ctx;

  ctx = (struct icy_ctx *)arg;

  pthread_mutex_lock(&ctx->lck);

  DPRINTF(E_DBG, L_SCAN, "ICY metadata request: Signal callback\n");

  status = ICY_DONE;
  pthread_cond_signal(&ctx->cond);
  pthread_mutex_unlock(&ctx->lck);
}

/* Will always return -1 to make evhttp close the connection - we only need the http headers */
static int
scan_icy_header_cb(struct evhttp_request *req, void *arg)
{
  struct icy_ctx *ctx;
  struct evkeyvalq *headers;
  const char *ptr;

  ctx = (struct icy_ctx *)arg;

  DPRINTF(E_DBG, L_SCAN, "ICY metadata request: Headers received\n");

  headers = evhttp_request_get_input_headers(req);
  if ( (ptr = evhttp_find_header(headers, "icy-name")) )
    {
      ctx->icy_name = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, name is %s\n", ctx->icy_name);
    }
  if ( (ptr = evhttp_find_header(headers, "icy-description")) )
    {
      ctx->icy_description = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, description is %s\n", ctx->icy_description);
    }
  if ( (ptr = evhttp_find_header(headers, "icy-genre")) )
    {
      ctx->icy_genre = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, genre is %s\n", ctx->icy_genre);
    }

  return -1;
}
#endif

int
scan_metadata_icy(char *url, struct media_file_info *mfi)
{
  struct icy_ctx *ctx;
  struct evhttp_connection *evcon;
#ifndef HAVE_LIBEVENT2_OLD
  struct evhttp_request *req;
  struct evkeyvalq *headers;
  char s[PATH_MAX];
#endif
  time_t start;
  time_t end;
  int ret;

  status = ICY_INIT;
  start = time(NULL);

  /* We can set this straight away */
  mfi->url = strdup(url);

  ctx = (struct icy_ctx *)malloc(sizeof(struct icy_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_SCAN, "Out of memory for ICY metadata context\n");

      return -1;
    }
  memset(ctx, 0, sizeof(struct icy_ctx));

  pthread_mutex_init(&ctx->lck, NULL);
  pthread_cond_init(&ctx->cond, NULL);

  ctx->url = url;

  /* TODO https */
  av_url_split(NULL, 0, NULL, 0, ctx->hostname, sizeof(ctx->hostname), &ctx->port, ctx->path, sizeof(ctx->path), ctx->url);
  if ((!ctx->hostname) || (strlen(ctx->hostname) == 0))
    {
      DPRINTF(E_LOG, L_SCAN, "Error extracting hostname from playlist URL: %s\n", ctx->url);

      return -1;
    }

  if (ctx->port < 0)
    ctx->port = 80;

  if (strlen(ctx->path) == 0)
    {
      ctx->path[0] = '/';
      ctx->path[1] = '\0';
    }

#ifdef HAVE_LIBEVENT2
  evcon = evhttp_connection_base_new(evbase_main, NULL, ctx->hostname, (unsigned short)ctx->port);
  if (!evcon)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create connection to %s\n", ctx->hostname);

      goto no_icy;
    }
#else
  /* Resolve IP address */
  ret = resolve_address(ctx->hostname, ctx->address, sizeof(ctx->address));
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not find IP address of %s\n", ctx->hostname);

      return -1;
    }

  DPRINTF(E_DBG, L_SCAN, "URL %s converted to hostname %s, port %d, path %s, IP %s\n", ctx->url, ctx->hostname, ctx->port, ctx->path, ctx->address);

  /* Set up connection */
  evcon = evhttp_connection_new(ctx->address, (unsigned short)ctx->port);
  if (!evcon)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create connection to %s\n", ctx->hostname);

      goto no_icy;
    }
  evhttp_connection_set_base(evcon, evbase_main);
#endif

#ifdef HAVE_LIBEVENT2_OLD
  DPRINTF(E_LOG, L_SCAN, "Skipping Shoutcast metadata request for %s (requires libevent>=2.1.4 or libav 10)\n", ctx->hostname);
#else
  evhttp_connection_set_timeout(evcon, ICY_TIMEOUT);
  
  /* Set up request */
  req = evhttp_request_new(scan_icy_request_cb, ctx);
  if (!req)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create request to %s\n", ctx->hostname);

      goto no_icy;
    }

  evhttp_request_set_header_cb(req, scan_icy_header_cb);

  headers = evhttp_request_get_output_headers(req);
  snprintf(s, PATH_MAX, "%s:%d", ctx->hostname, ctx->port);
  evhttp_add_header(headers, "Host", s);
  evhttp_add_header(headers, "Icy-MetaData", "1");

  /* Make request */
  DPRINTF(E_INFO, L_SCAN, "Making request to %s asking for ICY (Shoutcast) metadata\n", ctx->hostname);

  status = ICY_WAITING;
  ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, ctx->path);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Error making request to %s\n", ctx->hostname);

      status = ICY_DONE;
      goto no_icy;
    }
#endif

  /* Can't count on server support for ICY metadata, so
   * while waiting for a reply make a parallel call to scan_metadata_ffmpeg.
   */
 no_icy:
  ret = scan_metadata_ffmpeg(url, mfi);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Playlist URL is unavailable for probe/metadata, assuming MP3 encoding\n");
      mfi->type = strdup("mp3");
      mfi->codectype = strdup("mpeg");
      mfi->description = strdup("MPEG audio file");
    }

  /* Wait for ICY request to complete or timeout */
  pthread_mutex_lock(&ctx->lck);

  if (status == ICY_WAITING)
    pthread_cond_wait(&ctx->cond, &ctx->lck);

  pthread_mutex_unlock(&ctx->lck);

  /* Copy result to mfi */
  if (ctx->icy_name)
    {
      if (mfi->title)
	free(mfi->title);
      if (mfi->artist)
	free(mfi->artist);
      if (mfi->album_artist)
	free(mfi->album_artist);

      mfi->title = strdup(ctx->icy_name);
      mfi->artist = strdup(ctx->icy_name);
      mfi->album_artist = strdup(ctx->icy_name);

      free(ctx->icy_name);
    }

  if (ctx->icy_description)
    {
      if (mfi->album)
	free(mfi->album);

      mfi->album = ctx->icy_description;
    }

  if (ctx->icy_genre)
    {
      if (mfi->genre)
	free(mfi->genre);

      mfi->genre = ctx->icy_genre;
    }

  /* Clean up */
  if (evcon)
    evhttp_connection_free(evcon);

  pthread_cond_destroy(&ctx->cond);
  pthread_mutex_destroy(&ctx->lck);
  free(ctx);

  end = time(NULL);

  DPRINTF(E_DBG, L_SCAN, "ICY metadata scan of %s completed in %.f sec\n", url, difftime(end, start));

  return 1;
}
