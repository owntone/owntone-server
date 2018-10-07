/*
 * Copyright (C) 2018 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <regex.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "httpd_artworkapi.h"
#include "logger.h"
#include "misc.h"
#include "player.h"
#include "artwork.h"

static int
request_process(struct httpd_request *hreq, uint32_t *max_w, uint32_t *max_h)
{
  const char *param;
  int ret;

  *max_w = 0;
  *max_h = 0;

  param = evhttp_find_header(hreq->query, "maxwidth");
  if (param)
    {
      ret = safe_atou32(param, max_w);
      if (ret < 0)
	DPRINTF(E_LOG, L_WEB, "Invalid width in request: '%s'\n", hreq->uri_parsed->uri);
    }

  param = evhttp_find_header(hreq->query, "maxheight");
  if (param)
    {
      ret = safe_atou32(param, max_h);
      if (ret < 0)
	DPRINTF(E_LOG, L_WEB, "Invalid height in request: '%s'\n", hreq->uri_parsed->uri);
    }

  return 0;
}

static int
response_process(struct httpd_request *hreq, int format)
{
  struct evkeyvalq *headers;

  headers = evhttp_request_get_output_headers(hreq->req);

  if (format == ART_FMT_PNG)
    evhttp_add_header(headers, "Content-Type", "image/png");
  else if (format == ART_FMT_JPEG)
    evhttp_add_header(headers, "Content-Type", "image/jpeg");
  else
    return HTTP_NOCONTENT;

  return HTTP_OK;
}

static int
artworkapi_reply_nowplaying(struct httpd_request *hreq)
{
  uint32_t max_w;
  uint32_t max_h;
  uint32_t id;
  int ret;

  ret = request_process(hreq, &max_w, &max_h);
  if (ret != 0)
    return ret;

  ret = player_now_playing(&id);
  if (ret != 0)
    return HTTP_NOTFOUND;

  ret = artwork_get_item(hreq->reply, id, max_w, max_h);

  return response_process(hreq, ret);
}

static int
artworkapi_reply_item(struct httpd_request *hreq)
{
  uint32_t max_w;
  uint32_t max_h;
  uint32_t id;
  int ret;

  ret = request_process(hreq, &max_w, &max_h);
  if (ret != 0)
    return ret;

  ret = safe_atou32(hreq->uri_parsed->path_parts[2], &id);
  if (ret != 0)
    return HTTP_BADREQUEST;

  ret = artwork_get_item(hreq->reply, id, max_w, max_h);

  return response_process(hreq, ret);
}

static int
artworkapi_reply_group(struct httpd_request *hreq)
{
  uint32_t max_w;
  uint32_t max_h;
  uint32_t id;
  int ret;

  ret = request_process(hreq, &max_w, &max_h);
  if (ret != 0)
    return ret;

  ret = safe_atou32(hreq->uri_parsed->path_parts[2], &id);
  if (ret != 0)
    return HTTP_BADREQUEST;

  ret = artwork_get_group(hreq->reply, id, max_w, max_h);

  return response_process(hreq, ret);
}

static struct httpd_uri_map artworkapi_handlers[] =
{
  { EVHTTP_REQ_GET, "^/artwork/nowplaying$",         artworkapi_reply_nowplaying },
  { EVHTTP_REQ_GET, "^/artwork/item/[[:digit:]]+$",  artworkapi_reply_item },
  { EVHTTP_REQ_GET, "^/artwork/group/[[:digit:]]+$", artworkapi_reply_group },
  { 0, NULL, NULL }
};


/* ------------------------------- API --------------------------------- */
void
artworkapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;
  int status_code;

  DPRINTF(E_DBG, L_WEB, "Artwork api request: '%s'\n", uri_parsed->uri);

  if (!httpd_admin_check_auth(req))
    return;

  hreq = httpd_request_parse(req, uri_parsed, NULL, artworkapi_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path '%s' in artwork api request: '%s'\n", uri_parsed->path, uri_parsed->uri);

      httpd_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  CHECK_NULL(L_WEB, hreq->reply = evbuffer_new());

  status_code = hreq->handler(hreq);

  switch (status_code)
    {
      case HTTP_OK:                  /* 200 OK */
	httpd_send_reply(req, status_code, "OK", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOCONTENT:           /* 204 No Content */
	httpd_send_reply(req, status_code, "No Content", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOTMODIFIED:         /* 304 Not Modified */
	httpd_send_reply(req, HTTP_NOTMODIFIED, NULL, NULL, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_BADREQUEST:          /* 400 Bad Request */
	httpd_send_error(req, status_code, "Bad Request");
	break;
      case HTTP_NOTFOUND:            /* 404 Not Found */
	httpd_send_error(req, status_code, "Not Found");
	break;
      case HTTP_INTERNAL:            /* 500 Internal Server Error */
      default:
	httpd_send_error(req, HTTP_INTERNAL, "Internal Server Error");
    }

  evbuffer_free(hreq->reply);
  free(hreq);
}

int
artworkapi_is_request(const char *path)
{
  if (strncmp(path, "/artwork/", strlen("/artwork/")) == 0)
    return 1;

  return 0;
}

int
artworkapi_init(void)
{
  char buf[64];
  int i;
  int ret;

  for (i = 0; artworkapi_handlers[i].handler; i++)
    {
      ret = regcomp(&artworkapi_handlers[i].preg, artworkapi_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
	{
	  regerror(ret, &artworkapi_handlers[i].preg, buf, sizeof(buf));

	  DPRINTF(E_FATAL, L_WEB, "artwork api init failed; regexp error: %s\n", buf);
	  return -1;
	}
    }

  return 0;
}

void
artworkapi_deinit(void)
{
  int i;

  for (i = 0; artworkapi_handlers[i].handler; i++)
    regfree(&artworkapi_handlers[i].preg);
}
