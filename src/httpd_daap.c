/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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

#include <event.h>
#include "evhttp/evhttp.h"
#include <avl.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "httpd.h"
#include "transcode.h"
#include "httpd_daap.h"
#include "daap_query.h"
#include "dmap_helpers.h"


struct uri_map {
  regex_t preg;
  char *regexp;
  void (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query);
};

struct daap_session {
  int id;
};

struct daap_update_request {
  struct evhttp_request *req;

  struct daap_update_request *next;
};

struct dmap_field_map {
  uint32_t hash;
  enum dmap_type type;
  char *tag;
  char *desc;
  ssize_t mfi_offset;
  ssize_t pli_offset;
  ssize_t gri_offset;
};

static struct dmap_field_map dmap_fields[] =
  {
    { 0, DMAP_TYPE_INT,     "miid", "dmap.itemid",
      dbmfi_offsetof(id),                 dbpli_offsetof(id),     -1 },
    { 0, DMAP_TYPE_STRING,  "minm", "dmap.itemname",
      dbmfi_offsetof(title),              dbpli_offsetof(title), dbgri_offsetof(itemname) },
    { 0, DMAP_TYPE_BYTE,    "mikd", "dmap.itemkind",
      dbmfi_offsetof(item_kind),          -1,                    -1 },
    { 0, DMAP_TYPE_LONG,    "mper", "dmap.persistentid",
      dbmfi_offsetof(id),                 -1,                    dbgri_offsetof(persistentid) },
    { 0, DMAP_TYPE_LIST,    "mcon", "dmap.container",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mcti", "dmap.containeritemid",
      dbmfi_offsetof(id),                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mpco", "dmap.parentcontainerid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mstt", "dmap.status",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "msts", "dmap.statusstring",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mimc", "dmap.itemcount",
      dbmfi_offsetof(total_tracks),       dbpli_offsetof(items), dbgri_offsetof(itemcount) },
    { 0, DMAP_TYPE_INT,     "mctc", "dmap.containercount",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mrco", "dmap.returnedcount",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mtco", "dmap.specifiedtotalcount",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mlcl", "dmap.listing",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mlit", "dmap.listingitem",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mbcl", "dmap.bag",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mdcl", "dmap.dictionary",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "msrv", "dmap.serverinforesponse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msau", "dmap.authenticationmethod",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "mslr", "dmap.loginrequired",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_VERSION, "mpro", "dmap.protocolversion",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msal", "dmap.supportsautologout",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msup", "dmap.supportsupdate",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "mspi", "dmap.supportspersistentids",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msex", "dmap.supportsextensions",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msbr", "dmap.supportsbrowse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msqy", "dmap.supportsquery",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msix", "dmap.supportsindex",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "msrs", "dmap.supportsresolve",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mstm", "dmap.timeoutinterval",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "msdc", "dmap.databasescount",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mlog", "dmap.loginresponse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mlid", "dmap.sessionid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mupd", "dmap.updateresponse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "musr", "dmap.serverrevision",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "muty", "dmap.updatetype",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mudl", "dmap.deletedidlisting",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "mccr", "dmap.contentcodesresponse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "mcnm", "dmap.contentcodesnumber",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "mcna", "dmap.contentcodesname",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "mcty", "dmap.contentcodestype",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_VERSION, "apro", "daap.protocolversion",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "avdb", "daap.serverdatabases",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "abro", "daap.databasebrowse",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "abal", "daap.browsealbumlisting",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "abar", "daap.browseartistlisting",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "abcp", "daap.browsecomposerlisting",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "abgn", "daap.browsegenrelisting",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "adbs", "daap.databasesongs",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asal", "daap.songalbum",
      dbmfi_offsetof(album),              -1,                    -1 },
    { 0, DMAP_TYPE_LONG,    "asai", "daap.songalbumid",  /* special case; will be transformed to LONG (hash) */
      dbmfi_offsetof(album),              -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asaa", "daap.songalbumartist",
      dbmfi_offsetof(album_artist),       -1,                    dbgri_offsetof(songalbumartist) },
    { 0, DMAP_TYPE_STRING,  "asar", "daap.songartist",
      dbmfi_offsetof(artist),             -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "asbt", "daap.songbeatsperminute",
      dbmfi_offsetof(bpm),                -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "asbr", "daap.songbitrate",
      dbmfi_offsetof(bitrate),            -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "ascm", "daap.songcomment",
      dbmfi_offsetof(comment),            -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "asco", "daap.songcompilation",
      dbmfi_offsetof(compilation),        -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "ascp", "daap.songcomposer",
      dbmfi_offsetof(composer),           -1,                    -1 },
    { 0, DMAP_TYPE_DATE,    "asda", "daap.songdateadded",
      dbmfi_offsetof(time_added),         -1,                    -1 },
    { 0, DMAP_TYPE_DATE,    "asdm", "daap.songdatemodified",
      dbmfi_offsetof(time_modified),      -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "asdc", "daap.songdisccount",
      dbmfi_offsetof(total_discs),        -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "asdn", "daap.songdiscnumber",
      dbmfi_offsetof(disc),               -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "asdb", "daap.songdisabled",
      dbmfi_offsetof(disabled),           -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "aseq", "daap.songeqpreset",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asfm", "daap.songformat",
      dbmfi_offsetof(type),               -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asgn", "daap.songgenre",
      dbmfi_offsetof(genre),              -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asdt", "daap.songdescription",
      dbmfi_offsetof(description),        -1,                    -1 },
    { 0, DMAP_TYPE_UBYTE,   "asrv", "daap.songrelativevolume",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "assr", "daap.songsamplerate",
      dbmfi_offsetof(samplerate),         -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "assz", "daap.songsize",
      dbmfi_offsetof(file_size),          -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "asst", "daap.songstarttime",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "assp", "daap.songstoptime",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "astm", "daap.songtime",
      dbmfi_offsetof(song_length),        -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "astc", "daap.songtrackcount",
      dbmfi_offsetof(total_tracks),       -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "astn", "daap.songtracknumber",
      dbmfi_offsetof(track),              -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "asur", "daap.songuserrating",
      dbmfi_offsetof(rating),             -1,                    -1 },
    { 0, DMAP_TYPE_SHORT,   "asyr", "daap.songyear",
      dbmfi_offsetof(year),               -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "asdk", "daap.songdatakind",
      dbmfi_offsetof(data_kind),          -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asul", "daap.songdataurl",
      dbmfi_offsetof(url),                -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "aply", "daap.databaseplaylists",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "abpl", "daap.baseplaylist",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "apso", "daap.playlistsongs",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "arsv", "daap.resolve",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_LIST,    "arif", "daap.resolveinfo",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeNV", "com.apple.itunes.norm-volume",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aeSP", "com.apple.itunes.smart-playlist",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aePS", "com.apple.itunes.special-playlist",
      -1,                                 -1,                    -1 },

    /* iTunes 4.5+ */
#if 0 /* Duplicate: type changed to INT in iTunes 6.0.4 */
    { 0, DMAP_TYPE_BYTE,    "msas", "dmap.authenticationschemes",
      -1,                                 -1,                    -1 },
#endif
    { 0, DMAP_TYPE_INT,     "ascd", "daap.songcodectype",
      dbmfi_offsetof(codectype),          -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "ascs", "daap.songcodecsubtype",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "agrp", "daap.songgrouping",
      dbmfi_offsetof(grouping),           -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeSV", "com.apple.itunes.music-sharing-version",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aePI", "com.apple.itunes.itms-playlistid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeCI", "com.apple.iTunes.itms-composerid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeGI", "com.apple.iTunes.itms-genreid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeAI", "com.apple.iTunes.itms-artistid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeSI", "com.apple.iTunes.itms-songid",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeSF", "com.apple.iTunes.itms-storefrontid",
      -1,                                 -1,                    -1 },

    /* iTunes 5.0+ */
    { 0, DMAP_TYPE_BYTE,    "ascr", "daap.songcontentrating",
      dbmfi_offsetof(contentrating),      -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "f" "\x8d" "ch", "dmap.haschildcontainers",
      -1,                                 -1,                    -1 },

    /* iTunes 6.0.2+ */
    { 0, DMAP_TYPE_BYTE,    "aeHV", "com.apple.itunes.has-video",
      dbmfi_offsetof(has_video),          -1,                    -1 },

    /* iTunes 6.0.4+ */
    { 0, DMAP_TYPE_INT,     "msas", "dmap.authenticationschemes",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asct", "daap.songcategory",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "ascn", "daap.songcontentdescription",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "aslc", "daap.songlongcontentdescription",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "asky", "daap.songkeywords",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "apsm", "daap.playlistshufflemode",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aprm", "daap.playlistrepeatmode",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aePC", "com.apple.itunes.is-podcast",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aePP", "com.apple.itunes.is-podcast-playlist",
      -1,                                 -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aeMK", "com.apple.itunes.mediakind",
      dbmfi_offsetof(media_kind),         -1,                    -1 },
    { 0, DMAP_TYPE_BYTE,    "aeMk", "com.apple.itunes.mediakind.lower",
      dbmfi_offsetof(media_kind),         -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "aeSN", "com.apple.itunes.series-name",
      dbmfi_offsetof(tv_series_name),     -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "aeNN", "com.apple.itunes.network-name",
      dbmfi_offsetof(tv_network_name),    -1,                    -1 },
    { 0, DMAP_TYPE_STRING,  "aeEN", "com.apple.itunes.episode-num-str",
      dbmfi_offsetof(tv_episode_num_str), -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeES", "com.apple.itunes.episode-sort",
      dbmfi_offsetof(tv_episode_sort),    -1,                    -1 },
    { 0, DMAP_TYPE_INT,     "aeSU", "com.apple.itunes.season-num",
      dbmfi_offsetof(tv_season_num),      -1,                    -1 },
    { 0, 0,                 "",      NULL,
      -1,                                 -1,                    -1 }
  };

/* Default meta tags if not provided in the query */
static char *default_meta_plsongs = "dmap.itemkind,dmap.itemid,dmap.itemname,dmap.containeritemid,dmap.parentcontainerid";
static char *default_meta_pl = "dmap.itemid,dmap.itemname,dmap.persistentid,com.apple.itunes.smart-playlist";
static char *default_meta_group = "dmap.itemname,dmap.persistentid,daap.songalbumartist";

static avl_tree_t *dmap_fields_hash;

/* DAAP session tracking */
static avl_tree_t *daap_sessions;
static int next_session_id;

/* Update requests */
static struct daap_update_request *update_requests;


/* Session handling */
static int
daap_session_compare(const void *aa, const void *bb)
{
  struct daap_session *a = (struct daap_session *)aa;
  struct daap_session *b = (struct daap_session *)bb;

  if (a->id < b->id)
    return -1;

  if (a->id > b->id)
    return 1;

  return 0;
}

static struct daap_session *
daap_session_register(void)
{
  struct daap_session *s;
  avl_node_t *node;

  s = (struct daap_session *)malloc(sizeof(struct daap_session));
  if (!s)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for DAAP session\n");
      return NULL;
    }

  memset(s, 0, sizeof(struct daap_session));

  s->id = next_session_id;

  next_session_id++;

  node = avl_insert(daap_sessions, s);
  if (!node)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not register DAAP session: %s\n", strerror(errno));

      free(s);
      return NULL;
    }

  return s;
}

static void
daap_session_kill(struct daap_session *s)
{
  avl_delete(daap_sessions, s);
}

struct daap_session *
daap_session_find(struct evhttp_request *req, struct evkeyvalq *query, struct evbuffer *evbuf)
{
  struct daap_session needle;
  avl_node_t *node;
  const char *param;
  int ret;

  param = evhttp_find_header(query, "session-id");
  if (!param)
    {
      DPRINTF(E_WARN, L_DAAP, "No session-id specified in request\n");
      goto invalid;
    }

  ret = safe_atoi32(param, &needle.id);
  if (ret < 0)
    goto invalid;

  node = avl_search(daap_sessions, &needle);
  if (!node)
    {
      DPRINTF(E_WARN, L_DAAP, "DAAP session id %d not found\n", needle.id);
      goto invalid;
    }

  return (struct daap_session *)node->item;

 invalid:
  evhttp_send_error(req, 403, "Forbidden");
  return NULL;
}


/* Update requests helpers */
static void
update_fail_cb(struct evhttp_request *req, void *arg)
{
  struct daap_update_request *ur;
  struct daap_update_request *p;

  ur = (struct daap_update_request *)arg;

  DPRINTF(E_DBG, L_DAAP, "Update request: client closed connection\n");

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

  free(ur);
}


/* DMAP fields helpers */
static int
dmap_field_map_compare(const void *aa, const void *bb)
{
  struct dmap_field_map *a = (struct dmap_field_map *)aa;
  struct dmap_field_map *b = (struct dmap_field_map *)bb;

  if (a->hash < b->hash)
    return -1;

  if (a->hash > b->hash)
    return 1;

  return 0;
}

static struct dmap_field_map *
dmap_find_field(uint32_t hash)
{
  struct dmap_field_map dfm;
  avl_node_t *node;

  dfm.hash = hash;

  node = avl_search(dmap_fields_hash, &dfm);
  if (!node)
    return NULL;

  return (struct dmap_field_map *)node->item;
}

static void
dmap_add_field(struct evbuffer *evbuf, struct dmap_field_map *dfm, char *strval, int32_t intval)
{
  int64_t val64;
  int32_t val;
  int ret;

  if (strval && (dfm->type != DMAP_TYPE_STRING))
    {
      if (dfm->type == DMAP_TYPE_LONG)
	{
	  ret = safe_atoi64(strval, &val64);
	  if (ret < 0)
	    val64 = 0;
	}
      else
	{
	  ret = safe_atoi32(strval, &val);
	  if (ret < 0)
	    val = 0;
	}
    }
  else
    {
      val = intval;
      val64 = intval;
    }

  switch (dfm->type)
    {
    case DMAP_TYPE_BYTE:
    case DMAP_TYPE_UBYTE:
      if (val)
	dmap_add_char(evbuf, dfm->tag, val);
      break;

    case DMAP_TYPE_SHORT:
      if (val)
	dmap_add_short(evbuf, dfm->tag, val);
      break;

    case DMAP_TYPE_INT:
    case DMAP_TYPE_DATE:
      if (val)
	dmap_add_int(evbuf, dfm->tag, val);
      break;

    case DMAP_TYPE_LONG:
      if (val64)
	dmap_add_long(evbuf, dfm->tag, val64);
      break;

    case DMAP_TYPE_STRING:
      if (strval)
	dmap_add_string(evbuf, dfm->tag, strval);
      break;

    default:
      DPRINTF(E_LOG, L_DAAP, "Unsupported DMAP type %d for DMAP field %s\n", dfm->type, dfm->desc);
      break;
    }
}


static void
get_query_params(struct evkeyvalq *query, struct query_params *qp)
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

  param = evhttp_find_header(query, "query");
  if (!param)
    param = evhttp_find_header(query, "filter");

  if (param)
    {
      DPRINTF(E_DBG, L_DAAP, "DAAP browse query filter: %s\n", param);

      qp->filter = daap_query_parse_sql(param);
      if (!qp->filter)
	DPRINTF(E_LOG, L_DAAP, "Ignoring improper DAAP query\n");
    }
}

static void
parse_meta(struct evhttp_request *req, char *tag, const char *param, uint32_t **out_meta, int *out_nmeta)
{
  char *ptr;
  char *meta;
  char *metastr;
  uint32_t *hashes;
  int nmeta;
  int i;

  *out_nmeta = -1;

  metastr = strdup(param);
  if (!metastr)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not duplicate meta parameter; out of memory\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
    }

  nmeta = 1;
  ptr = metastr;
  while ((ptr = strchr(ptr + 1, ',')))
    nmeta++;

  DPRINTF(E_DBG, L_DAAP, "Asking for %d meta tags\n", nmeta);

  hashes = (uint32_t *)malloc((nmeta + 1) * sizeof(uint32_t));
  if (!hashes)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate meta array; out of memory\n");

      dmap_send_error(req, tag, "Out of memory");

      free(metastr);
      return;
    }
  memset(hashes, 0, (nmeta + 1) * sizeof(uint32_t));

  meta = strtok_r(metastr, ",", &ptr);
  for (i = 0; i < nmeta; i++)
    {
      hashes[i] = djb_hash(meta, strlen(meta));

      meta = strtok_r(NULL, ",", &ptr);
      if (!meta)
	break;
    }

  DPRINTF(E_DBG, L_DAAP, "Found %d meta tags\n", nmeta);

  *out_nmeta = nmeta;
  *out_meta = hashes;

  free(metastr);
}


static void
daap_reply_server_info(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  cfg_t *lib;
  char *name;
  char *passwd;
  const char *clientver;
  int supports_update;
  int mpro;
  int apro;
  int len;
  int ret;

  /* We don't support updates atm */
  supports_update = 0;

  lib = cfg_getnsec(cfg, "library", 0);
  passwd = cfg_getstr(lib, "password");
  name = cfg_getstr(lib, "name");

  len = 148 + strlen(name);

  if (!supports_update)
    len -= 9;

  ret = evbuffer_expand(evbuf, len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP server-info reply\n");

      dmap_send_error(req, "msrv", "Out of memory");
      return;
    }

  mpro = 2 << 16;
  apro = 3 << 16;

  clientver = evhttp_find_header(req->input_headers, "Client-DAAP-Version");
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

  dmap_add_container(evbuf, "msrv", len - 8);
  dmap_add_int(evbuf, "mstt", 200);  /* 12 */
  dmap_add_int(evbuf, "mpro", mpro); /* 12 */
  dmap_add_int(evbuf, "apro", apro); /* 12 */
  dmap_add_int(evbuf, "mstm", 1800); /* 12 */
  dmap_add_string(evbuf, "minm", name); /* 8 + strlen(name) */

  dmap_add_char(evbuf, "mslr", 1);   /* 9 */
  dmap_add_char(evbuf, "msau", (passwd) ? 2 : 0); /* 9 */
  dmap_add_char(evbuf, "msex", 1);   /* 9 */
  dmap_add_char(evbuf, "msix", 1);   /* 9 */
  dmap_add_char(evbuf, "msbr", 1);   /* 9 */
  dmap_add_char(evbuf, "msqy", 1);   /* 9 */

  dmap_add_char(evbuf, "mspi", 1);   /* 9 */
  dmap_add_int(evbuf, "msdc", 1);    /* 12 */

  if (supports_update)
    dmap_add_char(evbuf, "msup", 0); /* 9 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_content_codes(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  int i;
  int len;
  int ret;

  len = 12;
  for (i = 0; dmap_fields[i].type != 0; i++)
    len += 8 + 12 + 10 + 8 + strlen(dmap_fields[i].desc);

  ret = evbuffer_expand(evbuf, len + 8);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP content-codes reply\n");

      dmap_send_error(req, "mccr", "Out of memory");
      return;
    } 

  dmap_add_container(evbuf, "mccr", len);
  dmap_add_int(evbuf, "mstt", 200);

  for (i = 0; dmap_fields[i].type != 0; i++)
    {
      len = 12 + 10 + 8 + strlen(dmap_fields[i].desc);

      dmap_add_container(evbuf, "mdcl", len);
      dmap_add_string(evbuf, "mcnm", dmap_fields[i].tag);  /* 12 */
      dmap_add_string(evbuf, "mcna", dmap_fields[i].desc); /* 8 + strlen(desc) */
      dmap_add_short(evbuf, "mcty", dmap_fields[i].type);  /* 10 */
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_login(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct pairing_info pi;
  struct daap_session *s;
  const char *ua;
  const char *guid;
  int ret;

  ret = evbuffer_expand(evbuf, 32);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP login reply\n");

      dmap_send_error(req, "mlog", "Out of memory");
      return;
    }

  ua = evhttp_find_header(req->input_headers, "User-Agent");
  if (!ua)
    {
      DPRINTF(E_LOG, L_DAAP, "No User-Agent header, rejecting login request\n");

      evhttp_send_error(req, 403, "Forbidden");
      return;
    }

  if (strncmp(ua, "Remote", strlen("Remote")) == 0)
    {
      guid = evhttp_find_header(query, "pairing-guid");
      if (!guid)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt with U-A: Remote and no pairing-guid\n");

	  evhttp_send_error(req, 403, "Forbidden");
	  return;
	}

      memset(&pi, 0, sizeof(struct pairing_info));
      pi.guid = strdup(guid + 2); /* Skip leading 0X */

      ret = db_pairing_fetch_byguid(&pi);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Login attempt with invalid pairing-guid\n");

	  free_pi(&pi, 1);
	  evhttp_send_error(req, 403, "Forbidden");
	  return;
	}

      DPRINTF(E_INFO, L_DAAP, "Remote '%s' logging in with GUID %s\n", pi.name, pi.guid);
      free_pi(&pi, 1);
    }

  s = daap_session_register();
  if (!s)
    {
      dmap_send_error(req, "mlog", "Could not start session");
      return;
    }

  dmap_add_container(evbuf, "mlog", 24);
  dmap_add_int(evbuf, "mstt", 200);        /* 12 */
  dmap_add_int(evbuf, "mlid", s->id); /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_logout(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  daap_session_kill(s);

  evhttp_send_reply(req, 204, "Logout Successful", evbuf);
}

static void
daap_reply_update(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  struct daap_update_request *ur;
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
      DPRINTF(E_LOG, L_DAAP, "Missing revision-number in update request\n");

      dmap_send_error(req, "mupd", "Invalid request");
      return;
    }

  ret = safe_atoi32(param, &reqd_rev);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Parameter revision-number not an integer\n");

      dmap_send_error(req, "mupd", "Invalid request");
      return;
    }

  if (reqd_rev == 1) /* Or revision is not valid */
    {
      ret = evbuffer_expand(evbuf, 32);
      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP update reply\n");

	  dmap_send_error(req, "mupd", "Out of memory");
	  return;
	}

      /* Send back current revision */
      dmap_add_container(evbuf, "mupd", 24);
      dmap_add_int(evbuf, "mstt", 200);         /* 12 */
      dmap_add_int(evbuf, "musr", current_rev); /* 12 */

      evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

      return;
    }

  /* Else, just let the request hang until we have changes to push back */
  ur = (struct daap_update_request *)malloc(sizeof(struct daap_update_request));
  if (!ur)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for update request\n");

      dmap_send_error(req, "mupd", "Out of memory");
      return;
    }

  /* NOTE: we may need to keep reqd_rev in there too */
  ur->req = req;

  ur->next = update_requests;
  update_requests = ur;

  /* Set fail_cb; this is an extension to the stock evhttp and will
   * get called if the connection fails before we have an update to
   * push out to the client.
   */
  req->fail_cb = update_fail_cb;
  req->fail_cb_arg = ur;
}

static void
daap_reply_activity(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  /* That's so nice, thanks for letting us know */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
daap_reply_dblist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  cfg_t *lib;
  char *name;
  int namelen;
  int count;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  lib = cfg_getnsec(cfg, "library", 0);
  name = cfg_getstr(lib, "name");
  namelen = strlen(name);

  ret = evbuffer_expand(evbuf, 129 + namelen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP dblist reply\n");

      dmap_send_error(req, "avdb", "Out of memory");
      return;
    }

  dmap_add_container(evbuf, "avdb", 121 + namelen);
  dmap_add_int(evbuf, "mstt", 200);     /* 12 */
  dmap_add_char(evbuf, "muty", 0);      /* 9 */
  dmap_add_int(evbuf, "mtco", 1);       /* 12 */
  dmap_add_int(evbuf, "mrco", 1);       /* 12 */
  dmap_add_container(evbuf, "mlcl", 68 + namelen);
  dmap_add_container(evbuf, "mlit", 60 + namelen);
  dmap_add_int(evbuf, "miid", 1);       /* 12 */
  dmap_add_long(evbuf, "mper", 1);      /* 16 */
  dmap_add_string(evbuf, "minm", name); /* 8 + namelen */

  count = db_files_get_count();
  dmap_add_int(evbuf, "mimc", count); /* 12 */

  count = db_pl_get_count();
  dmap_add_int(evbuf, "mctc", count); /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_songlist_generic(struct evhttp_request *req, struct evbuffer *evbuf, int playlist, struct evkeyvalq *query)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct evbuffer *song;
  struct evbuffer *songlist;
  struct dmap_field_map *dfm;
  const char *param;
  char *tag;
  char **strval;
  char *ptr;
  uint32_t *meta;
  int64_t songalbumid;
  int nmeta;
  int nsongs;
  int transcode;
  int want_mikd;
  int want_asdk;
  int oom;
  int32_t val;
  int i;
  int ret;

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
      return;
    }

  songlist = evbuffer_new();
  if (!songlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP song list\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
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
      parse_meta(req, tag, param, &meta, &nmeta);
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
  get_query_params(query, &qp);

  if (playlist != -1)
    {
      qp.type = Q_PLITEMS;
      qp.pl_id = playlist;
    }
  else
    qp.type = Q_ITEMS;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, tag, "Could not start query");
      goto out_query_free;
    }

  want_mikd = 0;
  want_asdk = 0;
  oom = 0;
  nsongs = 0;
  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      nsongs++;

      transcode = transcode_needed(req->input_headers, dbmfi.codectype);

      i = -1;
      while (1)
	{
	  i++;

	  /* Specific meta tags requested (or default list) */
	  if (nmeta > 0)
	    {
	      if (i == nmeta)
		break;

	      dfm = dmap_find_field(meta[i]);

	      if (!dfm)
		{
		  DPRINTF(E_LOG, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
		  continue;
		}
	    }
	  /* No specific meta tags requested, send out everything */
	  else
	    {
	      /* End of list */
	      if (dmap_fields[i].type == 0)
		break;

	      dfm = &dmap_fields[i];
	    }

	  /* Not in struct media_file_info */
	  if (dfm->mfi_offset < 0)
	    continue;

	  /* Will be prepended to the list */
	  if (dfm->mfi_offset == dbmfi_offsetof(item_kind))
	    {
	      want_mikd = 1;
	      continue;
	    }
	  else if (dfm->mfi_offset == dbmfi_offsetof(data_kind))
	    {
	      want_asdk = 1;
	      continue;
	    }

	  DPRINTF(E_DBG, L_DAAP, "Investigating %s\n", dfm->desc);

	  strval = (char **) ((char *)&dbmfi + dfm->mfi_offset);

	  if (!(*strval) || (**strval == '\0'))
            continue;

	  /* Here's one exception ... codectype (ascd) is actually an integer */
	  if (dfm->mfi_offset == dbmfi_offsetof(codectype))
	    {
	      dmap_add_literal(song, dfm->tag, *strval, 4);
	      continue;
	    }

	  /* Special handling for songalbumid (asai)
	   * Return an int64_t hash of the album_artist & album
	   */
	  if (strcmp(dfm->tag, "asai") == 0)
	    {
	      songalbumid = daap_songalbumid(dbmfi.album_artist, dbmfi.album);

	      dmap_add_long(song, dfm->tag, songalbumid);

	      DPRINTF(E_DBG, L_DAAP, "Generated meta tag %s (%" PRIi64 ") based on (%s,%s)\n", dfm->desc, songalbumid, dbmfi.album_artist, dbmfi.album);
	      continue;
	    }

	  val = 0;

	  if (transcode)
            {
              switch (dfm->mfi_offset)
                {
		  case dbmfi_offsetof(type):
		    ptr = "wav";
		    strval = &ptr;
		    break;

		  case dbmfi_offsetof(bitrate):
		    val = 0;
		    ret = safe_atoi32(dbmfi.samplerate, &val);
		    if ((ret < 0) || (val == 0))
		      val = 1411;
		    else
		      val = (val * 8) / 250;

		    ptr = NULL;
		    strval = &ptr;
		    break;

		  case dbmfi_offsetof(description):
		    ptr = "wav audio file";
		    strval = &ptr;
		    break;

		  default:
		    break;
                }
            }

	  dmap_add_field(song, dfm, *strval, val);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->desc, *strval);
	}

      DPRINTF(E_DBG, L_DAAP, "Done with song\n");

      val = 0;
      if (want_mikd)
	val += 9;
      if (want_asdk)
	val += 9;

      dmap_add_container(songlist, "mlit", EVBUFFER_LENGTH(song) + val);

      /* Prepend mikd & asdk if needed */
      if (want_mikd)
	{
	  /* dmap.itemkind must come first */
	  ret = safe_atoi32(dbmfi.item_kind, &val);
	  if (ret < 0)
	    val = 2; /* music by default */
	  dmap_add_char(songlist, "mikd", val);
	}
      if (want_asdk)
	{
	  ret = safe_atoi32(dbmfi.data_kind, &val);
	  if (ret < 0)
	    val = 0;
	  dmap_add_char(songlist, "asdk", val);
	}

      ret = evbuffer_add_buffer(songlist, song);
      if (ret < 0)
	{
	  oom = 1;
	  break;
	}
    }

  DPRINTF(E_DBG, L_DAAP, "Done with song list, %d songs\n", nsongs);

  if (nmeta > 0)
    free(meta);

  evbuffer_free(song);

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");

      dmap_send_error(req, tag, "Error fetching query results");
      db_query_end(&qp);
      goto out_list_free;
    }

  if (oom)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add song to song list for DAAP song list reply\n");

      dmap_send_error(req, tag, "Out of memory");
      db_query_end(&qp);
      goto out_list_free;
    }

  /* Add header to evbuf, add songlist to evbuf */
  dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(songlist) + 53);
  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_char(evbuf, "muty", 0);     /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf, "mrco", nsongs); /* 12 */
  dmap_add_container(evbuf, "mlcl", EVBUFFER_LENGTH(songlist));

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, songlist);
  evbuffer_free(songlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add song list to DAAP song list reply\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  return;

 out_query_free:
  if (nmeta > 0)
    free(meta);

  if (qp.filter)
    free(qp.filter);

 out_song_free:
  evbuffer_free(song);

 out_list_free:
  evbuffer_free(songlist);
}

static void
daap_reply_dbsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  daap_reply_songlist_generic(req, evbuf, -1, query);
}

static void
daap_reply_plsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  int playlist;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  ret = safe_atoi32(uri[3], &playlist);
  if (ret < 0)
    {
      dmap_send_error(req, "apso", "Invalid playlist ID");

      return;
    }

  daap_reply_songlist_generic(req, evbuf, playlist, query);
}

static void
daap_reply_playlists(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct query_params qp;
  struct db_playlist_info dbpli;
  struct daap_session *s;
  struct evbuffer *playlistlist;
  struct evbuffer *playlist;
  struct dmap_field_map *dfm;
  const char *param;
  char **strval;
  uint32_t *meta;
  int nmeta;
  int npls;
  int oom;
  int32_t val;
  int i;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP playlists reply\n");

      dmap_send_error(req, "aply", "Out of memory");
      return;
    }

  playlistlist = evbuffer_new();
  if (!playlistlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP playlist list\n");

      dmap_send_error(req, "aply", "Out of memory");
      return;
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

  parse_meta(req, "aply", param, &meta, &nmeta);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      goto out_pl_free;
    }

  memset(&qp, 0, sizeof(struct query_params));
  get_query_params(query, &qp);
  qp.type = Q_PL;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, "aply", "Could not start query");
      goto out_query_free;
    }

  npls = 0;
  oom = 0;
  while (((ret = db_query_fetch_pl(&qp, &dbpli)) == 0) && (dbpli.id))
    {
      npls++;

      for (i = 0; i < nmeta; i++)
	{
	  /* dmap.itemcount - always added */
	  if (meta[i] == 0xd4b8b70d)
	    continue;

	  /* com.apple.itunes.smart-playlist - type = 1 AND id != 1 */
	  if (meta[i] == 0x670fc55e)
	    {
	      val = 0;
	      ret = safe_atoi32(dbpli.type, &val);
	      if ((ret == 0) && (val == PL_SMART))
		{
		  val = 1;
		  ret = safe_atoi32(dbpli.id, &val);
		  if ((ret == 0) && (val != 1))
		    {
		      int32_t aePS = 0;
		      dmap_add_char(playlist, "aeSP", 1);

		      ret = safe_atoi32(dbpli.special_id, &aePS);
		      if ((ret == 0) && (aePS > 0))
			dmap_add_char(playlist, "aePS", aePS);
		    }
		}

	      continue;
	    }

	  dfm = dmap_find_field(meta[i]);
	  if (!dfm)
	    {
	      DPRINTF(E_LOG, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
	      continue;
	    }

	  /* Not in struct playlist_info */
	  if (dfm->pli_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbpli + dfm->pli_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(playlist, dfm, *strval, 0);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->desc, *strval);
	}

      /* Item count (mimc) */
      val = 0;
      ret = safe_atoi32(dbpli.items, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(playlist, "mimc", val);

      /* Base playlist (abpl), id = 1 */
      val = 0;
      ret = safe_atoi32(dbpli.id, &val);
      if ((ret == 0) && (val == 1))
	dmap_add_char(playlist, "abpl", 1);

      DPRINTF(E_DBG, L_DAAP, "Done with playlist\n");

      dmap_add_container(playlistlist, "mlit", EVBUFFER_LENGTH(playlist));
      ret = evbuffer_add_buffer(playlistlist, playlist);
      if (ret < 0)
	{
	  oom = 1;
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
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");

      dmap_send_error(req, "aply", "Error fetching query results");
      db_query_end(&qp);
      goto out_list_free;
    }

  if (oom)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add playlist to playlist list for DAAP playlists reply\n");

      dmap_send_error(req, "aply", "Out of memory");
      db_query_end(&qp);
      goto out_list_free;
    }

  /* Add header to evbuf, add playlistlist to evbuf */
  dmap_add_container(evbuf, "aply", EVBUFFER_LENGTH(playlistlist) + 53);
  dmap_add_int(evbuf, "mstt", 200); /* 12 */
  dmap_add_char(evbuf, "muty", 0);  /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf,"mrco", npls); /* 12 */
  dmap_add_container(evbuf, "mlcl", EVBUFFER_LENGTH(playlistlist));

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, playlistlist);
  evbuffer_free(playlistlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add playlist list to DAAP playlists reply\n");

      dmap_send_error(req, "aply", "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  return;

 out_query_free:
  free(meta);
  if (qp.filter)
    free(qp.filter);

 out_pl_free:
  evbuffer_free(playlist);

 out_list_free:
  evbuffer_free(playlistlist);
}

static void
daap_reply_groups(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct query_params qp;
  struct db_group_info dbgri;
  struct daap_session *s;
  struct evbuffer *group;
  struct evbuffer *grouplist;
  struct dmap_field_map *dfm;
  const char *param;
  char **strval;
  uint32_t *meta;
  int nmeta;
  int ngrp;
  int oom;
  int32_t val;
  int64_t val64;
  int i;
  int ret;
  char *tag;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* For now we only support album groups */
  tag = "agal";

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP groups reply\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
    }

  grouplist = evbuffer_new();
  if (!grouplist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP group list\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
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

  parse_meta(req, tag, param, &meta, &nmeta);
  if (nmeta < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Failed to parse meta parameter in DAAP query\n");

      goto out_group_free;
    }

  memset(&qp, 0, sizeof(struct query_params));
  get_query_params(query, &qp);
  qp.type = Q_GROUPS;

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, tag, "Could not start query");
      goto out_query_free;
    }

  ngrp = 0;
  oom = 0;
  while ((ret = db_query_fetch_group(&qp, &dbgri)) == 0)
    {
      ngrp++;

      for (i = 0; i < nmeta; i++)
	{
	  /* dmap.itemcount - always added */
	  if (meta[i] == 0xd4b8b70d)
	    continue;

	  dfm = dmap_find_field(meta[i]);
	  if (!dfm)
	    {
	      DPRINTF(E_LOG, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
	      continue;
	    }

	  /* Not in struct group_info */
	  if (dfm->gri_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbgri + dfm->gri_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  /* Special handling for persistentid (mper)
	   * Correctly handle a DMAP long value (64bit)
	   */
	  if (strcmp(dfm->tag, "mper") == 0)
	    {
	      if (*strval)
		{
		  ret = safe_atoi64(*strval, &val64);
		  if (ret < 0)
		    val64 = 0;
		}

	      dmap_add_long(group, dfm->tag, val64);

	      DPRINTF(E_DBG, L_DAAP, "Done with LONG meta tag %s (%" PRIi64 ") \n", dfm->desc, val64);
	      continue;
	    }

	  dmap_add_field(group, dfm, *strval, 0);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->desc, *strval);
	}

      /* Item count, always added (mimc) */
      val = 0;
      ret = safe_atoi32(dbgri.itemcount, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(group, "mimc", val);

      /* Song album artist, always added (asaa) */
      dmap_add_string(group, "asaa", dbgri.songalbumartist);

      /* Item id (miid) */
      dmap_add_int(group, "miid", ngrp);

      DPRINTF(E_DBG, L_DAAP, "Done with group\n");

      dmap_add_container(grouplist, "mlit", EVBUFFER_LENGTH(group));
      ret = evbuffer_add_buffer(grouplist, group);
      if (ret < 0)
	{
	  oom = 1;
	  break;
	}
    }

  DPRINTF(E_DBG, L_DAAP, "Done with group list, %d groups\n", ngrp);

  free(meta);
  evbuffer_free(group);

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");

      dmap_send_error(req, tag, "Error fetching query results");
      db_query_end(&qp);
      goto out_list_free;
    }

  if (oom)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add group to group list for DAAP groups reply\n");

      dmap_send_error(req, tag, "Out of memory");
      db_query_end(&qp);
      goto out_list_free;
    }

  /* Add header to evbuf, add grouplist to evbuf */
  dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(grouplist) + 53);
  dmap_add_int(evbuf, "mstt", 200); /* 12 */
  dmap_add_char(evbuf, "muty", 0);  /* 9 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf,"mrco", ngrp); /* 12 */
  dmap_add_container(evbuf, "mlcl", EVBUFFER_LENGTH(grouplist));

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, grouplist);
  evbuffer_free(grouplist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add group list to DAAP groups reply\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  return;

 out_query_free:
  free(meta);
  if (qp.filter)
    free(qp.filter);

 out_group_free:
  evbuffer_free(group);

 out_list_free:
  evbuffer_free(grouplist);
}

static void
daap_reply_browse(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct query_params qp;
  struct daap_session *s;
  struct evbuffer *itemlist;
  char *browse_item;
  char *tag;
  int nitems;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  memset(&qp, 0, sizeof(struct query_params));

  if (strcmp(uri[3], "artists") == 0)
    {
      tag = "abar";
      qp.type = Q_BROWSE_ARTISTS;
    }
  else if (strcmp(uri[3], "genres") == 0)
    {
      tag = "abgn";
      qp.type = Q_BROWSE_GENRES;
    }
  else if (strcmp(uri[3], "albums") == 0)
    {
      tag = "abal";
      qp.type = Q_BROWSE_ALBUMS;
    }
  else if (strcmp(uri[3], "composers") == 0)
    {
      tag = "abcp";
      qp.type = Q_BROWSE_COMPOSERS;
    }
  else
    {
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP browse request type '%s'\n", uri[3]);

      dmap_send_error(req, "abro", "Invalid browse type");
      return;
    }

  ret = evbuffer_expand(evbuf, 52);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP browse reply\n");

      dmap_send_error(req, "abro", "Out of memory");
      return;
    }

  itemlist = evbuffer_new();
  if (!itemlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP browse item list\n");

      dmap_send_error(req, "abro", "Out of memory");
      return;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(itemlist, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP browse item list\n");

      dmap_send_error(req, "abro", "Out of memory");

      evbuffer_free(itemlist);
      return;
    }

  get_query_params(query, &qp);

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, "abro", "Could not start query");

      evbuffer_free(itemlist);
      if (qp.filter)
	free(qp.filter);
      return;
    }

  nitems = 0;
  while (((ret = db_query_fetch_string(&qp, &browse_item)) == 0) && (browse_item))
    {
      nitems++;

      dmap_add_string(itemlist, "mlit", browse_item);
    }

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results\n");

      dmap_send_error(req, "abro", "Error fetching query results");
      db_query_end(&qp);
      evbuffer_free(itemlist);
      return;
    }

  dmap_add_container(evbuf, "abro", EVBUFFER_LENGTH(itemlist) + 44);
  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_int(evbuf, "mtco", qp.results); /* 12 */
  dmap_add_int(evbuf, "mrco", nitems); /* 12 */
  dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(itemlist));

  db_query_end(&qp);

  ret = evbuffer_add_buffer(evbuf, itemlist);
  evbuffer_free(itemlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add item list to DAAP browse reply\n");

      dmap_send_error(req, tag, "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_extra_data(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  /* Sorry, we have no artwork */
  evhttp_send_reply(req, HTTP_NOCONTENT, "No Content", evbuf);
}

static void
daap_stream(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;
  int id;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  ret = safe_atoi32(uri[3], &id);
  if (ret < 0)
    evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
  else
    httpd_stream_file(req, id);
}


static char *
daap_fix_request_uri(struct evhttp_request *req, char *uri)
{
  char *ret;

  /* iTunes 9 gives us an absolute request-uri like
   *  daap://10.1.1.20:3689/server-info
   */

  if (strncmp(uri, "daap://", strlen("daap://")) != 0)
    return uri;

  /* Clear the proxy request flag set by evhttp
   * due to the request URI being absolute.
   * It has side-effects on Connection: keep-alive
   */
  req->flags &= ~EVHTTP_PROXY_REQUEST;

  ret = strchr(uri + strlen("daap://"), '/');
  if (!ret)
    {
      DPRINTF(E_LOG, L_DAAP, "Malformed DAAP Request URI '%s'\n", uri);
      return NULL;
    }

  return ret;
}


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
  const char *ua;
  cfg_t *lib;
  char *libname;
  char *passwd;
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
  lib = cfg_getnsec(cfg, "library", 0);
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
  ua = evhttp_find_header(req->input_headers, "User-Agent");
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

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for DAAP reply\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      free(uri);
      free(full_uri);
      return;
    }

  evhttp_parse_query(full_uri, &query);

  evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");
  evhttp_add_header(req->output_headers, "DAAP-Server", "forked-daapd/" VERSION);
  /* Content-Type for all replies, even the actual audio streaming. Note that
   * video streaming will override this Content-Type with a more appropriate
   * video/<type> Content-Type as expected by clients like Front Row.
   */
  evhttp_add_header(req->output_headers, "Content-Type", "application/x-dmap-tagged");

  daap_handlers[handler].handler(req, evbuf, uri_parts, &query);

  evbuffer_free(evbuf);
  evhttp_clear_headers(&query);
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

  return 0;
}

int
daap_init(void)
{
  char buf[64];
  avl_node_t *node;
  struct dmap_field_map *dfm;
  int i;
  int ret;

  next_session_id = 100; /* gotta start somewhere, right? */
  update_requests = NULL;

  ret = daap_query_init();
  if (ret < 0)
    return ret;

  for (i = 0; daap_handlers[i].handler; i++)
    {
      ret = regcomp(&daap_handlers[i].preg, daap_handlers[i].regexp, REG_EXTENDED | REG_NOSUB);
      if (ret != 0)
        {
          regerror(ret, &daap_handlers[i].preg, buf, sizeof(buf));

          DPRINTF(E_FATAL, L_DAAP, "DAAP init failed; regexp error: %s\n", buf);
	  goto regexp_fail;
        }
    }

  daap_sessions = avl_alloc_tree(daap_session_compare, free);
  if (!daap_sessions)
    {
      DPRINTF(E_FATAL, L_DAAP, "DAAP init could not allocate DAAP sessions AVL tree\n");

      goto daap_avl_alloc_fail;
    }

  dmap_fields_hash = avl_alloc_tree(dmap_field_map_compare, NULL);
  if (!dmap_fields_hash)
    {
      DPRINTF(E_FATAL, L_DAAP, "DAAP init could not allocate DMAP fields AVL tree\n");

      goto dmap_avl_alloc_fail;
    }

  for (i = 0; dmap_fields[i].type != 0; i++)
    {
      dmap_fields[i].hash = djb_hash(dmap_fields[i].desc, strlen(dmap_fields[i].desc));

      node = avl_insert(dmap_fields_hash, &dmap_fields[i]);
      if (!node)
	{
	  if (errno != EEXIST)
	    DPRINTF(E_FATAL, L_DAAP, "DAAP init failed; AVL insert error: %s\n", strerror(errno));
	  else
	    {
	      node = avl_search(dmap_fields_hash, &dmap_fields[i]);
	      dfm = node->item;

	      DPRINTF(E_FATAL, L_DAAP, "DAAP init failed; WARNING: duplicate hash key\n");
	      DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dmap_fields[i].hash, dmap_fields[i].desc);

	      DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dfm->hash, dfm->desc);
	    }

	  goto dmap_avl_insert_fail;
	}
    }

  return 0;

 dmap_avl_insert_fail:
  avl_free_tree(dmap_fields_hash);
 dmap_avl_alloc_fail:
  avl_free_tree(daap_sessions);
 daap_avl_alloc_fail:
  for (i = 0; daap_handlers[i].handler; i++)
    regfree(&daap_handlers[i].preg);
 regexp_fail:
  daap_query_deinit();

  return -1;
}

void
daap_deinit(void)
{
  struct daap_update_request *ur;
  int i;

  daap_query_deinit();

  for (i = 0; daap_handlers[i].handler; i++)
    regfree(&daap_handlers[i].preg);

  avl_free_tree(daap_sessions);
  avl_free_tree(dmap_fields_hash);

  for (ur = update_requests; update_requests; ur = update_requests)
    {
      update_requests = ur->next;

      free(ur);
    }
}
