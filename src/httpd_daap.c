/*
 * Copyright (C) 2016-2018 Espen Jürgensen <espenjurgensen@gmail.com>
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2010 Kai Elwert <elwertk@googlemail.com>
 *
 * Adapted from mt-daapd:
 * Copyright (C) 2003-2007 Ron Pedde <ron@pedde.com>
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
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>

#include <uninorm.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/bufferevent.h>

#include "httpd_daap.h"
#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "transcode.h"
#include "artwork.h"
#include "dmap_common.h"
#include "cache.h"


/* httpd event base, from httpd.c */
extern struct event_base *evbase_httpd;

/* Max number of sessions and session timeout
 * Many clients (including iTunes) don't seem to respect the timeout capability
 * that we announce, and just keep using the same session. Therefore we take a
 * lenient approach to actually timing out: We wait an entire week, and to
 * avoid running a timer for that long, we only check for expiration when adding
 * new sessions - see daap_session_cleanup().
 */
#define DAAP_SESSION_MAX 200
#define DAAP_SESSION_TIMEOUT 604800            // One week in seconds
/* We announce this timeout to the client when returning server capabilities */
#define DAAP_SESSION_TIMEOUT_CAPABILITY 1800   // 30 minutes
/* Update requests refresh interval in seconds */
#define DAAP_UPDATE_REFRESH  0

/* Database number for the Radio item */
#define DAAP_DB_RADIO 2

/* Errors that the reply handlers may return */
enum daap_reply_result
{
  DAAP_REPLY_LOGOUT          =  4,
  DAAP_REPLY_NONE            =  3,
  DAAP_REPLY_NO_CONTENT      =  2,
  DAAP_REPLY_OK_NO_GZIP      =  1,
  DAAP_REPLY_OK              =  0,
  DAAP_REPLY_NO_CONNECTION   = -1,
  DAAP_REPLY_ERROR           = -2,
  DAAP_REPLY_FORBIDDEN       = -3,
  DAAP_REPLY_BAD_REQUEST     = -4,
  DAAP_REPLY_SERVUNAVAIL     = -5,
};

struct daap_session {
  int id;
  time_t mtime;
  bool is_remote;

  struct daap_session *next;
};

struct daap_update_request {
  struct evhttp_request *req;

  /* Refresh tiemout */
  struct event *timeout;

  struct daap_update_request *next;
};

struct sort_ctx {
  struct evbuffer *headerlist;
  int16_t mshc;
  uint32_t mshi;
  uint32_t mshn;
  uint32_t misc_mshn;
};


/* Default meta tags if not provided in the query */
static char *default_meta_plsongs = "dmap.itemkind,dmap.itemid,dmap.itemname,dmap.containeritemid,dmap.parentcontainerid";
static char *default_meta_pl = "dmap.itemid,dmap.itemname,dmap.persistentid,com.apple.itunes.smart-playlist";
static char *default_meta_group = "dmap.itemname,dmap.persistentid,daap.songalbumartist";

/* DAAP session tracking */
static struct daap_session *daap_sessions;

/* Update requests */
static int current_rev;
static struct daap_update_request *update_requests;
static struct timeval daap_update_refresh_tv = { DAAP_UPDATE_REFRESH, 0 };


/* -------------------------- SESSION HANDLING ------------------------------ */

static void
daap_session_free(struct daap_session *s)
{
  free(s);
}

static void
daap_session_remove(struct daap_session *s)
{
  struct daap_session *ptr;
  struct daap_session *prev;

  prev = NULL;
  for (ptr = daap_sessions; ptr; ptr = ptr->next)
    {
      if (ptr == s)
	break;

      prev = ptr;
    }

  if (!ptr)
    {
      DPRINTF(E_LOG, L_DAAP, "Error: Request to remove non-existent or ad-hoc session. BUG!\n");
      return;
    }

  if (!prev)
    daap_sessions = s->next;
  else
    prev->next = s->next;

  daap_session_free(s);
}

static struct daap_session *
daap_session_get(int id)
{
  struct daap_session *s;

  for (s = daap_sessions; s; s = s->next)
    {
      if (id == s->id)
	return s;
    }

  return NULL;
}

/* Removes stale sessions and also drops the oldest sessions if DAAP_SESSION_MAX
 * will otherwise be exceeded
 */
static void
daap_session_cleanup(void)
{
  struct daap_session *s;
  struct daap_session *next;
  time_t now;
  int count;

  count = 0;
  now = time(NULL);

  for (s = daap_sessions; s; s = next)
    {
      count++;
      next = s->next;

      if ((difftime(now, s->mtime) > DAAP_SESSION_TIMEOUT) || (count > DAAP_SESSION_MAX))
	{
	  DPRINTF(E_LOG, L_DAAP, "Cleaning up DAAP session (id %d)\n", s->id);

	  daap_session_remove(s);
	}
    }
}

static struct daap_session *
daap_session_add(bool is_remote, int request_session_id)
{
  struct daap_session *s;

  daap_session_cleanup();

  CHECK_NULL(L_DAAP, s = calloc(1, sizeof(struct daap_session)));

  if (request_session_id)
    {
      if (daap_session_get(request_session_id))
	{
	  DPRINTF(E_LOG, L_DAAP, "Session id requested in login (%d) is not available\n", request_session_id);
	  free(s);
	  return NULL;
	}

      s->id = request_session_id;
    }
  else
    {
      while ( (s->id = rand() + 100) && daap_session_get(s->id) );
    }

  s->mtime = time(NULL);

  s->is_remote = is_remote;

  if (daap_sessions)
    s->next = daap_sessions;

  daap_sessions = s;

  return s;
}


/* ---------------------- UPDATE REQUESTS HANDLERS -------------------------- */

static void
update_free(struct daap_update_request *ur)
{
  if (ur->timeout)
    event_free(ur->timeout);

  free(ur);
}

static void
update_remove(struct daap_update_request *ur)
{
  struct daap_update_request *p;

  if (ur == update_requests)
    update_requests = ur->next;
  else
    {
      for (p = update_requests; p && (p->next != ur); p = p->next)
	;

      if (!p)
	{
	  DPRINTF(E_LOG, L_DAAP, "WARNING: struct daap_update_request not found in list; BUG!\n");
	  return;
	}

      p->next = ur->next;
    }

  update_free(ur);
}

static void
update_refresh_cb(int fd, short event, void *arg)
{
  struct daap_update_request *ur;
  struct evhttp_connection *evcon;
  struct evbuffer *reply;

  ur = (struct daap_update_request *)arg;

  CHECK_NULL(L_DAAP, reply = evbuffer_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(reply, 32));

  current_rev++;

  /* Send back current revision */
  dmap_add_container(reply, "mupd", 24);
  dmap_add_int(reply, "mstt", 200);         /* 12 */
  dmap_add_int(reply, "musr", current_rev); /* 12 */

  evcon = evhttp_request_get_connection(ur->req);
  evhttp_connection_set_closecb(evcon, NULL, NULL);

  httpd_send_reply(ur->req, HTTP_OK, "OK", reply, 0);

  update_remove(ur);
}

static void
update_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct evhttp_connection *evc;
  struct daap_update_request *ur;

  ur = (struct daap_update_request *)arg;

  DPRINTF(E_DBG, L_DAAP, "Update request: client closed connection\n");

  evc = evhttp_request_get_connection(ur->req);
  if (evc)
    evhttp_connection_set_closecb(evc, NULL, NULL);

  evhttp_request_free(ur->req);
  update_remove(ur);
}


/* ------------------------- SORT HEADERS HELPERS --------------------------- */

static struct sort_ctx *
daap_sort_context_new(void)
{
  struct sort_ctx *ctx;
  int ret;

  ctx = calloc(1, sizeof(struct sort_ctx));
  if (!ctx)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for sorting context\n");

      return NULL;
    }

  memset(ctx, 0, sizeof(struct sort_ctx));

  ctx->headerlist = evbuffer_new();
  if (!ctx->headerlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DAAP sort headers list\n");

      free(ctx);
      return NULL;
    }

  ret = evbuffer_expand(ctx->headerlist, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP sort headers list\n");

      evbuffer_free(ctx->headerlist);
      free(ctx);
      return NULL;
    }

  ctx->mshc = -1;

  return ctx;
}

static void
daap_sort_context_free(struct sort_ctx *ctx)
{
  evbuffer_free(ctx->headerlist);
  free(ctx);
}

static int
daap_sort_build(struct sort_ctx *ctx, char *str)
{
  uint8_t *ret;
  size_t len;
  char fl;

  len = strlen(str);
  if (len > 0)
    {
      ret = u8_normalize(UNINORM_NFD, (uint8_t *)str, len + 1, NULL, &len);
      if (!ret)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not normalize string for sort header\n");

	  return -1;
	}

      fl = ret[0];
      free(ret);
    }
  else
    fl = 0;

  if (isascii(fl) && isalpha(fl))
    {
      fl = toupper(fl);

      /* Init */
      if (ctx->mshc == -1)
	ctx->mshc = fl;

      if (fl == ctx->mshc)
	ctx->mshn++;
      else
        {
	  dmap_add_container(ctx->headerlist, "mlit", 34);
	  dmap_add_short(ctx->headerlist, "mshc", ctx->mshc); /* 10 */
	  dmap_add_int(ctx->headerlist, "mshi", ctx->mshi);   /* 12 */
	  dmap_add_int(ctx->headerlist, "mshn", ctx->mshn);   /* 12 */

	  DPRINTF(E_DBG, L_DAAP, "Added sort header: mshc = %c, mshi = %u, mshn = %u fl %c\n", ctx->mshc, ctx->mshi, ctx->mshn, fl);

	  ctx->mshi = ctx->mshi + ctx->mshn;
	  ctx->mshn = 1;
	  ctx->mshc = fl;
	}
    }
  else
    {
      /* Non-ASCII, goes to misc category */
      ctx->misc_mshn++;
    }

  return 0;
}

static void
daap_sort_finalize(struct sort_ctx *ctx)
{
  /* Add current entry, if any */
  if (ctx->mshc != -1)
    {
      dmap_add_container(ctx->headerlist, "mlit", 34);
      dmap_add_short(ctx->headerlist, "mshc", ctx->mshc); /* 10 */
      dmap_add_int(ctx->headerlist, "mshi", ctx->mshi);   /* 12 */
      dmap_add_int(ctx->headerlist, "mshn", ctx->mshn);   /* 12 */

      ctx->mshi = ctx->mshi + ctx->mshn;

      DPRINTF(E_DBG, L_DAAP, "Added sort header: mshc = %c, mshi = %u, mshn = %u (final)\n", ctx->mshc, ctx->mshi, ctx->mshn);
    }

  /* Add misc category */
  dmap_add_container(ctx->headerlist, "mlit", 34);
  dmap_add_short(ctx->headerlist, "mshc", '0');          /* 10 */
  dmap_add_int(ctx->headerlist, "mshi", ctx->mshi);      /* 12 */
  dmap_add_int(ctx->headerlist, "mshn", ctx->misc_mshn); /* 12 */
}


/* ----------------------------- OTHER HELPERS ------------------------------ */

/* We try not to return items that the client cannot play (like Spotify and
 * internet streams in iTunes), or which are inappropriate (like internet streams
 * in the album tab of remotes). Note that the function must never append a
 * filter if the SELECT is not from the files table.
 */
static void
user_agent_filter(struct query_params *qp, struct httpd_request *hreq)
{
  struct daap_session *s = hreq->extra_data;
  char *filter;

  if (s->is_remote)
    {
      // This makes sure 1) the SELECT is from files, 2) that the Remote query
      // contained extended_media_kind:1, which characterise the queries we want
      // to filter. TODO: Not a really nice way of doing this, but best I could
      // think of.
      if (!qp->filter || !strstr(qp->filter, "f.media_kind = 1"))
	return;

      filter = safe_asprintf("%s AND (f.data_kind <> %d)", qp->filter, DATA_KIND_HTTP);
    }
  else
    {
      if (qp->type != Q_ITEMS)
	return;

      if (qp->filter)
	filter = safe_asprintf("%s AND (f.data_kind = %d)", qp->filter, DATA_KIND_FILE);
      else
	filter = safe_asprintf("(f.data_kind = %d)", DATA_KIND_FILE);
    }

  free(qp->filter);
  qp->filter = filter;

  DPRINTF(E_DBG, L_DAAP, "SQL filter w/client mod: %s\n", qp->filter);
}

static void
query_params_set(struct query_params *qp, int *sort_headers, struct httpd_request *hreq, enum query_type type)
{
  const char *param;
  char *ptr;
  int low;
  int high;
  int ret;

  low = 0;
  high = -1; /* No limit */

  memset(qp, 0, sizeof(struct query_params));

  param = evhttp_find_header(hreq->query, "index");
  if (param)
    {
      if (param[0] == '-') /* -n, last n entries */
	DPRINTF(E_LOG, L_DAAP, "Unsupported index range: %s\n", param);
      else
	{
	  ret = safe_atoi32(param, &low);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_DAAP, "Could not parse index range: %s\n", param);
	  else
	    {
	      ptr = strchr(param, '-');
	      if (!ptr) /* single item */
		high = low;
	      else
		{
		  ptr++;
		  if (*ptr != '\0') /* low-high */
		    {
		      ret = safe_atoi32(ptr, &high);
		      if (ret < 0)
			  DPRINTF(E_LOG, L_DAAP, "Could not parse high index in range: %s\n", param);
		    }
		}
	    }
	}

      DPRINTF(E_DBG, L_DAAP, "Index range %s: low %d, high %d (offset %d, limit %d)\n", param, low, high, qp->offset, qp->limit);
    }

  if (high < low)
    high = -1; /* No limit */

  qp->offset = low;
  if (high < 0)
    qp->limit = -1; /* No limit */
  else
    qp->limit = (high - low) + 1;

  if (qp->limit == -1 && qp->offset == 0)
    qp->idx_type = I_NONE;
  else
    qp->idx_type = I_SUB;

  qp->sort = S_NONE;
  param = evhttp_find_header(hreq->query, "sort");
  if (param)
    {
      if (strcmp(param, "name") == 0)
	qp->sort = S_NAME;
      else if (strcmp(param, "album") == 0 && (type != Q_BROWSE_ALBUMS)) // Only set if non-default sort requested
	qp->sort = S_ALBUM;
      else if (strcmp(param, "artist") == 0 && (type != Q_BROWSE_ARTISTS)) // Only set if non-default sort requested
	qp->sort = S_ARTIST;
      else if (strcmp(param, "releasedate") == 0)
	qp->sort = S_DATE_RELEASED;
      else
	DPRINTF(E_DBG, L_DAAP, "Unknown sort param: %s\n", param);

      if (qp->sort != S_NONE)
	DPRINTF(E_DBG, L_DAAP, "Sorting songlist by %s\n", param);
    }

  if (sort_headers)
    {
      *sort_headers = 0;
      param = evhttp_find_header(hreq->query, "include-sort-headers");
      if (param && (strcmp(param, "1") == 0))
	{
	  *sort_headers = 1;
	  DPRINTF(E_SPAM, L_DAAP, "Sort headers requested\n");
	}
    }

  param = evhttp_find_header(hreq->query, "query");
  if (!param)
    param = evhttp_find_header(hreq->query, "filter");

  if (param)
    {
      DPRINTF(E_DBG, L_DAAP, "DAAP browse query filter: %s\n", param);

      qp->filter = dmap_query_parse_sql(param);
      if (!qp->filter)
	DPRINTF(E_LOG, L_DAAP, "Ignoring improper DAAP query: %s\n", param);

      /* iTunes seems to default to this when there is a query (which there is for audiobooks, but not for normal playlists) */
      if (!qp->sort && !(type & Q_F_BROWSE))
	qp->sort = S_ALBUM;
    }

  qp->type = type;

  user_agent_filter(qp, hreq);
}

static int
parse_meta(const struct dmap_field ***out_meta, const char *param)
{
  const struct dmap_field **meta;
  char *ptr;
  char *field;
  char *metastr;
  int nmeta;
  int i;
  int n;

  CHECK_NULL(L_DAAP, metastr = strdup(param));

  nmeta = 1;
  ptr = metastr;
  while ((ptr = strchr(ptr + 1, ',')) && (strlen(ptr) > 1))
    nmeta++;

  DPRINTF(E_DBG, L_DAAP, "Asking for %d meta tags\n", nmeta);

  CHECK_NULL(L_DAAP, meta = calloc(nmeta, sizeof(const struct dmap_field *)));

  field = strtok_r(metastr, ",", &ptr);
  for (i = 0; field != NULL && i < nmeta; i++)
    {
      for (n = 0; (n < i) && (strcmp(field, meta[n]->desc) != 0); n++);

      if (n == i)
	{
	  meta[i] = dmap_find_field_wrapper(field, strlen(field));

	  if (!meta[i])
	    {
	      DPRINTF(E_WARN, L_DAAP, "Could not find requested meta field '%s'\n", field);

	      i--;
	      nmeta--;
	    }
	}
      else
	{
	  DPRINTF(E_WARN, L_DAAP, "Parser will ignore duplicate occurrence of meta field '%s'\n", field);

	  i--;
	  nmeta--;
	}

      field = strtok_r(NULL, ",", &ptr);
    }

  free(metastr);

  DPRINTF(E_DBG, L_DAAP, "Found %d meta tags\n", nmeta);

  *out_meta = meta;

  return nmeta;
}

static void
daap_reply_send(struct httpd_request *hreq, enum daap_reply_result result)
{
  switch (result)
    {
      case DAAP_REPLY_LOGOUT:
	httpd_send_reply(hreq->req, 204, "Logout Successful", hreq->reply, 0);
	break;
      case DAAP_REPLY_NO_CONTENT:
	httpd_send_reply(hreq->req, HTTP_NOCONTENT, "No Content", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case DAAP_REPLY_OK:
	httpd_send_reply(hreq->req, HTTP_OK, "OK", hreq->reply, 0);
	break;
      case DAAP_REPLY_OK_NO_GZIP:
      case DAAP_REPLY_ERROR:
	httpd_send_reply(hreq->req, HTTP_OK, "OK", hreq->reply, HTTPD_SEND_NO_GZIP);
	break;
      case DAAP_REPLY_FORBIDDEN:
	httpd_send_error(hreq->req, 403, "Forbidden");
	break;
      case DAAP_REPLY_BAD_REQUEST:
	httpd_send_error(hreq->req, HTTP_BADREQUEST, "Bad Request");
	break;
      case DAAP_REPLY_SERVUNAVAIL:
	httpd_send_error(hreq->req, HTTP_SERVUNAVAIL, "Internal Server Error");
	break;
      case DAAP_REPLY_NO_CONNECTION:
      case DAAP_REPLY_NONE:
	// Send nothing
	break;
    }
}

static int
daap_request_authorize(struct httpd_request *hreq)
{
  struct daap_session *session = hreq->extra_data;
  const char *param;
  char *passwd;
  int ret;

  if (net_peer_address_is_trusted(hreq->peer_address))
    return 0;

  // Regular DAAP clients like iTunes will login with /login, and we will reply
  // with httpd_basic_auth() if a library password is set. Remote clients will
  // also call /login, but they should not get a httpd_basic_auth(), instead
  // daap_reply_login() will take care of auth.
  if (session->is_remote && (strcmp(hreq->uri_parsed->path, "/login") == 0))
    return 0;

  param = evhttp_find_header(hreq->query, "session-id");
  if (param)
    {
      if (session->id == 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Unauthorized request from '%s', DAAP session not found: '%s'\n", hreq->peer_address, hreq->uri_parsed->uri);

	  httpd_send_error(hreq->req, 401, "Unauthorized");
	  return -1;
	}

      session->mtime = time(NULL);
      return 0;
    }

  passwd = cfg_getstr(cfg_getsec(cfg, "library"), "password");
  if (!passwd)
    return 0;

  // If no valid session then we may need to authenticate
  if ((strcmp(hreq->uri_parsed->path, "/server-info") == 0)
      || (strcmp(hreq->uri_parsed->path, "/logout") == 0)
      || (strcmp(hreq->uri_parsed->path, "/content-codes") == 0)
      || (strncmp(hreq->uri_parsed->path, "/databases/1/items/", strlen("/databases/1/items/")) == 0))
    return 0; // No authentication

  DPRINTF(E_DBG, L_DAAP, "Checking authentication for library\n");

  // We don't care about the username
  ret = httpd_basic_auth(hreq->req, NULL, passwd, cfg_getstr(cfg_getsec(cfg, "library"), "name"));
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Unsuccessful library authorization attempt from '%s'\n", hreq->peer_address);
      return -1;
    }

  return 0;
}


/* --------------------------- REPLY HANDLERS ------------------------------- */
/* Note that some handlers can be called without a connection (needed for     */
/* cache regeneration), while others cannot. Those that cannot should check   */
/* that hreq->req is not a null pointer.                                      */

static enum daap_reply_result
daap_reply_server_info(struct httpd_request *hreq)
{
  struct evbuffer *content;
  struct evkeyvalq *headers;
  char *name;
  char *passwd;
  const char *clientver;
  size_t len;
  int mpro;
  int apro;

  if (!hreq->req)
    {
      DPRINTF(E_LOG, L_DAAP, "Bug! daap_reply_server_info() cannot be called without an actual connection\n");
      return DAAP_REPLY_NO_CONNECTION;
    }

  passwd = cfg_getstr(cfg_getsec(cfg, "library"), "password");
  name = cfg_getstr(cfg_getsec(cfg, "library"), "name");

  CHECK_NULL(L_DAAP, content = evbuffer_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(content, 512));

  mpro = 2 << 16 | 10;
  apro = 3 << 16 | 12;

  headers = evhttp_request_get_input_headers(hreq->req);
  if (headers && (clientver = evhttp_find_header(headers, "Client-DAAP-Version")))
    {
      if (strcmp(clientver, "1.0") == 0)
	{
	  mpro = 1 << 16;
	  apro = 1 << 16;
	}
      else if (strcmp(clientver, "2.0") == 0)
	{
	  mpro = 1 << 16;
	  apro = 2 << 16;
	}
    }

  dmap_add_int(content, "mstt", 200);
  dmap_add_int(content, "mpro", mpro);       // dmap.protocolversion
  dmap_add_string(content, "minm", name);    // dmap.itemname (server name)

  dmap_add_int(content, "apro", apro);       // daap.protocolversion
  dmap_add_int(content, "aeSV", apro);       // com.apple.itunes.music-sharing-version (determines if itunes shows share types)

  dmap_add_short(content, "ated", 7);        // daap.supportsextradata

  // Sub-optimal user-agent sniffing to solve the problem that iTunes 12.1 and
  // Apple Music do not work if we announce support for groups
  if (hreq->user_agent && (strncmp(hreq->user_agent, "iTunes", strlen("iTunes")) == 0))
    dmap_add_short(content, "asgr", 0);      // daap.supportsgroups (1=artists, 2=albums, 3=both)
  else if (hreq->user_agent && (strncmp(hreq->user_agent, "Music", strlen("Music")) == 0))
    dmap_add_short(content, "asgr", 0);      // daap.supportsgroups (1=artists, 2=albums, 3=both)
  else
    dmap_add_short(content, "asgr", 3);      // daap.supportsgroups (1=artists, 2=albums, 3=both)

//  dmap_add_long(content, "asse", 0x80000); // unknown - used by iTunes

  dmap_add_char(content, "aeMQ", 1);         // unknown - used by iTunes

//  dmap_add_long(content, "mscu", );        // unknown - used by iTunes
//  dmap_add_char(content, "aeFR", );        // unknown - used by iTunes

  dmap_add_char(content, "aeTr", 1);         // unknown - used by iTunes
  dmap_add_char(content, "aeSL", 1);         // unknown - used by iTunes
  dmap_add_char(content, "aeSR", 1);         // unknown - used by iTunes
//  dmap_add_char(content, "aeFP", 2);       // triggers FairPlay request
//  dmap_add_long(content, "aeSX", );        // unknown - used by iTunes

//  dmap_add_int(content, "ppro", );         // dpap.protocolversion

  dmap_add_char(content, "msed", 0);         // dmap.supportsedit? - we don't support playlist editing

  dmap_add_char(content, "mslr", 1);         // dmap.loginrequired
  dmap_add_int(content, "mstm", DAAP_SESSION_TIMEOUT_CAPABILITY); // dmap.timeoutinterval
  dmap_add_char(content, "msal", 1);         // dmap.supportsautologout
//  dmap_add_char(content, "msas", 3);       // dmap.authenticationschemes
  dmap_add_char(content, "msau", (passwd) ? 2 : 0); // dmap.authenticationmethod

  dmap_add_char(content, "msup", 1);         // dmap.supportsupdate
  dmap_add_char(content, "mspi", 1);         // dmap.supportspersistentids
  dmap_add_char(content, "msex", 1);         // dmap.supportsextensions
  dmap_add_char(content, "msbr", 1);         // dmap.supportsbrowse
  dmap_add_char(content, "msqy", 1);         // dmap.supportsquery
  dmap_add_char(content, "msix", 1);         // dmap.supportsindex
//  dmap_add_char(content, "msrs", 1);       // dmap.supportsresolve

  dmap_add_int(content, "msdc", 2);          // dmap.databasescount

//  dmap_add_int(content, "mstc", );          // dmap.utctime
//  dmap_add_int(content, "msto", );          // dmap.utcoffset

  // Create container
  len = evbuffer_get_length(content);
  dmap_add_container(hreq->reply, "msrv", len);

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, content));

  evbuffer_free(content);

  return DAAP_REPLY_OK;
}

static enum daap_reply_result
daap_reply_content_codes(struct httpd_request *hreq)
{
  const struct dmap_field *dmap_fields;
  size_t len;
  int nfields;
  int i;

  dmap_fields = dmap_get_fields_table(&nfields);

  len = 12;
  for (i = 0; i < nfields; i++)
    len += 8 + 12 + 10 + 8 + strlen(dmap_fields[i].desc);

  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, len + 8));

  dmap_add_container(hreq->reply, "mccr", len);
  dmap_add_int(hreq->reply, "mstt", 200);

  for (i = 0; i < nfields; i++)
    {
      len = 12 + 10 + 8 + strlen(dmap_fields[i].desc);

      dmap_add_container(hreq->reply, "mdcl", len);
      dmap_add_string(hreq->reply, "mcnm", dmap_fields[i].tag);  /* 12 */
      dmap_add_string(hreq->reply, "mcna", dmap_fields[i].desc); /* 8 + strlen(desc) */
      dmap_add_short(hreq->reply, "mcty", dmap_fields[i].type);  /* 10 */
    }

  return DAAP_REPLY_OK;
}

static enum daap_reply_result
daap_reply_login(struct httpd_request *hreq)
{
  struct daap_session *adhoc = hreq->extra_data;
  struct daap_session *session;
  struct pairing_info pi;
  const char *param;
  int request_session_id;
  int ret;

  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 32));

  param = evhttp_find_header(hreq->query, "pairing-guid");
  if (param && !net_peer_address_is_trusted(hreq->peer_address))
    {
      if (strlen(param) < 3)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt from %s with invalid pairing-guid: %s\n", hreq->peer_address, param);
	  return DAAP_REPLY_FORBIDDEN;
	}

      memset(&pi, 0, sizeof(struct pairing_info));
      pi.guid = strdup(param + 2); /* Skip leading 0X */

      ret = db_pairing_fetch_byguid(&pi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt from %s with invalid pairing-guid: %s\n", hreq->peer_address, param);
	  free_pi(&pi, 1);
	  return DAAP_REPLY_FORBIDDEN;
	}

      DPRINTF(E_INFO, L_DAAP, "Remote '%s' (%s) logging in with GUID %s\n", pi.name, hreq->peer_address, pi.guid);
      free_pi(&pi, 1);
    }
  else
    {
      if (hreq->user_agent)
        DPRINTF(E_INFO, L_DAAP, "Client '%s' logging in from %s\n", hreq->user_agent, hreq->peer_address);
      else
        DPRINTF(E_INFO, L_DAAP, "Client (unknown user-agent) logging in from %s\n", hreq->peer_address);
    }

  param = evhttp_find_header(hreq->query, "request-session-id");
  if (param)
    {
      ret = safe_atoi32(param, &request_session_id);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login request where request-session-id is not an integer\n");
	  request_session_id = 0;
	}
    }
  else
    request_session_id = 0;

  session = daap_session_add(adhoc->is_remote, request_session_id);
  if (!session)
    {
      dmap_error_make(hreq->reply, "mlog", "Could not start session");
      return DAAP_REPLY_ERROR;
    }

  dmap_add_container(hreq->reply, "mlog", 24);
  dmap_add_int(hreq->reply, "mstt", 200);          /* 12 */
  dmap_add_int(hreq->reply, "mlid", session->id);  /* 12 */

  return DAAP_REPLY_OK;
}

static enum daap_reply_result
daap_reply_logout(struct httpd_request *hreq)
{
  if (!hreq->extra_data)
    return DAAP_REPLY_FORBIDDEN;

  daap_session_remove(hreq->extra_data);

  hreq->extra_data = NULL;

  return DAAP_REPLY_LOGOUT;
}

static enum daap_reply_result
daap_reply_update(struct httpd_request *hreq)
{
  struct daap_update_request *ur;
  struct evhttp_connection *evcon;
  struct bufferevent *bufev;
  const char *param;
  int reqd_rev;
  int ret;

  if (!hreq->req)
    {
      DPRINTF(E_LOG, L_DAAP, "Bug! daap_reply_update() cannot be called without an actual connection\n");
      return DAAP_REPLY_NO_CONNECTION;
    }

  param = evhttp_find_header(hreq->query, "revision-number");
  if (!param)
    {
      DPRINTF(E_DBG, L_DAAP, "Missing revision-number in client update request\n");
      /* Some players (Amarok, Banshee) don't supply a revision number.
	 They get a standard update of everything. */
      param = "1";  /* Default to "1" will ensure an update */
    }

  ret = safe_atoi32(param, &reqd_rev);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Parameter revision-number not an integer\n");

      dmap_error_make(hreq->reply, "mupd", "Invalid request");
      return DAAP_REPLY_ERROR;
    }

  if (reqd_rev == 1) /* Or revision is not valid */
    {
      CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 32));

      /* Send back current revision */
      dmap_add_container(hreq->reply, "mupd", 24);
      dmap_add_int(hreq->reply, "mstt", 200);         /* 12 */
      dmap_add_int(hreq->reply, "musr", current_rev); /* 12 */

      return DAAP_REPLY_OK;
    }

  /* Else, just let the request hang until we have changes to push back */
  ur = calloc(1, sizeof(struct daap_update_request));
  if (!ur)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for update request\n");

      dmap_error_make(hreq->reply, "mupd", "Out of memory");
      return DAAP_REPLY_ERROR;
    }

  if (DAAP_UPDATE_REFRESH > 0)
    {
      ur->timeout = evtimer_new(evbase_httpd, update_refresh_cb, ur);
      if (ur->timeout)
	ret = evtimer_add(ur->timeout, &daap_update_refresh_tv);
      else
	ret = -1;

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Out of memory for update request event\n");

	  dmap_error_make(hreq->reply, "mupd", "Could not register timer");	
	  update_free(ur);
	  return DAAP_REPLY_ERROR;
	}
    }

  /* NOTE: we may need to keep reqd_rev in there too */
  ur->req = hreq->req;

  ur->next = update_requests;
  update_requests = ur;

  /* If the connection fails before we have an update to push out
   * to the client, we need to know.
   */
  evcon = evhttp_request_get_connection(hreq->req);
  if (evcon)
    {
      evhttp_connection_set_closecb(evcon, update_fail_cb, ur);

      // This is a workaround for some versions of libevent (2.0, but possibly
      // also 2.1) that don't detect if the client hangs up, and thus don't
      // clean up and never call update_fail_cb(). See github issue #870 and
      // https://github.com/libevent/libevent/issues/666. It should probably be
      // removed again in the future. The workaround is also present in dacp.c
      bufev = evhttp_connection_get_bufferevent(evcon);
      if (bufev)
	bufferevent_enable(bufev, EV_READ);
    }

  return DAAP_REPLY_NONE;
}

static enum daap_reply_result
daap_reply_activity(struct httpd_request *hreq)
{
  /* That's so nice, thanks for letting us know */
  return DAAP_REPLY_NO_CONTENT;
}

static enum daap_reply_result
daap_reply_dblist(struct httpd_request *hreq)
{
  struct evbuffer *content;
  struct evbuffer *item;
  char *name;
  char *name_radio;
  size_t len;
  uint32_t count = 0;

  name = cfg_getstr(cfg_getsec(cfg, "library"), "name");
  name_radio = cfg_getstr(cfg_getsec(cfg, "library"), "name_radio");

  CHECK_NULL(L_DAAP, content = evbuffer_new());
  CHECK_NULL(L_DAAP, item = evbuffer_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(item, 512));
  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 1024));

  // Add db entry for library with dbid = 1
  dmap_add_int(item, "miid", 1);
  dmap_add_long(item, "mper", 1);
  dmap_add_int(item, "mdbk", 1);
  dmap_add_int(item, "aeCs", 1);
  dmap_add_string(item, "minm", name);
  db_files_get_count(&count, NULL, NULL);
  dmap_add_int(item, "mimc", (int)count);
  db_pl_get_count(&count); // TODO Don't count empty smart playlists, because they get excluded in aply
  dmap_add_int(item, "mctc", (int)count);
//  dmap_add_int(content, "aeMk", 0x405);   // com.apple.itunes.extended-media-kind (OR of all in library)
  dmap_add_int(item, "meds", 3);

  // Create container for library db
  len = evbuffer_get_length(item);
  dmap_add_container(content, "mlit", len);

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(content, item));

  // Add second db entry for radio with dbid = DAAP_DB_RADIO
  CHECK_ERR(L_DAAP, evbuffer_expand(item, 512));

  dmap_add_int(item, "miid", DAAP_DB_RADIO);
  dmap_add_long(item, "mper", DAAP_DB_RADIO);
  dmap_add_int(item, "mdbk", 0x64);
  dmap_add_int(item, "aeCs", 0);
  dmap_add_string(item, "minm", name_radio);
  db_pl_get_count(&count); // TODO This counts too much, should only include stream playlists
  dmap_add_int(item, "mimc", (int)count);
  dmap_add_int(item, "mctc", 0);
  dmap_add_int(item, "aeMk", 1);   // com.apple.itunes.extended-media-kind (OR of all in library)
  dmap_add_int(item, "meds", 3);

  // Create container for radio db
  len = evbuffer_get_length(item);
  dmap_add_container(content, "mlit", len);

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(content, item));

  // Create container
  len = evbuffer_get_length(content);
  dmap_add_container(hreq->reply, "avdb", len + 53);
  dmap_add_int(hreq->reply, "mstt", 200);     /* 12 */
  dmap_add_char(hreq->reply, "muty", 0);      /* 9 */
  dmap_add_int(hreq->reply, "mtco", 2);       /* 12 */
  dmap_add_int(hreq->reply, "mrco", 2);       /* 12 */
  dmap_add_container(hreq->reply, "mlcl", len); /* 8 */

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, content));

  evbuffer_free(item);
  evbuffer_free(content);

  return DAAP_REPLY_OK;
}

static enum daap_reply_result
daap_reply_songlist_generic(struct httpd_request *hreq, int playlist)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct evbuffer *song;
  struct evbuffer *songlist;
  struct evkeyvalq *headers;
  struct daap_session *s;
  const struct dmap_field **meta = NULL;
  struct sort_ctx *sctx;
  const char *param;
  const char *client_codecs;
  const char *tag;
  char *last_codectype;
  size_t len;
  int nmeta = 0;
  int sort_headers;
  int nsongs;
  int transcode;
  int ret;

  DPRINTF(E_DBG, L_DAAP, "Fetching song list for playlist %d\n", playlist);

  s = hreq->extra_data;
  if (!s)
    {
      DPRINTF(E_LOG, L_DAAP, "Bug! daap_reply_songlist_generic() called with NULL session (playlist %d)\n", playlist);
      return DAAP_REPLY_ERROR;
    }

  if (playlist != -1)
    {
      // Songs in playlist
      tag = "apso";
      query_params_set(&qp, &sort_headers, hreq, Q_PLITEMS);
      qp.id = playlist;
    }
  else
    {
      // Songs in database
      tag = "adbs";
      query_params_set(&qp, &sort_headers, hreq, Q_ITEMS);
    }

  CHECK_NULL(L_DAAP, songlist = evbuffer_new());
  CHECK_NULL(L_DAAP, song = evbuffer_new());
  CHECK_NULL(L_DAAP, sctx = daap_sort_context_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 61));
  CHECK_ERR(L_DAAP, evbuffer_expand(songlist, 4096));
  CHECK_ERR(L_DAAP, evbuffer_expand(song, 512));

  param = evhttp_find_header(hreq->query, "meta");
  if (!param)
    {
      DPRINTF(E_DBG, L_DAAP, "No meta parameter in query, using default\n");

      if (playlist != -1)
	param = default_meta_plsongs;
    }

  if (param)
    {
      nmeta = parse_meta(&meta, param);
      if (nmeta < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");
	  goto error;
	}
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_error_make(hreq->reply, tag, "Could not start query");
      goto error;
    }

  client_codecs = NULL;
  if (!s->is_remote && hreq->req)
    {
      headers = evhttp_request_get_input_headers(hreq->req);
      client_codecs = evhttp_find_header(headers, "Accept-Codecs");
    }

  nsongs = 0;
  last_codectype = NULL;
  while ((ret = db_query_fetch_file(&dbmfi, &qp)) == 0)
    {
      nsongs++;

      if (!dbmfi.codectype)
	{
	  DPRINTF(E_LOG, L_DAAP, "Cannot transcode '%s', codec type is unknown\n", dbmfi.fname);

	  transcode = 0;
	}
      else if (s->is_remote)
	{
	  transcode = 1;
	}
      else if (!last_codectype || (strcmp(last_codectype, dbmfi.codectype) != 0))
	{
	  transcode = transcode_needed(hreq->user_agent, client_codecs, dbmfi.codectype);

	  free(last_codectype);
	  last_codectype = strdup(dbmfi.codectype);
	}

      ret = dmap_encode_file_metadata(songlist, song, &dbmfi, meta, nmeta, sort_headers, transcode);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Failed to encode song metadata\n");

	  ret = -100;
	  break;
	}

      if (sort_headers)
	{
	  ret = daap_sort_build(sctx, dbmfi.title_sort);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_DAAP, "Could not add sort header to DAAP song list reply\n");

	      ret = -100;
	      break;
	    }
   	}

      DPRINTF(E_SPAM, L_DAAP, "Done with song\n");
    }

  DPRINTF(E_DBG, L_DAAP, "Done with song list, %d songs\n", nsongs);

  free(last_codectype);
  db_query_end(&qp);

  if (ret == -100)
    {
      dmap_error_make(hreq->reply, tag, "Out of memory");
      goto error;
    }
  else if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
      dmap_error_make(hreq->reply, tag, "Error fetching query results");
      goto error;
    }

  /* Add header to evbuf, add songlist to evbuf */
  len = evbuffer_get_length(songlist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(hreq->reply, tag, len + evbuffer_get_length(sctx->headerlist) + 61);
    }
  else
    dmap_add_container(hreq->reply, tag, len + 53);

  dmap_add_int(hreq->reply, "mstt", 200);        /* 12 */
  dmap_add_char(hreq->reply, "muty", 0);         /* 9 */
  dmap_add_int(hreq->reply, "mtco", qp.results); /* 12 */
  dmap_add_int(hreq->reply, "mrco", nsongs);     /* 12 */
  dmap_add_container(hreq->reply, "mlcl", len); /* 8 */

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, songlist));

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(hreq->reply, "mshl", len); /* 8 */

      CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, sctx->headerlist));
    }

  free(meta);
  daap_sort_context_free(sctx);
  evbuffer_free(song);
  evbuffer_free(songlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_OK;

 error:
  free(meta);
  daap_sort_context_free(sctx);
  evbuffer_free(song);
  evbuffer_free(songlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_ERROR;
}

static enum daap_reply_result
daap_reply_dbsonglist(struct httpd_request *hreq)
{
  return daap_reply_songlist_generic(hreq, -1);
}

static enum daap_reply_result
daap_reply_plsonglist(struct httpd_request *hreq)
{
  int playlist;
  int ret;

  ret = safe_atoi32(hreq->uri_parsed->path_parts[3], &playlist);
  if (ret < 0)
    {
      dmap_error_make(hreq->reply, "apso", "Invalid playlist ID");
      return DAAP_REPLY_ERROR;
    }

  // This is a work-around for Remote for iTunes that for unknown reasons
  // sometimes requests playlist 0
  if (playlist == 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Client '%s' made invalid request for playlist 0, returning playlist 1\n", hreq->user_agent);

      playlist = 1;
    }

  return daap_reply_songlist_generic(hreq, playlist);
}

static enum daap_reply_result
daap_reply_playlists(struct httpd_request *hreq)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  struct evbuffer *playlistlist;
  struct evbuffer *playlist;
  const struct dmap_field_map *dfm;
  const struct dmap_field *df;
  const struct dmap_field **meta = NULL;
  const char *param;
  char **strval;
  size_t len;
  int database;
  int cfg_radiopl;
  int nmeta;
  int npls;
  int32_t plid;
  int32_t pltype;
  int32_t plitems;
  int32_t plstreams;
  int32_t plparent;
  int i;
  int ret;

  cfg_radiopl = cfg_getbool(cfg_getsec(cfg, "library"), "radio_playlists");

  ret = safe_atoi32(hreq->uri_parsed->path_parts[1], &database);
  if (ret < 0)
    {
      dmap_error_make(hreq->reply, "aply", "Invalid database ID");
      return DAAP_REPLY_ERROR;
    }

  query_params_set(&qp, NULL, hreq, Q_PL);
  qp.sort = S_PLAYLIST; // Only S_PLAYLIST (and S_NONE) works for Q_PL

  CHECK_NULL(L_DAAP, playlistlist = evbuffer_new());
  CHECK_NULL(L_DAAP, playlist = evbuffer_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 61));
  CHECK_ERR(L_DAAP, evbuffer_expand(playlistlist, 1024));
  CHECK_ERR(L_DAAP, evbuffer_expand(playlist, 128));

  param = evhttp_find_header(hreq->query, "meta");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "No meta parameter in query, using default\n");

      param = default_meta_pl;
    }

  nmeta = parse_meta(&meta, param);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      dmap_error_make(hreq->reply, "aply", "Failed to parse query");
      goto error;
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_error_make(hreq->reply, "aply", "Could not start query");
      goto error;
    }

  npls = 0;
  while (((ret = db_query_fetch_pl(&dbpli, &qp)) == 0) && (dbpli.id))
    {
      plid = 1;
      if (safe_atoi32(dbpli.id, &plid) != 0)
	continue;

      pltype = 0;
      if (safe_atoi32(dbpli.type, &pltype) != 0)
	continue;

      plitems = 0;
      if (safe_atoi32(dbpli.items, &plitems) != 0)
	continue;

      plstreams = 0;
      if (safe_atoi32(dbpli.streams, &plstreams) != 0)
	continue;

      /* Database DAAP_DB_RADIO is radio, so for that db skip playlists without
       * streams and for other databases skip playlists which are just streams
       */
      if ((database == DAAP_DB_RADIO) && (plstreams == 0))
	continue;
      if (!cfg_radiopl && (database != DAAP_DB_RADIO) && (plstreams > 0) && (plstreams == plitems))
	continue;

      /* Don't add empty Special playlists */
      if ((plid > 1) && (plitems == 0) && (pltype == PL_SPECIAL))
	continue;

      npls++;

      for (i = 0; i < nmeta; i++)
	{
	  df = meta[i];
	  dfm = df->dfm;

	  /* dmap.itemcount - always added */
	  if (dfm == &dfm_dmap_mimc)
	    continue;

	  /* Add field "com.apple.itunes.smart-playlist" for special and smart playlists
	     (excluding the special playlist for "library" with id = 1) */
	  if (dfm == &dfm_dmap_aeSP)
	    {
	      if ((pltype == PL_SMART) || ((pltype == PL_SPECIAL) && (plid != 1)))
		{
		  dmap_add_char(playlist, "aeSP", 1);
		}

	      /* Add field "com.apple.itunes.special-playlist" for special playlists
		 (excluding the special playlist for "library" with id = 1) */
	      if ((pltype == PL_SPECIAL) && (plid != 1))
		{
		  int32_t aePS = 0;
		  ret = safe_atoi32(dbpli.special_id, &aePS);
		  if ((ret == 0) && (aePS > 0))
		    dmap_add_char(playlist, "aePS", aePS);
		}

	      continue;
	    }

	  /* Not in struct playlist_info */
	  if (dfm->pli_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbpli + dfm->pli_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(playlist, df, *strval, 0);

	  DPRINTF(E_SPAM, L_DAAP, "Done with meta tag %s (%s)\n", df->desc, *strval);
	}

      /* Item count (mimc) */
      dmap_add_int(playlist, "mimc", plitems);

      /* Container ID (mpco) */
      ret = safe_atoi32(dbpli.parent_id, &plparent);
      if (ret == 0)
	dmap_add_int(playlist, "mpco", plparent);
      else
	dmap_add_int(playlist, "mpco", 0);

      /* Base playlist (abpl), id = 1 */
      if (plid == 1)
	dmap_add_char(playlist, "abpl", 1);

      DPRINTF(E_SPAM, L_DAAP, "Done with playlist\n");

      len = evbuffer_get_length(playlist);
      dmap_add_container(playlistlist, "mlit", len);
      ret = evbuffer_add_buffer(playlistlist, playlist);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add playlist to playlist list for DAAP playlists reply\n");

	  ret = -100;
	  break;
	}
    }

  db_query_end(&qp);

  DPRINTF(E_DBG, L_DAAP, "Done with playlist list, %d playlists\n", npls);

  if (ret == -100)
    {
      dmap_error_make(hreq->reply, "aply", "Out of memory");
      goto error;
    }
  else if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
      dmap_error_make(hreq->reply, "aply", "Error fetching query results");
      goto error;
    }

  /* Add header to evbuf, add playlistlist to evbuf */
  len = evbuffer_get_length(playlistlist);
  dmap_add_container(hreq->reply, "aply", len + 53);
  dmap_add_int(hreq->reply, "mstt", 200); /* 12 */
  dmap_add_char(hreq->reply, "muty", 0);  /* 9 */
  dmap_add_int(hreq->reply, "mtco", qp.results); /* 12 */
  dmap_add_int(hreq->reply,"mrco", npls); /* 12 */
  dmap_add_container(hreq->reply, "mlcl", len);

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, playlistlist));

  free(meta);
  evbuffer_free(playlist);
  evbuffer_free(playlistlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_OK;

 error:
  free(meta);
  evbuffer_free(playlist);
  evbuffer_free(playlistlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_ERROR;
}

static enum daap_reply_result
daap_reply_groups(struct httpd_request *hreq)
{
  struct query_params qp;
  struct db_group_info dbgri;
  struct evbuffer *group;
  struct evbuffer *grouplist;
  const struct dmap_field_map *dfm;
  const struct dmap_field *df;
  const struct dmap_field **meta = NULL;
  struct sort_ctx *sctx;
  cfg_t *lib;
  const char *param;
  char **strval;
  char *tag;
  size_t len;
  int nmeta;
  int sort_headers;
  int ngrp;
  int32_t val;
  int i;
  int ret;

  param = evhttp_find_header(hreq->query, "group-type");
  if (param && strcmp(param, "artists") == 0)
    {
      // Request from Remote may have the form:
      //  groups?meta=dmap.xxx,dma...&type=music&group-type=artists&sort=album&include-sort-headers=1&query=('...')&session-id=...
      // Note: Since grouping by artist and sorting by album is crazy we override
      tag = "agar";
      query_params_set(&qp, &sort_headers, hreq, Q_GROUP_ARTISTS);
      qp.sort = S_ARTIST;
    }
  else
    {
      // Request from Remote may have the form:
      //  groups?meta=dmap.xxx,dma...&type=music&group-type=albums&sort=artist&include-sort-headers=0&query=('...'))&session-id=...
      // Sort may also be 'album'
      tag = "agal";
      query_params_set(&qp, &sort_headers, hreq, Q_GROUP_ALBUMS);
      if (qp.sort == S_NONE)
	qp.sort = S_ALBUM;
    }

  CHECK_NULL(L_DAAP, grouplist = evbuffer_new());
  CHECK_NULL(L_DAAP, group = evbuffer_new());
  CHECK_NULL(L_DAAP, sctx = daap_sort_context_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 61));
  CHECK_ERR(L_DAAP, evbuffer_expand(grouplist, 1024));
  CHECK_ERR(L_DAAP, evbuffer_expand(group, 128));

  param = evhttp_find_header(hreq->query, "meta");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "No meta parameter in query, using default\n");

      param = default_meta_group;
    }

  nmeta = parse_meta(&meta, param);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      dmap_error_make(hreq->reply, tag, "Failed to parse query");
      goto error;
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_error_make(hreq->reply, tag, "Could not start query");
      goto error;
    }

  ngrp = 0;
  while ((ret = db_query_fetch_group(&dbgri, &qp)) == 0)
    {
      /* Don't add item if no name (eg blank album name) */
      if (strlen(dbgri.itemname) == 0)
	continue;

      /* Don't add single item albums/artists if configured to hide */
      lib = cfg_getsec(cfg, "library");
      if (cfg_getbool(lib, "hide_singles") && (strcmp(dbgri.itemcount, "1") == 0))
	continue;

      ngrp++;

      for (i = 0; i < nmeta; i++)
	{
	  df = meta[i];
	  if (!df)
	    continue;

	  dfm = df->dfm;

	  /* dmap.itemcount - always added */
	  if (dfm == &dfm_dmap_mimc)
	    continue;

	  /* Not in struct group_info */
	  if (dfm->gri_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbgri + dfm->gri_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(group, df, *strval, 0);

	  DPRINTF(E_SPAM, L_DAAP, "Done with meta tag %s (%s)\n", df->desc, *strval);
	}

      if (sort_headers)
	{
	  ret = daap_sort_build(sctx, dbgri.itemname_sort);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_DAAP, "Could not add sort header to DAAP groups reply\n");

	      ret = -100;
	      break;
	    }
	}

      /* Item count, always added (mimc) */
      val = 0;
      ret = safe_atoi32(dbgri.itemcount, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(group, "mimc", val);

      /* Song album artist (asaa), always added if group-type is albums  */
      if (qp.type == Q_GROUP_ALBUMS)
        dmap_add_string(group, "asaa", dbgri.songalbumartist);

      /* Item id (miid) */
      val = 0;
      ret = safe_atoi32(dbgri.id, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(group, "miid", val);

      DPRINTF(E_SPAM, L_DAAP, "Done with group\n");

      len = evbuffer_get_length(group);
      dmap_add_container(grouplist, "mlit", len);
      ret = evbuffer_add_buffer(grouplist, group);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add group to group list for DAAP groups reply\n");

	  ret = -100;
	  break;
	}
    }

  db_query_end(&qp);

  DPRINTF(E_DBG, L_DAAP, "Done with group list, %d groups\n", ngrp);

  if (ret == -100)
    {
      dmap_error_make(hreq->reply, tag, "Out of memory");
      goto error;
    }
  else if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
      dmap_error_make(hreq->reply, tag, "Error fetching query results");
      goto error;
    }

  /* Add header to evbuf, add grouplist to evbuf */
  len = evbuffer_get_length(grouplist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(hreq->reply, tag, len + evbuffer_get_length(sctx->headerlist) + 61);
    }
  else
    dmap_add_container(hreq->reply, tag, len + 53);

  dmap_add_int(hreq->reply, "mstt", 200);        /* 12 */
  dmap_add_char(hreq->reply, "muty", 0);         /* 9 */
  dmap_add_int(hreq->reply, "mtco", qp.results); /* 12 */
  dmap_add_int(hreq->reply,"mrco", ngrp);        /* 12 */
  dmap_add_container(hreq->reply, "mlcl", len);  /* 8 */

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, grouplist));

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(hreq->reply, "mshl", len); /* 8 */

      CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, sctx->headerlist));
    }

  free(meta);
  daap_sort_context_free(sctx);
  evbuffer_free(group);
  evbuffer_free(grouplist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_OK;

 error:
  free(meta);
  daap_sort_context_free(sctx);
  evbuffer_free(group);
  evbuffer_free(grouplist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_ERROR;
}

static enum daap_reply_result
daap_reply_browse(struct httpd_request *hreq)
{
  struct query_params qp;
  struct evbuffer *itemlist;
  struct sort_ctx *sctx;
  char *browse_item;
  char *sort_item;
  char *tag;
  size_t len;
  int sort_headers;
  int nitems;
  int ret;

  if (strcmp(hreq->uri_parsed->path_parts[3], "artists") == 0)
    {
      tag = "abar";
      query_params_set(&qp, &sort_headers, hreq, Q_BROWSE_ARTISTS);
    }
  else if (strcmp(hreq->uri_parsed->path_parts[3], "albums") == 0)
    {
      tag = "abal";
      query_params_set(&qp, &sort_headers, hreq, Q_BROWSE_ALBUMS);
    }
  else if (strcmp(hreq->uri_parsed->path_parts[3], "genres") == 0)
    {
      tag = "abgn";
      query_params_set(&qp, &sort_headers, hreq, Q_BROWSE_GENRES);
    }
  else if (strcmp(hreq->uri_parsed->path_parts[3], "composers") == 0)
    {
      tag = "abcp";
      query_params_set(&qp, &sort_headers, hreq, Q_BROWSE_COMPOSERS);
    }
  else
    {
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP browse request type '%s'\n", hreq->uri_parsed->path_parts[3]);
      dmap_error_make(hreq->reply, "abro", "Invalid browse type");
      return DAAP_REPLY_ERROR;
    }

  CHECK_NULL(L_DAAP, itemlist = evbuffer_new());
  CHECK_NULL(L_DAAP, sctx = daap_sort_context_new());
  CHECK_ERR(L_DAAP, evbuffer_expand(hreq->reply, 52));
  CHECK_ERR(L_DAAP, evbuffer_expand(itemlist, 1024)); // Just a starting alloc, it'll expand as needed

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_error_make(hreq->reply, "abro", "Could not start query");
      goto error;
    }

  nitems = 0;
  while (((ret = db_query_fetch_string_sort(&browse_item, &sort_item, &qp)) == 0) && (browse_item))
    {
      nitems++;

      if (sort_headers)
	{
	  ret = daap_sort_build(sctx, sort_item);
	  if (ret < 0)
	    {
	      DPRINTF(E_LOG, L_DAAP, "Could not add sort header to DAAP browse reply\n");
	      break;
	    }
	}

      dmap_add_string(itemlist, "mlit", browse_item);
    }

  db_query_end(&qp);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching/building results\n");

      dmap_error_make(hreq->reply, "abro", "Error fetching/building query results");
      goto error;
    }

  len = evbuffer_get_length(itemlist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(hreq->reply, "abro", len + evbuffer_get_length(sctx->headerlist) + 52);
    }
  else
    dmap_add_container(hreq->reply, "abro", len + 44);

  dmap_add_int(hreq->reply, "mstt", 200);        /* 12 */
  dmap_add_int(hreq->reply, "mtco", qp.results); /* 12 */
  dmap_add_int(hreq->reply, "mrco", nitems);     /* 12 */
  dmap_add_container(hreq->reply, tag, len);     /* 8 */

  CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, itemlist));

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(hreq->reply, "mshl", len); /* 8 */

      CHECK_ERR(L_DAAP, evbuffer_add_buffer(hreq->reply, sctx->headerlist));
    }

  daap_sort_context_free(sctx);
  evbuffer_free(itemlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_OK;

 error:
  daap_sort_context_free(sctx);
  evbuffer_free(itemlist);
  free_query_params(&qp, 1);

  return DAAP_REPLY_ERROR;
}

/* NOTE: We only handle artwork at the moment */
static enum daap_reply_result
daap_reply_extra_data(struct httpd_request *hreq)
{
  struct evkeyvalq *headers;
  char clen[32];
  const char *param;
  char *ctype;
  size_t len;
  int id;
  int max_w;
  int max_h;
  int ret;

  if (!hreq->req)
    {
      DPRINTF(E_LOG, L_DAAP, "Bug! daap_reply_extra_data() cannot be called without an actual connection\n");
      return DAAP_REPLY_NO_CONNECTION;
    }

  ret = safe_atoi32(hreq->uri_parsed->path_parts[3], &id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not convert id parameter to integer: '%s'\n", hreq->uri_parsed->path_parts[3]);
      return DAAP_REPLY_BAD_REQUEST;
    }

  if (evhttp_find_header(hreq->query, "mw") && evhttp_find_header(hreq->query, "mh"))
    {
      param = evhttp_find_header(hreq->query, "mw");
      ret = safe_atoi32(param, &max_w);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not convert mw parameter to integer: '%s'\n", param);
	  return DAAP_REPLY_BAD_REQUEST;
	}

      param = evhttp_find_header(hreq->query, "mh");
      ret = safe_atoi32(param, &max_h);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not convert mh parameter to integer: '%s'\n", param);
	  return DAAP_REPLY_BAD_REQUEST;
	}
    }
  else
    {
      DPRINTF(E_DBG, L_DAAP, "Request for artwork without mw or mh parameter\n");

      max_w = 0;
      max_h = 0;
    }

  if (strcmp(hreq->uri_parsed->path_parts[2], "groups") == 0)
    ret = artwork_get_group(hreq->reply, id, max_w, max_h, 0);
  else if (strcmp(hreq->uri_parsed->path_parts[2], "items") == 0)
    ret = artwork_get_item(hreq->reply, id, max_w, max_h, 0);

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
	if (len > 0)
	  evbuffer_drain(hreq->reply, len);

	goto no_artwork;
    }

  headers = evhttp_request_get_output_headers(hreq->req);
  evhttp_remove_header(headers, "Content-Type");
  evhttp_add_header(headers, "Content-Type", ctype);
  snprintf(clen, sizeof(clen), "%ld", (long)len);
  evhttp_add_header(headers, "Content-Length", clen);

  return DAAP_REPLY_OK_NO_GZIP;

 no_artwork:
  return DAAP_REPLY_NO_CONTENT;
}

static enum daap_reply_result
daap_stream(struct httpd_request *hreq)
{
  int id;
  int ret;

  if (!hreq->req)
    {
      DPRINTF(E_LOG, L_DAAP, "Bug! daap_stream() cannot be called without an actual connection\n");
      return DAAP_REPLY_NO_CONNECTION;
    }

  ret = safe_atoi32(hreq->uri_parsed->path_parts[3], &id);
  if (ret < 0)
    return DAAP_REPLY_BAD_REQUEST;

  httpd_stream_file(hreq->req, id);

  return DAAP_REPLY_NONE;
}

#ifdef DMAP_TEST
static const struct dmap_field dmap_TEST = { "test.container", "TEST", NULL, DMAP_TYPE_LIST };
static const struct dmap_field dmap_TST1 = { "test.ubyte",     "TST1", NULL, DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_TST2 = { "test.byte",      "TST2", NULL, DMAP_TYPE_BYTE };
static const struct dmap_field dmap_TST3 = { "test.ushort",    "TST3", NULL, DMAP_TYPE_USHORT };
static const struct dmap_field dmap_TST4 = { "test.short",     "TST4", NULL, DMAP_TYPE_SHORT };
static const struct dmap_field dmap_TST5 = { "test.uint",      "TST5", NULL, DMAP_TYPE_UINT };
static const struct dmap_field dmap_TST6 = { "test.int",       "TST6", NULL, DMAP_TYPE_INT };
static const struct dmap_field dmap_TST7 = { "test.ulong",     "TST7", NULL, DMAP_TYPE_ULONG };
static const struct dmap_field dmap_TST8 = { "test.long",      "TST8", NULL, DMAP_TYPE_LONG };
static const struct dmap_field dmap_TST9 = { "test.string",    "TST9", NULL, DMAP_TYPE_STRING };

static enum daap_reply_result
daap_reply_dmap_test(struct httpd_request *hreq)
{
  struct evbuffer *test;
  char buf[64];
  int ret;

  CHECK_NULL(L_DAAP, test = evbuffer_new());

  /* UBYTE */
  snprintf(buf, sizeof(buf), "%" PRIu8, UINT8_MAX);
  dmap_add_field(test, &dmap_TST1, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* BYTE */
  snprintf(buf, sizeof(buf), "%" PRIi8, INT8_MIN);
  dmap_add_field(test, &dmap_TST2, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);
  snprintf(buf, sizeof(buf), "%" PRIi8, INT8_MAX);
  dmap_add_field(test, &dmap_TST2, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* USHORT */
  snprintf(buf, sizeof(buf), "%" PRIu16, UINT16_MAX);
  dmap_add_field(test, &dmap_TST3, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* SHORT */
  snprintf(buf, sizeof(buf), "%" PRIi16, INT16_MIN);
  dmap_add_field(test, &dmap_TST4, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);
  snprintf(buf, sizeof(buf), "%" PRIi16, INT16_MAX);
  dmap_add_field(test, &dmap_TST4, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* UINT */
  snprintf(buf, sizeof(buf), "%" PRIu32, UINT32_MAX);
  dmap_add_field(test, &dmap_TST5, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* INT */
  snprintf(buf, sizeof(buf), "%" PRIi32, INT32_MIN);
  dmap_add_field(test, &dmap_TST6, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);
  snprintf(buf, sizeof(buf), "%" PRIi32, INT32_MAX);
  dmap_add_field(test, &dmap_TST6, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* ULONG */
  snprintf(buf, sizeof(buf), "%" PRIu64, UINT64_MAX);
  dmap_add_field(test, &dmap_TST7, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  /* LONG */
  snprintf(buf, sizeof(buf), "%" PRIi64, INT64_MIN);
  dmap_add_field(test, &dmap_TST8, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);
  snprintf(buf, sizeof(buf), "%" PRIi64, INT64_MAX);
  dmap_add_field(test, &dmap_TST8, buf, 0);
  dmap_add_field(test, &dmap_TST9, buf, 0);

  dmap_add_container(hreq->reply, dmap_TEST.tag, evbuffer_get_length(test));

  ret = evbuffer_add_buffer(hreq->reply, test);
  evbuffer_free(test);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add test results to DMAP test reply\n");

      dmap_error_make(hreq->reply, dmap_TEST.tag, "Out of memory");
      return DAAP_REPLY_ERROR;
    }

  return DAAP_REPLY_OK_NO_GZIP;
}
#endif /* DMAP_TEST */

static struct httpd_uri_map daap_handlers[] =
  {
    {
      .regexp = "^/server-info$",
      .handler = daap_reply_server_info
    },
    {
      .regexp = "^/content-codes$",
      .handler = daap_reply_content_codes
    },
    {
      .regexp = "^/login$",
      .handler = daap_reply_login
    },
    {
      .regexp = "^/logout$",
      .handler = daap_reply_logout
    },
    {
      .regexp = "^/update$",
      .handler = daap_reply_update
    },
    {
      .regexp = "^/activity$",
      .handler = daap_reply_activity
    },
    {
      .regexp = "^/databases$",
      .handler = daap_reply_dblist
    },
    {
      .regexp = "^/databases/[[:digit:]]+/browse/[^/]+$",
      .handler = daap_reply_browse
    },
    {
      .regexp = "^/databases/[[:digit:]]+/items$",
      .handler = daap_reply_dbsonglist
    },
    {
      .regexp = "^/databases/[[:digit:]]+/items/[[:digit:]]+[.][^/]+$",
      .handler = daap_stream
    },
    {
      .regexp = "^/databases/[[:digit:]]+/items/[[:digit:]]+/extra_data/artwork$",
      .handler = daap_reply_extra_data
    },
    {
      .regexp = "^/databases/[[:digit:]]+/containers$",
      .handler = daap_reply_playlists
    },
    {
      .regexp = "^/databases/[[:digit:]]+/containers/[[:digit:]]+/items$",
      .handler = daap_reply_plsonglist
    },
    {
      .regexp = "^/databases/[[:digit:]]+/groups$",
      .handler = daap_reply_groups
    },
    {
      .regexp = "^/databases/[[:digit:]]+/groups/[[:digit:]]+/extra_data/artwork$",
      .handler = daap_reply_extra_data
    },
#ifdef DMAP_TEST
    {
      .regexp = "^/dmap-test$",
      .handler = daap_reply_dmap_test
    },
#endif /* DMAP_TEST */
    {
      .regexp = NULL,
      .handler = NULL
    }
  };


/* ------------------------------- DAAP API --------------------------------- */

/* iTunes 9 gives us an absolute request-uri like
 *  daap://10.1.1.20:3689/server-info
 * iTunes 12.1 gives us an absolute request-uri for streaming like
 *  http://10.1.1.20:3689/databases/1/items/1.mp3
 */
void
daap_request(struct evhttp_request *req, struct httpd_uri_parsed *uri_parsed)
{
  struct httpd_request *hreq;
  struct evkeyvalq *headers;
  struct timespec start;
  struct timespec end;
  struct daap_session session;
  const char *param;
  int32_t id;
  int ret;
  int msec;

  DPRINTF(E_DBG, L_DAAP, "DAAP request: '%s'\n", uri_parsed->uri);

  hreq = httpd_request_parse(req, uri_parsed, NULL, daap_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_DAAP, "Unrecognized path '%s' in DAAP request: '%s'\n", uri_parsed->path, uri_parsed->uri);

      httpd_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  // Check if we have a session and point hreq->extra_data to it
  param = evhttp_find_header(hreq->query, "session-id");
  if (param)
    {
      ret = safe_atoi32(param, &id);
      if (ret < 0)
	DPRINTF(E_LOG, L_DAAP, "Ignoring non-numeric session id in DAAP request: '%s'\n", uri_parsed->uri);
      else
	hreq->extra_data = daap_session_get(id);
    }

  // Create an ad-hoc session, which is a way of passing is_remote to the handler, even though no real session exists
  if (!hreq->extra_data)
    {
      memset(&session, 0, sizeof(struct daap_session));
      session.is_remote = (evhttp_find_header(hreq->query, "pairing-guid") != NULL);
      hreq->extra_data = &session;
    }

  ret = daap_request_authorize(hreq);
  if (ret < 0)
    {
      free(hreq);
      return;
    }

  // Set reply headers
  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "Accept-Ranges", "bytes");
  evhttp_add_header(headers, "DAAP-Server", PACKAGE_NAME "/" VERSION);
  // Content-Type for all replies, even the actual audio streaming. Note that
  // video streaming will override this Content-Type with a more appropriate
  // video/<type> Content-Type as expected by clients like Front Row.
  evhttp_add_header(headers, "Content-Type", "application/x-dmap-tagged");

  // Now we create the actual reply
  CHECK_NULL(L_DAAP, hreq->reply = evbuffer_new());

  // Try the cache
  ret = cache_daap_get(hreq->reply, uri_parsed->uri);
  if (ret == 0)
    {
      // The cache will return the data gzipped, so httpd_send_reply won't need to do it
      evhttp_add_header(headers, "Content-Encoding", "gzip");
      httpd_send_reply(req, HTTP_OK, "OK", hreq->reply, HTTPD_SEND_NO_GZIP); // TODO not all want this reply

      evbuffer_free(hreq->reply);
      free(hreq);
      return;
    }

  // No dice, let's call the handler so it can construct a reply and then send it (note that the reply may be an error)
  clock_gettime(CLOCK_MONOTONIC, &start);

  ret = hreq->handler(hreq);

  daap_reply_send(hreq, ret);

  clock_gettime(CLOCK_MONOTONIC, &end);
  msec = (end.tv_sec * 1000 + end.tv_nsec / 1000000) - (start.tv_sec * 1000 + start.tv_nsec / 1000000);

  DPRINTF(E_DBG, L_DAAP, "DAAP request handled in %d milliseconds\n", msec);

  if (ret == DAAP_REPLY_OK && msec > cache_daap_threshold() && hreq->user_agent)
    cache_daap_add(uri_parsed->uri, hreq->user_agent, ((struct daap_session *)hreq->extra_data)->is_remote, msec);

  evbuffer_free(hreq->reply);
  free(hreq);
}

int
daap_is_request(const char *path)
{
  if (strncmp(path, "/databases/", strlen("/databases/")) == 0)
    return 1;
  if (strcmp(path, "/databases") == 0)
    return 1;
  if (strcmp(path, "/server-info") == 0)
    return 1;
  if (strcmp(path, "/content-codes") == 0)
    return 1;
  if (strcmp(path, "/login") == 0)
    return 1;
  if (strcmp(path, "/update") == 0)
    return 1;
  if (strcmp(path, "/activity") == 0)
    return 1;
  if (strcmp(path, "/logout") == 0)
    return 1;

#ifdef DMAP_TEST
  if (strcmp(path, "/dmap-test") == 0)
    return 1;
#endif

  return 0;
}

int
daap_session_is_valid(int id)
{
  struct daap_session *session;

  session = daap_session_get(id);

  if (session)
    session->mtime = time(NULL);

  return session ? 1 : 0;
}

// Thread: Cache
struct evbuffer *
daap_reply_build(const char *uri, const char *user_agent, int is_remote)
{
  struct httpd_request *hreq;
  struct httpd_uri_parsed *uri_parsed;
  struct evbuffer *reply;
  struct daap_session session;
  int ret;

  DPRINTF(E_DBG, L_DAAP, "Building reply for DAAP request: '%s'\n", uri);

  reply = NULL;

  uri_parsed = httpd_uri_parse(uri);
  if (!uri_parsed)
    return NULL;

  hreq = httpd_request_parse(NULL, uri_parsed, user_agent, daap_handlers);
  if (!hreq)
    {
      DPRINTF(E_LOG, L_DAAP, "Cannot build reply, unrecognized path '%s' in request: '%s'\n", uri_parsed->path, uri_parsed->uri);
      goto out_free_uri;
    }

  memset(&session, 0, sizeof(struct daap_session));
  session.is_remote = (bool)is_remote;

  hreq->extra_data = &session;

  CHECK_NULL(L_DAAP, hreq->reply = evbuffer_new());

  ret = hreq->handler(hreq);
  if (ret < 0)
    {
      evbuffer_free(hreq->reply);
      goto out_free_hreq;
    }

  reply = hreq->reply;

 out_free_hreq:
  free(hreq);
 out_free_uri:
  httpd_uri_free(uri_parsed);

  return reply;
}

int
daap_init(void)
{
  char buf[64];
  int i;
  int ret;

  srand((unsigned)time(NULL));
  current_rev = 2;
  update_requests = NULL;

  for (i = 0; daap_handlers[i].handler; i++)
    {
      ret = regcomp(&daap_handlers[i].preg, daap_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
        {
          regerror(ret, &daap_handlers[i].preg, buf, sizeof(buf));

          DPRINTF(E_FATAL, L_DAAP, "DAAP init failed; regexp error: %s\n", buf);
	  return -1;
        }
    }

  return 0;
}

void
daap_deinit(void)
{
  struct daap_session *s;
  struct daap_update_request *ur;
  struct evhttp_connection *evcon;
  int i;

  for (i = 0; daap_handlers[i].handler; i++)
    regfree(&daap_handlers[i].preg);

  for (s = daap_sessions; daap_sessions; s = daap_sessions)
    {
      daap_sessions = s->next;
      daap_session_free(s);
    }

  for (ur = update_requests; update_requests; ur = update_requests)
    {
      update_requests = ur->next;

      evcon = evhttp_request_get_connection(ur->req);
      if (evcon)
	{
	  evhttp_connection_set_closecb(evcon, NULL, NULL);
	  evhttp_connection_free(evcon);
	}

      update_free(ur);
    }
}
