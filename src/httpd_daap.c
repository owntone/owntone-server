/*
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
#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <regex.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>

#include <uninorm.h>
#include <unistd.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "httpd.h"
#include "transcode.h"
#include "artwork.h"
#include "httpd_daap.h"
#include "daap_query.h"
#include "dmap_common.h"
#include "cache.h"

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>

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

struct uri_map {
  regex_t preg;
  char *regexp;
  int (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua);
};

struct daap_session {
  int id;
  char *user_agent;
  time_t mtime;

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


/* Session handling */
static void
daap_session_free(struct daap_session *s)
{
  if (s->user_agent)
    free(s->user_agent);

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
      DPRINTF(E_LOG, L_DAAP, "Error: Request to remove non-existent session. BUG!\n");
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
daap_session_add(const char *user_agent, int request_session_id)
{
  struct daap_session *s;

  daap_session_cleanup();

  s = (struct daap_session *)malloc(sizeof(struct daap_session));
  if (!s)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for DAAP session\n");
      return NULL;
    }

  memset(s, 0, sizeof(struct daap_session));

  if (request_session_id)
    {
      if (daap_session_get(request_session_id))
	{
	  DPRINTF(E_LOG, L_DAAP, "Session id requested in login (%d) is not available\n", request_session_id);
	  return NULL;
	}
      
      s->id = request_session_id;
    }
  else
    {
      while ( (s->id = rand() + 100) && daap_session_get(s->id) );
    }

  s->mtime = time(NULL);

  if (user_agent)
    s->user_agent = strdup(user_agent);

  if (daap_sessions)
    s->next = daap_sessions;

  daap_sessions = s;

  return s;
}

struct daap_session *
daap_session_find(struct evhttp_request *req, struct evkeyvalq *query, struct evbuffer *evbuf)
{
  struct daap_session *s;
  const char *param;
  int id;
  int ret;

  if (!req)
    return NULL;

  param = evhttp_find_header(query, "session-id");
  if (!param)
    {
      DPRINTF(E_WARN, L_DAAP, "No session-id specified in request\n");
      goto invalid;
    }

  ret = safe_atoi32(param, &id);
  if (ret < 0)
    goto invalid;

  s = daap_session_get(id);
  if (!s)
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP session id %d not found\n", id);
      goto invalid;
    }

  s->mtime = time(NULL);

  return s;

 invalid:
  evhttp_send_error(req, 403, "Forbidden");
  return NULL;
}


/* Update requests helpers */
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
  struct evbuffer *evbuf;
  int ret;

  ur = (struct daap_update_request *)arg;

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for DAAP update data\n");

      return;
    }

  ret = evbuffer_expand(evbuf, 32);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP update data\n");

      return;
    }

  /* Send back current revision */
  dmap_add_container(evbuf, "mupd", 24);
  dmap_add_int(evbuf, "mstt", 200);         /* 12 */
  dmap_add_int(evbuf, "musr", current_rev); /* 12 */

  evcon = evhttp_request_get_connection(ur->req);
  evhttp_connection_set_closecb(evcon, NULL, NULL);

  httpd_send_reply(ur->req, HTTP_OK, "OK", evbuf);

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

  update_remove(ur);
}


/* DAAP sort headers helpers */
static struct sort_ctx *
daap_sort_context_new(void)
{
  struct sort_ctx *ctx;
  int ret;

  ctx = (struct sort_ctx *)malloc(sizeof(struct sort_ctx));
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

/* Remotes are clients that will issue DACP commands. For these clients we will
 * do the playback, and we will not stream to them. This is a crude function to
 * identify them, so we can give them appropriate treatment.
 */
static int
is_remote(const char *user_agent)
{
  if (!user_agent)
    return 0;
  if (strcasestr(user_agent, "remote"))
    return 1;
  if (strstr(user_agent, "Retune"))
    return 1;

  return 0;
}

/* We try not to return items that the client cannot play (like Spotify and
 * internet streams in iTunes), or which are inappropriate (like internet streams
 * in the album tab of remotes)
 */
static void
user_agent_filter(const char *user_agent, struct query_params *qp)
{
  const char *filter;
  char *buffer;
  int len;

  if (!user_agent)
    return;

  // Valgrind doesn't like strlen(filter) below, so instead we allocate 128 bytes
  // to hold the string and the leading " AND ". Remember to adjust the 128 if
  // you define strings here that will be too large for the buffer.
  if (is_remote(user_agent))
    filter = "(f.data_kind <> 1)"; // No internet radio
  else
    filter = "(f.data_kind = 0)"; // Only real files

  if (qp->filter)
    {
      len = strlen(qp->filter) + 128;
      buffer = (char *)malloc(len);
      snprintf(buffer, len, "%s AND %s", qp->filter, filter);
      free(qp->filter);
      qp->filter = strdup(buffer);
      free(buffer);
    }
  else
    qp->filter = strdup(filter);

  DPRINTF(E_DBG, L_DAAP, "SQL filter w/client mod: %s\n", qp->filter);
}

/* Returns eg /databases/1/containers from /databases/1/containers?meta=dmap.item... */
static char *
extract_uri(char *full_uri)
{
  char *uri;
  char *ptr;

  ptr = strchr(full_uri, '?');
  if (ptr)
    *ptr = '\0';

  uri = strdup(full_uri);

  if (ptr)
    *ptr = '?';

  if (!uri)
    return NULL;

  ptr = uri;
  uri = evhttp_decode_uri(uri);
  free(ptr);

  return uri;
}

static void
get_query_params(struct evkeyvalq *query, int *sort_headers, struct query_params *qp)
{
  const char *param;
  char *ptr;
  int low;
  int high;
  int ret;

  low = 0;
  high = -1; /* No limit */

  param = evhttp_find_header(query, "index");
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

  qp->idx_type = I_SUB;

  qp->sort = S_NONE;
  param = evhttp_find_header(query, "sort");
  if (param)
    {
      if (strcmp(param, "name") == 0)
	qp->sort = S_NAME;
      else if (strcmp(param, "album") == 0)
	qp->sort = S_ALBUM;
      else if (strcmp(param, "artist") == 0)
	qp->sort = S_ARTIST;
      else if (strcmp(param, "releasedate") == 0)
	qp->sort = S_NAME;
      else
	DPRINTF(E_DBG, L_DAAP, "Unknown sort param: %s\n", param);

      if (qp->sort != S_NONE)
	DPRINTF(E_DBG, L_DAAP, "Sorting songlist by %s\n", param);
    }

  if (sort_headers)
    {
      *sort_headers = 0;
      param = evhttp_find_header(query, "include-sort-headers");
      if (param)
	{
	  if (strcmp(param, "1") == 0)
	    {
	      *sort_headers = 1;
	      DPRINTF(E_DBG, L_DAAP, "Sort headers requested\n");
	    }
	  else
	    DPRINTF(E_DBG, L_DAAP, "Unknown include-sort-headers param: %s\n", param);
	}
    }

  param = evhttp_find_header(query, "query");
  if (!param)
    param = evhttp_find_header(query, "filter");

  if (param)
    {
      DPRINTF(E_DBG, L_DAAP, "DAAP browse query filter: %s\n", param);

      qp->filter = daap_query_parse_sql(param);
      if (!qp->filter)
	DPRINTF(E_LOG, L_DAAP, "Ignoring improper DAAP query: %s\n", param);

      /* iTunes seems to default to this when there is a query (which there is for audiobooks, but not for normal playlists) */
      if (qp->sort == S_NONE)
	qp->sort = S_ALBUM;
    }
}

static int
parse_meta(struct evhttp_request *req, char *tag, const char *param, const struct dmap_field ***out_meta)
{
  const struct dmap_field **meta;
  char *ptr;
  char *field;
  char *metastr;

  int nmeta;
  int i;
  int n;

  metastr = strdup(param);
  if (!metastr)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not duplicate meta parameter; out of memory\n");

      dmap_send_error(req, tag, "Out of memory");
      return -1;
    }

  nmeta = 1;
  ptr = metastr;
  while ((ptr = strchr(ptr + 1, ',')) && (strlen(ptr) > 1))
    nmeta++;

  DPRINTF(E_DBG, L_DAAP, "Asking for %d meta tags\n", nmeta);

  meta = (const struct dmap_field **)malloc(nmeta * sizeof(const struct dmap_field *));
  if (!meta)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate meta array; out of memory\n");

      dmap_send_error(req, tag, "Out of memory");

      nmeta = -1;
      goto out;
    }
  memset(meta, 0, nmeta * sizeof(struct dmap_field *));

  field = strtok_r(metastr, ",", &ptr);
  for (i = 0; i < nmeta; i++)
    {
      for (n = 0; (n < i) && (strcmp(field, meta[n]->desc) != 0); n++);

      if (n == i)
	{
	  meta[i] = dmap_find_field(field, strlen(field));

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
      if (!field)
	break;
    }

  DPRINTF(E_DBG, L_DAAP, "Found %d meta tags\n", nmeta);

  *out_meta = meta;

 out:
  free(metastr);

  return nmeta;
}


static int
daap_reply_server_info(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct evbuffer *content;
  struct evkeyvalq *headers;
  cfg_t *lib;
  char *name;
  char *passwd;
  const char *clientver;
  size_t len;
  int mpro;
  int apro;

  lib = cfg_getsec(cfg, "library");
  passwd = cfg_getstr(lib, "password");
  name = cfg_getstr(lib, "name");

  content = evbuffer_new();
  if (!content)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DAAP server-info reply\n");

      dmap_send_error(req, "msrv", "Out of memory");
      return -1;
    }

  mpro = 2 << 16 | 10;
  apro = 3 << 16 | 12;

  headers = evhttp_request_get_input_headers(req);
  clientver = evhttp_find_header(headers, "Client-DAAP-Version");
  if (clientver)
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

  /* Sub-optimal user-agent sniffing to solve the problem that iTunes 12.1
   * does not work if we announce support for groups.
   */ 
  ua = evhttp_find_header(headers, "User-Agent");
  if (ua && (strncmp(ua, "iTunes", strlen("iTunes")) == 0))
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
  dmap_add_container(evbuf, "msrv", len);
  evbuffer_add_buffer(evbuf, content);
  evbuffer_free(content);

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;
}

static int
daap_reply_content_codes(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  const struct dmap_field *dmap_fields;
  size_t len;
  int nfields;
  int i;
  int ret;

  dmap_fields = dmap_get_fields_table(&nfields);

  len = 12;
  for (i = 0; i < nfields; i++)
    len += 8 + 12 + 10 + 8 + strlen(dmap_fields[i].desc);

  ret = evbuffer_expand(evbuf, len + 8);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP content-codes reply\n");

      dmap_send_error(req, "mccr", "Out of memory");
      return -1;
    } 

  dmap_add_container(evbuf, "mccr", len);
  dmap_add_int(evbuf, "mstt", 200);

  for (i = 0; i < nfields; i++)
    {
      len = 12 + 10 + 8 + strlen(dmap_fields[i].desc);

      dmap_add_container(evbuf, "mdcl", len);
      dmap_add_string(evbuf, "mcnm", dmap_fields[i].tag);  /* 12 */
      dmap_add_string(evbuf, "mcna", dmap_fields[i].desc); /* 8 + strlen(desc) */
      dmap_add_short(evbuf, "mcty", dmap_fields[i].type);  /* 10 */
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;
}

static int
daap_reply_login(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct pairing_info pi;
  struct daap_session *s;
  const char *param;
  int request_session_id;
  int ret;

  ret = evbuffer_expand(evbuf, 32);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP login reply\n");

      dmap_send_error(req, "mlog", "Out of memory");
      return -1;
    }

  if (ua && (strncmp(ua, "Remote", strlen("Remote")) == 0))
    {
      param = evhttp_find_header(query, "pairing-guid");
      if (!param)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt with U-A: Remote and no pairing-guid\n");

	  evhttp_send_error(req, 403, "Forbidden");
	  return -1;
	}

      memset(&pi, 0, sizeof(struct pairing_info));
      pi.guid = strdup(param + 2); /* Skip leading 0X */

      ret = db_pairing_fetch_byguid(&pi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt with invalid pairing-guid\n");

	  free_pi(&pi, 1);
	  evhttp_send_error(req, 403, "Forbidden");
	  return -1;
	}

      DPRINTF(E_INFO, L_DAAP, "Remote '%s' logging in with GUID %s\n", pi.name, pi.guid);
      free_pi(&pi, 1);
    }

  param = evhttp_find_header(query, "request-session-id");
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

  s = daap_session_add(ua, request_session_id);
  if (!s)
    {
      dmap_send_error(req, "mlog", "Could not start session");
      return -1;
    }

  dmap_add_container(evbuf, "mlog", 24);
  dmap_add_int(evbuf, "mstt", 200);        /* 12 */
  dmap_add_int(evbuf, "mlid", s->id); /* 12 */

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;
}

static int
daap_reply_logout(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return -1;

  daap_session_remove(s);

  httpd_send_reply(req, 204, "Logout Successful", evbuf);

  return 0;
}

static int
daap_reply_update(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct daap_session *s;
  struct daap_update_request *ur;
  struct evhttp_connection *evcon;
  const char *param;
  int reqd_rev;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return -1;

  param = evhttp_find_header(query, "revision-number");
  if (!param)
    {
      DPRINTF(E_DBG, L_DAAP, "Missing revision-number in client update request\n");
      /* Some players (Amarok, Banshee) don't supply a revision number.
	 They get a standard update of everything. */
      param = "1";  /* Default to "1" will insure update */
    }

  ret = safe_atoi32(param, &reqd_rev);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Parameter revision-number not an integer\n");

      dmap_send_error(req, "mupd", "Invalid request");
      return -1;
    }

  if (reqd_rev == 1) /* Or revision is not valid */
    {
      ret = evbuffer_expand(evbuf, 32);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP update reply\n");

	  dmap_send_error(req, "mupd", "Out of memory");
	  return -1;
	}

      /* Send back current revision */
      dmap_add_container(evbuf, "mupd", 24);
      dmap_add_int(evbuf, "mstt", 200);         /* 12 */
      dmap_add_int(evbuf, "musr", current_rev); /* 12 */

      httpd_send_reply(req, HTTP_OK, "OK", evbuf);

      return 0;
    }

  /* Else, just let the request hang until we have changes to push back */
  ur = (struct daap_update_request *)malloc(sizeof(struct daap_update_request));
  if (!ur)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for update request\n");

      dmap_send_error(req, "mupd", "Out of memory");
      return -1;
    }
  memset(ur, 0, sizeof(struct daap_update_request));

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

	  dmap_send_error(req, "mupd", "Could not register timer");	
	  update_free(ur);
	  return -1;
	}
    }

  /* NOTE: we may need to keep reqd_rev in there too */
  ur->req = req;

  ur->next = update_requests;
  update_requests = ur;

  /* If the connection fails before we have an update to push out
   * to the client, we need to know.
   */
  evcon = evhttp_request_get_connection(req);
  if (evcon)
    evhttp_connection_set_closecb(evcon, update_fail_cb, ur);

  return 0;
}

static int
daap_reply_activity(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  /* That's so nice, thanks for letting us know */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);

  return 0;
}

static int
daap_reply_dblist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct evbuffer *content;
  struct evbuffer *item;
  struct daap_session *s;
  cfg_t *lib;
  char *name;
  char *name_radio;
  size_t len;
  int count;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return -1;

  lib = cfg_getsec(cfg, "library");
  name = cfg_getstr(lib, "name");
  name_radio = cfg_getstr(lib, "name_radio");

  content = evbuffer_new();
  if (!content)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DAAP dblist reply\n");

      dmap_send_error(req, "avdb", "Out of memory");
      return -1;
    }

  // Add db entry for library with dbid = 1
  item = evbuffer_new();
  if (!item)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DAAP dblist library item\n");

      dmap_send_error(req, "avdb", "Out of memory");
      return -1;
    }

  dmap_add_int(item, "miid", 1);
  dmap_add_long(item, "mper", 1);
  dmap_add_int(item, "mdbk", 1);
  dmap_add_int(item, "aeCs", 1);
  dmap_add_string(item, "minm", name);
  count = db_files_get_count();
  dmap_add_int(item, "mimc", count);
  count = db_pl_get_count(); // TODO Don't count empty smart playlists, because they get excluded in aply
  dmap_add_int(item, "mctc", count);
//  dmap_add_int(content, "aeMk", 0x405);   // com.apple.itunes.extended-media-kind (OR of all in library)
  dmap_add_int(item, "meds", 3);

  // Create container for library db
  len = evbuffer_get_length(item);
  dmap_add_container(content, "mlit", len);
  evbuffer_add_buffer(content, item);
  evbuffer_free(item);

  // Add second db entry for radio with dbid = DAAP_DB_RADIO
  item =  evbuffer_new();
  if (!item)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DAAP dblist radio item\n");

      dmap_send_error(req, "avdb", "Out of memory");
      return -1;
    }

  dmap_add_int(item, "miid", DAAP_DB_RADIO);
  dmap_add_long(item, "mper", DAAP_DB_RADIO);
  dmap_add_int(item, "mdbk", 0x64);
  dmap_add_int(item, "aeCs", 0);
  dmap_add_string(item, "minm", name_radio);
  count = db_pl_get_count();       // TODO This counts too much, should only include stream playlists
  dmap_add_int(item, "mimc", count);
  dmap_add_int(item, "mctc", 0);
  dmap_add_int(item, "aeMk", 1);   // com.apple.itunes.extended-media-kind (OR of all in library)
  dmap_add_int(item, "meds", 3);

  // Create container for radio db
  len = evbuffer_get_length(item);
  dmap_add_container(content, "mlit", len);
  evbuffer_add_buffer(content, item);
  evbuffer_free(item);

  // Create container
  len = evbuffer_get_length(content);
  dmap_add_container(evbuf, "avdb", len + 53);
  dmap_add_int(evbuf, "mstt", 200);     /* 12 */
  dmap_add_char(evbuf, "muty", 0);      /* 9 */
  dmap_add_int(evbuf, "mtco", 2);       /* 12 */
  dmap_add_int(evbuf, "mrco", 2);       /* 12 */
  dmap_add_container(evbuf, "mlcl", len); /* 8 */
  evbuffer_add_buffer(evbuf, content);
  evbuffer_free(content);

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;
}

static int
daap_reply_songlist_generic(struct evhttp_request *req, struct evbuffer *evbuf, int playlist, struct evkeyvalq *query, const char *ua)
{
  struct daap_session *s;
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct evbuffer *song;
  struct evbuffer *songlist;
  struct evkeyvalq *headers;
  const struct dmap_field **meta;
  struct sort_ctx *sctx;
  const char *param;
  const char *client_codecs;
  char *last_codectype;
  char *tag;
  size_t len;
  int nmeta;
  int sort_headers;
  int nsongs;
  int remote;
  int transcode;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s && req)
    return -1;

  DPRINTF(E_DBG, L_DAAP, "Fetching song list for playlist %d\n", playlist);

  if (playlist != -1)
    tag = "apso"; /* Songs in playlist */
  else
    tag = "adbs"; /* Songs in database */

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP song list reply\n");

      dmap_send_error(req, tag, "Out of memory");
      return -1;
    }

  songlist = evbuffer_new();
  if (!songlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP song list\n");

      dmap_send_error(req, tag, "Out of memory");
      return -1;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(songlist, 4096);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP song list\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_list_free;
    }

  song = evbuffer_new();
  if (!song)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP song block\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_list_free;
    }

  /* The buffer will expand if needed */
  ret = evbuffer_expand(song, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP song block\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_song_free;
    }

  param = evhttp_find_header(query, "meta");
  if (!param)
    {
      DPRINTF(E_DBG, L_DAAP, "No meta parameter in query, using default\n");

      if (playlist != -1)
	param = default_meta_plsongs;
    }

  if (param)
    {
      nmeta = parse_meta(req, tag, param, &meta);
      if (nmeta < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

	  goto out_song_free;
	}
    }
  else
    {
      meta = NULL;
      nmeta = 0;
    }

  memset(&qp, 0, sizeof(struct query_params));
  get_query_params(query, &sort_headers, &qp);

  if (playlist == -1)
    user_agent_filter(ua, &qp);

  sctx = NULL;
  if (sort_headers)
    {
      sctx = daap_sort_context_new();
      if (!sctx)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not create sort context\n");

	  dmap_send_error(req, tag, "Out of memory");
	  goto out_query_free;
	}
    }

  if (playlist != -1)
    {
      qp.type = Q_PLITEMS;
      qp.id = playlist;
    }
  else
    qp.type = Q_ITEMS;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, tag, "Could not start query");

      if (sort_headers)
	daap_sort_context_free(sctx);

      goto out_query_free;
    }

  remote = is_remote(ua);

  client_codecs = NULL;
  if (!remote && req)
    {
      headers = evhttp_request_get_input_headers(req);
      client_codecs = evhttp_find_header(headers, "Accept-Codecs");
    }

  nsongs = 0;
  last_codectype = NULL;
  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      nsongs++;

      if (!dbmfi.codectype)
	{
	  DPRINTF(E_LOG, L_DAAP, "Cannot transcode '%s', codec type is unknown\n", dbmfi.fname);

	  transcode = 0;
	}
      else if (remote)
	{
	  transcode = 1;
	}
      else if (!last_codectype || (strcmp(last_codectype, dbmfi.codectype) != 0))
	{
	  transcode = transcode_needed(ua, client_codecs, dbmfi.codectype);

	  if (last_codectype)
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

  if (last_codectype)
    free(last_codectype);

  if (nmeta > 0)
    free(meta);

  evbuffer_free(song);

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      if (ret == -100)
	  dmap_send_error(req, tag, "Out of memory");
      else
	{
	  DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
	  dmap_send_error(req, tag, "Error fetching query results");
	}

      db_query_end(&qp);

      if (sort_headers)
	daap_sort_context_free(sctx);

      goto out_list_free;
    }

  /* Add header to evbuf, add songlist to evbuf */
  len = evbuffer_get_length(songlist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(evbuf, tag, len + evbuffer_get_length(sctx->headerlist) + 61);
    }
  else
    dmap_add_container(evbuf, tag, len + 53);
  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_char(evbuf, "muty", 0);     /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf, "mrco", nsongs); /* 12 */
  dmap_add_container(evbuf, "mlcl", len); /* 8 */

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, songlist);
  evbuffer_free(songlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add song list to DAAP song list reply\n");

      dmap_send_error(req, tag, "Out of memory");

      if (sort_headers)
	daap_sort_context_free(sctx);

      return -1;
    }

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(evbuf, "mshl", len); /* 8 */
      ret = evbuffer_add_buffer(evbuf, sctx->headerlist);
      daap_sort_context_free(sctx);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP song list reply\n");

	  dmap_send_error(req, tag, "Out of memory");
	  return -1;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;

 out_query_free:
  if (nmeta > 0)
    free(meta);

  if (qp.filter)
    free(qp.filter);

 out_song_free:
  evbuffer_free(song);

 out_list_free:
  evbuffer_free(songlist);

  return -1;
}

static int
daap_reply_dbsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  return daap_reply_songlist_generic(req, evbuf, -1, query, ua);
}

static int
daap_reply_plsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  int playlist;
  int ret;

  ret = safe_atoi32(uri[3], &playlist);
  if (ret < 0)
    {
      dmap_send_error(req, "apso", "Invalid playlist ID");

      return -1;
    }

  return daap_reply_songlist_generic(req, evbuf, playlist, query, ua);
}

static int
daap_reply_playlists(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  struct daap_session *s;
  struct evbuffer *playlistlist;
  struct evbuffer *playlist;
  cfg_t *lib;
  const struct dmap_field_map *dfm;
  const struct dmap_field *df;
  const struct dmap_field **meta;
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

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return -1;

  ret = safe_atoi32(uri[1], &database);
  if (ret < 0)
    {
      dmap_send_error(req, "aply", "Invalid database ID");

      return -1;
    }

  lib = cfg_getsec(cfg, "library");
  cfg_radiopl = cfg_getbool(lib, "radio_playlists");

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP playlists reply\n");

      dmap_send_error(req, "aply", "Out of memory");
      return -1;
    }

  playlistlist = evbuffer_new();
  if (!playlistlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP playlist list\n");

      dmap_send_error(req, "aply", "Out of memory");
      return -1;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(playlistlist, 1024);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP playlist list\n");

      dmap_send_error(req, "aply", "Out of memory");
      goto out_list_free;
    }

  playlist = evbuffer_new();
  if (!playlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP playlist block\n");

      dmap_send_error(req, "aply", "Out of memory");
      goto out_list_free;
    }

  /* The buffer will expand if needed */
  ret = evbuffer_expand(playlist, 128);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP playlist block\n");

      dmap_send_error(req, "aply", "Out of memory");
      goto out_pl_free;
    }

  param = evhttp_find_header(query, "meta");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "No meta parameter in query, using default\n");

      param = default_meta_pl;
    }

  nmeta = parse_meta(req, "aply", param, &meta);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      goto out_pl_free;
    }

  memset(&qp, 0, sizeof(struct query_params));
  get_query_params(query, NULL, &qp);
  qp.type = Q_PL;
  qp.sort = S_PLAYLIST;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, "aply", "Could not start query");
      goto out_query_free;
    }

  npls = 0;
  while (((ret = db_query_fetch_pl(&qp, &dbpli)) == 0) && (dbpli.id))
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

      DPRINTF(E_DBG, L_DAAP, "Done with playlist\n");

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

  DPRINTF(E_DBG, L_DAAP, "Done with playlist list, %d playlists\n", npls);

  free(meta);
  evbuffer_free(playlist);

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      if (ret == -100)
	dmap_send_error(req, "aply", "Out of memory");
      else
	{
	  DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
	  dmap_send_error(req, "aply", "Error fetching query results");
	}

      db_query_end(&qp);
      goto out_list_free;
    }

  /* Add header to evbuf, add playlistlist to evbuf */
  len = evbuffer_get_length(playlistlist);
  dmap_add_container(evbuf, "aply", len + 53);
  dmap_add_int(evbuf, "mstt", 200); /* 12 */
  dmap_add_char(evbuf, "muty", 0);  /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf,"mrco", npls); /* 12 */
  dmap_add_container(evbuf, "mlcl", len);

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, playlistlist);
  evbuffer_free(playlistlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add playlist list to DAAP playlists reply\n");

      dmap_send_error(req, "aply", "Out of memory");
      return -1;
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;

 out_query_free:
  free(meta);
  if (qp.filter)
    free(qp.filter);

 out_pl_free:
  evbuffer_free(playlist);

 out_list_free:
  evbuffer_free(playlistlist);

  return -1;
}

static int
daap_reply_groups(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct query_params qp;
  struct db_group_info dbgri;
  struct daap_session *s;
  struct evbuffer *group;
  struct evbuffer *grouplist;
  const struct dmap_field_map *dfm;
  const struct dmap_field *df;
  const struct dmap_field **meta;
  struct sort_ctx *sctx;
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

  s = daap_session_find(req, query, evbuf);
  if (!s && req)
    return -1;

  memset(&qp, 0, sizeof(struct query_params));

  get_query_params(query, &sort_headers, &qp);

  user_agent_filter(ua, &qp);

  param = evhttp_find_header(query, "group-type");
  if (strcmp(param, "artists") == 0)
    {
      tag = "agar";
      qp.type = Q_GROUP_ARTISTS;
      qp.sort = S_ARTIST;
    }
  else
    {
      tag = "agal";
      qp.type = Q_GROUP_ALBUMS;
      qp.sort = S_ALBUM;
    }

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP groups reply\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_qfilter_free;
    }

  grouplist = evbuffer_new();
  if (!grouplist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP group list\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_qfilter_free;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(grouplist, 1024);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP group list\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_list_free;
    }

  group = evbuffer_new();
  if (!group)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP group block\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_list_free;
    }

  /* The buffer will expand if needed */
  ret = evbuffer_expand(group, 128);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP group block\n");

      dmap_send_error(req, tag, "Out of memory");
      goto out_group_free;
    }

  param = evhttp_find_header(query, "meta");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "No meta parameter in query, using default\n");

      param = default_meta_group;
    }

  nmeta = parse_meta(req, tag, param, &meta);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      goto out_group_free;
    }

  sctx = NULL;
  if (sort_headers)
    {
      sctx = daap_sort_context_new();
      if (!sctx)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not create sort context\n");

	  dmap_send_error(req, tag, "Out of memory");
	  goto out_query_free;
	}
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, tag, "Could not start query");

      if (sort_headers)
	daap_sort_context_free(sctx);

      goto out_query_free;
    }

  ngrp = 0;
  while ((ret = db_query_fetch_group(&qp, &dbgri)) == 0)
    {
      /* Don't add item if no name (eg blank album name) */
      if (strlen(dbgri.itemname) == 0)
	continue;

      ngrp++;

      for (i = 0; i < nmeta; i++)
	{
	  df = meta[i];
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

  DPRINTF(E_DBG, L_DAAP, "Done with group list, %d groups\n", ngrp);

  free(meta);
  evbuffer_free(group);

  if (qp.filter)
    {
        free(qp.filter);
        qp.filter = NULL;
    }

  if (ret < 0)
    {
      if (ret == -100)
	dmap_send_error(req, tag, "Out of memory");
      else
	{
	  DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");
	  dmap_send_error(req, tag, "Error fetching query results");
	}

      db_query_end(&qp);

      if (sort_headers)
	daap_sort_context_free(sctx);

      goto out_list_free;
    }

  /* Add header to evbuf, add grouplist to evbuf */
  len = evbuffer_get_length(grouplist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(evbuf, tag, len + evbuffer_get_length(sctx->headerlist) + 61);
    }
  else
    dmap_add_container(evbuf, tag, len + 53);

  dmap_add_int(evbuf, "mstt", 200); /* 12 */
  dmap_add_char(evbuf, "muty", 0);  /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf,"mrco", ngrp); /* 12 */
  dmap_add_container(evbuf, "mlcl", len); /* 8 */

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, grouplist);
  evbuffer_free(grouplist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add group list to DAAP groups reply\n");

      dmap_send_error(req, tag, "Out of memory");

      if (sort_headers)
	daap_sort_context_free(sctx);

      return -1;
    }

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(evbuf, "mshl", len); /* 8 */
      ret = evbuffer_add_buffer(evbuf, sctx->headerlist);
      daap_sort_context_free(sctx);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP groups reply\n");

	  dmap_send_error(req, tag, "Out of memory");
	  return -1;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;

 out_query_free:
  free(meta);

 out_group_free:
  evbuffer_free(group);

 out_list_free:
  evbuffer_free(grouplist);

 out_qfilter_free:
  free(qp.filter);

  return -1;
}

static int
daap_reply_browse(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  struct query_params qp;
  struct daap_session *s;
  struct evbuffer *itemlist;
  struct sort_ctx *sctx;
  char *browse_item;
  char *sort_item;
  char *tag;
  size_t len;
  int sort_headers;
  int nitems;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s && req)
    return -1;

  memset(&qp, 0, sizeof(struct query_params));

  get_query_params(query, &sort_headers, &qp);

  user_agent_filter(ua, &qp);

  if (strcmp(uri[3], "artists") == 0)
    {
      tag = "abar";
      qp.type = Q_BROWSE_ARTISTS;
      qp.sort = S_ARTIST;
    }
  else if (strcmp(uri[3], "albums") == 0)
    {
      tag = "abal";
      qp.type = Q_BROWSE_ALBUMS;
      qp.sort = S_ALBUM;
    }
  else if (strcmp(uri[3], "genres") == 0)
    {
      tag = "abgn";
      qp.type = Q_BROWSE_GENRES;
      qp.sort = S_GENRE;
    }
  else if (strcmp(uri[3], "composers") == 0)
    {
      tag = "abcp";
      qp.type = Q_BROWSE_COMPOSERS;
      qp.sort = S_COMPOSER;
    }
  else
    {
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP browse request type '%s'\n", uri[3]);

      dmap_send_error(req, "abro", "Invalid browse type");
      ret = -1;

      goto out_qfilter_free;
    }

  ret = evbuffer_expand(evbuf, 52);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP browse reply\n");

      dmap_send_error(req, "abro", "Out of memory");
      goto out_qfilter_free;
    }

  itemlist = evbuffer_new();
  if (!itemlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP browse item list\n");

      dmap_send_error(req, "abro", "Out of memory");
      ret = -1;

      goto out_qfilter_free;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(itemlist, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP browse item list\n");

      dmap_send_error(req, "abro", "Out of memory");

      goto out_itemlist_free;
    }

  sctx = NULL;
  if (sort_headers)
    {
      sctx = daap_sort_context_new();
      if (!sctx)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not create sort context\n");

	  dmap_send_error(req, "abro", "Out of memory");
	  ret = -1;

          goto out_itemlist_free;
	}
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, "abro", "Could not start query");

      goto out_sort_headers_free;
    }

  nitems = 0;
  while (((ret = db_query_fetch_string_sort(&qp, &browse_item, &sort_item)) == 0) && (browse_item))
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

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching/building results\n");

      dmap_send_error(req, "abro", "Error fetching/building query results");
      db_query_end(&qp);

      goto out_sort_headers_free;
    }

  len = evbuffer_get_length(itemlist);
  if (sort_headers)
    {
      daap_sort_finalize(sctx);
      dmap_add_container(evbuf, "abro", len + evbuffer_get_length(sctx->headerlist) + 52);
    }
  else
    dmap_add_container(evbuf, "abro", len + 44);

  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf, "mrco", nitems); /* 12 */

  dmap_add_container(evbuf, tag, len); /* 8 */

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, itemlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add item list to DAAP browse reply\n");

      dmap_send_error(req, tag, "Out of memory");

      goto out_sort_headers_free;
    }

  if (sort_headers)
    {
      len = evbuffer_get_length(sctx->headerlist);
      dmap_add_container(evbuf, "mshl", len); /* 8 */
      ret = evbuffer_add_buffer(evbuf, sctx->headerlist);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP browse reply\n");

	  dmap_send_error(req, tag, "Out of memory");

	  goto out_sort_headers_free;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

  ret = 0;

 out_sort_headers_free:
  if (sort_headers)
    daap_sort_context_free(sctx);

 out_itemlist_free:
  evbuffer_free(itemlist);

 out_qfilter_free:
  if (qp.filter)
    free(qp.filter);

  return ret;
}

/* NOTE: We only handle artwork at the moment */
static int
daap_reply_extra_data(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  char clen[32];
  struct daap_session *s;
  struct evkeyvalq *headers;
  const char *param;
  char *ctype;
  size_t len;
  int id;
  int max_w;
  int max_h;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return -1;

  ret = safe_atoi32(uri[3], &id);
  if (ret < 0)
    {
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return -1;
    }

  if (evhttp_find_header(query, "mw") && evhttp_find_header(query, "mh"))
    {
      param = evhttp_find_header(query, "mw");
      ret = safe_atoi32(param, &max_w);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not convert mw parameter to integer\n");

	  evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
	  return -1;
	}

      param = evhttp_find_header(query, "mh");
      ret = safe_atoi32(param, &max_h);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not convert mh parameter to integer\n");

	  evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
	  return -1;
	}
    }
  else
    {
      DPRINTF(E_DBG, L_DAAP, "Request for artwork without mw/mh parameter\n");

      max_w = 0;
      max_h = 0;
    }

  if (strcmp(uri[2], "groups") == 0)
    ret = artwork_get_group(evbuf, id, max_w, max_h);
  else if (strcmp(uri[2], "items") == 0)
    ret = artwork_get_item(evbuf, id, max_w, max_h);

  len = evbuffer_get_length(evbuf);

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
	  evbuffer_drain(evbuf, len);

	goto no_artwork;
    }

  headers = evhttp_request_get_output_headers(req);
  evhttp_remove_header(headers, "Content-Type");
  evhttp_add_header(headers, "Content-Type", ctype);
  snprintf(clen, sizeof(clen), "%ld", (long)len);
  evhttp_add_header(headers, "Content-Length", clen);

  /* No gzip compression for artwork */
  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
  return 0;

 no_artwork:
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
  return -1;
}

static int
daap_stream(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  int id;
  int ret;

  ret = safe_atoi32(uri[3], &id);
  if (ret < 0)
    evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
  else
    httpd_stream_file(req, id);

  return ret;
}

static char *
uri_relative(char *uri, const char *protocol)
{
  char *ret;

  if (strncmp(uri, protocol, strlen(protocol)) != 0)
    return NULL;

  ret = strchr(uri + strlen(protocol), '/');
  if (!ret)
    {
      DPRINTF(E_LOG, L_DAAP, "Malformed DAAP Request URI '%s'\n", uri);
      return NULL;
    }

  return ret;
}

static char *
daap_fix_request_uri(struct evhttp_request *req, char *uri)
{
  char *ret;

  /* iTunes 9 gives us an absolute request-uri like
   *  daap://10.1.1.20:3689/server-info
   * iTunes 12.1 gives us an absolute request-uri for streaming like
   *  http://10.1.1.20:3689/databases/1/items/1.mp3
   */

  if ( (ret = uri_relative(uri, "daap://")) || (ret = uri_relative(uri, "http://")) )
    {
      /* Clear the proxy request flag set by evhttp
       * due to the request URI being absolute.
       * It has side-effects on Connection: keep-alive
       */
      req->flags &= ~EVHTTP_PROXY_REQUEST;
      return ret;
    }

  return uri;
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

static int
daap_reply_dmap_test(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query, const char *ua)
{
  char buf[64];
  struct evbuffer *test;
  int ret;

  test = evbuffer_new();
  if (!test)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP test\n");

      dmap_send_error(req, dmap_TEST.tag, "Out of memory");
      return -1;
    }

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

  dmap_add_container(evbuf, dmap_TEST.tag, evbuffer_get_length(test));

  ret = evbuffer_add_buffer(evbuf, test);
  evbuffer_free(test);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add test results to DMAP test reply\n");

      dmap_send_error(req, dmap_TEST.tag, "Out of memory");
      return -1;      
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  return 0;
}
#endif /* DMAP_TEST */


static struct uri_map daap_handlers[] =
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


void
daap_request(struct evhttp_request *req)
{
  char *full_uri;
  char *uri;
  char *ptr;
  char *uri_parts[7];
  struct evbuffer *evbuf;
  struct evkeyvalq query;
  struct evkeyvalq *headers;
  struct timespec start;
  struct timespec end;
  const char *ua;
  cfg_t *lib;
  char *libname;
  char *passwd;
  int msec;
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

  ptr = daap_fix_request_uri(req, full_uri);
  if (!ptr)
    {
      free(full_uri);
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  if (ptr != full_uri)
    {
      uri = strdup(ptr);
      free(full_uri);

      if (!uri)
	{
	  evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
	  return;
	}

      full_uri = uri;
    }

  uri = extract_uri(full_uri);
  if (!uri)
    {
      free(full_uri);
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  DPRINTF(E_DBG, L_DAAP, "DAAP request: %s\n", full_uri);

  handler = -1;
  for (i = 0; daap_handlers[i].handler; i++)
    {
      ret = regexec(&daap_handlers[i].preg, uri, 0, NULL, 0);
      if (ret == 0)
        {
          handler = i;
          break;
        }
    }

  if (handler < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Unrecognized DAAP request\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

      free(uri);
      free(full_uri);
      return;
    }

  /* Check authentication */
  lib = cfg_getsec(cfg, "library");
  passwd = cfg_getstr(lib, "password");

  /* No authentication for these URIs */
  if ((strcmp(uri, "/server-info") == 0)
      || (strcmp(uri, "/logout") == 0)
      || (strncmp(uri, "/databases/1/items/", strlen("/databases/1/items/")) == 0))
    passwd = NULL;

  /* Waive HTTP authentication for Remote
   * Remotes are authentified by their pairing-guid; DAAP queries require a
   * valid session-id that Remote can only obtain if its pairing-guid is in
   * our database. So HTTP authentication is waived for Remote.
   */
  headers = evhttp_request_get_input_headers(req);
  ua = evhttp_find_header(headers, "User-Agent");
  if ((ua) && (strncmp(ua, "Remote", strlen("Remote")) == 0))
    passwd = NULL;

  if (passwd)
    {
      libname = cfg_getstr(lib, "name");

      DPRINTF(E_DBG, L_HTTPD, "Checking authentication for library '%s'\n", libname);

      /* We don't care about the username */
      ret = httpd_basic_auth(req, NULL, passwd, libname);
      if (ret != 0)
	{
	  free(uri);
	  free(full_uri);
	  return;
	}

      DPRINTF(E_DBG, L_HTTPD, "Library authentication successful\n");
    }

  memset(uri_parts, 0, sizeof(uri_parts));

  uri_parts[0] = strtok_r(uri, "/", &ptr);
  for (i = 1; (i < sizeof(uri_parts) / sizeof(uri_parts[0])) && uri_parts[i - 1]; i++)
    {
      uri_parts[i] = strtok_r(NULL, "/", &ptr);
    }

  if (!uri_parts[0] || uri_parts[i - 1] || (i < 2))
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP URI has too many/few components (%d)\n", (uri_parts[0]) ? i : 0);

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");

      free(uri);
      free(full_uri);
      return;
    }

  // Set reply headers
  headers = evhttp_request_get_output_headers(req);
  evhttp_add_header(headers, "Accept-Ranges", "bytes");
  evhttp_add_header(headers, "DAAP-Server", "forked-daapd/" VERSION);
  /* Content-Type for all replies, even the actual audio streaming. Note that
   * video streaming will override this Content-Type with a more appropriate
   * video/<type> Content-Type as expected by clients like Front Row.
   */
  evhttp_add_header(headers, "Content-Type", "application/x-dmap-tagged");

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for DAAP reply\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      free(uri);
      free(full_uri);
      return;
    }

  // Try the cache
  ret = cache_daap_get(full_uri, evbuf);
  if (ret == 0)
    {
      httpd_send_reply(req, HTTP_OK, "OK", evbuf); // TODO not all want this reply

      evbuffer_free(evbuf);
      free(uri);
      free(full_uri);
      return;
    }

  // No cache, so prepare handler arguments and send to the handler
  evhttp_parse_query(full_uri, &query);

  clock_gettime(CLOCK_MONOTONIC, &start);

  daap_handlers[handler].handler(req, evbuf, uri_parts, &query, ua);

  clock_gettime(CLOCK_MONOTONIC, &end);

  msec = (end.tv_sec * 1000 + end.tv_nsec / 1000000) - (start.tv_sec * 1000 + start.tv_nsec / 1000000);

  DPRINTF(E_DBG, L_DAAP, "DAAP request handled in %d milliseconds\n", msec);

  if (msec > cache_daap_threshold())
    cache_daap_add(full_uri, ua, msec);

  evhttp_clear_headers(&query);
  evbuffer_free(evbuf);
  free(uri);
  free(full_uri);
}

int
daap_is_request(struct evhttp_request *req, char *uri)
{
  uri = daap_fix_request_uri(req, uri);
  if (!uri)
    return 0;

  if (strncmp(uri, "/databases/", strlen("/databases/")) == 0)
    return 1;
  if (strcmp(uri, "/databases") == 0)
    return 1;
  if (strcmp(uri, "/server-info") == 0)
    return 1;
  if (strcmp(uri, "/content-codes") == 0)
    return 1;
  if (strcmp(uri, "/login") == 0)
    return 1;
  if (strcmp(uri, "/update") == 0)
    return 1;
  if (strcmp(uri, "/activity") == 0)
    return 1;
  if (strcmp(uri, "/logout") == 0)
    return 1;

#ifdef DMAP_TEST
  if (strcmp(uri, "/dmap-test") == 0)
    return 1;
#endif

  return 0;
}

struct evbuffer *
daap_reply_build(char *full_uri, const char *ua)
{
  char *uri;
  char *ptr;
  char *uri_parts[7];
  struct evbuffer *evbuf;
  struct evkeyvalq query;
  int handler;
  int ret;
  int i;

  DPRINTF(E_DBG, L_DAAP, "Building reply for DAAP request: %s\n", full_uri);

  uri = extract_uri(full_uri);
  if (!uri)
    {
      DPRINTF(E_LOG, L_DAAP, "Error extracting DAAP request: %s\n", full_uri);

      return NULL;
    }

  handler = -1;
  for (i = 0; daap_handlers[i].handler; i++)
    {
      ret = regexec(&daap_handlers[i].preg, uri, 0, NULL, 0);
      if (ret == 0)
        {
          handler = i;
          break;
        }
    }

  if (handler < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Unrecognized DAAP request: %s\n", full_uri);

      free(uri);
      return NULL;
    }

  memset(uri_parts, 0, sizeof(uri_parts));

  uri_parts[0] = strtok_r(uri, "/", &ptr);
  for (i = 1; (i < sizeof(uri_parts) / sizeof(uri_parts[0])) && uri_parts[i - 1]; i++)
    {
      uri_parts[i] = strtok_r(NULL, "/", &ptr);
    }

  if (!uri_parts[0] || uri_parts[i - 1] || (i < 2))
    {
      DPRINTF(E_LOG, L_DAAP, "DAAP URI has too many/few components (%d)\n", (uri_parts[0]) ? i : 0);

      free(uri);
      return NULL;
    }

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for building DAAP reply\n");

      free(uri);
      return NULL;
    }

  evhttp_parse_query(full_uri, &query);

  ret = daap_handlers[handler].handler(NULL, evbuf, uri_parts, &query, ua);
  if (ret < 0)
    {
      evbuffer_free(evbuf);
      evbuf = NULL;
    }

  evhttp_clear_headers(&query);
  free(uri);

  return evbuf;
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
