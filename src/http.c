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

#include <libavutil/opt.h>

#include <event2/event.h>

#include "http.h"
#include "logger.h"
#include "misc.h"

/* ======================= libevent HTTP client  =============================*/

// Number of seconds the client will wait for a response before aborting
#define HTTP_CLIENT_TIMEOUT 8

/* The strict libevent api does not permit walking through an evkeyvalq and saving
 * all the http headers, so we predefine what we are looking for. You can add 
 * extra headers here that you would like to save.
 */
static char *header_list[] =
{
  "icy-name",
  "icy-description",
  "icy-metaint",
  "icy-genre",
  "Content-Type",
};

/* Copies headers we are searching for from one keyval struct to another
 *
 */
static void
headers_save(struct keyval *kv, struct evkeyvalq *headers)
{
  const char *value;
  int i;

  if (!kv || !headers)
    return;

  for (i = 0; i < (sizeof(header_list) / sizeof(header_list[0])); i++)
    {
      if ( (value = evhttp_find_header(headers, header_list[i])) )
	keyval_add(kv, header_list[i], value);
    }
  
}

static void
request_cb(struct evhttp_request *req, void *arg)
{
  struct http_client_ctx *ctx;
  const char *response_code_line;
  int response_code;

  ctx = (struct http_client_ctx *)arg;

  if (ctx->headers_only)
    {
      ctx->ret = 0;

      event_base_loopbreak(ctx->evbase);

      return;
    }

  if (!req)
    {
      DPRINTF(E_WARN, L_HTTP, "Connection to %s failed: Connection timed out\n", ctx->url);
      goto connection_error;
    }

  response_code = evhttp_request_get_response_code(req);
#ifndef HAVE_LIBEVENT2_OLD
  response_code_line = evhttp_request_get_response_code_line(req);
#else
  response_code_line = "no error text";
#endif

  if (response_code == 0)
    {
      DPRINTF(E_WARN, L_HTTP, "Connection to %s failed: Connection refused\n", ctx->url);
      goto connection_error;
    }
  else if (response_code != 200)
    {
      DPRINTF(E_WARN, L_HTTP, "Connection to %s failed: %s (error %d)\n", ctx->url, response_code_line, response_code);
      goto connection_error;
    }

  ctx->ret = 0;

  if (ctx->headers)
    headers_save(ctx->headers, evhttp_request_get_input_headers(req));
  if (ctx->body)
    evbuffer_add_buffer(ctx->body, evhttp_request_get_input_buffer(req));
      
  event_base_loopbreak(ctx->evbase);

  return;

 connection_error:

  ctx->ret = -1;

  event_base_loopbreak(ctx->evbase);

  return;
}

/* This callback is only invoked if ctx->headers_only is set. Since that means
 * we only want headers, it will always return -1 to make evhttp close the 
 * connection. The headers will be saved in a keyval struct in ctx, since we
 * cannot address the *evkeyvalq after the connection is free'd.
 */
#ifndef HAVE_LIBEVENT2_OLD
static int
request_header_cb(struct evhttp_request *req, void *arg)
{
  struct http_client_ctx *ctx;

  ctx = (struct http_client_ctx *)arg;

  if (!ctx->headers)
    {
      DPRINTF(E_LOG, L_HTTP, "BUG: Header callback invoked but caller did not say where to save the headers\n");
      return -1;
    }

  headers_save(ctx->headers, evhttp_request_get_input_headers(req));

  return -1;
}
#endif

int
http_client_request(struct http_client_ctx *ctx)
{
  struct evhttp_connection *evcon;
  struct evhttp_request *req;
  struct evkeyvalq *headers;
  char hostname[PATH_MAX];
  char path[PATH_MAX];
  char s[PATH_MAX];
  int port;
  int ret;

  ctx->ret = -1;

  av_url_split(NULL, 0, NULL, 0, hostname, sizeof(hostname), &port, path, sizeof(path), ctx->url);
  if (strlen(hostname) == 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Error extracting hostname from URL: %s\n", ctx->url);

      return ctx->ret;
    }

  if (port <= 0)
    snprintf(s, PATH_MAX, "%s", hostname);
  else
    snprintf(s, PATH_MAX, "%s:%d", hostname, port);

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

      return ctx->ret;
    }

  evcon = evhttp_connection_base_new(ctx->evbase, NULL, hostname, (unsigned short)port);
  if (!evcon)
    {
      DPRINTF(E_LOG, L_HTTP, "Could not create connection to %s\n", hostname);

      event_base_free(ctx->evbase);
      return ctx->ret;
    }

  evhttp_connection_set_timeout(evcon, HTTP_CLIENT_TIMEOUT);
  
  /* Set up request */
  req = evhttp_request_new(request_cb, ctx);
  if (!req)
    {
      DPRINTF(E_LOG, L_HTTP, "Could not create request to %s\n", hostname);

      evhttp_connection_free(evcon);
      event_base_free(ctx->evbase);
      return ctx->ret;
    }

#ifndef HAVE_LIBEVENT2_OLD
  if (ctx->headers_only)
    evhttp_request_set_header_cb(req, request_header_cb);
#endif

  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "Host", s);
  evhttp_add_header(headers, "Content-Length", "0");
  evhttp_add_header(headers, "User-Agent", "forked-daapd/" VERSION);
  evhttp_add_header(headers, "Icy-MetaData", "1");

  /* Make request */
  DPRINTF(E_INFO, L_HTTP, "Making request for http://%s%s\n", s, path);

  ret = evhttp_make_request(evcon, req, EVHTTP_REQ_GET, path);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Error making request for http://%s%s\n", s, path);

      evhttp_connection_free(evcon);
      event_base_free(ctx->evbase);
      return ctx->ret;
    }

  event_base_dispatch(ctx->evbase);

  evhttp_connection_free(evcon);
  event_base_free(ctx->evbase);

  return ctx->ret;
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

  ctx.url = url;
  ctx.body = evbuf;

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
  while ((line = evbuffer_readln(ctx.body, NULL, EVBUFFER_EOL_ANY)) && (n < 10))
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

  evbuffer_free(ctx.body);

  if (n != -1)
    {
      DPRINTF(E_LOG, L_HTTP, "Couldn't find stream in internet playlist: %s\n", url);

      return -1;
    }

  *stream = line;

  return 0;
}


/* ======================= ICY metadata handling =============================*/


#if LIBAVFORMAT_VERSION_MAJOR >= 56 || (LIBAVFORMAT_VERSION_MAJOR == 55 && LIBAVFORMAT_VERSION_MINOR >= 13)
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

#elif defined(HAVE_LIBEVENT2_OLD)
struct http_icy_metadata *
http_icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  DPRINTF(E_INFO, L_HTTP, "Skipping Shoutcast metadata request for %s (requires libevent>=2.1.4 or libav 10)\n", fmtctx->filename);
  return NULL;
}

#else
/* Earlier versions of ffmpeg/libav do not seem to allow access to the http
 * headers, so we must instead open the stream ourselves to get the metadata.
 * Sorry about the extra connections, you radio streaming people!
 *
 * It is not possible to get the packet metadata with these versions of ffmpeg
 */
struct http_icy_metadata *
http_icy_metadata_get(AVFormatContext *fmtctx, int packet_only)
{
  struct http_icy_metadata *metadata;
  struct http_client_ctx ctx;
  struct keyval *kv;
  const char *value;
  int got_header;
  int ret;

  /* Can only get header metadata */
  if (packet_only)
    return NULL;

  kv = keyval_alloc();
  if (!kv)
    return NULL;

  memset(&ctx, 0, sizeof(struct http_client_ctx));
  ctx.url = fmtctx->filename;
  ctx.headers = kv;
  ctx.headers_only = 1;
  ctx.body = NULL;

  ret = http_client_request(&ctx);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_HTTP, "Error fetching %s\n", fmtctx->filename);

      free(kv);
      return NULL;
    }

  metadata = malloc(sizeof(struct http_icy_metadata));
  if (!metadata)
    return NULL;
  memset(metadata, 0, sizeof(struct http_icy_metadata));

  got_header = 0;
  if ( (value = keyval_get(ctx.headers, "icy-name")) )
    {
      metadata->name = strdup(value);
      got_header = 1;
    }
  if ( (value = keyval_get(ctx.headers, "icy-description")) )
    {
      metadata->description = strdup(value);
      got_header = 1;
    }
  if ( (value = keyval_get(ctx.headers, "icy-genre")) )
    {
      metadata->genre = strdup(value);
      got_header = 1;
    }

  keyval_clear(kv);
  free(kv);

  if (!got_header)
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
	);*/

  return metadata;
}
#endif

void
http_icy_metadata_free(struct http_icy_metadata *metadata, int content_only)
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

  if (!content_only)
    free(metadata);
}
