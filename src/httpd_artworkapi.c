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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "httpd_internal.h"
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

  param = httpd_query_value_find(hreq->query, "maxwidth");
  if (param)
    {
      ret = safe_atou32(param, max_w);
      if (ret < 0)
	DPRINTF(E_LOG, L_WEB, "Invalid width in request: '%s'\n", hreq->uri);
    }

  param = httpd_query_value_find(hreq->query, "maxheight");
  if (param)
    {
      ret = safe_atou32(param, max_h);
      if (ret < 0)
	DPRINTF(E_LOG, L_WEB, "Invalid height in request: '%s'\n", hreq->uri);
    }

  return 0;
}

static int
response_process(struct httpd_request *hreq, int format)
{
  if (format == ART_FMT_PNG)
    httpd_header_add(hreq->out_headers, "Content-Type", "image/png");
  else if (format == ART_FMT_JPEG)
    httpd_header_add(hreq->out_headers, "Content-Type", "image/jpeg");
  else
    return HTTP_NOCONTENT;

  return HTTP_OK;
}

static int
artworkapi_reply_nowplaying(struct httpd_request *hreq)
{
  struct player_status status;
  uint32_t max_w;
  uint32_t max_h;
  int ret;

  ret = request_process(hreq, &max_w, &max_h);
  if (ret != 0)
    return ret;

  player_get_status(&status);
  if (status.status == PLAY_STOPPED)
    return HTTP_NOTFOUND;

  ret = artwork_get_by_queue_item_id(hreq->out_body, status.item_id, max_w, max_h, 0);

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

  ret = safe_atou32(hreq->path_parts[2], &id);
  if (ret != 0)
    return HTTP_BADREQUEST;

  ret = artwork_get_by_file_id(hreq->out_body, id, max_w, max_h, 0);

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

  ret = safe_atou32(hreq->path_parts[2], &id);
  if (ret != 0)
    return HTTP_BADREQUEST;

  ret = artwork_get_by_group_id(hreq->out_body, id, max_w, max_h, 0);

  return response_process(hreq, ret);
}

static struct httpd_uri_map artworkapi_handlers[] =
{
  { HTTPD_METHOD_GET, "^/artwork/nowplaying$",         artworkapi_reply_nowplaying },
  { HTTPD_METHOD_GET, "^/artwork/item/[[:digit:]]+$",  artworkapi_reply_item },
  { HTTPD_METHOD_GET, "^/artwork/group/[[:digit:]]+$", artworkapi_reply_group },
  { 0, NULL, NULL }
};


/* ------------------------------- API --------------------------------- */

static void
artworkapi_request(struct httpd_request *hreq)
{
  int status_code;

  if (!httpd_request_is_authorized(hreq))
    return;

  if (!hreq->handler)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path in artwork api request: '%s'\n", hreq->uri);

      httpd_send_error(hreq, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  status_code = hreq->handler(hreq);

  switch (status_code)
    {
      case HTTP_OK:                  /* 200 OK */
	httpd_send_reply(hreq, status_code, "OK", HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOCONTENT:           /* 204 No Content */
	httpd_send_reply(hreq, status_code, "No Content", HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_NOTMODIFIED:         /* 304 Not Modified */
	httpd_send_reply(hreq, HTTP_NOTMODIFIED, NULL, HTTPD_SEND_NO_GZIP);
	break;
      case HTTP_BADREQUEST:          /* 400 Bad Request */
	httpd_send_error(hreq, status_code, "Bad Request");
	break;
      case HTTP_NOTFOUND:            /* 404 Not Found */
	httpd_send_error(hreq, status_code, "Not Found");
	break;
      case HTTP_INTERNAL:            /* 500 Internal Server Error */
      default:
	httpd_send_error(hreq, HTTP_INTERNAL, "Internal Server Error");
    }
}

struct httpd_module httpd_artworkapi =
{
  .name = "Artwork API",
  .type = MODULE_ARTWORKAPI,
  .logdomain = L_WEB,
  .subpaths = { "/artwork/", NULL },
  .handlers = artworkapi_handlers,
  .request = artworkapi_request,
};
