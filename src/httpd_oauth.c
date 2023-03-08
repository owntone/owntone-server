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

#include "httpd_internal.h"
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
  const char *errmsg;
  int httpd_port;
  int ret;

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");

  snprintf(redirect_uri, sizeof(redirect_uri), "http://owntone.local:%d/oauth/spotify", httpd_port);
  ret = spotifywebapi_oauth_callback(hreq->query, redirect_uri, &errmsg);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Could not parse Spotify OAuth callback '%s': %s\n", hreq->uri, errmsg);
      httpd_send_error(hreq, HTTP_INTERNAL, errmsg);
      return -1;
    }

  httpd_redirect_to(hreq, "/#/settings/online-services");

  return 0;
}
#else
static int
oauth_reply_spotify(struct httpd_request *hreq)
{
  DPRINTF(E_LOG, L_WEB, "This version was built without support for Spotify\n");

  httpd_send_error(hreq, HTTP_NOTFOUND, "This version was built without support for Spotify");

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

static void
oauth_request(struct httpd_request *hreq)
{
  if (!hreq->handler)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path in OAuth request: '%s'\n", hreq->uri);

      httpd_send_error(hreq, HTTP_NOTFOUND, NULL);
      return;
    }

  hreq->handler(hreq);
}

struct httpd_module httpd_oauth =
{
  .name = "OAuth",
  .type = MODULE_OAUTH,
  .logdomain = L_WEB,
  .subpaths = { "/oauth/", NULL },
  .fullpaths = { "/oauth", NULL },
  .handlers = oauth_handlers,
  .request = oauth_request,
};
