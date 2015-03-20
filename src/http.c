/*
 * Copyright (C) 2015 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <pthread.h>

#include <libavutil/opt.h>

#include <event2/event.h>

#include "http.h"
#include "logger.h"
#include "misc.h"

/* ======================= libevent HTTP client  =============================*/

// Number of seconds the client will wait for a response before aborting
#define HTTP_CLIENT_TIMEOUT 5

static void
request_cb(struct evhttp_request *req, void *arg)
{
  struct http_client_ctx *ctx;
  const char *response_code_line;
  int response_code;

  ctx = (struct http_client_ctx *)arg;

  response_code = evhttp_request_get_response_code(req);
  response_code_line = evhttp_request_get_response_code_line(req);

  if (req == NULL)
    {
      DPRINTF(E_LOG, L_HTTP, "Connection to %s failed: Connection timed out\n", ctx->url);
      goto connection_error;
    }
  else if (response_code == 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Connection to %s failed: Connection refused\n", ctx->url);
      goto connection_error;
    }
  else if (response_code != 200)
    {
      DPRINTF(E_LOG, L_HTTP, "Connection to %s failed: %s (error %d)\n", ctx->url, response_code_line, response_code);
      goto connection_error;
    }

  /* Async: we make a callback to caller, Sync: we move the body into the callers evbuf */
  ctx->ret = 0;

  if (ctx->async)
    ctx->cb(req, arg);
  else
    evbuffer_add_buffer(ctx->evbuf, evhttp_request_get_input_buffer(req));
      
  event_base_loopbreak(ctx->evbase);

  if (ctx->async)
    free(ctx);

  return;

 connection_error:

  ctx->ret = -1;

  event_base_loopbreak(ctx->evbase);

  if (ctx->async)
    free(ctx);

  return;
}

static void *
request_make(void *arg)
{
  struct http_client_ctx *ctx;
  struct evhttp_connection *evcon;
  struct evhttp_request *req;
  struct evkeyvalq *headers;
  char hostname[PATH_MAX];
  char path[PATH_MAX];
  char s[PATH_MAX];
  int port;
  int ret;

  ctx = (struct http_client_ctx *)arg;

  ctx->ret = -1;

  av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, path, sizeof(path), ctx->url);
  if (strlen(hostname) == 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Error extracting hostname from URL: %s\n", ctx->url);

      return NULL;
    }

  if (port <= 0)
    port = 80;

  if (strlen(path) == 0)
    {
      path[0] = '/';
      path[1] = '\0';
    }

  ctx->evbase = event_base_new();
  if (!ctx->evbase)
    {
      DPRINTF(E_LOG, L_HTTP, "Could not create or find http client event base\n");

      return NULL;
    }

  evcon = evhttp_connection_base_new(ctx->evbase, NULL, hostname, (unsigned short)port);
  if (!evcon)
    {
      DPRINTF(E_LOG, L_HTTP, "Could not create connection to %s\n", hostname);

      event_base_free(ctx->evbase);
      return NULL;
    }

  evhttp_connection_set_timeout(evcon, HTTP_CLIENT_TIMEOUT);
  
  /* Set up request */
  req = evhttp_request_new(request_cb, ctx);
  if (!req)
    {
      DPRINTF(E_LOG, L_HTTP, "Could not create request to %s\n", hostname);

      evhttp_connection_free(evcon);
      event_base_free(ctx->evbase);
      return NULL;
    }

  headers = evhttp_request_get_output_headers(req);
  snprintf(s, PATH_MAX, "%s:%d", hostname, port);
  evhttp_add_header(headers, "Host", s);
  evhttp_add_header(headers, "Content-Length", "0");
  evhttp_add_header(headers, "User-Agent", "forked-daapd/" VERSION);

  /* Make request */
  DPRINTF(E_INFO, L_HTTP, "Making request to %s asking for playlist\n", hostname);

  ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, path);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Error making http request to %s\n", hostname);

      evhttp_connection_free(evcon);
      event_base_free(ctx->evbase);
      return NULL;
    }

  event_base_dispatch(ctx->evbase);

  evhttp_connection_free(evcon);
  event_base_free(ctx->evbase);

  return NULL;
}

int
http_client_request(struct http_client_ctx *ctx)
{
  pthread_t tid;
  pthread_attr_t attr;  
  int ret;

  /* If async make the request in a spawned thread, otherwise just make it */
  if (ctx->async)
    {
      ret = pthread_attr_init(&attr);
      if (ret != 0)
	{
	  DPRINTF(E_LOG, L_HTTP, "Error in http_client_request: Could not init attributes\n");
          return -1;
        }

      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      ret = pthread_create(&tid, &attr, request_make, ctx);
      if (ret != 0)
	{
	  DPRINTF(E_LOG, L_HTTP, "Error in http_client_request: Could not create thread\n");
	  return -1;
	}
    }
  else
    {
      request_make(ctx);
      ret = ctx->ret;
    }

  return ret;
}

int
http_stream_setup(char **stream, const char *url)
{
  struct http_client_ctx ctx;
  struct evbuffer *evbuf;
  const char *ext;
  char *line;
  int ret;
  int n;

  *stream = NULL;

  ext = strrchr(url, '.');
  if (strcasecmp(ext, ".m3u") != 0)
    {
      *stream = strdup(url);
      return 0;
    }

  // It was a m3u playlist, so now retrieve it
  memset(&ctx, 0, sizeof(struct http_client_ctx));

  evbuf = evbuffer_new();
  if (!evbuf)
    return -1;

  ctx.async = 0;
  ctx.url = url;
  ctx.evbuf = evbuf;

  ret = http_client_request(&ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Couldn't fetch internet playlist: %s\n", url);

      evbuffer_free(evbuf);
      return -1;
    }

  /* Read the playlist until the first stream link is found, but give up if
   * nothing is found in the first 10 lines
   */
  n = 0;
  while ((line = evbuffer_readln(ctx.evbuf, NULL, EVBUFFER_EOL_ANY)) && (n < 10))
    {
      n++;
      if (strncasecmp(line, "http://", strlen("http://")) == 0)
	{
	  DPRINTF(E_DBG, L_HTTP, "Found internet playlist stream (line %d): %s\n", n, line);

	  n = -1;
	  break;
	}

      free(line);
    }

  evbuffer_free(ctx.evbuf);

  if (n != -1)
    {
      DPRINTF(E_LOG, L_HTTP, "Couldn't find stream in internet playlist: %s\n", url);

      return -1;
    }

  *stream = line;

  return 0;
}


/* ======================= ICY metadata handling =============================*/

static int
metadata_packet_get(struct http_icy_metadata *metadata, AVFormatContext *fmtctx)
{
  uint8_t *buffer;
  char *icy_token;
  char *ptr;
  char *end;

  av_opt_get(fmtctx, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &buffer);
  if (!buffer)
    return -1;

  icy_token = strtok((char *)buffer, ";");
  while (icy_token != NULL)
    {
      ptr = strchr(icy_token, '=');
      if (!ptr || (ptr[1] == '\0'))
	{
	  icy_token = strtok(NULL, ";");
	  continue;
	}

      ptr++;
      if (ptr[0] == '\'')
	ptr++;

      end = strrchr(ptr, '\'');
      if (end)
        *end = '\0';

      if ((strncmp(icy_token, "StreamTitle", strlen("StreamTitle")) == 0) && !metadata->title)
	{
	  metadata->title = ptr;

	  /* Dash separates artist from title, if no dash assume all is title */
	  ptr = strstr(ptr, " - ");
	  if (ptr)
	    {
	      *ptr = '\0';
	      metadata->title = strdup(metadata->title);
	      *ptr = ' ';

	      metadata->artist = strdup(ptr + 3);
	    }
	  else
	    metadata->title = strdup(metadata->title);
	}
      else if ((strncmp(icy_token, "StreamUrl", strlen("StreamUrl")) == 0) && !metadata->artwork_url)
	{
	  metadata->artwork_url = strdup(ptr);
	}

      if (end)
	*end = '\'';

      icy_token = strtok(NULL, ";");
    }
  av_free(buffer);

  if (metadata->title)
    metadata->hash = djb_hash(metadata->title, strlen(metadata->title));

  return 0;
}

static int
metadata_header_get(struct http_icy_metadata *metadata, AVFormatContext *fmtctx)
{
  uint8_t *buffer;
  char *icy_token;
  char *ptr;

  av_opt_get(fmtctx, "icy_metadata_headers", AV_OPT_SEARCH_CHILDREN, &buffer);
  if (!buffer)
    return -1;

  icy_token = strtok((char *)buffer, "\r\n");
  while (icy_token != NULL)
    {
      ptr = strchr(icy_token, ':');
      if (!ptr || (ptr[1] == '\0'))
	{
	  icy_token = strtok(NULL, "\r\n");
	  continue;
	}

      ptr++;
      if (ptr[0] == ' ')
	ptr++;

      if ((strncmp(icy_token, "icy-name", strlen("icy-name")) == 0) && !metadata->name)
	metadata->name = strdup(ptr);
      else if ((strncmp(icy_token, "icy-description", strlen("icy-description")) == 0) && !metadata->description)
	metadata->description = strdup(ptr);
      else if ((strncmp(icy_token, "icy-genre", strlen("icy-genre")) == 0) && !metadata->genre)
	metadata->genre = strdup(ptr);

      icy_token = strtok(NULL, "\r\n");
    }
  av_free(buffer);

  return 0;
}

void
http_icy_metadata_free(struct http_icy_metadata *metadata)
{
  if (metadata->name)
    free(metadata->name);

  if (metadata->description)
    free(metadata->description);

  if (metadata->genre)
    free(metadata->genre);

  if (metadata->title)
    free(metadata->title);

  if (metadata->artist)
    free(metadata->artist);

  if (metadata->artwork_url)
    free(metadata->artwork_url);

  free(metadata);
}

#if LIBAVFORMAT_VERSION_MAJOR >= 56 || (LIBAVFORMAT_VERSION_MAJOR == 55 && LIBAVFORMAT_VERSION_MINOR >= 13)
struct http_icy_metadata *
http_icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  struct http_icy_metadata *metadata;
  int got_packet;
  int got_header;

  metadata = malloc(sizeof(struct http_icy_metadata));
  if (!metadata)
    return NULL;
  memset(metadata, 0, sizeof(struct http_icy_metadata));

  got_packet = (metadata_packet_get(metadata, fmtctx) == 0);
  got_header = (!packet_only) && (metadata_header_get(metadata, fmtctx) == 0);

  if (!got_packet && !got_header)
   {
     free(metadata);
     return NULL;
   }

/*  DPRINTF(E_DBG, L_HTTP, "Found ICY: N %s, D %s, G %s, T %s, A %s, U %s, I %" PRIu32 "\n",
	metadata->name,
	metadata->description,
	metadata->genre,
	metadata->title,
	metadata->artist,
	metadata->artwork_url,
	metadata->hash
	);
*/
  return metadata;
}
#else
struct http_icy_metadata *
http_icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  return NULL;
}
#endif

