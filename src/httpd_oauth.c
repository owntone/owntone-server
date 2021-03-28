/*
 * Copyright (C) 2017 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "httpd_oauth.h"
#include "logger.h"
#include "misc.h"
#include "conffile.h"
#ifdef SPOTIFY
# include "library/spotify_webapi.h"
#endif


/* --------------------------- REPLY HANDLERS ------------------------------- */

#ifdef SPOTIFY
static int
oauth_reply_spotify(struct httpd_request *hreq)
{
  char redirect_uri[256];
  char *errmsg;
  int httpd_port;
  int ret;

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");

  snprintf(redirect_uri, sizeof(redirect_uri), "http://owntone.local:%d/oauth/spotify", httpd_port);
  ret = spotifywebapi_oauth_callback(hreq->query, redirect_uri, &errmsg);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Could not parse Spotify OAuth callback: '%s'\n", hreq->uri_parsed->uri);
      httpd_send_error(hreq->req, HTTP_INTERNAL, errmsg);
      free(errmsg);
      return -1;
    }

  httpd_redirect_to(hreq->req, "/#/settings/online-services");

  return 0;
}
#else
static int
oauth_reply_spotify(struct httpd_request *hreq)
{
  DPRINTF(E_LOG, L_WEB, "This version was built without support for Spotify\n");

  httpd_send_error(hreq->req, HTTP_NOTFOUND, "This version was built without support for Spotify");

  return -1;
}
#endif

static struct httpd_uri_map oauth_handlers[] =
  {
    {
      .regexp = "^/oauth/spotify$",
      .handler = oauth_reply_spotify
    },
    {
      .regexp = NULL,
      .handler = NULL
    }
  };


/* ------------------------------- OAUTH API -------------------------------- */

void
oauth_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;

  DPRINTF(E_LOG, L_WEB, "OAuth request: '%s'\n", uri_parsed->uri);

  hreq = httpd_request_parse(req, uri_parsed, NULL, oauth_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path '%s' in OAuth request: '%s'\n", uri_parsed->path, uri_parsed->uri);

      httpd_send_error(req, HTTP_NOTFOUND, NULL);
      return;
    }

  hreq->handler(hreq);

  free(hreq);
}

int
oauth_is_request(const char *path)
{
  if (strncmp(path, "/oauth/", strlen("/oauth/")) == 0)
    return 1;
  if (strcmp(path, "/oauth") == 0)
    return 1;

  return 0;
}

int
oauth_init(void)
{
  char buf[64];
  int i;
  int ret;

  for (i = 0; oauth_handlers[i].handler; i++)
    {
      ret = regcomp(&oauth_handlers[i].preg, oauth_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
        {
          regerror(ret, &oauth_handlers[i].preg, buf, sizeof(buf));

          DPRINTF(E_FATAL, L_WEB, "OAuth init failed; regexp error: %s\n", buf);
	  return -1;
        }
    }

  return 0;
}

void
oauth_deinit(void)
{
  int i;

  for (i = 0; oauth_handlers[i].handler; i++)
    regfree(&oauth_handlers[i].preg);
}
