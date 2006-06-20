/*
 * $Id$
 * Generic db implementation for specific sql backend
 *
 * Copyright (C) 2005 Ron Pedde (ron@pedde.com)
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
#  include "config.h"
#endif

#define _XOPEN_SOURCE 500  /** unix98?  pthread_once_t, etc */

#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_STRING_H
# include <string.h>
#endif
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#include "db-generic.h"
#include "err.h"
#include "mp3-scanner.h"

#include "db-sql.h"

#define DB_VERSION 1
#define MAYBEFREE(a) { if((a)) free((a)); };

/** pointers to database-specific functions */
typedef struct tag_db_functions {
    char *name;
    int(*dbs_open)(char **, char *);
    int(*dbs_init)(int*);
    int(*dbs_deinit)(void);
    int(*dbs_add)(char **, MP3FILE*, int*);
    int(*dbs_add_playlist)(char **, char *, int, char *,char *, int, int *);
    int(*dbs_add_playlist_item)(char **, int, int);
    int(*dbs_delete_playlist)(char **, int);
    int(*dbs_delete_playlist_item)(char **, int, int);
    int(*dbs_edit_playlist)(char **, int, char*, char*);
    int(*dbs_playcount_increment)(char **, int);
    int(*dbs_enum_start)(char **, DBQUERYINFO *);
    int(*dbs_enum_size)(char **, DBQUERYINFO *, int *, int *);
    int(*dbs_enum_fetch)(char **, DBQUERYINFO *, int *, unsigned char **);
    int(*dbs_enum_fetch_row)(char **, PACKED_MP3FILE *, DBQUERYINFO *);    
    int(*dbs_enum_reset)(char **, DBQUERYINFO *);
    int(*dbs_enum_end)(char **);
    int(*dbs_force_rescan)(char **);
    int(*dbs_start_scan)(void);
    int(*dbs_end_song_scan)(void);
    int(*dbs_end_scan)(void);
    int(*dbs_get_count)(char **, int *, CountType_t);
    MP3FILE*(*dbs_fetch_item)(char **, int);
    MP3FILE*(*dbs_fetch_path)(char **, char *,int);
    M3UFILE*(*dbs_fetch_playlist)(char **, char *, int);
    void(*dbs_dispose_item)(MP3FILE*);
    void(*dbs_dispose_playlist)(M3UFILE*);
}DB_FUNCTIONS;

/** All supported backend databases, and pointers to the db specific implementations */
DB_FUNCTIONS db_functions[] = {
#ifdef HAVE_LIBSQLITE
    {
        "sqlite",
        db_sql_open_sqlite2,
        db_sql_init,
        db_sql_deinit,
        db_sql_add,
        db_sql_add_playlist,
        db_sql_add_playlist_item,
        db_sql_delete_playlist,
        db_sql_delete_playlist_item,
        db_sql_edit_playlist,
        db_sql_playcount_increment,
        db_sql_enum_start,
        db_sql_enum_size,
        db_sql_enum_fetch,
        db_sql_enum_fetch_row,
        db_sql_enum_reset,
        db_sql_enum_end,
        db_sql_force_rescan,
        db_sql_start_scan,
        db_sql_end_song_scan,
        db_sql_end_scan,
        db_sql_get_count,
        db_sql_fetch_item,
        db_sql_fetch_path,
        db_sql_fetch_playlist,
        db_sql_dispose_item,
        db_sql_dispose_playlist
    },
#endif
#ifdef HAVE_LIBSQLITE3
    {
        "sqlite3",
        db_sql_open_sqlite3,
        db_sql_init,
        db_sql_deinit,
        db_sql_add,
        db_sql_add_playlist,
        db_sql_add_playlist_item,
        db_sql_delete_playlist,
        db_sql_delete_playlist_item,
        db_sql_edit_playlist,
        db_sql_playcount_increment,
        db_sql_enum_start,
        db_sql_enum_size,
        db_sql_enum_fetch,
        db_sql_enum_fetch_row,
        db_sql_enum_reset,
        db_sql_enum_end,
        db_sql_force_rescan,
        db_sql_start_scan,
        db_sql_end_song_scan,
        db_sql_end_scan,
        db_sql_get_count,
        db_sql_fetch_item,
        db_sql_fetch_path,
        db_sql_fetch_playlist,
        db_sql_dispose_item,
        db_sql_dispose_playlist
    },
#endif
    { NULL,NULL }
};

DAAP_ITEMS taglist[] = {
    { 0x05, "miid", "dmap.itemid" },
    { 0x09, "minm", "dmap.itemname" },
    { 0x01, "mikd", "dmap.itemkind" },
    { 0x07, "mper", "dmap.persistentid" },
    { 0x0C, "mcon", "dmap.container" },
    { 0x05, "mcti", "dmap.containeritemid" },
    { 0x05, "mpco", "dmap.parentcontainerid" },
    { 0x05, "mstt", "dmap.status" },
    { 0x09, "msts", "dmap.statusstring" },
    { 0x05, "mimc", "dmap.itemcount" },
    { 0x05, "mctc", "dmap.containercount" },
    { 0x05, "mrco", "dmap.returnedcount" },
    { 0x05, "mtco", "dmap.specifiedtotalcount" },
    { 0x0C, "mlcl", "dmap.listing" },
    { 0x0C, "mlit", "dmap.listingitem" },
    { 0x0C, "mbcl", "dmap.bag" },
    { 0x0C, "mdcl", "dmap.dictionary" },
    { 0x0C, "msrv", "dmap.serverinforesponse" },
    { 0x01, "msau", "dmap.authenticationmethod" },
    { 0x01, "mslr", "dmap.loginrequired" },
    { 0x0B, "mpro", "dmap.protocolversion" },
    { 0x01, "msal", "dmap.supportsautologout" },
    { 0x01, "msup", "dmap.supportsupdate" },
    { 0x01, "mspi", "dmap.supportspersistentids" },
    { 0x01, "msex", "dmap.supportsextensions" },
    { 0x01, "msbr", "dmap.supportsbrowse" },
    { 0x01, "msqy", "dmap.supportsquery" },
    { 0x01, "msix", "dmap.supportsindex" },
    { 0x01, "msrs", "dmap.supportsresolve" },
    { 0x05, "mstm", "dmap.timeoutinterval" },
    { 0x05, "msdc", "dmap.databasescount" },
    { 0x0C, "mlog", "dmap.loginresponse" },
    { 0x05, "mlid", "dmap.sessionid" },
    { 0x0C, "mupd", "dmap.updateresponse" },
    { 0x05, "musr", "dmap.serverrevision" },
    { 0x01, "muty", "dmap.updatetype" },
    { 0x0C, "mudl", "dmap.deletedidlisting" },
    { 0x0C, "mccr", "dmap.contentcodesresponse" },
    { 0x05, "mcnm", "dmap.contentcodesnumber" },
    { 0x09, "mcna", "dmap.contentcodesname" },
    { 0x03, "mcty", "dmap.contentcodestype" },
    { 0x0B, "apro", "daap.protocolversion" },
    { 0x0C, "avdb", "daap.serverdatabases" },
    { 0x0C, "abro", "daap.databasebrowse" },
    { 0x0C, "abal", "daap.browsealbumlisting" },
    { 0x0C, "abar", "daap.browseartistlisting" },
    { 0x0C, "abcp", "daap.browsecomposerlisting" },
    { 0x0C, "abgn", "daap.browsegenrelisting" },
    { 0x0C, "adbs", "daap.databasesongs" },
    { 0x09, "asal", "daap.songalbum" },
    { 0x09, "asar", "daap.songartist" },
    { 0x03, "asbt", "daap.songbeatsperminute" },
    { 0x03, "asbr", "daap.songbitrate" },
    { 0x09, "ascm", "daap.songcomment" },
    { 0x01, "asco", "daap.songcompilation" },
    { 0x09, "ascp", "daap.songcomposer" },
    { 0x0A, "asda", "daap.songdateadded" },
    { 0x0A, "asdm", "daap.songdatemodified" },
    { 0x03, "asdc", "daap.songdisccount" },
    { 0x03, "asdn", "daap.songdiscnumber" },
    { 0x01, "asdb", "daap.songdisabled" },
    { 0x09, "aseq", "daap.songeqpreset" },
    { 0x09, "asfm", "daap.songformat" },
    { 0x09, "asgn", "daap.songgenre" },
    { 0x09, "asdt", "daap.songdescription" },
    { 0x02, "asrv", "daap.songrelativevolume" },
    { 0x05, "assr", "daap.songsamplerate" },
    { 0x05, "assz", "daap.songsize" },
    { 0x05, "asst", "daap.songstarttime" },
    { 0x05, "assp", "daap.songstoptime" },
    { 0x05, "astm", "daap.songtime" },
    { 0x03, "astc", "daap.songtrackcount" },
    { 0x03, "astn", "daap.songtracknumber" },
    { 0x01, "asur", "daap.songuserrating" },
    { 0x03, "asyr", "daap.songyear" },
    { 0x01, "asdk", "daap.songdatakind" },
    { 0x09, "asul", "daap.songdataurl" },
    { 0x0C, "aply", "daap.databaseplaylists" },
    { 0x01, "abpl", "daap.baseplaylist" },
    { 0x0C, "apso", "daap.playlistsongs" },
    { 0x0C, "arsv", "daap.resolve" },
    { 0x0C, "arif", "daap.resolveinfo" },
    { 0x05, "aeNV", "com.apple.itunes.norm-volume" },
    { 0x01, "aeSP", "com.apple.itunes.smart-playlist" },

    /* iTunes 4.5+ */
    { 0x01, "msas", "dmap.authenticationschemes" },
    { 0x05, "ascd", "daap.songcodectype" },
    { 0x05, "ascs", "daap.songcodecsubtype" },
    { 0x09, "agrp", "daap.songgrouping" },
    { 0x05, "aeSV", "com.apple.itunes.music-sharing-version" },
    { 0x05, "aePI", "com.apple.itunes.itms-playlistid" },
    { 0x05, "aeCI", "com.apple.iTunes.itms-composerid" },
    { 0x05, "aeGI", "com.apple.iTunes.itms-genreid" },
    { 0x05, "aeAI", "com.apple.iTunes.itms-artistid" },
    { 0x05, "aeSI", "com.apple.iTunes.itms-songid" },
    { 0x05, "aeSF", "com.apple.iTunes.itms-storefrontid" },

    /* iTunes 5.0+ */
    { 0x01, "ascr", "daap.songcontentrating" },
    { 0x01, "f" "\x8d" "ch", "dmap.haschildcontainers" },

    /* iTunes 6.0.2+ */
    { 0x01, "aeHV", "com.apple.itunes.has-video" },

    /* iTunes 6.0.4+ */
    { 0x05, "msas", "dmap.authenticationschemes" },
    { 0x09, "asct", "daap.songcategory" },
    { 0x09, "ascn", "daap.songcontentdescription" },
    { 0x09, "aslc", "daap.songlongcontentdescription" },
    { 0x09, "asky", "daap.songkeywords" },
    { 0x01, "apsm", "daap.playlistshufflemode" },
    { 0x01, "aprm", "daap.playlistrepeatmode" },
    { 0x01, "aePC", "com.apple.itunes.is-podcast" },
    { 0x01, "aePP", "com.apple.itunes.is-podcast-playlist" },
    { 0x01, "aeMK", "com.apple.itunes.mediakind" },
    { 0x09, "aeSN", "com.apple.itunes.series-name" },
    { 0x09, "aeNN", "com.apple.itunes.network-name" },
    { 0x09, "aeEN", "com.apple.itunes.episode-num-str" },
    { 0x05, "aeES", "com.apple.itunes.episode-sort" },
    { 0x05, "aeSU", "com.apple.itunes.season-num" },

    /* mt-daapd specific */
    { 0x09, "MSPS", "org.mt-daapd.smart-playlist-spec" },
    { 0x01, "MPTY", "org.mt-daapd.playlist-type" },
    { 0x0C, "MAPR", "org.mt-daapd.addplaylist" },
    { 0x0C, "MAPI", "org.mt-daapd.addplaylistitem" },
    { 0x0C, "MDPR", "org.mt-daapd.delplaylist" },
    { 0x0C, "MDPI", "org.mt-daapd.delplaylistitem" },
    { 0x0C, "MEPR", "org.mt-daapd.editplaylist" },

    { 0x00, NULL,   NULL }
};

/** map the string names specified in the meta= tag to bit numbers */
static METAMAP  db_metamap[] = {
    { "dmap.itemid",                       metaItemId },
    { "dmap.itemname",                     metaItemName },
    { "dmap.itemkind",                     metaItemKind },
    { "dmap.persistentid",                 metaPersistentId },
    { "dmap.containeritemid",              metaContainerItemId },
    { "dmap.parentcontainerid",            metaParentContainerId },
    /* end generics */
    { "daap.songalbum",                    metaSongAlbum },
    { "daap.songartist",                   metaSongArtist },
    { "daap.songbitrate",                  metaSongBitRate },
    { "daap.songbeatsperminute",           metaSongBPM },
    { "daap.songcomment",                  metaSongComment },
    { "daap.songcompilation",              metaSongCompilation },
    { "daap.songcomposer",                 metaSongComposer },
    { "daap.songdatakind",                 metaSongDataKind },
    { "daap.songdataurl",                  metaSongDataURL },
    { "daap.songdateadded",                metaSongDateAdded },
    { "daap.songdatemodified",             metaSongDateModified },
    { "daap.songdescription",              metaSongDescription },
    { "daap.songdisabled",                 metaSongDisabled },
    { "daap.songdisccount",                metaSongDiscCount },
    { "daap.songdiscnumber",               metaSongDiscNumber },
    { "daap.songeqpreset",                 metaSongEqPreset },
    { "daap.songformat",                   metaSongFormat },
    { "daap.songgenre",                    metaSongGenre },
    { "daap.songgrouping",                 metaSongGrouping },
    { "daap.songrelativevolume",           metaSongRelativeVolume },
    { "daap.songsamplerate",               metaSongSampleRate },
    { "daap.songsize",                     metaSongSize },
    { "daap.songstarttime",                metaSongStartTime },
    { "daap.songstoptime",                 metaSongStopTime },
    { "daap.songtime",                     metaSongTime },
    { "daap.songtrackcount",               metaSongTrackCount },
    { "daap.songtracknumber",              metaSongTrackNumber },
    { "daap.songuserrating",               metaSongUserRating },
    { "daap.songyear",                     metaSongYear },

    /* iTunes 4.5+ (forgot exactly when) */
    { "daap.songcodectype",                metaSongCodecType },
    { "daap.songcodecsubtype",             metaSongCodecSubType },
    { "com.apple.itunes.norm-volume",      metaItunesNormVolume },
    { "com.apple.itunes.itms-songid",      metaItmsSongId },
    { "com.apple.itunes.itms-artistid",    metaItmsArtistId },
    { "com.apple.itunes.itms-playlistid",  metaItmsPlaylistId },
    { "com.apple.itunes.itms-composerid",  metaItmsComposerId },
    { "com.apple.itunes.itms-genreid",     metaItmsGenreId },
    { "com.apple.itunes.itms-storefrontid",metaItmsStorefrontId },
    { "com.apple.itunes.smart-playlist",   metaItunesSmartPlaylist },

    /* iTunes 5.0+ */
    { "daap.songcontentrating",            metaSongContentRating },
    { "dmap.haschildcontainers",           metaHasChildContainers },

    /* iTunes 6.0.2+ */
    { "com.apple.itunes.has-video",        metaItunesHasVideo },

    /* mt-daapd specific */
    { "org.mt-daapd.smart-playlist-spec",  metaMPlaylistSpec },
    { "org.mt-daapd.playlist-type",        metaMPlaylistType },

    { 0,                                   0 }
};

char *db_error_list[] = {
    "Success",
    "Misc SQL Error: %s",
    "Duplicate Playlist: %s",
    "Missing playlist spec",
    "Cannot add playlist items to a playlist of that type",
    "No rows returned",
    "Invalid playlist id: %d",
    "Invalid song id: %d",
    "Parse error: %s",
    "No backend database support for type: %s",
    "Could not initialize thread pool",
    "Passed buffer too small for result",
    "Wrong db schema.  Use mtd-update to upgrade the db.",
    "Database error: %s",
    "Malloc error",
    "Path not found"
};

/* Globals */
static DB_FUNCTIONS *db_current=&db_functions[0];     /**< current database backend */
static int db_revision_no=2;                          /**< current revision of the db */
static pthread_once_t db_initlock=PTHREAD_ONCE_INIT;  /**< to initialize the rwlock */
static int db_is_scanning=0;
static pthread_rwlock_t db_rwlock;                    /**< pthread r/w sync for the database */

/* Forwards */
static void db_writelock(void);
static void db_readlock(void);
static int db_unlock(void);
static void db_init_once(void);
static void db_utf8_validate(MP3FILE *pmp3);
static int db_utf8_validate_string(char *string);
static void db_trim_strings(MP3FILE *pmp3);
static void db_trim_string(char *string);
/**
 * encode a string meta request into a MetaField_t
 *
 * \param meta meta string variable from GET request
 */
MetaField_t db_encode_meta(char *meta) {
    MetaField_t bits = 0;
    char *start;
    char *end;
    METAMAP *m;

    for(start = meta ; *start ; start = end) {
        int     len;

        if(0 == (end = strchr(start, ',')))
            end = start + strlen(start);

        len = (int)(end - start);

        if(*end != 0)
            end++;

        for(m = db_metamap ; m->tag ; ++m)
            if(!strncmp(m->tag, start, len))
                break;

        if(m->tag)
            bits |= (((MetaField_t) 1) << m->bit);
        else
            DPRINTF(E_WARN,L_DAAP,"Unknown meta code: %.*s\n", len, start);
    }

    DPRINTF(E_DBG, L_DAAP, "meta codes: %llu\n", bits);

    return bits;
}

/**
 * see if a specific metafield was requested
 *
 * \param meta encoded list of requested metafields
 * \param fieldNo field to test for
 */
int db_wantsmeta(MetaField_t meta, MetaFieldName_t fieldNo) {
    return 0 != (meta & (((MetaField_t) 1) << fieldNo));
}


/*
 * db_readlock
 *
 * If this fails, something is so amazingly hosed, we might just as well
 * terminate.
 */
void db_readlock(void) {
    int err;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock rdlock: %s\n",strerror(err));
    }
}

/*
 * db_writelock
 *
 * same as above
 */
void db_writelock(void) {
    int err;

    if((err=pthread_rwlock_wrlock(&db_rwlock))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock rwlock: %s\n",strerror(err));
    }
}

/*
 * db_unlock
 *
 * useless, but symmetrical
 */
int db_unlock(void) {
    return pthread_rwlock_unlock(&db_rwlock);
}

/**
 * Build an error string
 */
void db_get_error(char **pe, int error, ...) {
    va_list ap;
    char errbuf[1024];

    if(!pe)
        return;

    va_start(ap, error);
    vsnprintf(errbuf, sizeof(errbuf), db_error_list[error], ap);
    va_end(ap);

    DPRINTF(E_SPAM,L_MISC,"Raising error: %s\n",errbuf);

    *pe = strdup(errbuf);
}

/**
 * Must dynamically initialize the rwlock, as Mac OSX 10.3 (at least)
 * doesn't have a static initializer for rwlocks
 */
void db_init_once(void) {
    pthread_rwlock_init(&db_rwlock,NULL);
}

/**
 * Open the database.  This is done before we drop privs, that
 * way if the database only has root perms, then it can still
 * be opened.
 *
 * \param parameters This is backend-specific (mysql, sqlite, etc)
 */
int db_open(char **pe, char *type, char *parameters) {
    int result;

    DPRINTF(E_DBG,L_DB,"Opening database\n");

    if(pthread_once(&db_initlock,db_init_once))
        return -1;

    db_current = &db_functions[0];
    if(type) {
        while((db_current->name) && (strcasecmp(db_current->name,type))) {
            db_current++;
        }

        if(!db_current->name) {
            /* end of list -- no match */
            db_get_error(pe,DB_E_BADPROVIDER,type);
            return DB_E_BADPROVIDER;
        }
    }

    result=db_current->dbs_open(pe, parameters);


    DPRINTF(E_DBG,L_DB,"Results: %d\n",result);
    return result;
}

/**
 * Initialize the database, including marking it for full reload if necessary.
 *
 * \param reload whether or not to do a full reload of the database
 */
int db_init(int *reload) {
    return db_current->dbs_init(reload);
}

/**
 * Close the database.
 */
int db_deinit(void) {
    return db_current->dbs_deinit();
}

/**
 * return the current db revision.  this is mostly to determine
 * when its time to send an updated version to the client
 */
int db_revision(void) {
    int revision;

    db_readlock();
    revision=db_revision_no;
    db_unlock();

    return revision;
}

/**
 * is the db currently in scanning mode?
 */
int db_scanning(void) {
    return db_is_scanning;
}

/**
 * add (or update) a file
 */
int db_add(char **pe, MP3FILE *pmp3, int *id) {
    int retval;

    db_writelock();
    db_utf8_validate(pmp3);
    db_trim_strings(pmp3);
    retval=db_current->dbs_add(pe,pmp3,id);
    db_revision_no++;
    db_unlock();

    return retval;
}

/**
 * add a playlist
 *
 * \param name name of playlist to add
 * \param type type of playlist to add: 0 - static, 1 - smart, 2 - m3u
 * \param clause where clause (if type 1)
 * \param playlistid returns the id of the playlist created
 * \returns 0 on success, error code otherwise
 */
int db_add_playlist(char **pe, char *name, int type, char *clause, char *path, int index, int *playlistid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_add_playlist(pe,name,type,clause,path,index,playlistid);
    if(retval == DB_E_SUCCESS)
        db_revision_no++;
    db_unlock();

    return retval;
}

/**
 * add a song to a static playlist
 *
 * \param playlistid playlist to add song to
 * \param songid song to add to playlist
 * \returns 0 on success, DB_E_ code otherwise
 */
int db_add_playlist_item(char **pe, int playlistid, int songid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_add_playlist_item(pe,playlistid,songid);
    if(retval == DB_E_SUCCESS)
        db_revision_no++;
    db_unlock();

    return retval;
}

/**
 * delete a playlist
 *
 * \param playlistid id of the playlist to delete
 * \returns 0 on success, error code otherwise
 */
int db_delete_playlist(char **pe, int playlistid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_delete_playlist(pe,playlistid);
    if(retval == DB_E_SUCCESS)
        db_revision_no++;
    db_unlock();

    return retval;
}

/**
 * delete an item from a playlist
 *
 * \param playlistid id of the playlist to delete
 * \param songid id of the song to delete
 * \returns 0 on success, error code otherwise
 */
int db_delete_playlist_item(char **pe, int playlistid, int songid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_delete_playlist_item(pe,playlistid,songid);
    if(retval == DB_E_SUCCESS)
        db_revision_no++;
    db_unlock();

    return retval;
}

/**
 * edit a playlist
 *
 * @param id playlist id to edit
 * @param name new name of playlist
 * @param clause new where clause
 */
int db_edit_playlist(char **pe, int id, char *name, char *clause) {
    int retval;

    db_writelock();

    retval = db_current->dbs_edit_playlist(pe, id, name, clause);
    db_unlock();
    return retval;
}


/**
 * increment the playcount info for a particular song
 * (play_count and time_played).
 *
 * @param pe error string
 * @param id id of song to incrmrent
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_playcount_increment(char **pe, int id) {
    int retval;

    db_writelock();
    retval = db_current->dbs_playcount_increment(pe, id);
    db_unlock();

    return retval;
}

/**
 * start a db enumeration, based info in the DBQUERYINFO struct
 *
 * \param pinfo pointer to DBQUERYINFO struction
 * \returns 0 on success, -1 on failure
 */
int db_enum_start(char **pe, DBQUERYINFO *pinfo) {
    int retval;

    db_writelock();
    retval=db_current->dbs_enum_start(pe, pinfo);

    if(retval) {
        db_unlock();
        return retval;
    }

    return 0;
}

/**
 * get size info about the returned query.  This implicitly calls
 * db_<dbase>_enum_reset, so it should be positioned at the head
 * of the list of returned items.
 */
int db_enum_size(char **pe, DBQUERYINFO *pinfo, int *size, int *count) {
    return db_current->dbs_enum_size(pe,pinfo,size,count);
}


/**
 * fetch the next item in the result set started by the db enum.   this item
 * will the the appropriate dmap item.  It is the application's duty to free
 * the dmap item.
 *
 * \param plen length of the dmap item returned
 * \returns dmap item
 */
int db_enum_fetch(char **pe, DBQUERYINFO *pinfo, int *size,
                  unsigned char **pdmap)
{
    return db_current->dbs_enum_fetch(pe,pinfo,size,pdmap);
}


/**
 * fetch the next item int he result set started by the db enum.  this
 * will be in native packed row format 
 */
int db_enum_fetch_row(char **pe, PACKED_MP3FILE *row, DBQUERYINFO *pinfo) {
    return db_current->dbs_enum_fetch_row(pe, row, pinfo);
}


/**
 * reset the enum, without coming out the the db_writelock
 */
int db_enum_reset(char **pe, DBQUERYINFO *pinfo) {
    return db_current->dbs_enum_reset(pe,pinfo);
}

/**
 * finish the enumeration
 */
int db_enum_end(char **pe) {
    int retval;

    retval=db_current->dbs_enum_end(pe);
    db_unlock();
    return retval;
}


/**
 * fetch a MP3FILE struct given an id.  This will be done
 * mostly only by the web interface, and when streaming a song
 *
 * \param id id of the item to get details for
 */
MP3FILE *db_fetch_item(char **pe, int id) {
    MP3FILE *retval;

    db_readlock();
    retval=db_current->dbs_fetch_item(pe, id);
    db_unlock();

    return retval;
}

MP3FILE *db_fetch_path(char **pe, char *path,int index) {
    MP3FILE *retval;

    db_readlock();
    retval=db_current->dbs_fetch_path(pe,path, index);
    db_unlock();

    return retval;
}

M3UFILE *db_fetch_playlist(char **pe, char *path, int index) {
    M3UFILE *retval;

    db_readlock();
    retval=db_current->dbs_fetch_playlist(pe,path,index);
    db_unlock();

    return retval;
}

int db_force_rescan(char **pe) {
    int retval;
    db_writelock();
    retval = db_current->dbs_force_rescan(pe);
    db_unlock();

    return retval;
}

int db_start_scan(void) {
    int retval;

    db_writelock();
    retval=db_current->dbs_start_scan();
    db_is_scanning=1;
    db_unlock();

    return retval;
}

int db_end_song_scan(void) {
    int retval;

    db_writelock();
    retval=db_current->dbs_end_song_scan();
    db_unlock();

    return retval;
}

int db_end_scan(void) {
    int retval;

    db_writelock();
    retval=db_current->dbs_end_scan();
    db_is_scanning=0;
    db_unlock();

    return retval;
}

void db_dispose_item(MP3FILE *pmp3) {
    db_current->dbs_dispose_item(pmp3);
}

void db_dispose_playlist(M3UFILE *pm3u) {
    db_current->dbs_dispose_playlist(pm3u);
}

int db_get_count(char **pe, int *count, CountType_t type) {
    int retval;

    db_readlock();
    retval=db_current->dbs_get_count(pe,count,type);
    db_unlock();

    return retval;
}


/*
 * FIXME: clearly a stub
 */
int db_get_song_count(char **pe, int *count) {
    return db_get_count(pe, count, countSongs);
}

int db_get_playlist_count(char **pe, int *count) {
    return db_get_count(pe, count, countPlaylists);
}


/* These dmap functions arguably don't belong here, but with
 * the database delivering dmap objects by preference over MP3FILE
 * objects, it does make some amount of sense to be here
 */

/**
 * add a character type to a dmap block (type 0x01)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */
int db_dmap_add_char(unsigned char *where, char *tag, char value) {
    /* tag */
    memcpy(where,tag,4);

    /* len */
    where[4]=where[5]=where[6]=0;
    where[7]=1;

    /* value */
    where[8] = value;
    return 9;
}

/**
 * add a short type to a dmap block (type 0x03)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */
int db_dmap_add_short(unsigned char *where, char *tag, short value) {
    /* tag */
    memcpy(where,tag,4);

    /* len */
    where[4]=where[5]=where[6]=0;
    where[7]=2;

    /* value */
    where[8] = (value >> 8) & 0xFF;
    where[9] = value & 0xFF;
    return 10;
}


/**
 * add an int type to a dmap block (type 0x05)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */

int db_dmap_add_int(unsigned char *where, char *tag, int value) {
    /* tag */
    memcpy(where,tag,4);
    /* len */
    where[4]=where[5]=where[6]=0;
    where[7]=4;

    /* value */
    where[8] = (value >> 24) & 0xFF;
    where[9] = (value >> 16) & 0xFF;
    where[10] = (value >> 8) & 0xFF;
    where[11] = value & 0xFF;

    return 12;
}

/**
 * add a string type to a dmap block (type 0x09)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */

int db_dmap_add_string(unsigned char *where, char *tag, char *value) {
    int len=0;

    if(value)
        len = (int)strlen(value);

    /* tag */
    memcpy(where,tag,4);

    /* length */
    where[4]=(len >> 24) & 0xFF;
    where[5]=(len >> 16) & 0xFF;
    where[6]=(len >> 8) & 0xFF;
    where[7]=len & 0xFF;

    if(len)
        strncpy((char*)where+8,value,len);
    return 8 + len;
}

/**
 * add a literal chunk of data to a dmap block
 *
 * \param where where to serialize the dmap info
 * \param tag what four byte tag
 * \param value what to put there
 * \param size how much data to cram in there
 */
int db_dmap_add_literal(unsigned char *where, char *tag,
                        char *value, int size) {
    /* tag */
    memcpy(where,tag,4);

    /* length */
    where[4]=(size >> 24) & 0xFF;
    where[5]=(size >> 16) & 0xFF;
    where[6]=(size >> 8) & 0xFF;
    where[7]=size & 0xFF;

    memcpy(where+8,value,size);
    return 8+size;
}


/**
 * add a container type to a dmap block (type 0x0C)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */

int db_dmap_add_container(unsigned char *where, char *tag, int size) {
    int len=size;

    /* tag */
    memcpy(where,tag,4);

    /* length */
    where[4]=(len >> 24) & 0xFF;
    where[5]=(len >> 16) & 0xFF;
    where[6]=(len >> 8) & 0xFF;
    where[7]=len & 0xFF;

    return 8;
}


/**
 * check the strings in a MP3FILE to ensure they are
 * valid utf-8.  If they are not, the string will be corrected
 *
 * \param pmp3 MP3FILE to verify for valid utf-8
 */
void db_utf8_validate(MP3FILE *pmp3) {
    int is_invalid=0;

    /* we won't bother with path and fname... those were culled with the
     * scan.  Even if they are invalid (_could_ they be?), then we
     * won't be able to open the file if we change them.  Likewise,
     * we won't do type or description, as these can't be bad, or they
     * wouldn't have been scanned */

    is_invalid = db_utf8_validate_string(pmp3->title);
    is_invalid |= db_utf8_validate_string(pmp3->artist);
    is_invalid |= db_utf8_validate_string(pmp3->album);
    is_invalid |= db_utf8_validate_string(pmp3->genre);
    is_invalid |= db_utf8_validate_string(pmp3->comment);
    is_invalid |= db_utf8_validate_string(pmp3->composer);
    is_invalid |= db_utf8_validate_string(pmp3->orchestra);
    is_invalid |= db_utf8_validate_string(pmp3->conductor);
    is_invalid |= db_utf8_validate_string(pmp3->grouping);
    is_invalid |= db_utf8_validate_string(pmp3->url);

    if(is_invalid) {
        DPRINTF(E_LOG,L_SCAN,"Invalid UTF-8 in %s\n",pmp3->path);
    }
}

/**
 * check a string to verify it is valid utf-8.  The passed
 * string will be in-place modified to be utf-8 clean by substituting
 * the character '?' for invalid utf-8 codepoints
 *
 * \param string string to clean
 */
int db_utf8_validate_string(char *string) {
    char *current = string;
    int run,r_current;
    int retval=0;

    if(!string)
        return 0;

     while(*current) {
        if(!((*current) & 0x80)) {
            current++;
        } else {
            run=0;

            /* it's a lead utf-8 character */
            if((*current & 0xE0) == 0xC0) run=1;
            if((*current & 0xF0) == 0xE0) run=2;
            if((*current & 0xF8) == 0xF0) run=3;

            if(!run) {
                /* high bit set, but invalid */
                *current++='?';
                retval=1;
            } else {
                r_current=0;
                while((r_current != run) && (*(current + r_current + 1)) &&
                      ((*(current + r_current + 1) & 0xC0) == 0x80)) {
                    r_current++;
                }

                if(r_current != run) {
                    *current++ = '?';
                    retval=1;
                } else {
                    current += (1 + run);
                }
            }
        }
    }

    return retval;
}

/**
 * Trim the spaces off the string values.  It throws off
 * browsing when there are some with and without spaces.
 * This should probably be better fixed by having clean tags,
 * but seemed simple enough, and it does make sense that
 * while we are cleaning tags for, say, utf-8 hygene we might
 * as well get this too.
 *
 * @param pmp3 mp3 struct to fix
 */
void db_trim_strings(MP3FILE *pmp3) {
    db_trim_string(pmp3->title);
    db_trim_string(pmp3->artist);
    db_trim_string(pmp3->album);
    db_trim_string(pmp3->genre);
    db_trim_string(pmp3->comment);
    db_trim_string(pmp3->composer);
    db_trim_string(pmp3->orchestra);
    db_trim_string(pmp3->conductor);
    db_trim_string(pmp3->grouping);
    db_trim_string(pmp3->url);
}

/**
 * trim trailing spaces in a string.  Used by db_trim_strings
 *
 * @param string string to trim
 */
void db_trim_string(char *string) {
    if(!string)
        return;

    while(strlen(string) && (string[strlen(string) - 1] == ' '))
        string[strlen(string) - 1] = '\0';
}

