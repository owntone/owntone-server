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
#include <stdlib.h>
#include <string.h>

#include "db-generic.h"
#include "err.h"
#include "mp3-scanner.h"

#include "dbs-sqlite.h"

#define DB_VERSION 1
#define MAYBEFREE(a) { if((a)) free((a)); };

/** pointers to database-specific functions */
typedef struct tag_db_functions {
    char *name;
    int(*dbs_open)(char *);
    int(*dbs_init)(int);
    int(*dbs_deinit)(void);
    int(*dbs_add)(MP3FILE*);
    int(*dbs_add_playlist)(char *, int, char *, int *);
    int(*dbs_add_playlist_item)(int, int);
    int(*dbs_enum_start)(DBQUERYINFO *);
    int(*dbs_enum_size)(DBQUERYINFO *, int *);
    int(*dbs_enum_fetch)(DBQUERYINFO *, unsigned char **);
    int(*dbs_enum_reset)(DBQUERYINFO *);
    int(*dbs_enum_end)(void);
    int(*dbs_start_scan)(void);
    int(*dbs_end_scan)(void);
    int(*dbs_get_count)(CountType_t);
    MP3FILE*(*dbs_fetch_item)(int);
    MP3FILE*(*dbs_fetch_path)(char *);
    void(*dbs_dispose_item)(MP3FILE*);
}DB_FUNCTIONS;

/** All supported backend databases, and pointers to the db specific implementations */
DB_FUNCTIONS db_functions[] = {
#ifdef HAVE_LIBSQLITE
    {
	"sqlite",
	db_sqlite_open,
	db_sqlite_init,
	db_sqlite_deinit,
	db_sqlite_add,
	db_sqlite_add_playlist,
	db_sqlite_add_playlist_item,
	db_sqlite_enum_start,
	db_sqlite_enum_size,
	db_sqlite_enum_fetch,
	db_sqlite_enum_reset,
	db_sqlite_enum_end,
	db_sqlite_start_scan,
	db_sqlite_end_scan,
	db_sqlite_get_count,
	db_sqlite_fetch_item,
	db_sqlite_fetch_path,
	db_sqlite_dispose_item
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

    /* mt-daapd specific */
    { 0x09, "MSPS", "org.mt-daapd.smart-playlist-spec" },
    { 0x01, "MPTY", "org.mt-daapd.playlist-type" },
    { 0x0C, "MAPR", "org.mt-daapd.addplaylist" },
    { 0x0C, "MAPI", "org.mt-daapd.addplaylistitem" },
    { 0x00, NULL,   NULL }
};

/** map the string names specified in the meta= tag to bit numbers */
static METAMAP	db_metamap[] = {
    { "dmap.itemid",		           metaItemId },
    { "dmap.itemname",		           metaItemName },
    { "dmap.itemkind",		           metaItemKind },
    { "dmap.persistentid", 	           metaPersistentId },
    { "dmap.containeritemid",              metaContainerItemId },
    { "dmap.parentcontainerid",	           metaParentContainerId },
    /* end generics */
    { "daap.songalbum",		           metaSongAlbum },
    { "daap.songartist",	           metaSongArtist },
    { "daap.songbitrate",	           metaSongBitRate },
    { "daap.songbeatsperminute",           metaSongBPM },
    { "daap.songcomment",	           metaSongComment },
    { "daap.songcompilation",	           metaSongCompilation },
    { "daap.songcomposer",	           metaSongComposer },
    { "daap.songdatakind",	           metaSongDataKind },
    { "daap.songdataurl",                  metaSongDataURL },
    { "daap.songdateadded",	           metaSongDateAdded },
    { "daap.songdatemodified",	           metaSongDateModified },
    { "daap.songdescription",	           metaSongDescription },
    { "daap.songdisabled",	           metaSongDisabled },
    { "daap.songdisccount",	           metaSongDiscCount },
    { "daap.songdiscnumber",	           metaSongDiscNumber },
    { "daap.songeqpreset",	           metaSongEqPreset },
    { "daap.songformat",	           metaSongFormat },
    { "daap.songgenre",		           metaSongGenre },
    { "daap.songgrouping",	           metaSongGrouping },
    { "daap.songrelativevolume",           metaSongRelativeVolume },
    { "daap.songsamplerate",	           metaSongSampleRate },
    { "daap.songsize",		           metaSongSize },
    { "daap.songstarttime",	           metaSongStartTime },
    { "daap.songstoptime",	           metaSongStopTime },
    { "daap.songtime",		           metaSongTime },
    { "daap.songtrackcount",	           metaSongTrackCount },
    { "daap.songtracknumber",	           metaSongTrackNumber },
    { "daap.songuserrating",	           metaSongUserRating },
    { "daap.songyear",		           metaSongYear },
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
    /* mt-daapd specific */
    { "org.mt-daapd.smart-playlist-spec",  metaMPlaylistSpec },
    { "org.mt-daapd.playlist-type",        metaMPlaylistType },
    { 0,			           0 }
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

/**
 * encode a string meta request into a MetaField_t
 *
 * \param meta meta string variable from GET request
 */
MetaField_t db_encode_meta(char *meta) {
    MetaField_t	bits = 0;
    char *start;
    char *end;
    METAMAP *m;

    for(start = meta ; *start ; start = end) {
	int	len;

	if(0 == (end = strchr(start, ',')))
	    end = start + strlen(start);

	len = end - start;

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
 * Must dynamically initialize the rwlock, as Mac OSX 10.3 (at least)
 * doesn't have a static initializer for rwlocks
 */
void db_init_once(void) {
    pthread_rwlock_init(&db_rwlock,NULL);
}


/**
 * Set the database backend. This should show a friendly error about
 * what databse types are supported.
 *
 * \param type what database backend to use (by friendly name)
 */
extern int db_set_backend(char *type) {
    DPRINTF(E_DBG,L_DB,"Setting backend database to %s\n",type);

    if(!db_functions[0].name) {
	DPRINTF(E_FATAL,L_DB,"No database backends are available.  Install sqlite!\n");
    }
    
    db_current=&db_functions[0];
    while((db_current->name) && (strcasecmp(db_current->name,type))) {
	db_current++;
    }

    if(!db_current->name) {
	DPRINTF(E_WARN,L_DB,"Could not find db backend %s.  Aborting.\n",type);
	return -1;
    }

    DPRINTF(E_DBG,L_DB,"Backend database set\n");
    return 0;
}

/**
 * Open the database.  This is done before we drop privs, that
 * way if the database only has root perms, then it can still
 * be opened.
 *
 * \param parameters This is backend-specific (mysql, sqlite, etc)
 */
int db_open(char *parameters) {
    int result;

    DPRINTF(E_DBG,L_DB,"Opening database\n");

    if(pthread_once(&db_initlock,db_init_once))
	return -1;

    result=db_current->dbs_open(parameters);

    DPRINTF(E_DBG,L_DB,"Results: %d\n",result);
    return result;
}

/**
 * Initialize the database, including marking it for full reload if necessary.
 *
 * \param reload whether or not to do a full reload of the database
 */
int db_init(int reload) {
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
int db_add(MP3FILE *pmp3) {
    int retval;

    db_writelock();
    db_utf8_validate(pmp3);
    retval=db_current->dbs_add(pmp3);
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
int db_add_playlist(char *name, int type, char *clause, int *playlistid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_add_playlist(name,type,clause,playlistid);
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
int db_add_playlist_item(int playlistid, int songid) {
    int retval;

    db_writelock();
    retval=db_current->dbs_add_playlist_item(playlistid,songid);
    if(retval == DB_E_SUCCESS)
	db_revision_no++;
    db_unlock();

    return retval;
}


/**
 * start a db enumeration, based info in the DBQUERYINFO struct
 *
 * \param pinfo pointer to DBQUERYINFO struction
 * \returns 0 on success, -1 on failure
 */
int db_enum_start(DBQUERYINFO *pinfo) {
    int retval;

    db_writelock();
    retval=db_current->dbs_enum_start(pinfo);

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
int db_enum_size(DBQUERYINFO *pinfo, int *count) {
    return db_current->dbs_enum_size(pinfo,count);
}


/**
 * fetch the next item in the result set started by the db enum.   this item
 * will the the appropriate dmap item.  It is the application's duty to free
 * the dmap item.
 *
 * \param plen length of the dmap item returned
 * \returns dmap item 
 */
int db_enum_fetch(DBQUERYINFO *pinfo, unsigned char **pdmap) {
    return db_current->dbs_enum_fetch(pinfo,pdmap);
}


/**
 * reset the enum, without coming out the the db_writelock
 */
int db_enum_reset(DBQUERYINFO *pinfo) {
    return db_current->dbs_enum_reset(pinfo);
}

/**
 * finish the enumeration
 */
int db_enum_end(void) {
    int retval;

    retval=db_current->dbs_enum_end();
    db_unlock();
    return retval;
}


/**
 * fetch a MP3FILE struct given an id.  This will be done
 * mostly only by the web interface, and when streaming a song
 *
 * \param id id of the item to get details for
 */ 
MP3FILE *db_fetch_item(int id) {
    MP3FILE *retval;
    
    db_readlock();
    retval=db_current->dbs_fetch_item(id);
    db_unlock();

    return retval;
}

MP3FILE *db_fetch_path(char *path) {
    MP3FILE *retval;
    
    db_readlock();
    retval=db_current->dbs_fetch_path(path);
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

int db_end_scan(void) {
    int retval;

    db_writelock();
    retval=db_current->dbs_end_scan();
    db_is_scanning=0;
    db_unlock();
    
    return retval;
}
    
void db_dispose_item(MP3FILE *pmp3) {
    return db_current->dbs_dispose_item(pmp3);
}

int db_get_count(CountType_t type) {
    int retval; 

    db_readlock();
    retval=db_current->dbs_get_count(type);
    db_unlock();

    return retval;
}
    

/* 
 * FIXME: clearly a stub
 */
int db_get_song_count() {
    return db_get_count(countSongs);
}

int db_get_playlist_count() {
    return db_get_count(countPlaylists);
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
int db_dmap_add_char(char *where, char *tag, char value) {
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
int db_dmap_add_short(char *where, char *tag, short value) {
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

int db_dmap_add_int(char *where, char *tag, int value) {
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

int db_dmap_add_string(char *where, char *tag, char *value) {
    int len=strlen(value);

    /* tag */
    memcpy(where,tag,4);

    /* length */
    where[4]=(len >> 24) & 0xFF;
    where[5]=(len >> 16) & 0xFF;
    where[6]=(len >> 8) & 0xFF;
    where[7]=len & 0xFF;

    strncpy(where+8,value,strlen(value));
    return 8 + strlen(value);
}

/**
 * add a container type to a dmap block (type 0x0C)
 *
 * \param where where to serialize the dmap info
 * \tag what four byte tag
 * \value what character value
 */

int db_dmap_add_container(char *where, char *tag, int size) {
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


