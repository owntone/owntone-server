/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 *
 * Rewritten from mt-daapd code:
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <netinet/in.h>
#endif

#include <event.h>
#include "evhttp/evhttp.h"

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
};

static void
free_icy(struct icy_ctx *ctx)
{
  if (ctx)
    free(ctx);
}

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
	return -1;
    }

  freeaddrinfo(result);
  return 0;
}

static void
scan_icy_request_cb(struct evhttp_request *req, void *arg)
{
  DPRINTF(E_DBG, L_SCAN, "ICY metadata request completed\n");

  status = ICY_DONE;
  return;
}

/* Will always return -1 to make evhttp close the connection - we only need the http headers */
static int
scan_icy_header_cb(struct evhttp_request *req, void *arg)
{
  struct media_file_info *mfi;
  const char *ptr;

  mfi = (struct media_file_info *)arg;

  if ( (ptr = evhttp_find_header(req->input_headers, "icy-name")) )
    {
      mfi->title = strdup(ptr);
      mfi->artist = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, name (title/artist) is %s\n", mfi->title);
    }
  if ( (ptr = evhttp_find_header(req->input_headers, "icy-description")) )
    {
      mfi->album = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, description (album) is %s\n", mfi->album);
    }
  if ( (ptr = evhttp_find_header(req->input_headers, "icy-genre")) )
    {
      mfi->genre = strdup(ptr);
      DPRINTF(E_DBG, L_SCAN, "Found ICY metadata, genre is %s\n", mfi->genre);
    }

  status = ICY_DONE;
  return -1;
}

int
scan_metadata_icy(char *url, struct media_file_info *mfi)
{
  struct evhttp_connection *evcon;
  struct evhttp_request *req;
  struct icy_ctx *ctx;
  int ret;
  int i;

  status = ICY_INIT;

  /* We can set this straight away */
  mfi->url = strdup(url);

  ctx = (struct icy_ctx *)malloc(sizeof(struct icy_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_SCAN, "Out of memory for ICY metadata context\n");

      goto no_icy;
    }
  memset(ctx, 0, sizeof(struct icy_ctx));

  ctx->url = url;

  /* TODO https */
  av_url_split(NULL, 0, NULL, 0, ctx->hostname, sizeof(ctx->hostname), &ctx->port, ctx->path, sizeof(ctx->path), ctx->url);
  if (ctx->port < 0)
    ctx->port = 80;

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
  evhttp_connection_set_timeout(evcon, ICY_TIMEOUT);
  
  /* Set up request */
  req = evhttp_request_new(scan_icy_request_cb, mfi);
  if (!req)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not create request to %s\n", ctx->hostname);

      evhttp_connection_free(evcon);
      goto no_icy;
    }
  req->header_cb = scan_icy_header_cb;
  evhttp_add_header(req->output_headers, "Host", ctx->hostname);
  evhttp_add_header(req->output_headers, "Icy-MetaData", "1");

  /* Make request */
  status = ICY_WAITING;
  ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, ctx->path);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_SCAN, "Could not make request to %s\n", ctx->hostname);

      status = ICY_DONE;
      evhttp_connection_free(evcon);
      goto no_icy;
    }
  DPRINTF(E_INFO, L_SCAN, "Making request to %s asking for ICY (Shoutcast) metadata\n", url);

  /* Can't count on server support for ICY metadata, so
   * while waiting for a reply make a parallel call to scan_metadata_ffmpeg.
   * This call will also determine final return value.
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

  /* Wait till ICY request completes or we reach timeout */
  for (i = 0; (status == ICY_WAITING) && (i <= ICY_TIMEOUT); i++)
    sleep(1);

  free_icy(ctx);

  DPRINTF(E_DBG, L_SCAN, "scan_metadata_icy exiting with status %d after waiting %d sec\n", status, i);

  return 1;
}
