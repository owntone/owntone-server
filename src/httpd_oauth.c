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
#include <regex.h>
#include <stdint.h>
#include <inttypes.h>

#include "httpd_oauth.h"
#include "logger.h"
#include "misc.h"
#include "conffile.h"
#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

struct oauth_request {
  // The parsed request URI given to us by httpd.c
  struct httpd_uri_parsed *uri_parsed;
  // Shortcut to &uri_parsed->ev_query
  struct evkeyvalq *query;
  // http request struct
  struct evhttp_request *req;
  // A pointer to the handler that will process the request
  void (*handler)(struct oauth_request *oreq);
};

struct uri_map {
  regex_t preg;
  char *regexp;
  void (*handler)(struct oauth_request *oreq);
};

/* Forward declaration of handlers */
static struct uri_map oauth_handlers[];


/* ------------------------------- HELPERS ---------------------------------- */

static struct oauth_request *
oauth_request_parse(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct oauth_request *oreq;
  int i;
  int ret;

  CHECK_NULL(L_WEB, oreq = calloc(1, sizeof(struct oauth_request)));

  oreq->req        = req;
  oreq->uri_parsed = uri_parsed;
  oreq->query      = &(uri_parsed->ev_query);

  for (i = 0; oauth_handlers[i].handler; i++)
    {
      ret = regexec(&oauth_handlers[i].preg, uri_parsed->path, 0, NULL, 0);
      if (ret == 0)
        {
          oreq->handler = oauth_handlers[i].handler;
          break;
        }
    }

  if (!oreq->handler)
    {
      DPRINTF(E_LOG, L_WEB, "Unrecognized path '%s' in OAuth request: '%s'\n", uri_parsed->path, uri_parsed->uri);
      goto error;
    }

  return oreq;

 error:
  free(oreq);

  return NULL;
}


/* --------------------------- REPLY HANDLERS ------------------------------- */

#ifdef HAVE_SPOTIFY_H
static void
oauth_reply_spotify(struct oauth_request *oreq)
{
  char redirect_uri[256];
  char *errmsg;
  int httpd_port;
  int ret;

  httpd_port = cfg_getint(cfg_getsec(cfg, "library"), "port");

  snprintf(redirect_uri, sizeof(redirect_uri), "http://forked-daapd.local:%d/oauth/spotify", httpd_port);
  ret = spotify_oauth_callback(oreq->query, redirect_uri, &errmsg);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Could not parse Spotify OAuth callback: '%s'\n", oreq->uri_parsed->uri);
      httpd_send_error(oreq->req, HTTP_INTERNAL, errmsg);
      free(errmsg);
      return;
    }

  httpd_redirect_to_admin(oreq->req);
}
#else
static void
oauth_reply_spotify(struct oauth_request *oreq)
{
  DPRINTF(E_LOG, L_HTTPD, "This version of forked-daapd was built without support for Spotify\n");

  httpd_send_error(oreq->req, HTTP_NOTFOUND, "This version of forked-daapd was built without support for Spotify");
}
#endif

static struct uri_map oauth_handlers[] =
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
  struct oauth_request *oreq;

  DPRINTF(E_LOG, L_WEB, "OAuth request: '%s'\n", uri_parsed->uri);

  oreq = oauth_request_parse(req, uri_parsed);
  if (!oreq)
    {
      httpd_send_error(req, HTTP_NOTFOUND, NULL);
      return;
    }

  oreq->handler(oreq);

  free(oreq);
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
