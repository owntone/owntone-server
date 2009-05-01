/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
#include <stddef.h>
#include <limits.h>
#include <stdint.h>

#include <event.h>
#include <evhttp.h>
#include <avl.h>

#include "daapd.h"
#include "err.h"
#include "ff-dbstruct.h"
#include "db-generic.h"
#include "conffile.h"
#include "misc.h"
#include "httpd.h"
#include "transcode.h"
#include "httpd_daap.h"


struct uri_map {
  regex_t preg;
  char *regexp;
  void (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query);
};

#define DMAP_TYPE_BYTE     0x01
#define DMAP_TYPE_UBYTE    0x02
#define DMAP_TYPE_SHORT    0x03
#define DMAP_TYPE_INT      0x05
#define DMAP_TYPE_LONG     0x07
#define DMAP_TYPE_STRING   0x09
#define DMAP_TYPE_DATE     0x0a
#define DMAP_TYPE_VERSION  0x0b
#define DMAP_TYPE_LIST     0x0c

struct dmap_field_map {
  uint32_t hash;
  short type;
  char *tag;
  char *desc;
  ssize_t mfi_offset;
  ssize_t pli_offset;
};


#define dbmfi_offsetof(field) offsetof(struct db_media_file_info, field)
#define dbpli_offsetof(field) offsetof(struct db_playlist_info, field)

static struct dmap_field_map dmap_fields[] =
  {
    { 0, DMAP_TYPE_INT,     "miid", "dmap.itemid",
      dbmfi_offsetof(id),            dbpli_offsetof(id) },
    { 0, DMAP_TYPE_STRING,  "minm", "dmap.itemname",
      dbmfi_offsetof(title),         dbpli_offsetof(title) },
    { 0, DMAP_TYPE_BYTE,    "mikd", "dmap.itemkind",
      dbmfi_offsetof(item_kind),     -1 },
    { 0, DMAP_TYPE_LONG,    "mper", "dmap.persistentid",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mcon", "dmap.container",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mcti", "dmap.containeritemid",
      dbmfi_offsetof(id),            -1 },
    { 0, DMAP_TYPE_INT,     "mpco", "dmap.parentcontainerid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mstt", "dmap.status",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "msts", "dmap.statusstring",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mimc", "dmap.itemcount",
      -1,                            dbpli_offsetof(items) },
    { 0, DMAP_TYPE_INT,     "mctc", "dmap.containercount",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mrco", "dmap.returnedcount",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mtco", "dmap.specifiedtotalcount",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mlcl", "dmap.listing",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mlit", "dmap.listingitem",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mbcl", "dmap.bag",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mdcl", "dmap.dictionary",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "msrv", "dmap.serverinforesponse",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msau", "dmap.authenticationmethod",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "mslr", "dmap.loginrequired",
      -1,                            -1 },
    { 0, DMAP_TYPE_VERSION, "mpro", "dmap.protocolversion",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msal", "dmap.supportsautologout",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msup", "dmap.supportsupdate",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "mspi", "dmap.supportspersistentids",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msex", "dmap.supportsextensions",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msbr", "dmap.supportsbrowse",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msqy", "dmap.supportsquery",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msix", "dmap.supportsindex",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "msrs", "dmap.supportsresolve",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mstm", "dmap.timeoutinterval",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "msdc", "dmap.databasescount",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mlog", "dmap.loginresponse",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mlid", "dmap.sessionid",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mupd", "dmap.updateresponse",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "musr", "dmap.serverrevision",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "muty", "dmap.updatetype",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mudl", "dmap.deletedidlisting",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "mccr", "dmap.contentcodesresponse",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "mcnm", "dmap.contentcodesnumber",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "mcna", "dmap.contentcodesname",
      -1,                            -1 },
    { 0, DMAP_TYPE_SHORT,   "mcty", "dmap.contentcodestype",
      -1,                            -1 },
    { 0, DMAP_TYPE_VERSION, "apro", "daap.protocolversion",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "avdb", "daap.serverdatabases",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "abro", "daap.databasebrowse",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "abal", "daap.browsealbumlisting",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "abar", "daap.browseartistlisting",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "abcp", "daap.browsecomposerlisting",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "abgn", "daap.browsegenrelisting",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "adbs", "daap.databasesongs",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "asal", "daap.songalbum",
      dbmfi_offsetof(album),         -1 },
    { 0, DMAP_TYPE_STRING,  "asar", "daap.songartist",
      dbmfi_offsetof(artist),        -1 },
    { 0, DMAP_TYPE_SHORT,   "asbt", "daap.songbeatsperminute",
      dbmfi_offsetof(bpm),           -1 },
    { 0, DMAP_TYPE_SHORT,   "asbr", "daap.songbitrate",
      dbmfi_offsetof(bitrate),       -1 },
    { 0, DMAP_TYPE_STRING,  "ascm", "daap.songcomment",
      dbmfi_offsetof(comment),       -1 },
    { 0, DMAP_TYPE_BYTE,    "asco", "daap.songcompilation",
      dbmfi_offsetof(compilation),   -1 },
    { 0, DMAP_TYPE_STRING,  "ascp", "daap.songcomposer",
      dbmfi_offsetof(composer),      -1 },
    { 0, DMAP_TYPE_DATE,    "asda", "daap.songdateadded",
      dbmfi_offsetof(time_added),    -1 },
    { 0, DMAP_TYPE_DATE,    "asdm", "daap.songdatemodified",
      dbmfi_offsetof(time_modified), -1 },
    { 0, DMAP_TYPE_SHORT,   "asdc", "daap.songdisccount",
      dbmfi_offsetof(total_discs),   -1 },
    { 0, DMAP_TYPE_SHORT,   "asdn", "daap.songdiscnumber",
      dbmfi_offsetof(disc),          -1 },
    { 0, DMAP_TYPE_BYTE,    "asdb", "daap.songdisabled",
      dbmfi_offsetof(disabled),      -1 },
    { 0, DMAP_TYPE_STRING,  "aseq", "daap.songeqpreset",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "asfm", "daap.songformat",
      dbmfi_offsetof(type),          -1 },
    { 0, DMAP_TYPE_STRING,  "asgn", "daap.songgenre",
      dbmfi_offsetof(genre),         -1 },
    { 0, DMAP_TYPE_STRING,  "asdt", "daap.songdescription",
      dbmfi_offsetof(description),   -1 },
    { 0, DMAP_TYPE_UBYTE,   "asrv", "daap.songrelativevolume",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "assr", "daap.songsamplerate",
      dbmfi_offsetof(samplerate),    -1 },
    { 0, DMAP_TYPE_INT,     "assz", "daap.songsize",
      dbmfi_offsetof(file_size),     -1 },
    { 0, DMAP_TYPE_INT,     "asst", "daap.songstarttime",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "assp", "daap.songstoptime",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "astm", "daap.songtime",
      dbmfi_offsetof(song_length),   -1 },
    { 0, DMAP_TYPE_SHORT,   "astc", "daap.songtrackcount",
      dbmfi_offsetof(total_tracks),  -1 },
    { 0, DMAP_TYPE_SHORT,   "astn", "daap.songtracknumber",
      dbmfi_offsetof(track),         -1 },
    { 0, DMAP_TYPE_BYTE,    "asur", "daap.songuserrating",
      dbmfi_offsetof(rating),        -1 },
    { 0, DMAP_TYPE_SHORT,   "asyr", "daap.songyear",
      dbmfi_offsetof(year),          -1 },
    { 0, DMAP_TYPE_BYTE,    "asdk", "daap.songdatakind",
      dbmfi_offsetof(data_kind),     -1 },
    { 0, DMAP_TYPE_STRING,  "asul", "daap.songdataurl",
      dbmfi_offsetof(url),           -1 },
    { 0, DMAP_TYPE_LIST,    "aply", "daap.databaseplaylists",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "abpl", "daap.baseplaylist",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "apso", "daap.playlistsongs",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "arsv", "daap.resolve",
      -1,                            -1 },
    { 0, DMAP_TYPE_LIST,    "arif", "daap.resolveinfo",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeNV", "com.apple.itunes.norm-volume",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "aeSP", "com.apple.itunes.smart-playlist",
      -1,                            -1 },

    /* iTunes 4.5+ */
#if 0 /* Duplicate: type changed to INT in iTunes 6.0.4 */
    { 0, DMAP_TYPE_BYTE,    "msas", "dmap.authenticationschemes",
      -1,                            -1 },
#endif
    { 0, DMAP_TYPE_INT,     "ascd", "daap.songcodectype",
      dbmfi_offsetof(codectype),     -1 },
    { 0, DMAP_TYPE_INT,     "ascs", "daap.songcodecsubtype",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "agrp", "daap.songgrouping",
      dbmfi_offsetof(grouping),      -1 },
    { 0, DMAP_TYPE_INT,     "aeSV", "com.apple.itunes.music-sharing-version",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aePI", "com.apple.itunes.itms-playlistid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeCI", "com.apple.iTunes.itms-composerid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeGI", "com.apple.iTunes.itms-genreid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeAI", "com.apple.iTunes.itms-artistid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeSI", "com.apple.iTunes.itms-songid",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeSF", "com.apple.iTunes.itms-storefrontid",
      -1,                            -1 },

    /* iTunes 5.0+ */
    { 0, DMAP_TYPE_BYTE,    "ascr", "daap.songcontentrating",
      dbmfi_offsetof(contentrating), -1 },
    { 0, DMAP_TYPE_BYTE,    "f" "\x8d" "ch", "dmap.haschildcontainers",
      -1,                            -1 },

    /* iTunes 6.0.2+ */
    { 0, DMAP_TYPE_BYTE,    "aeHV", "com.apple.itunes.has-video",
      dbmfi_offsetof(has_video),     -1 },

    /* iTunes 6.0.4+ */
    { 0, DMAP_TYPE_INT,     "msas", "dmap.authenticationschemes",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "asct", "daap.songcategory",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "ascn", "daap.songcontentdescription",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "aslc", "daap.songlongcontentdescription",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "asky", "daap.songkeywords",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "apsm", "daap.playlistshufflemode",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "aprm", "daap.playlistrepeatmode",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "aePC", "com.apple.itunes.is-podcast",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "aePP", "com.apple.itunes.is-podcast-playlist",
      -1,                            -1 },
    { 0, DMAP_TYPE_BYTE,    "aeMK", "com.apple.itunes.mediakind",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "aeSN", "com.apple.itunes.series-name",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "aeNN", "com.apple.itunes.network-name",
      -1,                            -1 },
    { 0, DMAP_TYPE_STRING,  "aeEN", "com.apple.itunes.episode-num-str",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeES", "com.apple.itunes.episode-sort",
      -1,                            -1 },
    { 0, DMAP_TYPE_INT,     "aeSU", "com.apple.itunes.season-num",
      -1,                            -1 },

    { 0, 0,                 "",      NULL,
      -1,                            -1 }
  };

/* Default meta tags if not provided in the query */
static char *default_meta_plsongs = "dmap.itemkind,dmap.itemid,dmap.itemname,dmap.containeritemid,dmap.parentcontainerid";
static char *default_meta_pl = "dmap.itemid,dmap.itemname,dmap.persistentid,com.apple.itunes.smart-playlist";

avl_tree_t *dmap_fields_hash;

/* Next session ID */
static int session_id;


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


/* DMAP encoding routines */
static void
dmap_add_container(struct evbuffer *evbuf, char *tag, int len)
{
  unsigned char buf[4];

  evbuffer_add(evbuf, tag, 4);

  /* Container length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

static void
dmap_add_long(struct evbuffer *evbuf, char *tag, int64_t val)
{
  unsigned char buf[12];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 8;

  /* Value */
  buf[4] = (val >> 56) & 0xff;
  buf[5] = (val >> 48) & 0xff;
  buf[6] = (val >> 40) & 0xff;
  buf[7] = (val >> 32) & 0xff;
  buf[8] = (val >> 24) & 0xff;
  buf[9] = (val >> 16) & 0xff;
  buf[10] = (val >> 8) & 0xff;
  buf[11] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

static void
dmap_add_int(struct evbuffer *evbuf, char *tag, int val)
{
  unsigned char buf[8];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 4;

  /* Value */
  buf[4] = (val >> 24) & 0xff;
  buf[5] = (val >> 16) & 0xff;
  buf[6] = (val >> 8) & 0xff;
  buf[7] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

static void
dmap_add_short(struct evbuffer *evbuf, char *tag, short val)
{
  unsigned char buf[6];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 2;

  /* Value */
  buf[4] = (val >> 8) & 0xff;
  buf[5] = val & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

static void
dmap_add_char(struct evbuffer *evbuf, char *tag, char val)
{
  unsigned char buf[5];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = 0;
  buf[1] = 0;
  buf[2] = 0;
  buf[3] = 1;

  /* Value */
  buf[4] = val;

  evbuffer_add(evbuf, buf, sizeof(buf));
}

static void
dmap_add_literal(struct evbuffer *evbuf, char *tag, char *str, int len)
{
  char buf[4];

  evbuffer_add(evbuf, tag, 4);

  /* Length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));

  if (str && (len > 0))
    evbuffer_add(evbuf, str, len);
}

static void
dmap_add_string(struct evbuffer *evbuf, char *tag, char *str)
{
  unsigned char buf[4];
  int len;

  if (str)
    len = strlen(str);
  else
    len = 0;

  evbuffer_add(evbuf, tag, 4);

  /* String length */
  buf[0] = (len >> 24) & 0xff;
  buf[1] = (len >> 16) & 0xff;
  buf[2] = (len >> 8) & 0xff;
  buf[3] = len & 0xff;

  evbuffer_add(evbuf, buf, sizeof(buf));

  if (len)
    evbuffer_add(evbuf, str, len);
}

static void
dmap_add_field(struct evbuffer *evbuf, struct dmap_field_map *dfm, char *strval, int intval)
{
  int val;
  int ret;

  val = 0;

  if (intval != 0)
    val = intval;
  else if (strval)
    {
      ret = safe_atoi(strval, &val);
      if (ret < 0)
	val = 0;
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
      /* FIXME: "long" is thought of as a 64bit value */
      if (val)
	dmap_add_long(evbuf, dfm->tag, val);
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


/* Forward */
static void
daap_send_error(struct evhttp_request *req, char *container, char *errmsg);


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
get_query_params(struct evkeyvalq *query, DBQUERYINFO *qi)
{
  const char *param;
  char *ptr;
  int val;
  int ret;

  qi->index_low = 0;
  qi->index_high = 9999999; /* Arbitrarily high number */

  param = evhttp_find_header(query, "index");
  if (param)
    {
      if (param[0] == '-') /* -n, last n entries */
	DPRINTF(E_LOG, L_DAAP, "Unsupported index range: %s\n", param);
      else
	{
	  ret = safe_atoi(param, &val);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_DAAP, "Could not parse index range: %s\n", param);
	  else
	    {
	      qi->index_low = val;

	      ptr = strchr(param, '-');
	      if (!ptr) /* single item */
		qi->index_high = qi->index_low;
	      else
		{
		  ptr++;
		  if (*ptr != '\0') /* low-high */
		    {
		      ret = safe_atoi(ptr, &val);
		      if (ret < 0)
			  DPRINTF(E_LOG, L_DAAP, "Could not parse high index in range: %s\n", param);
		      else
			qi->index_high = val;
		    }
		}
	    }
	}

      DPRINTF(E_DBG, L_DAAP, "Index range %s: low %d, high %d\n", param, qi->index_low, qi->index_high);
    }

  if (qi->index_high < qi->index_low)
    qi->index_high = 9999999; /* Arbitrarily high number */

  qi->index_type = indexTypeSub;

  param = evhttp_find_header(query, "query");
  if (param)
    {
      DPRINTF(E_DBG, L_DAAP, "DAAP browse query filter: %s\n", param);

      qi->pt = sp_init();
      if (!qi->pt)
        {
          DPRINTF(E_LOG, L_DAAP, "Could not init query filter\n");
        }
      else
        {
          ret = sp_parse(qi->pt, param, FILTER_TYPE_APPLE);
          if (ret != 1)
            {
              DPRINTF(E_LOG, L_DAAP, "Ignoring improper query: %s\n", sp_get_error(qi->pt));

              sp_dispose(qi->pt);
              qi->pt = NULL;
            }
        }
    }

  qi->want_count = 1;
  qi->correct_order = 1;
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

      daap_send_error(req, tag, "Out of memory");
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

      daap_send_error(req, tag, "Out of memory");

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
daap_send_error(struct evhttp_request *req, char *container, char *errmsg)
{
  struct evbuffer *evbuf;
  int len;
  int ret;

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not allocate evbuffer for DAAP error\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");
      return;
    }

  len = 12 + 8 + 8 + strlen(errmsg);

  ret = evbuffer_expand(evbuf, len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP error\n");

      evhttp_send_error(req, HTTP_SERVUNAVAIL, "Internal Server Error");

      evbuffer_free(evbuf);
      return;
    }

  dmap_add_container(evbuf, container, len - 8);
  dmap_add_int(evbuf, "mstt", 500);
  dmap_add_string(evbuf, "msts", errmsg);

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);

  evbuffer_free(evbuf);
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

  len = 139 + strlen(name);

  if (!supports_update)
    len -= 9;

  ret = evbuffer_expand(evbuf, len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP server-info reply\n");

      daap_send_error(req, "msrv", "Out of memory");
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

  dmap_add_char(evbuf, "msau", (passwd) ? 2 : 0); /* 9 */
  dmap_add_char(evbuf, "msex", 0);   /* 9 */
  dmap_add_char(evbuf, "msix", 0);   /* 9 */
  dmap_add_char(evbuf, "msbr", 0);   /* 9 */
  dmap_add_char(evbuf, "msqy", 0);   /* 9 */

  dmap_add_char(evbuf, "mspi", 0);   /* 9 */
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

      daap_send_error(req, "mccr", "Out of memory");
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
  int ret;

  ret = evbuffer_expand(evbuf, 32);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP login reply\n");

      daap_send_error(req, "mlog", "Out of memory");
      return;
    }

  dmap_add_container(evbuf, "mlog", 24);
  dmap_add_int(evbuf, "mstt", 200);        /* 12 */
  dmap_add_int(evbuf, "mlid", session_id); /* 12 */

  /* We don't actually care about session id at the moment */
  session_id++;

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_logout(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  evhttp_send_reply(req, 204, "Logout Successful", evbuf);
}

static void
daap_reply_update(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  int ret;

  /* Just send back the current db revision.
   *
   * This probably doesn't cut it, but then again we don't claim to support
   * updates, so... that support should be added eventually.
   */

  ret = evbuffer_expand(evbuf, 32);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP update reply\n");

      daap_send_error(req, "mupd", "Out of memory");
      return;
    }

  dmap_add_container(evbuf, "mupd", 24);
  dmap_add_int(evbuf, "mstt", 200);           /* 12 */
  dmap_add_int(evbuf, "musr", db_revision()); /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_dblist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  int count;

  cfg_t *lib;
  char *db_errmsg;
  char *name;
  int namelen;
  int ret;

  lib = cfg_getnsec(cfg, "library", 0);
  name = cfg_getstr(lib, "name");
  namelen = strlen(name);

  ret = evbuffer_expand(evbuf, 129 + namelen);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP dblist reply\n");

      daap_send_error(req, "avdb", "Out of memory");
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

  ret = db_get_song_count(&db_errmsg, &count);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not get song count: %s\n", db_errmsg);

      count = 0;
      free(db_errmsg);
    }
  dmap_add_int(evbuf, "mimc", count); /* 12 */

  ret = db_get_playlist_count(&db_errmsg, &count);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not get playlist count: %s\n", db_errmsg);

      count = 0;
      free(db_errmsg);
    }
  dmap_add_int(evbuf, "mctc", count); /* 12 */

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_songlist_generic(struct evhttp_request *req, struct evbuffer *evbuf, int playlist, struct evkeyvalq *query)
{
  DBQUERYINFO qi;
  struct db_media_file_info *dbmfi;
  struct evbuffer *song;
  struct evbuffer *songlist;
  struct dmap_field_map *dfm;
  char *db_errmsg;
  const char *param;
  char *tag;
  char **strval;
  char *ptr;
  uint32_t *meta;
  int nmeta;
  int nsongs;
  int transcode;
  int want_mikd;
  int want_asdk;
  int oom;
  int val;
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

      daap_send_error(req, tag, "Out of memory");
      return;
    }

  songlist = evbuffer_new();
  if (!songlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP song list\n");

      daap_send_error(req, tag, "Out of memory");
      return;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(songlist, 4096);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP song list\n");

      daap_send_error(req, tag, "Out of memory");

      evbuffer_free(songlist);
      return;
    }

  song = evbuffer_new();
  if (!song)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP song block\n");

      daap_send_error(req, tag, "Out of memory");

      evbuffer_free(songlist);
      return;
    }

  /* The buffer will expand if needed */
  ret = evbuffer_expand(song, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP song block\n");

      daap_send_error(req, tag, "Out of memory");

      evbuffer_free(song);
      evbuffer_free(songlist);
      return;
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

	  evbuffer_free(song);
	  evbuffer_free(songlist);
	  return;
	}
    }
  else
    {
      meta = NULL;
      nmeta = 0;
    }

  memset(&qi, 0, sizeof(DBQUERYINFO));
  get_query_params(query, &qi);
  qi.query_type = queryTypePlaylistItems;

  if (playlist != -1)
    qi.playlist_id = playlist;

  ret = db_enum_start(&db_errmsg, &qi);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not fetch song list: %s\n", db_errmsg);

      daap_send_error(req, tag, db_errmsg);

      free(db_errmsg);
      free(meta);
      evbuffer_free(song);
      evbuffer_free(songlist);
      if (qi.pt)
	sp_dispose(qi.pt);
      return;
    }

  want_mikd = 0;
  want_asdk = 0;
  oom = 0;
  nsongs = 0;
  while (((ret = db_enum_fetch_row(&db_errmsg, &dbmfi, &qi)) == DB_E_SUCCESS) && (dbmfi))
    {
      nsongs++;

      transcode = transcode_needed(req->input_headers, dbmfi->codectype);

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

	  strval = (char **) ((char *)dbmfi + dfm->mfi_offset);

	  if (!(*strval) || (**strval == '\0'))
            continue;

	  /* Here's one exception ... codectype (ascd) is actually an integer */
	  if (dfm->mfi_offset == dbmfi_offsetof(codectype))
	    {
	      dmap_add_literal(song, dfm->tag, *strval, 4);
	      continue;
	    }

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
		    ret = safe_atoi(dbmfi->samplerate, &val);
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

	  if (*strval)
	    {
	      ret = safe_atoi(*strval, &val);
	      if (ret < 0)
		val = 0;
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
	  ret = safe_atoi(dbmfi->item_kind, &val);
	  if (ret < 0)
	    val = 2; /* music by default */
	  dmap_add_char(songlist, "mikd", val);
	}
      if (want_asdk)
	{
	  ret = safe_atoi(dbmfi->data_kind, &val);
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

  if (qi.pt)
    sp_dispose(qi.pt);

  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results: %s\n", db_errmsg);

      daap_send_error(req, tag, db_errmsg);

      free(db_errmsg);
      evbuffer_free(songlist);
      return;
    }

  ret = db_enum_end(&db_errmsg);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error cleaning up DB enum: %s\n", db_errmsg);

      free(db_errmsg);
    }

  if (oom)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add song to song list for DAAP song list reply\n");

      daap_send_error(req, tag, "Out of memory");

      evbuffer_free(songlist);
      return;
    }

  /* Add header to evbuf, add songlist to evbuf */
  dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(songlist) + 53);
  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_char(evbuf, "muty", 0);     /* 9 */
  dmap_add_int(evbuf, "mtco", qi.specifiedtotalcount); /* 12 */
  dmap_add_int(evbuf, "mrco", nsongs); /* 12 */
  dmap_add_container(evbuf, "mlcl", EVBUFFER_LENGTH(songlist));

  ret = evbuffer_add_buffer(evbuf, songlist);
  evbuffer_free(songlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add song list to DAAP song list reply\n");

      daap_send_error(req, tag, "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_dbsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  daap_reply_songlist_generic(req, evbuf, -1, query);
}

static void
daap_reply_plsonglist(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  int playlist;
  int ret;

  ret = safe_atoi(uri[3], &playlist);
  if (ret < 0)
    {
      daap_send_error(req, "apso", "Invalid playlist ID");

      return;
    }

  daap_reply_songlist_generic(req, evbuf, playlist, query);
}

static void
daap_reply_playlists(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  DBQUERYINFO qi;
  struct evbuffer *playlistlist;
  struct evbuffer *playlist;
  struct db_playlist_info *dbpli;
  struct dmap_field_map *dfm;
  const char *param;
  char *db_errmsg;
  char **strval;
  uint32_t *meta;
  int nmeta;
  int npls;
  int oom;
  int val;
  int i;
  int ret;

  ret = evbuffer_expand(evbuf, 61);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP playlists reply\n");

      daap_send_error(req, "aply", "Out of memory");
      return;
    }

  playlistlist = evbuffer_new();
  if (!playlistlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP playlist list\n");

      daap_send_error(req, "aply", "Out of memory");
      return;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(playlistlist, 1024);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP playlist list\n");

      daap_send_error(req, "aply", "Out of memory");

      evbuffer_free(playlistlist);
      return;
    }

  playlist = evbuffer_new();
  if (!playlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP playlist block\n");

      daap_send_error(req, "aply", "Out of memory");

      evbuffer_free(playlistlist);
      return;
    }

  /* The buffer will expand if needed */
  ret = evbuffer_expand(playlist, 128);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP playlist block\n");

      daap_send_error(req, "aply", "Out of memory");

      evbuffer_free(playlist);
      evbuffer_free(playlistlist);
      return;
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

      evbuffer_free(playlist);
      evbuffer_free(playlistlist);
      return;
    }

  memset(&qi, 0, sizeof(DBQUERYINFO));
  get_query_params(query, &qi);
  qi.query_type = queryTypePlaylists;

  ret = db_enum_start(&db_errmsg, &qi);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not fetch playlist list: %s\n", db_errmsg);

      daap_send_error(req, "aply", db_errmsg);

      free(db_errmsg);
      free(meta);
      evbuffer_free(playlist);
      evbuffer_free(playlistlist);
      if (qi.pt)
	sp_dispose(qi.pt);
      return;
    }

  npls = 0;
  oom = 0;
  while (((ret = db_enum_fetch_row(&db_errmsg, (struct db_media_file_info **)&dbpli, &qi)) == DB_E_SUCCESS) && (dbpli))
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
	      ret = safe_atoi(dbpli->type, &val);
	      if ((ret == 0) && (val == 1))
		{
		  val = 1;
		  ret = safe_atoi(dbpli->id, &val);
		  if ((ret == 0) && (val != 1))
		    dmap_add_char(playlist, "aeSP", 1);
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

          strval = (char **) ((char *)dbpli + dfm->pli_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(playlist, dfm, *strval, 0);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->desc, *strval);
	}

      /* Item count (mimc) */
      val = 0;
      ret = safe_atoi(dbpli->items, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(playlist, "mimc", val);

      /* Base playlist (abpl), id = 1 */
      val = 0;
      ret = safe_atoi(dbpli->id, &val);
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

  DPRINTF(E_DBG, L_DAAP, "Done with playlist list, %d playlists\n");

  free(meta);
  evbuffer_free(playlist);

  if (qi.pt)
    sp_dispose(qi.pt);

  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results: %s\n", db_errmsg);

      daap_send_error(req, "aply", db_errmsg);

      free(db_errmsg);
      evbuffer_free(playlistlist);
      return;
    }

  ret = db_enum_end(&db_errmsg);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error cleaning up DB enum: %s\n", db_errmsg);

      free(db_errmsg);
    }

  if (oom)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add playlist to playlist list for DAAP playlists reply\n");

      daap_send_error(req, "aply", "Out of memory");

      evbuffer_free(playlistlist);
      return;
    }

  /* Add header to evbuf, add playlistlist to evbuf */
  dmap_add_container(evbuf, "aply", EVBUFFER_LENGTH(playlistlist) + 53);
  dmap_add_int(evbuf, "mstt", 200); /* 12 */
  dmap_add_char(evbuf, "muty", 0);  /* 9 */
  dmap_add_int(evbuf, "mtco", qi.specifiedtotalcount); /* 12 */
  dmap_add_int(evbuf,"mrco", npls); /* 12 */
  dmap_add_container(evbuf, "mlcl", EVBUFFER_LENGTH(playlistlist));

  ret = evbuffer_add_buffer(evbuf, playlistlist);
  evbuffer_free(playlistlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add playlist list to DAAP playlists reply\n");

      daap_send_error(req, "aply", "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_browse(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  DBQUERYINFO qi;
  struct evbuffer *itemlist;
  char **item;
  char *tag;
  char *db_errmsg;
  int nitems;
  int ret;

  memset(&qi, 0, sizeof(DBQUERYINFO));

  if (strcmp(uri[3], "artists") == 0)
    {
      tag = "abar";
      qi.query_type = queryTypeBrowseArtists;
    }
  else if (strcmp(uri[3], "genres") == 0)
    {
      tag = "abgn";
      qi.query_type = queryTypeBrowseGenres;
    }
  else if (strcmp(uri[3], "albums") == 0)
    {
      tag = "abal";
      qi.query_type = queryTypeBrowseAlbums;
    }
  else if (strcmp(uri[3], "composers") == 0)
    {
      tag = "abcp";
      qi.query_type = queryTypeBrowseComposers;
    }
  else
    {
      DPRINTF(E_LOG, L_DAAP, "Invalid DAAP browse request type '%s'\n", uri[3]);

      daap_send_error(req, "abro", "Invalid browse type");
      return;
    }

  ret = evbuffer_expand(evbuf, 52);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP browse reply\n");

      daap_send_error(req, "abro", "Out of memory");
      return;
    }

  itemlist = evbuffer_new();
  if (!itemlist)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP browse item list\n");

      daap_send_error(req, "abro", "Out of memory");
      return;
    }

  /* Start with a big enough evbuffer - it'll expand as needed */
  ret = evbuffer_expand(itemlist, 512);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DMAP browse item list\n");

      daap_send_error(req, "abro", "Out of memory");

      evbuffer_free(itemlist);
      return;
    }

  get_query_params(query, &qi);

  ret = db_enum_start(&db_errmsg, &qi);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not fetch browse item list: %s\n", db_errmsg);

      daap_send_error(req, "abro", db_errmsg);

      free(db_errmsg);
      evbuffer_free(itemlist);
      if (qi.pt)
	sp_dispose(qi.pt);
      return;
    }

  nitems = 0;
  while (((ret = db_enum_fetch_row(&db_errmsg, (struct db_media_file_info **)&item, &qi)) == DB_E_SUCCESS) && (item))
    {
      nitems++;

      dmap_add_string(itemlist, "mlit", *item);
    }

  if (qi.pt)
    sp_dispose(qi.pt);

  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching results: %s\n", db_errmsg);

      daap_send_error(req, "abro", db_errmsg);

      free(db_errmsg);
      evbuffer_free(itemlist);
      return;
    }

  ret = db_enum_end(&db_errmsg);
  if (ret != DB_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_DAAP, "Error cleaning up DB enum: %s\n", db_errmsg);

      free(db_errmsg);
    }

  dmap_add_container(evbuf, "abro", EVBUFFER_LENGTH(itemlist) + 44);
  dmap_add_int(evbuf, "mstt", 200);    /* 12 */
  dmap_add_int(evbuf, "mtco", qi.specifiedtotalcount); /* 12 */
  dmap_add_int(evbuf, "mrco", nitems); /* 12 */
  dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(itemlist));

  ret = evbuffer_add_buffer(evbuf, itemlist);
  evbuffer_free(itemlist);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add item list to DAAP browse reply\n");

      daap_send_error(req, tag, "Out of memory");
      return;
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_stream(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  int id;
  int ret;

  ret = safe_atoi(uri[3], &id);
  if (ret < 0)
    evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
  else
    httpd_stream_file(req, id);
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
      .regexp = "^/databases$",
      .handler = daap_reply_dblist
    },
    {
      .regexp = "^/databases/[[:digit:]]/browse/[^/]+$",
      .handler = daap_reply_browse
    },
    {
      .regexp = "^/databases/[[:digit:]]/items$",
      .handler = daap_reply_dbsonglist
    },
    {
      .regexp = "^/databases/[[:digit:]]/items/[[:digit:]][.][^/]+$",
      .handler = daap_stream
    },
    {
      .regexp = "^/databases/[[:digit:]]/containers$",
      .handler = daap_reply_playlists
    },
    {
      .regexp = "^/databases/[[:digit:]]/containers/[[:digit:]]/items$",
      .handler = daap_reply_plsonglist
    },
    { 
      .regexp = NULL,
      .handler = NULL
    }
  };


void
daap_request(struct evhttp_request *req)
{
  const char *req_uri;
  char *full_uri;
  char *uri;
  char *ptr;
  char *uri_parts[7];
  struct evbuffer *evbuf;
  struct evkeyvalq query;
  int handler;
  int ret;
  int i;

  req_uri = evhttp_request_uri(req);

  full_uri = strdup(req_uri);

  ptr = strchr(full_uri, '?');
  if (ptr)
    *ptr = '\0';

  uri = strdup(full_uri);

  if (ptr)
    *ptr = '?';

  ptr = full_uri;
  full_uri = evhttp_decode_uri(full_uri);
  free(ptr);

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
  /* Content-Type for all replies, even the actual streaming */
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

  session_id = 100; /* gotta start somewhere, right? */

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

  dmap_fields_hash = avl_alloc_tree(dmap_field_map_compare, NULL);
  if (!dmap_fields_hash)
    {
      DPRINTF(E_FATAL, L_DAAP, "DAAP init could not allocate AVL tree\n");

      goto avl_alloc_fail;
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

	  goto avl_insert_fail;
	}
    }

  return 0;

 avl_insert_fail:
  avl_free_tree(dmap_fields_hash);
 avl_alloc_fail:
  for (i = 0; daap_handlers[i].handler; i++)
    regfree(&daap_handlers[i].preg);

  return -1;
}

void
daap_deinit(void)
{
  int i;

  for (i = 0; daap_handlers[i].handler; i++)
    regfree(&daap_handlers[i].preg);

  avl_free_tree(dmap_fields_hash);
}
