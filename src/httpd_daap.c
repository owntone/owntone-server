/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include <ctype.h>

#include <uninorm.h>

#include <event.h>
#include "evhttp/evhttp.h"
#include <avl.h>

#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "misc.h"
#include "httpd.h"
#include "transcode.h"
#include "artwork.h"
#include "httpd_daap.h"
#include "daap_query.h"
#include "dmap_helpers.h"

/* httpd event base, from httpd.c */
extern struct event_base *evbase_httpd;


/* Session timeout in seconds */
#define DAAP_SESSION_TIMEOUT 1800


struct uri_map {
  regex_t preg;
  char *regexp;
  void (*handler)(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query);
};

struct daap_session {
  int id;

  struct event timeout;
};

struct daap_update_request {
  struct evhttp_request *req;

  struct daap_update_request *next;
};

struct dmap_field {
  char *tag;
  char *desc;
  enum dmap_type type;
};

struct dmap_field_map {
  uint32_t hash;
  const struct dmap_field *field;
  ssize_t mfi_offset;
  ssize_t pli_offset;
  ssize_t gri_offset;
};

struct sort_ctx {
  struct evbuffer *headerlist;
  int16_t mshc;
  uint32_t mshi;
  uint32_t mshn;
  uint32_t misc_mshn;
};


static const struct dmap_field dmap_abal = { "abal", "daap.browsealbumlisting",                DMAP_TYPE_LIST };
static const struct dmap_field dmap_abar = { "abar", "daap.browseartistlisting",               DMAP_TYPE_LIST };
static const struct dmap_field dmap_abcp = { "abcp", "daap.browsecomposerlisting",             DMAP_TYPE_LIST };
static const struct dmap_field dmap_abgn = { "abgn", "daap.browsegenrelisting",                DMAP_TYPE_LIST };
static const struct dmap_field dmap_abpl = { "abpl", "daap.baseplaylist",                      DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_abro = { "abro", "daap.databasebrowse",                    DMAP_TYPE_LIST };
static const struct dmap_field dmap_adbs = { "adbs", "daap.databasesongs",                     DMAP_TYPE_LIST };
//static const struct dmap_field dmap_aeAD = { "aeAD", "com.apple.itunes.adam-ids-array",        DMAP_TYPE_LIST };
static const struct dmap_field dmap_aeAI = { "aeAI", "com.apple.itunes.itms-artistid",         DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeCI = { "aeCI", "com.apple.itunes.itms-composerid",       DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeCR = { "aeCR", "com.apple.itunes.content-rating",        DMAP_TYPE_STRING };
//static const struct dmap_field dmap_aeDP = { "aeDP", "com.apple.itunes.drm-platform-id",       DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeDR = { "aeDR", "com.apple.itunes.drm-user-id",           DMAP_TYPE_ULONG };
//static const struct dmap_field dmap_aeDV = { "aeDV", "com.apple.itunes.drm-versions",          DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeEN = { "aeEN", "com.apple.itunes.episode-num-str",       DMAP_TYPE_STRING };
static const struct dmap_field dmap_aeES = { "aeES", "com.apple.itunes.episode-sort",          DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeGD = { "aeGD", "com.apple.itunes.gapless-enc-dr",        DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeGE = { "aeGE", "com.apple.itunes.gapless-enc-del",       DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeGH = { "aeGH", "com.apple.itunes.gapless-heur",          DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeGI = { "aeGI", "com.apple.itunes.itms-genreid",          DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeGR = { "aeGR", "com.apple.itunes.gapless-resy",          DMAP_TYPE_ULONG };
//static const struct dmap_field dmap_aeGU = { "aeGU", "com.apple.itunes.gapless-dur",           DMAP_TYPE_ULONG };
//static const struct dmap_field dmap_aeHD = { "aeHD", "com.apple.itunes.is-hd-video",           DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_aeHV = { "aeHV", "com.apple.itunes.has-video",             DMAP_TYPE_UBYTE };
//static const struct dmap_field dmap_aeK1 = { "aeK1", "com.apple.itunes.drm-key1-id",           DMAP_TYPE_ULONG };
//static const struct dmap_field dmap_aeK2 = { "aeK2", "com.apple.itunes.drm-key2-id",           DMAP_TYPE_ULONG };
static const struct dmap_field dmap_aeMk = { "aeMk", "com.apple.itunes.extended-media-kind",   DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeMK = { "aeMK", "com.apple.itunes.mediakind",             DMAP_TYPE_UBYTE };
//static const struct dmap_field dmap_aeND = { "aeND", "com.apple.itunes.non-drm-user-id",       DMAP_TYPE_ULONG };
static const struct dmap_field dmap_aeNN = { "aeNN", "com.apple.itunes.network-name",          DMAP_TYPE_STRING };
static const struct dmap_field dmap_aeNV = { "aeNV", "com.apple.itunes.norm-volume",           DMAP_TYPE_UINT };
static const struct dmap_field dmap_aePC = { "aePC", "com.apple.itunes.is-podcast",            DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_aePI = { "aePI", "com.apple.itunes.itms-playlistid",       DMAP_TYPE_UINT };
static const struct dmap_field dmap_aePP = { "aePP", "com.apple.itunes.is-podcast-playlist",   DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_aePS = { "aePS", "com.apple.itunes.special-playlist",      DMAP_TYPE_UBYTE };
//static const struct dmap_field dmap_aeSE = { "aeSE", "com.apple.itunes.store-pers-id",         DMAP_TYPE_ULONG };
static const struct dmap_field dmap_aeSF = { "aeSF", "com.apple.itunes.itms-storefrontid",     DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeSG = { "aeSG", "com.apple.itunes.saved-genius",          DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_aeSI = { "aeSI", "com.apple.itunes.itms-songid",           DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeSN = { "aeSN", "com.apple.itunes.series-name",           DMAP_TYPE_STRING };
static const struct dmap_field dmap_aeSP = { "aeSP", "com.apple.itunes.smart-playlist",        DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_aeSU = { "aeSU", "com.apple.itunes.season-num",            DMAP_TYPE_UINT };
static const struct dmap_field dmap_aeSV = { "aeSV", "com.apple.itunes.music-sharing-version", DMAP_TYPE_UINT };
//static const struct dmap_field dmap_aeXD = { "aeXD", "com.apple.itunes.xid",                   DMAP_TYPE_STRING };
static const struct dmap_field dmap_agrp = { "agrp", "daap.songgrouping",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_aply = { "aply", "daap.databaseplaylists",                 DMAP_TYPE_LIST };
static const struct dmap_field dmap_aprm = { "aprm", "daap.playlistrepeatmode",                DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_apro = { "apro", "daap.protocolversion",                   DMAP_TYPE_VERSION };
static const struct dmap_field dmap_apsm = { "apsm", "daap.playlistshufflemode",               DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_apso = { "apso", "daap.playlistsongs",                     DMAP_TYPE_LIST };
static const struct dmap_field dmap_arif = { "arif", "daap.resolveinfo",                       DMAP_TYPE_LIST };
static const struct dmap_field dmap_arsv = { "arsv", "daap.resolve",                           DMAP_TYPE_LIST };
static const struct dmap_field dmap_asaa = { "asaa", "daap.songalbumartist",                   DMAP_TYPE_STRING };
static const struct dmap_field dmap_asai = { "asai", "daap.songalbumid",                       DMAP_TYPE_ULONG };
static const struct dmap_field dmap_asal = { "asal", "daap.songalbum",                         DMAP_TYPE_STRING };
static const struct dmap_field dmap_asar = { "asar", "daap.songartist",                        DMAP_TYPE_STRING };
//static const struct dmap_field dmap_asbk = { "asbk", "daap.bookmarkable",                      DMAP_TYPE_UBYTE };
//static const struct dmap_field dmap_asbo = { "asbo", "daap.songbookmark",                      DMAP_TYPE_UINT };
static const struct dmap_field dmap_asbr = { "asbr", "daap.songbitrate",                       DMAP_TYPE_USHORT };
static const struct dmap_field dmap_asbt = { "asbt", "daap.songbeatsperminute",                DMAP_TYPE_USHORT };
static const struct dmap_field dmap_ascd = { "ascd", "daap.songcodectype",                     DMAP_TYPE_UINT };
static const struct dmap_field dmap_ascm = { "ascm", "daap.songcomment",                       DMAP_TYPE_STRING };
static const struct dmap_field dmap_ascn = { "ascn", "daap.songcontentdescription",            DMAP_TYPE_STRING };
static const struct dmap_field dmap_asco = { "asco", "daap.songcompilation",                   DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_ascp = { "ascp", "daap.songcomposer",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_ascr = { "ascr", "daap.songcontentrating",                 DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_ascs = { "ascs", "daap.songcodecsubtype",                  DMAP_TYPE_UINT };
static const struct dmap_field dmap_asct = { "asct", "daap.songcategory",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_asda = { "asda", "daap.songdateadded",                     DMAP_TYPE_DATE };
static const struct dmap_field dmap_asdb = { "asdb", "daap.songdisabled",                      DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_asdc = { "asdc", "daap.songdisccount",                     DMAP_TYPE_USHORT };
static const struct dmap_field dmap_asdk = { "asdk", "daap.songdatakind",                      DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_asdm = { "asdm", "daap.songdatemodified",                  DMAP_TYPE_DATE };
static const struct dmap_field dmap_asdn = { "asdn", "daap.songdiscnumber",                    DMAP_TYPE_USHORT };
//static const struct dmap_field dmap_asdp = { "asdp", "daap.songdatepurchased",                 DMAP_TYPE_DATE };
//static const struct dmap_field dmap_asdr = { "asdr", "daap.songdatereleased",                  DMAP_TYPE_DATE };
static const struct dmap_field dmap_asdt = { "asdt", "daap.songdescription",                   DMAP_TYPE_STRING };
//static const struct dmap_field dmap_ased = { "ased", "daap.songextradata",                     DMAP_TYPE_USHORT };
static const struct dmap_field dmap_aseq = { "aseq", "daap.songeqpreset",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_asfm = { "asfm", "daap.songformat",                        DMAP_TYPE_STRING };
static const struct dmap_field dmap_asgn = { "asgn", "daap.songgenre",                         DMAP_TYPE_STRING };
//static const struct dmap_field dmap_asgp = { "asgp", "daap.songgapless",                       DMAP_TYPE_UBYTE };
//static const struct dmap_field dmap_ashp = { "ashp", "daap.songhasbeenplayed",                 DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_asky = { "asky", "daap.songkeywords",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_aslc = { "aslc", "daap.songlongcontentdescription",        DMAP_TYPE_STRING };
//static const struct dmap_field dmap_asls = { "asls", "daap.songlongsize",                      DMAP_TYPE_ULONG };
//static const struct dmap_field dmap_aspu = { "aspu", "daap.songpodcasturl",                    DMAP_TYPE_STRING };
static const struct dmap_field dmap_asrv = { "asrv", "daap.songrelativevolume",                DMAP_TYPE_BYTE };
static const struct dmap_field dmap_assa = { "assa", "daap.sortartist",                        DMAP_TYPE_STRING };
static const struct dmap_field dmap_assc = { "assc", "daap.sortcomposer",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_assl = { "assl", "daap.sortalbumartist",                   DMAP_TYPE_STRING };
static const struct dmap_field dmap_assn = { "assn", "daap.sortname",                          DMAP_TYPE_STRING };
static const struct dmap_field dmap_assp = { "assp", "daap.songstoptime",                      DMAP_TYPE_UINT };
static const struct dmap_field dmap_assr = { "assr", "daap.songsamplerate",                    DMAP_TYPE_UINT };
//static const struct dmap_field dmap_asss = { "asss", "daap.sortseriesname",                    DMAP_TYPE_STRING };
static const struct dmap_field dmap_asst = { "asst", "daap.songstarttime",                     DMAP_TYPE_UINT };
static const struct dmap_field dmap_assu = { "assu", "daap.sortalbum",                         DMAP_TYPE_STRING };
static const struct dmap_field dmap_assz = { "assz", "daap.songsize",                          DMAP_TYPE_UINT };
static const struct dmap_field dmap_astc = { "astc", "daap.songtrackcount",                    DMAP_TYPE_USHORT };
static const struct dmap_field dmap_astm = { "astm", "daap.songtime",                          DMAP_TYPE_UINT };
static const struct dmap_field dmap_astn = { "astn", "daap.songtracknumber",                   DMAP_TYPE_USHORT };
static const struct dmap_field dmap_asul = { "asul", "daap.songdataurl",                       DMAP_TYPE_STRING };
static const struct dmap_field dmap_asur = { "asur", "daap.songuserrating",                    DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_asyr = { "asyr", "daap.songyear",                          DMAP_TYPE_USHORT };
//static const struct dmap_field dmap_ated = { "ated", "daap.supportsextradata",                 DMAP_TYPE_USHORT };
static const struct dmap_field dmap_avdb = { "avdb", "daap.serverdatabases",                   DMAP_TYPE_LIST };
//static const struct dmap_field dmap_ceJC = { "ceJC", "com.apple.itunes.jukebox-client-vote",   DMAP_TYPE_BYTE };
//static const struct dmap_field dmap_ceJI = { "ceJI", "com.apple.itunes.jukebox-current",       DMAP_TYPE_UINT };
//static const struct dmap_field dmap_ceJS = { "ceJS", "com.apple.itunes.jukebox-score",         DMAP_TYPE_SHORT };
//static const struct dmap_field dmap_ceJV = { "ceJV", "com.apple.itunes.jukebox-vote",          DMAP_TYPE_UINT };
static const struct dmap_field dmap_mbcl = { "mbcl", "dmap.bag",                               DMAP_TYPE_LIST };
static const struct dmap_field dmap_mccr = { "mccr", "dmap.contentcodesresponse",              DMAP_TYPE_LIST };
static const struct dmap_field dmap_mcna = { "mcna", "dmap.contentcodesname",                  DMAP_TYPE_STRING };
static const struct dmap_field dmap_mcnm = { "mcnm", "dmap.contentcodesnumber",                DMAP_TYPE_UINT };
static const struct dmap_field dmap_mcon = { "mcon", "dmap.container",                         DMAP_TYPE_LIST };
static const struct dmap_field dmap_mctc = { "mctc", "dmap.containercount",                    DMAP_TYPE_UINT };
static const struct dmap_field dmap_mcti = { "mcti", "dmap.containeritemid",                   DMAP_TYPE_UINT };
static const struct dmap_field dmap_mcty = { "mcty", "dmap.contentcodestype",                  DMAP_TYPE_USHORT };
static const struct dmap_field dmap_mdcl = { "mdcl", "dmap.dictionary",                        DMAP_TYPE_LIST };
//static const struct dmap_field dmap_meds = { "meds", "dmap.editcommandssupported",             DMAP_TYPE_UINT };
static const struct dmap_field dmap_miid = { "miid", "dmap.itemid",                            DMAP_TYPE_UINT };
static const struct dmap_field dmap_mikd = { "mikd", "dmap.itemkind",                          DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_mimc = { "mimc", "dmap.itemcount",                         DMAP_TYPE_UINT };
static const struct dmap_field dmap_minm = { "minm", "dmap.itemname",                          DMAP_TYPE_STRING };
static const struct dmap_field dmap_mlcl = { "mlcl", "dmap.listing",                           DMAP_TYPE_LIST };
static const struct dmap_field dmap_mlid = { "mlid", "dmap.sessionid",                         DMAP_TYPE_UINT };
static const struct dmap_field dmap_mlit = { "mlit", "dmap.listingitem",                       DMAP_TYPE_LIST };
static const struct dmap_field dmap_mlog = { "mlog", "dmap.loginresponse",                     DMAP_TYPE_LIST };
static const struct dmap_field dmap_mpco = { "mpco", "dmap.parentcontainerid",                 DMAP_TYPE_UINT };
static const struct dmap_field dmap_mper = { "mper", "dmap.persistentid",                      DMAP_TYPE_ULONG };
static const struct dmap_field dmap_mpro = { "mpro", "dmap.protocolversion",                   DMAP_TYPE_VERSION };
static const struct dmap_field dmap_mrco = { "mrco", "dmap.returnedcount",                     DMAP_TYPE_UINT };
static const struct dmap_field dmap_msal = { "msal", "dmap.supportsautologout",                DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msas = { "msas", "dmap.authenticationschemes",             DMAP_TYPE_UINT };
static const struct dmap_field dmap_msau = { "msau", "dmap.authenticationmethod",              DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msbr = { "msbr", "dmap.supportsbrowse",                    DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msdc = { "msdc", "dmap.databasescount",                    DMAP_TYPE_UINT };
static const struct dmap_field dmap_mshl = { "mshl", "dmap.sortingheaderlisting",              DMAP_TYPE_UINT };
static const struct dmap_field dmap_mshc = { "mshc", "dmap.sortingheaderchar",                 DMAP_TYPE_SHORT };
static const struct dmap_field dmap_mshi = { "mshi", "dmap.sortingheaderindex",                DMAP_TYPE_UINT };
static const struct dmap_field dmap_mshn = { "mshn", "dmap.sortingheadernumber",               DMAP_TYPE_UINT };
static const struct dmap_field dmap_msex = { "msex", "dmap.supportsextensions",                DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msix = { "msix", "dmap.supportsindex",                     DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_mslr = { "mslr", "dmap.loginrequired",                     DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_mspi = { "mspi", "dmap.supportspersistentids",             DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msqy = { "msqy", "dmap.supportsquery",                     DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msrs = { "msrs", "dmap.supportsresolve",                   DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_msrv = { "msrv", "dmap.serverinforesponse",                DMAP_TYPE_LIST };
//static const struct dmap_field dmap_mstc = { "mstc", "dmap.utctime",                           DMAP_TYPE_DATE };
static const struct dmap_field dmap_mstm = { "mstm", "dmap.timeoutinterval",                   DMAP_TYPE_UINT };
//static const struct dmap_field dmap_msto = { "msto", "dmap.utcoffset",                         DMAP_TYPE_INT };
static const struct dmap_field dmap_msts = { "msts", "dmap.statusstring",                      DMAP_TYPE_STRING };
static const struct dmap_field dmap_mstt = { "mstt", "dmap.status",                            DMAP_TYPE_UINT };
static const struct dmap_field dmap_msup = { "msup", "dmap.supportsupdate",                    DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_mtco = { "mtco", "dmap.specifiedtotalcount",               DMAP_TYPE_UINT };
static const struct dmap_field dmap_mudl = { "mudl", "dmap.deletedidlisting",                  DMAP_TYPE_LIST };
static const struct dmap_field dmap_mupd = { "mupd", "dmap.updateresponse",                    DMAP_TYPE_LIST };
static const struct dmap_field dmap_musr = { "musr", "dmap.serverrevision",                    DMAP_TYPE_UINT };
static const struct dmap_field dmap_muty = { "muty", "dmap.updatetype",                        DMAP_TYPE_UBYTE };

static struct dmap_field_map dmap_fields[] =
  {
    { 0, &dmap_miid,
      dbmfi_offsetof(id),                 dbpli_offsetof(id),     -1 },
    { 0, &dmap_minm,
      dbmfi_offsetof(title),              dbpli_offsetof(title), dbgri_offsetof(itemname) },
    { 0, &dmap_mikd,
      dbmfi_offsetof(item_kind),          -1,                    -1 },
    { 0, &dmap_mper,
      dbmfi_offsetof(id),                 dbpli_offsetof(id),    dbgri_offsetof(persistentid) },
    { 0, &dmap_mcon,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mcti,
      dbmfi_offsetof(id),                 -1,                    -1 },
    { 0, &dmap_mpco,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mstt,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msts,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mimc,
      dbmfi_offsetof(total_tracks),       dbpli_offsetof(items), dbgri_offsetof(itemcount) },
    { 0, &dmap_mctc,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mrco,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mtco,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mlcl,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mlit,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mbcl,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mdcl,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msrv,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msau,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mslr,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mpro,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msal,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msup,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mspi,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msex,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msbr,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msqy,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msix,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msrs,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mstm,
      -1,                                 -1,                    -1 },
    { 0, &dmap_msdc,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mlog,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mlid,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mupd,
      -1,                                 -1,                    -1 },
    { 0, &dmap_musr,
      -1,                                 -1,                    -1 },
    { 0, &dmap_muty,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mudl,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mccr,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mcnm,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mcna,
      -1,                                 -1,                    -1 },
    { 0, &dmap_mcty,
      -1,                                 -1,                    -1 },
    { 0, &dmap_apro,
      -1,                                 -1,                    -1 },
    { 0, &dmap_avdb,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abro,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abal,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abar,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abcp,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abgn,
      -1,                                 -1,                    -1 },
    { 0, &dmap_adbs,
      -1,                                 -1,                    -1 },
    { 0, &dmap_asal,
      dbmfi_offsetof(album),              -1,                    -1 },
    { 0, &dmap_asai,
      dbmfi_offsetof(songalbumid),        -1,                    -1 },
    { 0, &dmap_asaa,
      dbmfi_offsetof(album_artist),       -1,                    dbgri_offsetof(songalbumartist) },
    { 0, &dmap_asar,
      dbmfi_offsetof(artist),             -1,                    -1 },
    { 0, &dmap_asbt,
      dbmfi_offsetof(bpm),                -1,                    -1 },
    { 0, &dmap_asbr,
      dbmfi_offsetof(bitrate),            -1,                    -1 },
    { 0, &dmap_ascm,
      dbmfi_offsetof(comment),            -1,                    -1 },
    { 0, &dmap_asco,
      dbmfi_offsetof(compilation),        -1,                    -1 },
    { 0, &dmap_ascp,
      dbmfi_offsetof(composer),           -1,                    -1 },
    { 0, &dmap_asda,
      dbmfi_offsetof(time_added),         -1,                    -1 },
    { 0, &dmap_asdm,
      dbmfi_offsetof(time_modified),      -1,                    -1 },
    { 0, &dmap_asdc,
      dbmfi_offsetof(total_discs),        -1,                    -1 },
    { 0, &dmap_asdn,
      dbmfi_offsetof(disc),               -1,                    -1 },
    { 0, &dmap_asdb,
      dbmfi_offsetof(disabled),           -1,                    -1 },
    { 0, &dmap_aseq,
      -1,                                 -1,                    -1 },
    { 0, &dmap_asfm,
      dbmfi_offsetof(type),               -1,                    -1 },
    { 0, &dmap_asgn,
      dbmfi_offsetof(genre),              -1,                    -1 },
    { 0, &dmap_asdt,
      dbmfi_offsetof(description),        -1,                    -1 },
    { 0, &dmap_asrv,
      -1,                                 -1,                    -1 },
    { 0, &dmap_assr,
      dbmfi_offsetof(samplerate),         -1,                    -1 },
    { 0, &dmap_assz,
      dbmfi_offsetof(file_size),          -1,                    -1 },
    { 0, &dmap_asst,
      -1,                                 -1,                    -1 },
    { 0, &dmap_assp,
      -1,                                 -1,                    -1 },
    { 0, &dmap_astm,
      dbmfi_offsetof(song_length),        -1,                    -1 },
    { 0, &dmap_astc,
      dbmfi_offsetof(total_tracks),       -1,                    -1 },
    { 0, &dmap_astn,
      dbmfi_offsetof(track),              -1,                    -1 },
    { 0, &dmap_asur,
      dbmfi_offsetof(rating),             -1,                    -1 },
    { 0, &dmap_asyr,
      dbmfi_offsetof(year),               -1,                    -1 },
    { 0, &dmap_asdk,
      dbmfi_offsetof(data_kind),          -1,                    -1 },
    { 0, &dmap_asul,
      dbmfi_offsetof(url),                -1,                    -1 },
    { 0, &dmap_aply,
      -1,                                 -1,                    -1 },
    { 0, &dmap_abpl,
      -1,                                 -1,                    -1 },
    { 0, &dmap_apso,
      -1,                                 -1,                    -1 },
    { 0, &dmap_arsv,
      -1,                                 -1,                    -1 },
    { 0, &dmap_arif,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeNV,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeSP,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aePS,
      -1,                                 -1,                    -1 },

    /* iTunes 4.5+ */
    { 0, &dmap_ascd,
      dbmfi_offsetof(codectype),          -1,                    -1 },
    { 0, &dmap_ascs,
      -1,                                 -1,                    -1 },
    { 0, &dmap_agrp,
      dbmfi_offsetof(grouping),           -1,                    -1 },
    { 0, &dmap_aeSV,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aePI,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeCI,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeGI,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeAI,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeSI,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeSF,
      -1,                                 -1,                    -1 },

    /* iTunes 5.0+ */
    { 0, &dmap_ascr,
      dbmfi_offsetof(contentrating),      -1,                    -1 },
#if 0
    { 0, DMAP_TYPE_BYTE,    "f" "\x8d" "ch", "dmap.haschildcontainers",
      -1,                                 -1,                    -1 },
#endif

    /* iTunes 6.0.2+ */
    { 0, &dmap_aeHV,
      dbmfi_offsetof(has_video),          -1,                    -1 },

    /* iTunes 6.0.4+ */
    { 0, &dmap_msas,
      -1,                                 -1,                    -1 },
    { 0, &dmap_asct,
      -1,                                 -1,                    -1 },
    { 0, &dmap_ascn,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aslc,
      -1,                                 -1,                    -1 },
    { 0, &dmap_asky,
      -1,                                 -1,                    -1 },
    { 0, &dmap_apsm,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aprm,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aePC,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aePP,
      -1,                                 -1,                    -1 },
    { 0, &dmap_aeMK,
      dbmfi_offsetof(media_kind),         -1,                    -1 },
    { 0, &dmap_aeMk,
      dbmfi_offsetof(media_kind),         -1,                    -1 },
    { 0, &dmap_aeSN,
      dbmfi_offsetof(tv_series_name),     -1,                    -1 },
    { 0, &dmap_aeNN,
      dbmfi_offsetof(tv_network_name),    -1,                    -1 },
    { 0, &dmap_aeEN,
      dbmfi_offsetof(tv_episode_num_str), -1,                    -1 },
    { 0, &dmap_aeES,
      dbmfi_offsetof(tv_episode_sort),    -1,                    -1 },
    { 0, &dmap_aeSU,
      dbmfi_offsetof(tv_season_num),      -1,                    -1 },

    { 0, &dmap_assn,
      dbmfi_offsetof(title_sort),     	  -1,                    -1 },
    { 0, &dmap_assa,
      dbmfi_offsetof(artist_sort),        -1,                    -1 },
    { 0, &dmap_assu,
      dbmfi_offsetof(album_sort),         -1,                    -1 },
    { 0, &dmap_assc,
      dbmfi_offsetof(composer_sort),      -1,                    -1 },
    { 0, &dmap_assl,
      dbmfi_offsetof(album_artist_sort),  -1,                    -1 },

    { 0, NULL,
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

static void
daap_session_free(void *item)
{
  struct daap_session *s;

  s = (struct daap_session *)item;

  evtimer_del(&s->timeout);
  free(s);
}

static void
daap_session_kill(struct daap_session *s)
{
  avl_delete(daap_sessions, s);
}

static void
daap_session_timeout_cb(int fd, short what, void *arg)
{
  struct daap_session *s;

  s = (struct daap_session *)arg;

  DPRINTF(E_DBG, L_DAAP, "Session %d timed out\n", s->id);

  daap_session_kill(s);
}

static struct daap_session *
daap_session_register(void)
{
#if 0
  struct timeval tv;
#endif
  struct daap_session *s;
  avl_node_t *node;
#if 0
  int ret;
#endif

  s = (struct daap_session *)malloc(sizeof(struct daap_session));
  if (!s)
    {
      DPRINTF(E_LOG, L_DAAP, "Out of memory for DAAP session\n");
      return NULL;
    }

  memset(s, 0, sizeof(struct daap_session));

  s->id = next_session_id;

  next_session_id++;

  evtimer_set(&s->timeout, daap_session_timeout_cb, s);
  event_base_set(evbase_httpd, &s->timeout);

  node = avl_insert(daap_sessions, s);
  if (!node)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not register DAAP session: %s\n", strerror(errno));

      free(s);
      return NULL;
    }

#if 0
  evutil_timerclear(&tv);
  tv.tv_sec = DAAP_SESSION_TIMEOUT;

  ret = evtimer_add(&s->timeout, &tv);
  if (ret < 0)
    DPRINTF(E_LOG, L_DAAP, "Could not add session timeout event for session %d\n", s->id);
#endif /* 0 */

  return s;
}

struct daap_session *
daap_session_find(struct evhttp_request *req, struct evkeyvalq *query, struct evbuffer *evbuf)
{
  struct daap_session needle;
#if 0
  struct timeval tv;
#endif
  struct daap_session *s;
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

  s = (struct daap_session *)node->item;

#if 0
  event_del(&s->timeout);

  evutil_timerclear(&tv);
  tv.tv_sec = DAAP_SESSION_TIMEOUT;

  ret = evtimer_add(&s->timeout, &tv);
  if (ret < 0)
    DPRINTF(E_LOG, L_DAAP, "Could not add session timeout event for session %d\n", s->id);
#endif /* 0 */

  return s;

 invalid:
  evhttp_send_error(req, 403, "Forbidden");
  return NULL;
}


/* Update requests helpers */
static void
update_fail_cb(struct evhttp_connection *evcon, void *arg)
{
  struct daap_update_request *ur;
  struct daap_update_request *p;

  ur = (struct daap_update_request *)arg;

  DPRINTF(E_DBG, L_DAAP, "Update request: client closed connection\n");

  if (ur->req->evcon)
    evhttp_connection_set_closecb(ur->req->evcon, NULL, NULL);

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
dmap_add_field(struct evbuffer *evbuf, const struct dmap_field *df, char *strval, int32_t intval)
{
  union {
    int32_t v_i32;
    uint32_t v_u32;
    int64_t v_i64;
    uint64_t v_u64;
  } val;
  int ret;

  if (strval && (df->type != DMAP_TYPE_STRING))
    {
      switch (df->type)
	{
	  case DMAP_TYPE_DATE:
	  case DMAP_TYPE_UBYTE:
	  case DMAP_TYPE_USHORT:
	  case DMAP_TYPE_UINT:
	    ret = safe_atou32(strval, &val.v_u32);
	    if (ret < 0)
	      val.v_u32 = 0;
	    break;

	  case DMAP_TYPE_BYTE:
	  case DMAP_TYPE_SHORT:
	  case DMAP_TYPE_INT:
	    ret = safe_atoi32(strval, &val.v_i32);
	    if (ret < 0)
	      val.v_i32 = 0;
	    break;

	  case DMAP_TYPE_ULONG:
	    ret = safe_atou64(strval, &val.v_u64);
	    if (ret < 0)
	      val.v_u64 = 0;
	    break;

	  case DMAP_TYPE_LONG:
	    ret = safe_atoi64(strval, &val.v_i64);
	    if (ret < 0)
	      val.v_i64 = 0;
	    break;

	  /* DMAP_TYPE_VERSION & DMAP_TYPE_LIST not handled here */
	  default:
	    DPRINTF(E_LOG, L_DAAP, "Unsupported DMAP type %d for DMAP field %s\n", df->type, df->desc);
	    return;
	}
    }
  else if (!strval && (df->type != DMAP_TYPE_STRING))
    {
      switch (df->type)
	{
	  case DMAP_TYPE_DATE:
	  case DMAP_TYPE_UBYTE:
	  case DMAP_TYPE_USHORT:
	  case DMAP_TYPE_UINT:
	    val.v_u32 = intval;
	    break;

	  case DMAP_TYPE_BYTE:
	  case DMAP_TYPE_SHORT:
	  case DMAP_TYPE_INT:
	    val.v_i32 = intval;
	    break;

	  case DMAP_TYPE_ULONG:
	    val.v_u64 = intval;
	    break;

	  case DMAP_TYPE_LONG:
	    val.v_i64 = intval;
	    break;

	  /* DMAP_TYPE_VERSION & DMAP_TYPE_LIST not handled here */
	  default:
	    DPRINTF(E_LOG, L_DAAP, "Unsupported DMAP type %d for DMAP field %s\n", df->type, df->desc);
	    return;
	}
    }

  switch (df->type)
    {
      case DMAP_TYPE_UBYTE:
	if (val.v_u32)
	  dmap_add_char(evbuf, df->tag, val.v_u32);
	break;

      case DMAP_TYPE_BYTE:
	if (val.v_i32)
	  dmap_add_char(evbuf, df->tag, val.v_i32);
	break;

      case DMAP_TYPE_USHORT:
	if (val.v_u32)
	  dmap_add_short(evbuf, df->tag, val.v_u32);
	break;

      case DMAP_TYPE_SHORT:
	if (val.v_i32)
	  dmap_add_short(evbuf, df->tag, val.v_i32);
	break;

      case DMAP_TYPE_DATE:
      case DMAP_TYPE_UINT:
	if (val.v_u32)
	  dmap_add_int(evbuf, df->tag, val.v_u32);
	break;

      case DMAP_TYPE_INT:
	if (val.v_i32)
	  dmap_add_int(evbuf, df->tag, val.v_i32);
	break;

      case DMAP_TYPE_ULONG:
	if (val.v_u64)
	  dmap_add_long(evbuf, df->tag, val.v_u64);
	break;

      case DMAP_TYPE_LONG:
	if (val.v_i64)
	  dmap_add_long(evbuf, df->tag, val.v_i64);
	break;

      case DMAP_TYPE_STRING:
	if (strval)
	  dmap_add_string(evbuf, df->tag, strval);
	break;

      case DMAP_TYPE_VERSION:
      case DMAP_TYPE_LIST:
	return;
    }
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
  ret = u8_normalize(UNINORM_NFD, (uint8_t *)str, len, NULL, &len);
  if (!ret)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not normalize string for sort header\n");

      return -1;
    }

  fl = ret[0];
  free(ret);

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

static int
daap_sort_finalize(struct sort_ctx *ctx, struct evbuffer *evbuf)
{
  int ret;

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

  dmap_add_container(evbuf, "mshl", EVBUFFER_LENGTH(ctx->headerlist));
  ret = evbuffer_add_buffer(evbuf, ctx->headerlist);
  if (ret < 0)
    return -1;

  return 0;
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
  int mpro;
  int apro;
  int len;
  int ret;

  lib = cfg_getsec(cfg, "library");
  passwd = cfg_getstr(lib, "password");
  name = cfg_getstr(lib, "name");

  len = 136 + strlen(name);

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
  dmap_add_string(evbuf, "minm", name); /* 8 + strlen(name) */

#if 0
  dmap_add_int(evbuf, "mstm", DAAP_SESSION_TIMEOUT); /* 12 */
  dmap_add_char(evbuf, "msal", 1);   /* 9 */
#endif

  dmap_add_char(evbuf, "mslr", 1);   /* 9 */
  dmap_add_char(evbuf, "msau", (passwd) ? 2 : 0); /* 9 */
  dmap_add_char(evbuf, "msex", 1);   /* 9 */
  dmap_add_char(evbuf, "msix", 1);   /* 9 */
  dmap_add_char(evbuf, "msbr", 1);   /* 9 */
  dmap_add_char(evbuf, "msqy", 1);   /* 9 */

  dmap_add_char(evbuf, "mspi", 1);   /* 9 */
  dmap_add_int(evbuf, "msdc", 1);    /* 12 */

  /* Advertise updates support even though we don't send updates */
  dmap_add_char(evbuf, "msup", 1);   /* 9 */

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_content_codes(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  const struct dmap_field *df;
  int i;
  int len;
  int ret;

  len = 12;
  for (i = 0; dmap_fields[i].field; i++)
    len += 8 + 12 + 10 + 8 + strlen(dmap_fields[i].field->desc);

  ret = evbuffer_expand(evbuf, len + 8);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not expand evbuffer for DAAP content-codes reply\n");

      dmap_send_error(req, "mccr", "Out of memory");
      return;
    } 

  dmap_add_container(evbuf, "mccr", len);
  dmap_add_int(evbuf, "mstt", 200);

  for (i = 0; dmap_fields[i].field; i++)
    {
      df = dmap_fields[i].field;

      len = 12 + 10 + 8 + strlen(df->desc);

      dmap_add_container(evbuf, "mdcl", len);
      dmap_add_string(evbuf, "mcnm", df->tag);  /* 12 */
      dmap_add_string(evbuf, "mcna", df->desc); /* 8 + strlen(desc) */
      dmap_add_short(evbuf, "mcty", df->type);  /* 10 */
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);
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

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_logout(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  struct daap_session *s;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  daap_session_kill(s);

  httpd_send_reply(req, 204, "Logout Successful", evbuf);
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

      httpd_send_reply(req, HTTP_OK, "OK", evbuf);

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

  /* If the connection fails before we have an update to push out
   * to the client, we need to know.
   */
  evhttp_connection_set_closecb(req->evcon, update_fail_cb, ur);
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

  lib = cfg_getsec(cfg, "library");
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

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);
}

static void
daap_reply_songlist_generic(struct evhttp_request *req, struct evbuffer *evbuf, int playlist, struct evkeyvalq *query)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct evbuffer *song;
  struct evbuffer *songlist;
  struct dmap_field_map *dfm;
  struct sort_ctx *sctx;
  const char *param;
  char *tag;
  char **strval;
  char *ptr;
  uint32_t *meta;
  int nmeta;
  int sort_headers;
  int nsongs;
  int transcode;
  int want_mikd;
  int want_asdk;
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
  get_query_params(query, &sort_headers, &qp);

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

  want_mikd = 0;
  want_asdk = 0;
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
		  DPRINTF(E_WARN, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
		  continue;
		}
	    }
	  /* No specific meta tags requested, send out everything */
	  else
	    {
	      /* End of list */
	      if (!dmap_fields[i].field)
		break;

	      dfm = &dmap_fields[i];
	    }

	  /* Not in struct media_file_info */
	  if (dfm->mfi_offset < 0)
	    continue;

	  /* Will be prepended to the list */
	  if (dfm->field == &dmap_mikd)
	    {
	      /* item kind */
	      want_mikd = 1;
	      continue;
	    }
	  else if (dfm->field == &dmap_asdk)
	    {
	      /* data kind */
	      want_asdk = 1;
	      continue;
	    }

	  DPRINTF(E_DBG, L_DAAP, "Investigating %s\n", dfm->field->desc);

	  strval = (char **) ((char *)&dbmfi + dfm->mfi_offset);

	  if (!(*strval) || (**strval == '\0'))
            continue;

	  /* Here's one exception ... codectype (ascd) is actually an integer */
	  if (dfm->field == &dmap_ascd)
	    {
	      dmap_add_literal(song, dfm->field->tag, *strval, 4);
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

	  dmap_add_field(song, dfm->field, *strval, val);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->field->desc, *strval);
	}

      /* Always include sort tags */
      dmap_add_string(song, "assn", dbmfi.title_sort);
      dmap_add_string(song, "assa", dbmfi.artist_sort);
      dmap_add_string(song, "assu", dbmfi.album_sort);
      dmap_add_string(song, "assl", dbmfi.album_artist_sort);

      if (dbmfi.composer_sort)
	dmap_add_string(song, "assc", dbmfi.composer_sort);

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
	  DPRINTF(E_LOG, L_DAAP, "Could not add song to song list for DAAP song list reply\n");

	  ret = -100;
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
  if (sort_headers)
    dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(songlist) + EVBUFFER_LENGTH(sctx->headerlist) + 53);
  else
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

      if (sort_headers)
	daap_sort_context_free(sctx);

      return;
    }

  if (sort_headers)
    {
      ret = daap_sort_finalize(sctx, evbuf);
      daap_sort_context_free(sctx);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP song list reply\n");

	  dmap_send_error(req, tag, "Out of memory");
	  return;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

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
  get_query_params(query, NULL, &qp);
  qp.type = Q_PL;

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
      npls++;

      for (i = 0; i < nmeta; i++)
	{
	  dfm = dmap_find_field(meta[i]);
	  if (!dfm)
	    {
	      DPRINTF(E_WARN, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
	      continue;
	    }

	  /* dmap.itemcount - always added */
	  if (dfm->field == &dmap_mimc)
	    continue;

	  /* com.apple.itunes.smart-playlist - type = 1 AND id != 1 */
	  if (dfm->field == &dmap_aeSP)
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

	  /* Not in struct playlist_info */
	  if (dfm->pli_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbpli + dfm->pli_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(playlist, dfm->field, *strval, 0);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->field->desc, *strval);
	}

      /* Item count (mimc) */
      val = 0;
      ret = safe_atoi32(dbpli.items, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(playlist, "mimc", val);

      /* Container ID (mpco) */
      dmap_add_int(playlist, "mpco", 0);

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

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

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
  struct sort_ctx *sctx;
  const char *param;
  char **strval;
  uint32_t *meta;
  int nmeta;
  int sort_headers;
  int ngrp;
  int32_t val;
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
  get_query_params(query, &sort_headers, &qp);
  qp.type = Q_GROUPS;

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
      ngrp++;

      for (i = 0; i < nmeta; i++)
	{
	  dfm = dmap_find_field(meta[i]);
	  if (!dfm)
	    {
	      DPRINTF(E_WARN, L_DAAP, "Could not find requested meta field (%d)\n", i + 1);
	      continue;
	    }

	  /* dmap.itemcount - always added */
	  if (dfm->field == &dmap_mimc)
	    continue;

	  /* Not in struct group_info */
	  if (dfm->gri_offset < 0)
	    continue;

          strval = (char **) ((char *)&dbgri + dfm->gri_offset);

          if (!(*strval) || (**strval == '\0'))
            continue;

	  dmap_add_field(group, dfm->field, *strval, 0);

	  DPRINTF(E_DBG, L_DAAP, "Done with meta tag %s (%s)\n", dfm->field->desc, *strval);
	}

      if (sort_headers)
	{
	  ret = daap_sort_build(sctx, dbgri.itemname);
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

      /* Song album artist, always added (asaa) */
      dmap_add_string(group, "asaa", dbgri.songalbumartist);

      /* Item id (miid) */
      val = 0;
      ret = safe_atoi32(dbgri.id, &val);
      if ((ret == 0) && (val > 0))
	dmap_add_int(group, "miid", val);

      DPRINTF(E_DBG, L_DAAP, "Done with group\n");

      dmap_add_container(grouplist, "mlit", EVBUFFER_LENGTH(group));
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

  /* Add header to evbuf, add grouplist to evbuf */
  if (sort_headers)
    dmap_add_container(evbuf, tag, EVBUFFER_LENGTH(grouplist) + EVBUFFER_LENGTH(sctx->headerlist) + 53);
  else
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

      if (sort_headers)
	daap_sort_context_free(sctx);

      return;
    }

  if (sort_headers)
    {
      ret = daap_sort_finalize(sctx, evbuf);
      daap_sort_context_free(sctx);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP browse reply\n");

	  dmap_send_error(req, tag, "Out of memory");
	  return;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);

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
  struct sort_ctx *sctx;
  char *browse_item;
  char *sort_item;
  char *tag;
  int sort_headers;
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

  get_query_params(query, &sort_headers, &qp);

  sctx = NULL;
  if (sort_headers)
    {
      sctx = daap_sort_context_new();
      if (!sctx)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not create sort context\n");

	  dmap_send_error(req, "abro", "Out of memory");

	  evbuffer_free(itemlist);
	  if (qp.filter)
	    free(qp.filter);
	  return;
	}
    }

  ret = db_query_start(&qp);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not start query\n");

      dmap_send_error(req, "abro", "Could not start query");

      if (sort_headers)
	daap_sort_context_free(sctx);

      evbuffer_free(itemlist);
      if (qp.filter)
	free(qp.filter);
      return;
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

  if (qp.filter)
    free(qp.filter);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Error fetching/building results\n");

      dmap_send_error(req, "abro", "Error fetching/building query results");
      db_query_end(&qp);

      if (sort_headers)
	daap_sort_context_free(sctx);

      evbuffer_free(itemlist);
      return;
    }

  if (sort_headers)
    dmap_add_container(evbuf, "abro", EVBUFFER_LENGTH(itemlist) + EVBUFFER_LENGTH(sctx->headerlist) + 44);
  else
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

      if (sort_headers)
	daap_sort_context_free(sctx);

      return;
    }

  if (sort_headers)
    {
      ret = daap_sort_finalize(sctx, evbuf);
      daap_sort_context_free(sctx);

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_DAAP, "Could not add sort headers to DAAP browse reply\n");

	  dmap_send_error(req, tag, "Out of memory");
	  return;
	}
    }

  httpd_send_reply(req, HTTP_OK, "OK", evbuf);
}

/* NOTE: We only handle artwork at the moment */
static void
daap_reply_extra_data(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  char clen[32];
  struct daap_session *s;
  const char *param;
  int id;
  int max_w;
  int max_h;
  int ret;

  s = daap_session_find(req, query, evbuf);
  if (!s)
    return;

  ret = safe_atoi32(uri[3], &id);
  if (ret < 0)
    {
      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  param = evhttp_find_header(query, "mw");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "Request for artwork without mw parameter\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  ret = safe_atoi32(param, &max_w);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not convert mw parameter to integer\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  param = evhttp_find_header(query, "mh");
  if (!param)
    {
      DPRINTF(E_LOG, L_DAAP, "Request for artwork without mh parameter\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  ret = safe_atoi32(param, &max_h);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not convert mh parameter to integer\n");

      evhttp_send_error(req, HTTP_BADREQUEST, "Bad Request");
      return;
    }

  if (strcmp(uri[2], "groups") == 0)
    ret = artwork_get_group(id, max_w, max_h, evbuf);
  else if (strcmp(uri[2], "items") == 0)
    ret = artwork_get_item(id, max_w, max_h, evbuf);

  if (ret < 0)
    {
      if (EVBUFFER_LENGTH(evbuf) > 0)
	evbuffer_drain(evbuf, EVBUFFER_LENGTH(evbuf));

      goto no_artwork;
    }

  evhttp_remove_header(req->output_headers, "Content-Type");
  evhttp_add_header(req->output_headers, "Content-Type", "image/png");
  snprintf(clen, sizeof(clen), "%ld", (long)EVBUFFER_LENGTH(evbuf));
  evhttp_add_header(req->output_headers, "Content-Length", clen);

  /* No gzip compression for artwork */
  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
  return;

 no_artwork:
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


#ifdef DMAP_TEST
static const struct dmap_field dmap_TEST = { "TEST", "test.container", DMAP_TYPE_LIST };
static const struct dmap_field dmap_TST1 = { "TST1", "test.ubyte",     DMAP_TYPE_UBYTE };
static const struct dmap_field dmap_TST2 = { "TST2", "test.byte",      DMAP_TYPE_BYTE };
static const struct dmap_field dmap_TST3 = { "TST3", "test.ushort",    DMAP_TYPE_USHORT };
static const struct dmap_field dmap_TST4 = { "TST4", "test.short",     DMAP_TYPE_SHORT };
static const struct dmap_field dmap_TST5 = { "TST5", "test.uint",      DMAP_TYPE_UINT };
static const struct dmap_field dmap_TST6 = { "TST6", "test.int",       DMAP_TYPE_INT };
static const struct dmap_field dmap_TST7 = { "TST7", "test.ulong",     DMAP_TYPE_ULONG };
static const struct dmap_field dmap_TST8 = { "TST8", "test.long",      DMAP_TYPE_LONG };
static const struct dmap_field dmap_TST9 = { "TST9", "test.string",    DMAP_TYPE_STRING };

static void
daap_reply_dmap_test(struct evhttp_request *req, struct evbuffer *evbuf, char **uri, struct evkeyvalq *query)
{
  char buf[64];
  struct evbuffer *test;
  int ret;

  test = evbuffer_new();
  if (!test)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not create evbuffer for DMAP test\n");

      dmap_send_error(req, dmap_TEST.tag, "Out of memory");
      return;
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

  dmap_add_container(evbuf, dmap_TEST.tag, EVBUFFER_LENGTH(test));

  ret = evbuffer_add_buffer(evbuf, test);
  evbuffer_free(test);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_DAAP, "Could not add test results to DMAP test reply\n");

      dmap_send_error(req, dmap_TEST.tag, "Out of memory");
      return;      
    }

  evhttp_send_reply(req, HTTP_OK, "OK", evbuf);
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
      free(full_uri);
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

#ifdef DMAP_TEST
  if (strcmp(uri, "/dmap-test") == 0)
    return 1;
#endif

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

  daap_sessions = avl_alloc_tree(daap_session_compare, daap_session_free);
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

  for (i = 0; dmap_fields[i].field; i++)
    {
      dmap_fields[i].hash = djb_hash(dmap_fields[i].field->desc, strlen(dmap_fields[i].field->desc));

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
	      DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dmap_fields[i].hash, dmap_fields[i].field->desc);

	      DPRINTF(E_FATAL, L_DAAP, "Hash %x, string %s\n", dfm->hash, dfm->field->desc);
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

      if (ur->req->evcon)
	{
	  evhttp_connection_set_closecb(ur->req->evcon, NULL, NULL);
	  evhttp_connection_free(ur->req->evcon);
	}

      free(ur);
    }
}
