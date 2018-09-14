/*
 * Copyright (C) 2018 Ray/whatdoineed2do <whatdoineed2do@nospam.gmail.com>
 *
 * Adapted from httpd_jsonapi.c:
 * Copyright (C) 2017 Christian Meffert <christian.meffert@googlemail.com>
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
#include <string.h>

#include "httpd_artwork.h"
#include "db.h"
#include "library.h"
#include "logger.h"
#include "misc.h"
#include "player.h"
#include "artwork.h"


static int
artwork_to_evbuf(struct httpd_request *hreq, struct db_group_info *dbgri, uint32_t *dbmfi_id)
{
  int  ret = 0;
  int  len = 0;
  const char*  ctype = "invalid";
  char  clen[32];
  struct evkeyvalq *headers;

  if ( (dbgri == NULL && dbmfi_id == NULL) || (dbgri && dbmfi_id)) 
    {
      return -1;
    }

  if (dbgri)
    {
      int  gpid;
      safe_atoi32(dbgri->id, &gpid);
      if ( (ret = artwork_get_group(hreq->reply, gpid, 600, 600)) < 0)
        {
          DPRINTF(E_LOG, L_WEB, "artwork: failed to retreive for group %d\n", gpid);
        }
    }
  if (dbmfi_id)
    {
      if ( (ret = artwork_get_item(hreq->reply, *dbmfi_id, 600, 600)) < 0)
        {
          DPRINTF(E_LOG, L_WEB, "artwork: failed to retreive for item %d\n", *dbmfi_id);
        }
    }

  len = evbuffer_get_length(hreq->reply);
  switch (ret)
    {
      case ART_FMT_PNG:
        ctype = "image/png";
        break;

      case ART_FMT_JPEG:
        ctype = "image/jpeg";
        break;

      default:
        if (len > 0) evbuffer_drain(hreq->reply, len);
        ret = -1;
    }
  DPRINTF(E_DBG, L_WEB, "artwork: via %s  type=%s len=%d\n", (dbgri ? "group" : "item"), ctype, len);

  if (ret)
    {
      headers = evhttp_request_get_output_headers(hreq->req);
      evhttp_remove_header(headers, "Content-Type");
      evhttp_add_header(headers, "Content-Type", ctype);
      snprintf(clen, sizeof(clen), "%ld", (long)len);
      evhttp_add_header(headers, "Content-Length", clen);

      ret = 0;
    }
  return ret;
}

static int
fetch_artwork(struct httpd_request *hreq, const char *artist_id, const char *album_id)
{
  struct db_group_info dbgri;
  struct query_params query_params;
  int ret;

  if (artist_id == NULL && album_id == NULL || (artist_id && album_id)) 
    {
      return -1;
    }
  memset(&query_params, 0, sizeof(struct query_params));
  memset(&dbgri, 0, sizeof(dbgri));

  if (album_id) 
    {
      query_params.type = Q_GROUP_ALBUMS;
      query_params.sort = S_ALBUM;
      query_params.filter = db_mprintf("(f.songalbumid = %s)", album_id);
    }
  else if (artist_id)
    {
      query_params.type = Q_GROUP_ARTISTS;
      query_params.sort = S_ARTIST;
      query_params.filter = db_mprintf("(f.songartistid = %s)", artist_id);
    }

  if ( (ret = db_query_start(&query_params)) < 0) 
    {
      goto error;
    }

  if ( (ret = db_query_fetch_group(&query_params, &dbgri)) < 0) 
    {
      DPRINTF(E_LOG, L_WEB, "artwork: Couldn't find dbgri\n");
    }
  else 
    {
      ret = (dbgri.id == NULL) ? 
                  -1 : // couldn't find it, bad data from client?
                  artwork_to_evbuf(hreq, &dbgri, NULL);
    }

error:
  db_query_end(&query_params);
  free_query_params(&query_params, 1);

  return ret;
}

static int
artworkapi_reply_playing(struct httpd_request *hreq)
{
  uint32_t dbmfi_id;
  time_t db_update;

  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;

  if ( safe_atou32(hreq->uri_parsed->path_parts[2], &dbmfi_id) < 0)
    return HTTP_BADREQUEST;

  return dbmfi_id == 0 ? HTTP_NOCONTENT :
                         artwork_to_evbuf(hreq, NULL, &dbmfi_id) == 0 ? HTTP_OK : HTTP_NOCONTENT;
}

static int
artworkapi_reply_artist(struct httpd_request *hreq)
{
  time_t db_update;
  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;

  return fetch_artwork(hreq, hreq->uri_parsed->path_parts[2], NULL) == 0 ? HTTP_OK : HTTP_NOCONTENT;
}

static int
artworkapi_reply_album(struct httpd_request *hreq)
{
  time_t db_update;
  db_update = (time_t) db_admin_getint64(DB_ADMIN_DB_UPDATE);
  if (db_update && httpd_request_not_modified_since(hreq->req, &db_update))
    return HTTP_NOTMODIFIED;

  return fetch_artwork(hreq, NULL, hreq->uri_parsed->path_parts[3]) == 0 ? HTTP_OK : HTTP_NOCONTENT;
}

static struct httpd_uri_map adm_handlers[] =
{
  { EVHTTP_REQ_GET,  "^/artwork/track/[[:digit:]]+$",   artworkapi_reply_playing },
  { EVHTTP_REQ_GET,  "^/artwork/artist/[[:digit:]]+$",  artworkapi_reply_artist  },
  { EVHTTP_REQ_GET,  "^/artwork/album/[[:digit:]]+$",   artworkapi_reply_album   },

  { 0, NULL, NULL }
};


/* ------------------------------- API --------------------------------- */

void
artworkapi_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;
  struct evkeyvalq *headers;
  int status_code;

  DPRINTF(E_DBG, L_WEB, "artwork api request: '%s'\n", uri_parsed->uri);

  if (!httpd_admin_check_auth(req))
    return;

  hreq = httpd_request_parse(req, uri_parsed, NULL, adm_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path '%s' in artwrok api request: '%s'\n", uri_parsed->path, uri_parsed->uri);

      httpd_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  CHECK_NULL(L_WEB, hreq->reply = evbuffer_new());

  status_code = hreq->handler(hreq);

  switch (status_code)
    {
      case HTTP_OK:                  /* 200 OK */
	headers = evhttp_request_get_output_headers(req);
        if ( (evhttp_find_header(headers, "Content-Type")) == NULL) {
            // TODO - clear other headers?
            httpd_send_error(req, HTTP_INTERNAL, "Internal Server Error");
        } else {
            httpd_send_reply(req, status_code, "OK", hreq->reply, HTTPD_SEND_NO_GZIP);
        }
	break;
      case HTTP_NOCONTENT:           /* 204 No Content */
	httpd_send_reply(req, status_code, "No Content", hreq->reply, HTTPD_SEND_NO_GZIP);
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
	break;
    }

  evbuffer_free(hreq->reply);
  free(hreq);
}

int
artworkapi_is_request(const char *path)
{
  return (strncmp(path, "/artwork/", strlen("/artwork/")) == 0) ? 1 : 0;
}

int
artworkapi_init(void)
{
  char buf[64];
  int i;
  int ret;

  DPRINTF(E_DBG, L_WEB, "artwork api started\n");
  for (i = 0; adm_handlers[i].handler; i++)
    {
      ret = regcomp(&adm_handlers[i].preg, adm_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
	{
	  regerror(ret, &adm_handlers[i].preg, buf, sizeof(buf));

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

  for (i = 0; adm_handlers[i].handler; i++)
    regfree(&adm_handlers[i].preg);
}
