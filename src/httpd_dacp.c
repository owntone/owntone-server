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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <regex.h>
#include <stdint.h>
#include <inttypes.h>

#if defined(HAVE_SYS_EVENTFD_H) && defined(HAVE_EVENTFD)
# define USE_EVENTFD
# include <sys/eventfd.h>
#endif

#include <event.h>
#include "evhttp/evhttp.h"

#include "logger.h"
#include "misc.h"
#include "conffile.h"
#include "httpd.h"
#include "httpd_dacp.h"
#include "dmap_helpers.h"
#include "db.h"
#include "player.h"


/* httpd event base, from httpd.c */
extern struct event_base *evbase_httpd;

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


/* Play status update */
#ifdef USE_EVENTFD
static int update_efd;
#else
static int update_pipe[2];
#endif
static struct event updateev;
static int current_rev;

/* Play status update requests */
static struct dacp_update_request *update_requests;


/* Update requests helpers */
static int
make_playstatusupdate(struct evbuffer *evbuf)
{
  struct player_status status;
  char canp[16];
  struct media_file_info *mfi;
  struct evbuffer *psu;
  int ret;

  psu = evbuffer_new();
  if (!psu)
    {
      DPRINTF(E_LOG, L_DACP, "Could not allocate evbuffer for playstatusupdate\n");

      return -1;
    }

  memset(&status, 0, sizeof(struct player_status));

  player_get_status(&status);

  if (status.status != PLAY_STOPPED)
    {
      mfi = db_file_fetch_byid(status.id);
      if (!mfi)
	{
	  DPRINTF(E_LOG, L_DACP, "Could not fetch file id %d\n", status.id);

	  return -1;
	}
    }
  else
    mfi = NULL;

  dmap_add_int(psu, "mstt", 200);         /* 12 */

  dmap_add_int(psu, "cmsr", current_rev); /* 12 */

  dmap_add_char(psu, "cavc", 1);              /* 9 */ /* volume controllable */
  dmap_add_char(psu, "caps", status.status);  /* 9 */ /* play status, 2 = stopped, 3 = paused, 4 = playing */
  dmap_add_char(psu, "cash", status.shuffle); /* 9 */ /* shuffle, true/false */
  dmap_add_char(psu, "carp", status.repeat);  /* 9 */ /* repeat, 0 = off, 1 = repeat song, 2 = repeat (playlist) */

  dmap_add_int(psu, "caas", 2);           /* 12 */ /* available shuffle states */
  dmap_add_int(psu, "caar", 6);           /* 12 */ /* available repeat states */

  if (mfi)
    {
      memset(canp, 0, sizeof(canp));

      canp[3] = 1; /* 0-3 database ID */

      /* 4-7 playlist ID FIXME */

      canp[8]  = (status.pos_pl >> 24) & 0xff; /* 8-11 position in playlist */
      canp[9]  = (status.pos_pl >> 16) & 0xff;
      canp[10] = (status.pos_pl >> 8) & 0xff;
      canp[11] = status.pos_pl & 0xff;

      canp[12] = (status.id >> 24) & 0xff; /* 12-15 track ID */
      canp[13] = (status.id >> 16) & 0xff;
      canp[14] = (status.id >> 8) & 0xff;
      canp[15] = status.id & 0xff;

      dmap_add_literal(psu, "canp", canp, sizeof(canp));

      dmap_add_string(psu, "cann", mfi->title);
      dmap_add_string(psu, "cana", mfi->artist);
      dmap_add_string(psu, "canl", mfi->album);
      dmap_add_string(psu, "cang", mfi->genre);
      dmap_add_long(psu, "asai", mfi->songalbumid);

      dmap_add_int(psu, "cmmk", 1);

      dmap_add_int(psu, "cant", mfi->song_length - status.pos_ms); /* Remaining time in ms */
      dmap_add_int(psu, "cast", mfi->song_length); /* Song length in ms */

      free_mfi(mfi, 0);
    }

  dmap_add_container(evbuf, "cmst", EVBUFFER_LENGTH(psu));    /* 8 + len */

  ret = evbuffer_add_buffer(evbuf, psu);
  evbuffer_free(psu);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not add status data to playstatusupdate reply\n");

      return -1;
    }

  return 0;
}

static void
playstatusupdate_cb(int fd, short what, void *arg)
{
  struct dacp_update_request *ur;
  struct evbuffer *evbuf;
  struct evbuffer *update;
  int ret;

#ifdef USE_EVENTFD
  eventfd_t count;

  ret = eventfd_read(update_efd, &count);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not read playstatusupdate event counter: %s\n", strerror(errno));

      goto readd;
    }
#else
  int dummy;

  read(update_pipe[0], &dummy, sizeof(dummy));
#endif

  if (!update_requests)
    goto readd;

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DACP, "Could not allocate evbuffer for playstatusupdate reply\n");

      goto readd;
    }

  update = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DACP, "Could not allocate evbuffer for playstatusupdate data\n");

      goto out_free_evbuf;
    }

  ret = make_playstatusupdate(update);
  if (ret < 0)
    goto out_free_update;

  for (ur = update_requests; update_requests; ur = update_requests)
    {
      update_requests = ur->next;

      evhttp_connection_set_closecb(ur->req->evcon, NULL, NULL);

      evbuffer_add(evbuf, EVBUFFER_DATA(update), EVBUFFER_LENGTH(update));

      evhttp_send_reply(ur->req, HTTP_OK, "OK", evbuf);

      free(ur);
    }

  current_rev++;

 out_free_update:
  evbuffer_free(update);
 out_free_evbuf:
  evbuffer_free(evbuf);
 readd:
  ret = event_add(&updateev, NULL);
  if (ret < 0)
    DPRINTF(E_LOG, L_DACP, "Couldn't re-add event for playstatusupdate\n");
}

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
dacp_reply_cue_play(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  const char *sort;
  const char *cuequery;
  const char *param;
  struct player_source *ps;
  uint32_t id;
  int ret;

  /* /cue?command=play&query=...&sort=...&index=N */

  cuequery = evhttp_find_header(query, "query");
  if (!cuequery)
    {
      DPRINTF(E_LOG, L_DACP, "No query given for cue play command\n");

      dmap_send_error(req, "cacr", "No query given");
      return;
    }

  sort = evhttp_find_header(query, "sort");

  ps = player_queue_make(cuequery, sort);
  if (!ps)
    {
      DPRINTF(E_LOG, L_DACP, "Could not build song queue\n");

      dmap_send_error(req, "cacr", "Could not build song queue");
      return;
    }

  player_queue_add(ps);

  id = 0;
  param = evhttp_find_header(query, "index");
  if (param)
    {
      ret = safe_atou32(param, &id);
      if (ret < 0)
	DPRINTF(E_LOG, L_DACP, "Invalid index (%s) in cue request\n", param);
    }

  ret = player_playback_start(&id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not start playback\n");

      dmap_send_error(req, "cacr", "Playback failed to start");
      return;
    }

  dmap_add_container(evbuf, "cacr", 24); /* 8 + len */
  dmap_add_int(evbuf, "mstt", 200);      /* 12 */
  dmap_add_int(evbuf, "miid", id);       /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
dacp_reply_cue_clear(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  /* /cue?command=clear */

  player_playback_stop();

  player_queue_clear();

  dmap_add_container(evbuf, "cacr", 24); /* 8 + len */
  dmap_add_int(evbuf, "mstt", 200);      /* 12 */
  dmap_add_int(evbuf, "miid", 0);        /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
dacp_reply_cue(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  const char *param;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  param = evhttp_find_header(query, "command");
  if (!param)
    {
      DPRINTF(E_DBG, L_DACP, "No command in cue request\n");

      dmap_send_error(req, "cacr", "No command in cue request");
      return;
    }

  if (strcmp(param, "clear") == 0)
    dacp_reply_cue_clear(req, evbuf, uri, query);
  else if (strcmp(param, "play") == 0)
    dacp_reply_cue_play(req, evbuf, uri, query);
  else
    {
      DPRINTF(E_LOG, L_DACP, "Unknown cue command %s\n", param);

      dmap_send_error(req, "cacr", "Unknown command in cue request");
      return;
    }
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
      ret = make_playstatusupdate(evbuf);
      if (ret < 0)
	evhttp_send_error(req, 500, "Internal Server Error");
      else
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
speaker_enum_cb(uint64_t id, const char *name, int selected, int has_password, void *arg)
{
  struct evbuffer *evbuf;
  int len;

  evbuf = (struct evbuffer *)arg;

  len = 8 + strlen(name) + 16;
  if (selected)
    len += 9;
  if (has_password)
    len += 9;

  dmap_add_container(evbuf, "mdcl", len); /* 8 + len */
  if (selected)
    dmap_add_char(evbuf, "caia", 1);      /* 9 */
  if (has_password)
    dmap_add_char(evbuf, "cahp", 1);      /* 9 */
  dmap_add_string(evbuf, "minm", name);   /* 8 + len */
  dmap_add_long(evbuf, "msma", id);       /* 16 */
}

static void
dacp_reply_getspeakers(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  struct evbuffer *spklist;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  spklist = evbuffer_new();
  if (!spklist)
    {
      DPRINTF(E_LOG, L_DACP, "Could not create evbuffer for speaker list\n");

      dmap_send_error(req, "casp", "Out of memory");

      return;
    }

  player_speaker_enumerate(speaker_enum_cb, spklist);

  dmap_add_container(evbuf, "casp", 12 + EVBUFFER_LENGTH(spklist)); /* 8 + len */
  dmap_add_int(evbuf, "mstt", 200); /* 12 */

  evbuffer_add_buffer(evbuf, spklist);

  evbuffer_free(spklist);

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
dacp_reply_setspeakers(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  const char *param;
  const char *ptr;
  uint64_t *ids;
  int nspk;
  int i;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  param = evhttp_find_header(query, "speaker-id");
  if (!param)
    {
      DPRINTF(E_LOG, L_DACP, "Missing speaker-id parameter in DACP setspeakers request\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  if (strlen(param) == 0)
    {
      ids = NULL;
      goto fastpath;
    }

  nspk = 1;
  ptr = param;
  while ((ptr = strchr(ptr + 1, ',')))
    nspk++;

  ids = (uint64_t *)malloc((nspk + 1) * sizeof(uint64_t));
  if (!ids)
    {
      DPRINTF(E_LOG, L_DACP, "Out of memory for speaker ids\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return;
    }

  param--;
  i = 1;
  do
    {
      param++;
      ret = safe_hextou64(param, &ids[i]);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DACP, "Invalid speaker id in request: %s\n", param);

	  nspk--;
	  continue;
	}

      i++;
    }
  while ((param = strchr(param + 1, ',')));

  ids[0] = nspk;

 fastpath:
  ret = player_speaker_set(ids);

  if (ids)
    free(ids);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Speakers de/activation failed!\n");

      /* Password problem */
      if (ret == -2)
	evhttp_send_error(req, 902, "");
      else
	evhttp_send_error(req, 500, "Internal Server Error");

      return;
    }

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

  current_rev = 2;
  update_requests = NULL;

#ifdef USE_EVENTFD
  update_efd = eventfd(0, EFD_CLOEXEC);
  if (update_efd < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not create update eventfd: %s\n", strerror(errno));

      return -1;
    }
#else
# if defined(__linux__)
  ret = pipe2(update_pipe, O_CLOEXEC);
# else
  ret = pipe(update_pipe);
# endif
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DACP, "Could not create update pipe: %s\n", strerror(errno));

      return -1;
    }
#endif /* USE_EVENTFD */

  for (i = 0; dacp_handlers[i].handler; i++)
    {
      ret = regcomp(&dacp_handlers[i].preg, dacp_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
        {
          regerror(ret, &dacp_handlers[i].preg, buf, sizeof(buf));

          DPRINTF(E_FATAL, L_DACP, "DACP init failed; regexp error: %s\n", buf);
	  goto regexp_fail;
        }
    }

#ifdef USE_EVENTFD
  event_set(&updateev, update_efd, EV_READ, playstatusupdate_cb, NULL);
#else
  event_set(&updateev, update_pipe[0], EV_READ, playstatusupdate_cb, NULL);
#endif
  event_base_set(evbase_httpd, &updateev);
  event_add(&updateev, NULL);

#ifdef USE_EVENTFD
  player_set_updatefd(update_efd);
#else
  player_set_updatefd(update_pipe[1]);
#endif

  return 0;

 regexp_fail:
#ifdef USE_EVENTFD
  close(update_efd);
#else
  close(update_pipe[0]);
  close(update_pipe[1]);
#endif
  return -1;
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

  player_set_updatefd(-1);

#ifdef USE_EVENTFD
  close(update_efd);
#else
  close(update_pipe[0]);
  close(update_pipe[1]);
#endif
}
