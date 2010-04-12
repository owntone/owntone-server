/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
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
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <regex.h>
#include <stdint.h>

#include <event.h>
#include "evhttp/evhttp.h"

#include "logger.h"
#include "misc.h"
#include "conffile.h"
#include "httpd.h"
#include "httpd_dacp.h"
#include "dmap_helpers.h"


/* From httpd_daap.c */
struct daap_session;

struct daap_session *
daap_session_find(struct evhttp_request *req, struct evkeyvalq *query, struct evbuffer *evbuf);


struct uri_map {
  regex_t preg;
  char *regexp;
  void (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query);
};

struct dacp_update_request {
  struct evhttp_request *req;

  struct dacp_update_request *next;
};


/* Play status update requests */
static struct dacp_update_request *update_requests;


/* Update requests helpers */

static void
update_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct dacp_update_request *ur;
  struct dacp_update_request *p;

  ur = (struct dacp_update_request *)arg;

  DPRINTF(E_DBG, L_DACP, "Update request: client closed connection\n");

  evhttp_connection_set_closecb(ur->req->evcon, NULL, NULL);

  if (ur == update_requests)
    update_requests = ur->next;
  else
    {
      for (p = update_requests; p && (p->next != ur); p = p->next)
	;

      if (!p)
	{
	  DPRINTF(E_LOG, L_DACP, "WARNING: struct dacp_update_request not found in list; BUG!\n");
	  return;
	}

      p->next = ur->next;
    }

  free(ur);
}


static void
dacp_reply_ctrlint(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  dmap_add_container(evbuf, "caci", 127); /* 8 + len */
  dmap_add_int(evbuf, "mstt", 200);       /* 12 */
  dmap_add_char(evbuf, "muty", 0);        /* 9 */
  dmap_add_int(evbuf, "mtco", 1);         /* 12 */
  dmap_add_int(evbuf, "mrco", 1);         /* 12 */
  dmap_add_container(evbuf, "mlcl", 74);  /* 8 + len */
  dmap_add_container(evbuf, "mlit", 66);  /* 8 + len */
  dmap_add_int(evbuf, "miid", 1);         /* 12 */ /* Database ID */
  dmap_add_char(evbuf, "cmik", 1);        /* 9 */
  dmap_add_char(evbuf, "cmsp", 1);        /* 9 */
  dmap_add_char(evbuf, "cmsv", 1);        /* 9 */
  dmap_add_char(evbuf, "cass", 1);        /* 9 */
  dmap_add_char(evbuf, "casu", 1);        /* 9 */
  dmap_add_char(evbuf, "ceSG", 1);        /* 9 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
dacp_reply_cue(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* /cue?command=play&query=...&sort=...&index=N */
  /* /cue?command=clear */

  /* TODO */

  dmap_send_error(req, "cacr", "Not implemented");
}

static void
dacp_reply_pause(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_playpause(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_nextitem(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_previtem(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_beginff(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_beginrew(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_playresume(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_playstatusupdate(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  struct dacp_update_request *ur;
  const char *param;
  int current_rev = 2;
  int reqd_rev;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  param = evhttp_find_header(query, "revision-number");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Missing revision-number in update request\n");

      dmap_send_error(req, "cmst", "Invalid request");
      return;
    }

  ret = safe_atoi32(param, &reqd_rev);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Parameter revision-number not an integer\n");

      dmap_send_error(req, "cmst", "Invalid request");
      return;
    }

  if (reqd_rev == 1)
    {
      dmap_add_container(evbuf, "cmst", 84);    /* 8 + len */
      dmap_add_int(evbuf, "mstt", 200);         /* 12 */

      dmap_add_int(evbuf, "cmsr", current_rev); /* 12 */

      dmap_add_char(evbuf, "cavc", 1);          /* 9 */ /* volume controllable */
      dmap_add_char(evbuf, "caps", 2);          /* 9 */ /* play status, 2 = stopped, 3 = paused, 4 = playing */
      dmap_add_char(evbuf, "cash", 0);          /* 9 */ /* shuffle, true/false */
      dmap_add_char(evbuf, "carp", 0);          /* 9 */ /* repeat, 0 = off, 1 = repeat song, 2 = repeat (playlist) */

      dmap_add_int(evbuf, "caas", 2);           /* 12 */ /* album shuffle */
      dmap_add_int(evbuf, "caar", 6);           /* 12 */ /* album repeat */

      evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

      return;
    }

  /* Else, just let the request hang until we have changes to push back */
  ur = (struct dacp_update_request *)malloc(sizeof(struct dacp_update_request));
  if (!ur)
    {
      DPRINTF(E_LOG, L_DACP, "Out of memory for update request\n");

      dmap_send_error(req, "cmst", "Out of memory");
      return;
    }

  ur->req = req;

  ur->next = update_requests;
  update_requests = ur;

  /* If the connection fails before we have an update to push out
   * to the client, we need to know.
   */
  evhttp_connection_set_closecb(req->evcon, update_fail_cb, ur);
}

static void
dacp_reply_nowplayingartwork(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* We don't have any support for artwork at the moment... */
  /* No artwork -> 404 Not Found */
  evhttp_send_error(req, HTTP_NOTFOUND, "Not Found");
}

static void
dacp_reply_getproperty(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  const char *param;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  param = evhttp_find_header(query, "properties");
  if (!param)
    {
      DPRINTF(E_WARN, L_DACP, "Invalid DACP getproperty request, no properties\n");

      dmap_send_error(req, "cmgt", "Invalid request");
      return;
    }

  /* The client can request multiple properties at once (hence the parameter
   * name), similar to DAAP meta tags. Should be treated likewise.
   */

  if (strcmp(param, "dmcp.volume") == 0)
    {
      dmap_add_container(evbuf, "cmgt", 24); /* 8 + len */
      dmap_add_int(evbuf, "mstt", 200);      /* 12 */
      dmap_add_int(evbuf, "cmvo", 42);       /* 12 */ /* Volume, 0-100 */

      evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
    }
  else
    dmap_send_error(req, "cmgt", "Not implemented");
}

static void
dacp_reply_setproperty(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* Known properties:
   * dacp.shufflestate 0/1
   * dacp.repeatstate  0/1/2
   * dacp.playingtime  seek to time in ms
   * dmcp.volume       0-100, float
   */

  /* /ctrl-int/1/setproperty?dacp.shufflestate=1&session-id=100 */

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
dacp_reply_getspeakers(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* We're going to pretend we have local speakers... */
  dmap_add_container(evbuf, "casp", 61);      /* 8 + len */
  dmap_add_int(evbuf, "mstt", 200);           /* 12 */
  dmap_add_container(evbuf, "mdcl", 41);      /* 8 + len */
  dmap_add_char(evbuf, "caia", 1);            /* 9 */ /* Speaker is active */
  dmap_add_string(evbuf, "minm", "Computer"); /* 8 + len */
  dmap_add_long(evbuf, "msma", 0);            /* 16 */ /* Speaker identifier (ApEx mac address or 0 for computer) */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
dacp_reply_setspeakers(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* Query: /setspeakers?speaker-id=0,0xapexid,0xapexid */

  /* TODO */

  /* 204 No Content is the canonical reply */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}


static struct uri_map dacp_handlers[] =
  {
    {
      .regexp = "^/ctrl-int$",
      .handler = dacp_reply_ctrlint
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/cue$",
      .handler = dacp_reply_cue
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/pause$",
      .handler = dacp_reply_pause
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playpause$",
      .handler = dacp_reply_playpause
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/nextitem$",
      .handler = dacp_reply_nextitem
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/previtem$",
      .handler = dacp_reply_previtem
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/beginff$",
      .handler = dacp_reply_beginff
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/beginrew$",
      .handler = dacp_reply_beginrew
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playresume$",
      .handler = dacp_reply_playresume
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/playstatusupdate$",
      .handler = dacp_reply_playstatusupdate
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/nowplayingartwork$",
      .handler = dacp_reply_nowplayingartwork
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/getproperty$",
      .handler = dacp_reply_getproperty
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/setproperty$",
      .handler = dacp_reply_setproperty
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/getspeakers$",
      .handler = dacp_reply_getspeakers
    },
    {
      .regexp = "^/ctrl-int/[[:digit:]]+/setspeakers$",
      .handler = dacp_reply_setspeakers
    },
    {
      .regexp = NULL,
      .handler = NULL
    }
  };

void
dacp_request(struct evhttp_request *req)
{
  char *full_uri;
  char *uri;
  char *ptr;
  char *uri_parts[7];
  struct evbuffer *evbuf;
  struct evkeyvalq query;
  int handler;
  int ret;
  int i;

  memset(&query, 0, sizeof(struct evkeyvalq));

  full_uri = httpd_fixup_uri(req);
  if (!full_uri)
    {
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  ptr = strchr(full_uri, '?');
  if (ptr)
    *ptr = '\0';

  uri = strdup(full_uri);
  if (!uri)
    {
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  if (ptr)
    *ptr = '?';

  ptr = uri;
  uri = evhttp_decode_uri(uri);
  free(ptr);

  DPRINTF(E_DBG, L_DACP, "DACP request: %s\n", full_uri);

  handler = -1;
  for (i = 0; dacp_handlers[i].handler; i++)
    {
      ret = regexec(&dacp_handlers[i].preg, uri, 0, NULL, 0);
      if (ret == 0)
        {
          handler = i;
          break;
        }
    }

  if (handler < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Unrecognized DACP request\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

      free(uri);
      free(full_uri);
      return;
    }

  /* DACP has no HTTP authentication - Remote is identified by its pairing-guid */

  memset(uri_parts, 0, sizeof(uri_parts));

  uri_parts[0] = strtok_r(uri, "/", &ptr);
  for (i = 1; (i < sizeof(uri_parts) / sizeof(uri_parts[0])) && uri_parts[i - 1]; i++)
    {
      uri_parts[i] = strtok_r(NULL, "/", &ptr);
    }

  if (!uri_parts[0] || uri_parts[i - 1] || (i < 2))
    {
      DPRINTF(E_LOG, L_DACP, "DACP URI has too many/few components (%d)\n", (uri_parts[0]) ? i : 0);

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

      free(uri);
      free(full_uri);
      return;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DACP, "Could not allocate evbuffer for DACP reply\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      free(uri);
      free(full_uri);
      return;
    }

  evhttp_parse_query(full_uri, &query);

  evhttp_add_header(req->output_headers, "DAAP-Server", "forked-daapd/" VERSION);
  /* Content-Type for all DACP replies; can be overriden as needed */
  evhttp_add_header(req->output_headers, "Content-Type", "application/x-dmap-tagged");

  dacp_handlers[handler].handler(req, evbuf, uri_parts, &query);

  evbuffer_free(evbuf);
  evhttp_clear_headers(&query);
  free(uri);
  free(full_uri);
}

int
dacp_is_request(struct evhttp_request *req, char *uri)
{
  if (strncmp(uri, "/ctrl-int/", strlen("/ctrl-int/")) == 0)
    return 1;
  if (strcmp(uri, "/ctrl-int") == 0)
    return 1;

  return 0;
}


int
dacp_init(void)
{
  char buf[64];
  int i;
  int ret;

  update_requests = NULL;

  for (i = 0; dacp_handlers[i].handler; i++)
    {
      ret = regcomp(&dacp_handlers[i].preg, dacp_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
        {
          regerror(ret, &dacp_handlers[i].preg, buf, sizeof(buf));

          DPRINTF(E_FATAL, L_DACP, "DACP init failed; regexp error: %s\n", buf);
	  return -1;
        }
    }

  return 0;
}

void
dacp_deinit(void)
{
  struct dacp_update_request *ur;
  int i;

  for (i = 0; dacp_handlers[i].handler; i++)
    regfree(&dacp_handlers[i].preg);

  for (ur = update_requests; update_requests; ur = update_requests)
    {
      update_requests = ur->next;

      evhttp_connection_set_closecb(ur->req->evcon, NULL, NULL);
      free(ur);
    }
}
