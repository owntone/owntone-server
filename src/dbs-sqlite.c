/*
 * $Id$
 * sqlite-specific db implementation
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

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite.h>

#include "err.h"
#include "mp3-scanner.h"
#include "db-generic.h"
#include "dbs-sqlite.h"
#include "restart.h"
#include "ssc.h"
#include "smart-parser.h"

/* Globals */
static sqlite *db_sqlite_songs; /**< Database that holds the mp3 info */
static pthread_mutex_t db_sqlite_mutex = PTHREAD_MUTEX_INITIALIZER; /**< sqlite not reentrant */
static sqlite_vm *db_sqlite_pvm;
static int db_sqlite_in_scan=0;
static int db_sqlite_reload=0;
static int db_sqlite_in_playlist_scan=0;

static char db_path[PATH_MAX + 1];

/* Forwards */
int db_sqlite_get_size(DBQUERYINFO *pinfo, char **valarray);
int db_sqlite_build_dmap(DBQUERYINFO *pinfo, char **valarray, unsigned char *presult, int len);
void db_sqlite_build_mp3file(char **valarray, MP3FILE *pmp3);
int db_sqlite_exec(int fatal, char *fmt, ...);
int db_sqlite_get_table(int fatal, char ***resarray, int *rows, int *cols, char *fmt, ...);
int db_sqlite_free_table(char **resarray);
int db_sqlite_get_int(int loglevel, int *result, char *fmt, ...);
int db_sqlite_update(MP3FILE *pmp3);
int db_sqlite_update_version(int from_version);
int db_sqlite_get_version(void);
int db_sqlite_update_playlists(void);
char *db_sqlite_parse_smart(char *phrase);

#define STR(a) (a) ? (a) : ""
#define ISSTR(a) ((a) && strlen((a)))
#define MAYBEFREE(a) { if((a)) free((a)); };

/**
 * get the sql where clause for a smart playlist spec.  This
 * where clause must be freed by the caller
 *
 * @param phrase playlist spec to be converted
 * @returns sql where clause if successful, NULL otherwise
 */
char *db_sqlite_parse_smart(char *phrase) {
    PARSETREE pt;
    char *result = NULL;
    
    if(strcmp(phrase,"1") == 0)
        return strdup("1");
        
    pt=sp_init();
    if(!pt)
        return NULL;
        
    if(!sp_parse(pt,phrase)) {
        DPRINTF(E_LOG,L_DB,"Error parsing smart playlist: %s",sp_get_error(pt));
    } else {
        result = sp_sql_clause(pt);
    }
    
    sp_dispose(pt);
    return result;
}    

/**
 * lock the db_mutex
 */
void db_sqlite_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&db_sqlite_mutex))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock sqlite lock: %s\n",strerror(err));
    }
}

/**
 * unlock the db_mutex
 */
int db_sqlite_unlock(void) {
    return pthread_mutex_unlock(&db_sqlite_mutex);
}

/**
 * exec a simple statement
 *
 * \param what level to log a failure of this sql statement to exec
 */
int db_sqlite_exec(int loglevel, char *fmt, ...) {
    va_list ap;
    char *query;
    int err;
    char *perr;

    va_start(ap,fmt);
    query=sqlite_vmprintf(fmt,ap);
    va_end(ap);

    DPRINTF(E_DBG,L_DB,"Executing: %s\n",query);

    db_sqlite_lock();
    err=sqlite_exec(db_sqlite_songs,query,NULL,NULL,&perr);
    if(err != SQLITE_OK) {
        DPRINTF(loglevel == E_FATAL ? E_LOG : loglevel,L_DB,"Query: %s\n",
                query);
        DPRINTF(loglevel,L_DB,"Error: %s\n",perr);
        sqlite_freemem(perr);
    } else {
        DPRINTF(E_DBG,L_DB,"Rows: %d\n",sqlite_changes(db_sqlite_songs));
    }
    sqlite_freemem(query);

    db_sqlite_unlock();
    return err;
}

/**
 * get a sqlite table
 *
 */
int db_sqlite_get_table(int loglevel, char ***resarray, int *rows, int *cols, char *fmt, ...) {
    va_list ap;
    char *query;
    int err;
    char *perr;

    va_start(ap,fmt);
    query=sqlite_vmprintf(fmt,ap);
    va_end(ap);

    DPRINTF(E_DBG,L_DB,"Executing: %s\n",query);

    db_sqlite_lock();
    err=sqlite_get_table(db_sqlite_songs,query,resarray,rows,cols,&perr);
    if(err == SQLITE_OK)
        sqlite_freemem(query);
    db_sqlite_unlock();

    if(err != SQLITE_OK) {
        DPRINTF(loglevel == E_FATAL ? E_LOG : loglevel,L_DB,"Query: %s\n",query);
        DPRINTF(loglevel,L_DB,"Error: %s\n",perr);
        db_sqlite_lock();
        sqlite_freemem(query);
        sqlite_freemem(perr);
        db_sqlite_unlock();
        return err;
    }

    return 0;
}

int db_sqlite_free_table(char **resarray) {
    db_sqlite_lock();
    sqlite_free_table(resarray);
    db_sqlite_unlock();
    return 0;
}

/**
 * db_sqlite_get_int
 */
int db_sqlite_get_int(int loglevel, int *result, char *fmt, ...) {
    int rows, cols;
    char **resarray;
    va_list ap;
    char *query;
    int err;
    char *perr;

    va_start(ap,fmt);
    query=sqlite_vmprintf(fmt,ap);
    va_end(ap);

    DPRINTF(E_DBG,L_DB,"Executing: %s\n",query);

    db_sqlite_lock();
    err=sqlite_get_table(db_sqlite_songs,query,&resarray,&rows,&cols,&perr);
    if(err == SQLITE_OK)
        sqlite_freemem(query);
    db_sqlite_unlock();

    if(err != SQLITE_OK) {
        DPRINTF(loglevel == E_FATAL ? E_LOG : loglevel,L_DB,"Query: %s\n",query);
        DPRINTF(loglevel,L_DB,"Error: %s\n",perr);
        db_sqlite_lock();
        sqlite_freemem(query);
        sqlite_freemem(perr);
        db_sqlite_unlock();
        return DB_E_SQL_ERROR;
    }

    if(rows==0) {
        sqlite_free_table(resarray);
        return DB_E_NOROWS;
    }

    *result=atoi(resarray[cols]);

    sqlite_free_table(resarray);
    return DB_E_SUCCESS;
}


/**
 * open sqlite database
 */
int db_sqlite_open(char *parameters) {
    char *perr;

    snprintf(db_path,sizeof(db_path),"%s/songs.db",parameters);
    
    db_sqlite_lock();
    db_sqlite_songs=sqlite_open(db_path,0666,&perr);
    if(!db_sqlite_songs) {
        DPRINTF(E_FATAL,L_DB,"db_sqlite_open: %s (%s)\n",perr,db_path);
        sqlite_freemem(perr);
    }

    sqlite_busy_timeout(db_sqlite_songs,30000);  /* 30 seconds */

    db_sqlite_unlock();
    return 0;
}

/**
 * initialize the sqlite database, reloading if requested
 *
 * \param reload whether or not to do a full reload on the db
 */
int db_sqlite_init(int reload) {
    int items;
    int rescan=0;

    /* make sure we have an index... might not if aborted during scan */
    db_sqlite_exec(E_DBG,"CREATE INDEX idx_path ON songs(path)");
    db_sqlite_update_version(db_sqlite_get_version());
    db_sqlite_get_int(E_DBG,&rescan,"SELECT value FROM config WHERE term='rescan'");

    if(rescan)
        reload=1;

    items=db_sqlite_get_count(countSongs);

    if((reload) || (!items)) {
        DPRINTF(E_LOG,L_DB,"Full reload...\n");
        /* this may or may not fail, depending if the index is already in place */
        db_sqlite_reload=1;
        db_sqlite_exec(E_DBG,"DROP INDEX idx_path");
        db_sqlite_exec(E_FATAL,"DELETE FROM songs");
    } else {
        db_sqlite_exec(E_FATAL,"VACUUM");
    }
    return 0;
}

/**
 * close the database
 */
int db_sqlite_deinit(void) {
    db_sqlite_lock();
    sqlite_close(db_sqlite_songs);
    db_sqlite_unlock();

    return 0;
}


/**
 * start a background scan
 */
int db_sqlite_start_scan(void) {
    if(db_sqlite_reload) {
        db_sqlite_exec(E_FATAL,"PRAGMA synchronous = OFF");
        db_sqlite_exec(E_FATAL,"BEGIN TRANSACTION");
    } else {
        /* if not a full reload, we'll be doing update checks */
        db_sqlite_exec(E_DBG,"drop table updated");
        db_sqlite_exec(E_FATAL,"create temp table updated (id int)");
        db_sqlite_exec(E_DBG,"drop table plupdated");
        db_sqlite_exec(E_FATAL,"create temp table plupdated(id int)");
    }

    db_sqlite_in_scan=1;
    db_sqlite_in_playlist_scan=0;
    return 0;
}

/**
 * end song scan -- start playlist scan
 */
int db_sqlite_end_song_scan(void) {
    if(db_sqlite_reload) {
        db_sqlite_exec(E_FATAL,"commit transaction");
        db_sqlite_exec(E_FATAL,"create index idx_path on songs(path)");
        db_sqlite_exec(E_DBG,"delete from config where term='rescan'");
    } else {
        db_sqlite_exec(E_FATAL,"delete from songs where id not in (select id from updated)");
        db_sqlite_exec(E_FATAL,"update songs set force_update=0");
        db_sqlite_exec(E_FATAL,"drop table updated");
    }

    db_sqlite_exec(E_FATAL,"begin transaction");

    db_sqlite_in_scan=0;
    db_sqlite_in_playlist_scan=1;

    return 0;
}

/**
 * stop a db scan
 */
int db_sqlite_end_scan(void) {
    db_sqlite_exec(E_FATAL,"end transaction");

    if(db_sqlite_reload) {
        db_sqlite_exec(E_FATAL,"pragma synchronous=normal");
    } else {
        db_sqlite_exec(E_FATAL,"delete from playlists where ((type=%d) OR (type=%d)) and "
                       "id not in (select id from plupdated)",PL_STATICFILE,PL_STATICXML);
        db_sqlite_exec(E_FATAL,"delete from playlistitems where playlistid not in (select distinct "
                       "id from playlists)");
        db_sqlite_exec(E_FATAL,"drop table plupdated");
    }

    db_sqlite_update_playlists();
    db_sqlite_reload=0;
    db_sqlite_in_playlist_scan=0;

    return 0;
}

/**
 * delete a playlist
 *
 * \param playlistid playlist to delete
 */
int db_sqlite_delete_playlist(int playlistid) {
    int type;
    int result;

    result=db_sqlite_get_int(E_DBG,&type,"select type from playlists where id=%d",playlistid);
    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS)
            return DB_E_INVALID_PLAYLIST;
        return result;
    }

    /* got a good playlist, now do what we need to do */
    db_sqlite_exec(E_FATAL,"delete from playlists where id=%d",playlistid);
    db_sqlite_exec(E_FATAL,"delete from playlistitems where playlistid=%d",playlistid);

    return DB_E_SUCCESS;
}

/**
 * delete a song from a playlist
 *
 * \param playlistid playlist to delete item from
 * \param songid song to delete from playlist
 */
int db_sqlite_delete_playlist_item(int playlistid, int songid) {
    int result;
    int playlist_type;
    int count;

    /* first, check the playlist */
    result=db_sqlite_get_int(E_DBG,&playlist_type,
                             "select type from playlists where id=%d",playlistid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS)
            return DB_E_INVALID_PLAYLIST;
        return result;
    }

    if(playlist_type == PL_SMART)       /* can't delete from a smart playlist */
        return DB_E_INVALIDTYPE;

    /* make sure the songid is valid */
    result=db_sqlite_get_int(E_DBG,&count,"select count(*) from playlistitems where playlistid=%d "
                             "and songid=%d",playlistid,songid);
    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS)
            return DB_E_INVALID_SONGID;
        return result;
    }

    /* looks valid, so lets add the item */
    result=db_sqlite_exec(E_DBG,"delete from playlistitems where playlistid=%d and songid=%d",
                          playlistid,songid);
    return result;
}


/**
 * add a playlist
 *
 * \param name name of the playlist
 * \param type playlist type: 0 - static, 1 - smart, 2 - m3u
 * \param clause: "where" clause for smart playlist
 */
int db_sqlite_add_playlist(char *name, int type, char *clause, char *path, int index, int *playlistid) {
    int cnt=0;
    int result=DB_E_SUCCESS;

    db_sqlite_get_int(E_DBG,&cnt,"select count(*) from playlists where "
                      "upper(title)=upper('%q')",name);

    if(cnt) return DB_E_DUPLICATE_PLAYLIST;
    if((type == PL_SMART) && (!clause)) return DB_E_NOCLAUSE;

    /* Let's throw it in  */
    switch(type) {
    case PL_STATICWEB: /* static, maintained in web interface */
    case PL_STATICFILE: /* static, from file */
    case PL_STATICXML: /* from iTunes XML file */
        result = db_sqlite_exec(E_LOG,"insert into playlists "
                                "(title,type,items,query,db_timestamp,path,idx) "
                                "values ('%q',%d,0,NULL,%d,'%q',%d)",name,type,time(NULL),path,index);
        break;
    case PL_SMART: /* smart */
        result=db_sqlite_get_int(E_DBG,&cnt,"select count (*) from songs where %s",clause);
        if(result != DB_E_SUCCESS) return result;
        result = db_sqlite_exec(E_LOG,"insert into playlists "
                                "(title,type,items,query,db_timestamp,idx) "
                                "values ('%q',%d,%d,'%q',%d,0)",name,PL_SMART,cnt,clause,time(NULL));
        break;
    }

    if(result)
        return result;

    result = db_sqlite_get_int(E_LOG,playlistid,
                               "select id from playlists where title='%q'", name);

    if(((type==PL_STATICFILE)||(type==PL_STATICXML)) 
        && (db_sqlite_in_playlist_scan) && (!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"insert into plupdated values (%d)",*playlistid);
    }

    return result;
}

/**
 * add a song to a static playlist
 *
 * \param playlistid playlist to add song to
 * \param songid song to add
 * \returns 0 on success, otherwise a DB_E error code
 */
int db_sqlite_add_playlist_item(int playlistid, int songid) {
    int result;
    int playlist_type;
    int count;

    /* first, check the playlist */
    result=db_sqlite_get_int(E_DBG,&playlist_type,
                             "select type from playlists where id=%d",playlistid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS)
            return DB_E_INVALID_PLAYLIST;
        return result;
    }

    if(playlist_type == 1)       /* can't add to smart playlists, or static */
        return DB_E_INVALIDTYPE;

    /* make sure the songid is valid */
    result=db_sqlite_get_int(E_DBG,&count,"select count(*) from songs where id=%d",songid);
    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS)
            return DB_E_INVALID_SONGID;
        return result;
    }

    /* looks valid, so lets add the item */
    result=db_sqlite_exec(E_DBG,"insert into playlistitems (playlistid, songid) values (%d,%d)",playlistid,songid);
    return result;
}


/**
 * add a database item
 *
 * \param pmp3 mp3 file to add
 */
int db_sqlite_add(MP3FILE *pmp3) {
    int err;

    DPRINTF(E_SPAM,L_DB,"Entering db_sqlite_add\n");

    if(!pmp3->time_added)
        pmp3->time_added = (int)time(NULL);
    
    if(!pmp3->time_modified)
        pmp3->time_modified = (int)time(NULL);

    pmp3->db_timestamp = (int)time(NULL);
    pmp3->play_count=0;
    pmp3->time_played=0;

    err=db_sqlite_exec(E_DBG,"INSERT INTO songs VALUES "
                       "(NULL,"   // id
                       "'%q',"  // path
                       "'%q',"  // fname
                       "'%q',"  // title
                       "'%q',"  // artist
                       "'%q',"  // album
                       "'%q',"  // genre
                       "'%q',"  // comment
                       "'%q',"  // type
                       "'%q',"  // composer
                       "'%q',"  // orchestra
                       "'%q',"  // conductor
                       "'%q',"  // grouping
                       "'%q',"  // url
                       "%d,"    // bitrate
                       "%d,"    // samplerate
                       "%d,"    // song_length
                       "%d,"    // file_size
                       "%d,"    // year
                       "%d,"    // track
                       "%d,"    // total_tracks
                       "%d,"    // disc
                       "%d,"    // total_discs
                       "%d,"    // bpm
                       "%d,"    // compilation
                       "%d,"    // rating
                       "0,"     // play_count
                       "%d,"    // data_kind
                       "%d,"    // item_kind
                       "'%q',"  // description
                       "%d,"    // time_added
                       "%d,"    // time_modified
                       "%d,"    // time_played
                       "%d,"    // db_timestamp
                       "%d,"    // disabled
                       "%d,"    // sample_count
                       "0,"     // force_update    
                       "'%q',"  // codectype
                       "%d)",   // index
                       STR(pmp3->path),
                       STR(pmp3->fname),
                       STR(pmp3->title),
                       STR(pmp3->artist),
                       STR(pmp3->album),
                       STR(pmp3->genre),
                       STR(pmp3->comment),
                       STR(pmp3->type),
                       STR(pmp3->composer),
                       STR(pmp3->orchestra),
                       STR(pmp3->conductor),
                       STR(pmp3->grouping),
                       STR(pmp3->url),
                       pmp3->bitrate,
                       pmp3->samplerate,
                       pmp3->song_length,
                       pmp3->file_size,
                       pmp3->year,
                       pmp3->track,
                       pmp3->total_tracks,
                       pmp3->disc,
                       pmp3->total_discs,
                       pmp3->bpm,
                       pmp3->compilation,
                       pmp3->rating,
                       pmp3->data_kind,
                       pmp3->item_kind,
                       STR(pmp3->description),
                       pmp3->time_added,
                       pmp3->time_modified,
                       pmp3->time_played,
                       pmp3->db_timestamp,
                       pmp3->disabled,
                       pmp3->sample_count,
                       STR(pmp3->codectype),
                       pmp3->index);
    
    if(err == SQLITE_CONSTRAINT) {
        /* probably because the path already exists... */
        DPRINTF(E_DBG,L_DB,"Could not add mp3 file: %s... updating instead\n",pmp3->path);
        return db_sqlite_update(pmp3);
    }

    if(err != SQLITE_OK)
        DPRINTF(E_FATAL,L_DB,"Error inserting file %s in database\n",pmp3->fname);

    if((db_sqlite_in_scan)&&(!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"INSERT INTO updated VALUES (last_insert_rowid())");
    }

    if((!db_sqlite_in_scan)  && (!db_sqlite_in_playlist_scan))
        db_sqlite_update_playlists();

    DPRINTF(E_SPAM,L_DB,"Exiting db_sqlite_add\n");
    return 0;
}

/**
 * update a database item
 *
 * \param pmp3 mp3 file to update
 */
int db_sqlite_update(MP3FILE *pmp3) {
    int err;

    if(!pmp3->time_modified)
        pmp3->time_modified = (int)time(NULL);

    pmp3->db_timestamp = (int)time(NULL);

    err=db_sqlite_exec(E_FATAL,"UPDATE songs SET "
                       "title='%q',"  // title
                       "artist='%q',"  // artist
                       "album='%q',"  // album
                       "genre='%q',"  // genre
                       "comment='%q',"  // comment
                       "type='%q',"  // type
                       "composer='%q',"  // composer
                       "orchestra='%q',"  // orchestra
                       "conductor='%q',"  // conductor
                       "grouping='%q',"  // grouping
                       "url='%q',"  // url
                       "bitrate=%d,"    // bitrate
                       "samplerate=%d,"    // samplerate
                       "song_length=%d,"    // song_length
                       "file_size=%d,"    // file_size
                       "year=%d,"    // year
                       "track=%d,"    // track
                       "total_tracks=%d,"    // total_tracks
                       "disc=%d,"    // disc
                       "total_discs=%d,"    // total_discs
                       "time_modified=%d,"    // time_modified
                       "db_timestamp=%d,"    // db_timestamp
                       "bpm=%d,"    // bpm
                       "disabled=%d," // disabled
                       "compilation=%d,"    // compilation
                       "rating=%d,"    // rating
                       "sample_count=%d," // sample_count
                       "codectype='%q'"   // codec
                       " WHERE path='%q'",
                       STR(pmp3->title),
                       STR(pmp3->artist),
                       STR(pmp3->album),
                       STR(pmp3->genre),
                       STR(pmp3->comment),
                       STR(pmp3->type),
                       STR(pmp3->composer),
                       STR(pmp3->orchestra),
                       STR(pmp3->conductor),
                       STR(pmp3->grouping),
                       STR(pmp3->url),
                       pmp3->bitrate,
                       pmp3->samplerate,
                       pmp3->song_length,
                       pmp3->file_size,
                       pmp3->year,
                       pmp3->track,
                       pmp3->total_tracks,
                       pmp3->disc,
                       pmp3->total_discs,
                       pmp3->time_modified,
                       pmp3->db_timestamp,
                       pmp3->bpm,
                       pmp3->disabled,
                       pmp3->compilation,
                       pmp3->rating,
                       pmp3->sample_count,
                       STR(pmp3->codectype),
                       pmp3->path);

    if((db_sqlite_in_scan) && (!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"INSERT INTO updated (id) select id from songs where path='%q'",
                       pmp3->path);
    }

    if((!db_sqlite_in_scan) && (!db_sqlite_in_playlist_scan))
        db_sqlite_update_playlists();

    return 0;
}


/**
 * Update the playlist item counts
 */
int db_sqlite_update_playlists(void) {
    char **resarray;
    int rows, cols, index;
    char *where_clause;

    db_sqlite_get_table(E_FATAL,&resarray, &rows, &cols, "SELECT * FROM playlists");

    for(index=1;index <= rows; index ++) {
        DPRINTF(E_DBG,L_DB,"Updating playlist counts for %s\n",resarray[cols * index + 1]);
        if(atoi(resarray[cols * index + 2]) == 1) { // is a smart playlist
            where_clause=db_sqlite_parse_smart(resarray[cols * index + 4]);
            if(where_clause) {
                db_sqlite_exec(E_FATAL,"UPDATE playlists SET items=(SELECT COUNT(*) "
                               "FROM songs WHERE %s) WHERE id=%s",where_clause,
                               resarray[cols * index]);
                free(where_clause);
            }
        } else {
            db_sqlite_exec(E_FATAL,"UPDATE playlists SET items=(SELECT COUNT(*) "
                           "FROM playlistitems WHERE playlistid=%s) WHERE id=%s",
                           resarray[cols * index], resarray[cols * index]);
        }
    }

    db_sqlite_free_table(resarray);

    return 0;
}


/**
 * start enum based on the DBQUERYINFO struct passed
 *
 * \param pinfo DBQUERYINFO struct detailing what to enum
 */
int db_sqlite_enum_start(DBQUERYINFO *pinfo) {
    char scratch[4096];
    char query[4096];
    char query_select[255];
    char query_count[255];
    char query_rest[4096];
    char *where_clause;

    int is_smart;
    int have_clause=0;
    int err;
    char *perr;
    char **resarray;
    int rows, cols;
    int browse=0;
    int results=0;
    
    const char *ptail;

    query[0] = '\0';
    query_select[0] = '\0';
    query_count[0] = '\0';
    query_rest[0] = '\0';

    switch(pinfo->query_type) {
    case queryTypeItems:
        strcpy(query_select,"SELECT * FROM songs ");
        strcpy(query_count,"SELECT COUNT(*) FROM songs ");
        break;

    case queryTypePlaylists:
        strcpy(query_select,"SELECT * FROM playlists ");
        strcpy(query_count,"SELECT COUNT (*) FROM playlists ");
        break;

    case queryTypePlaylistItems:  /* Figure out if it's smart or dull */
        db_sqlite_lock();
        sprintf(scratch,"SELECT type,query FROM playlists WHERE id=%d",pinfo->playlist_id);
        DPRINTF(E_DBG,L_DB,"Executing %s\n",scratch);
        err=sqlite_get_table(db_sqlite_songs,scratch,&resarray,&rows,&cols,&perr);
        if(err != SQLITE_OK) {
            DPRINTF(E_LOG,L_DB|L_DAAP,"Error: %s\n",perr);
            sqlite_freemem(perr);
            db_sqlite_unlock();
            return -1;
        }
        is_smart=(atoi(resarray[2]) == 1); 
        have_clause=1;
        if(is_smart) {
            where_clause=db_sqlite_parse_smart(resarray[3]);
            if(!where_clause) {
                return -1;
            }
            sprintf(query_select,"SELECT * FROM songs ");
            sprintf(query_count,"SELECT COUNT(id) FROM songs ");
            sprintf(query_rest,"WHERE (%s)",where_clause);
            free(where_clause);
        } else {
            sprintf(query_select,"SELECT * FROM songs,playlistitems ");
            sprintf(query_count,"SELECT COUNT(id) FROM songs ");
            sprintf(query_rest,"WHERE (songs.id=playlistitems.songid and playlistitems.playlistid=%d) ORDER BY playlistitems.id",
                    pinfo->playlist_id);
        }
        sqlite_free_table(resarray);
        db_sqlite_unlock();
        break;

        /* Note that sqlite doesn't support COUNT(DISTINCT x) */
    case queryTypeBrowseAlbums:
        strcpy(query_select,"SELECT DISTINCT album FROM songs ");
        strcpy(query_count,"SELECT COUNT(album) FROM (SELECT DISTINCT album FROM songs ");
        browse=1;
        break;

    case queryTypeBrowseArtists:
        strcpy(query_select,"SELECT DISTINCT artist FROM songs ");
        strcpy(query_count,"SELECT COUNT(artist) FROM (SELECT DISTINCT artist FROM songs ");
        browse=1;
        break;

    case queryTypeBrowseGenres:
        strcpy(query_select,"SELECT DISTINCT genre FROM songs ");
        strcpy(query_count,"SELECT COUNT(genre) FROM (SELECT DISTINCT genre FROM songs ");
        browse=1;
        break;

    case queryTypeBrowseComposers:
        strcpy(query_select,"SELECT DISTINCT composer FROM songs ");
        strcpy(query_count,"SELECT COUNT(composer) FROM (SELECT DISTINCT composer FROM songs ");
        browse=1;
        break;
    default:
        DPRINTF(E_LOG,L_DB|L_DAAP,"Unknown query type\n");
        return -1;
    }

    /* Apply the query/filter */
    if(pinfo->whereclause) {
        if(have_clause)
            strcat(query_rest," AND ");
        else
            strcpy(query_rest," WHERE ");

        strcat(query_rest,"(");
        strcat(query_rest,pinfo->whereclause);
        strcat(query_rest,")");
    }


    if(pinfo->index_type == indexTypeLast) {
        /* We don't really care how many items unless we are
         * doing a "last n items" query */
        strcpy(scratch,query_count);
        strcat(scratch,query_rest);
        if(browse) 
            strcat(scratch,")");

        DPRINTF(E_DBG,L_DB,"result count query: %s\n",scratch);

        db_sqlite_lock();
        err=sqlite_get_table(db_sqlite_songs,scratch,&resarray,&rows,&cols,&perr);
        if(err != SQLITE_OK) {
            db_sqlite_unlock();
            DPRINTF(E_LOG,L_DB,"Error in results query: %s\n",perr);
            sqlite_freemem(perr);
            return -1;
        }


        results=atoi(resarray[1]);
        sqlite_free_table(resarray);
        db_sqlite_unlock();

        DPRINTF(E_DBG,L_DB,"Number of results: %d\n",results);
    }

    strcpy(query,query_select);
    strcat(query,query_rest);

    /* Apply any index */
    switch(pinfo->index_type) {
    case indexTypeFirst:
        sprintf(scratch," LIMIT %d",pinfo->index_high);
        break;
    case indexTypeLast:
        if(pinfo->index_low >= results) {
            sprintf(scratch," LIMIT %d",pinfo->index_low); /* unnecessary */
        } else {
            sprintf(scratch," LIMIT %d OFFSET %d",pinfo->index_low, results-pinfo->index_low);
        }
        break;
    case indexTypeSub:
        sprintf(scratch," LIMIT %d OFFSET %d",pinfo->index_high - pinfo->index_low,
                pinfo->index_low);
        break;
    case indexTypeNone:
        break;
    default:
        DPRINTF(E_LOG,L_DB,"Bad indexType: %d\n",(int)pinfo->index_type);
        scratch[0]='\0';
        break;
    }

    if(pinfo->index_type != indexTypeNone)
        strcat(query,scratch);

    /* start fetching... */
    db_sqlite_lock();
    err=sqlite_compile(db_sqlite_songs,query,&ptail,&db_sqlite_pvm,&perr);
    db_sqlite_unlock();

    DPRINTF(E_DBG,L_DB,"Enum query: %s\n",query);

    if(err != SQLITE_OK) {
        DPRINTF(E_LOG,L_DB,"Could not compile query: %s\n",query);
        sqlite_freemem(perr);
        return -1;
    }

    return 0;
}

int db_sqlite_enum_size(DBQUERYINFO *pinfo, int *count) {
    const char **valarray;
    const char **colarray;
    int err;
    char *perr;
    int cols;
    int total_size=0;
    int record_size;

    DPRINTF(E_DBG,L_DB,"Enumerating size\n");

    *count=0;

    db_sqlite_lock();
    while((err=sqlite_step(db_sqlite_pvm,&cols,&valarray,&colarray)) == SQLITE_ROW) {
        if((record_size = db_sqlite_get_size(pinfo,(char**)valarray))) {
            total_size += record_size;
            *count = *count + 1;
        }
    }

    if(err != SQLITE_DONE) {
        sqlite_finalize(db_sqlite_pvm,&perr);
        DPRINTF(E_FATAL,L_DB,"sqlite_step: %s\n",perr);
        sqlite_freemem(perr);
        db_sqlite_unlock();
    }

    db_sqlite_unlock();
    db_sqlite_enum_reset(pinfo);

    DPRINTF(E_DBG,L_DB,"Got size: %d\n",total_size);
    return total_size;
}


/**
 * fetch the next record from the enum
 */
int db_sqlite_enum_fetch(DBQUERYINFO *pinfo, unsigned char **pdmap) {
    const char **valarray;
    const char **colarray;
    int err;
    char *perr;
    int cols;
    int result_size=-1;
    unsigned char *presult;

    db_sqlite_lock();
    err=sqlite_step(db_sqlite_pvm,&cols,&valarray,&colarray);
    db_sqlite_unlock();

    while((err == SQLITE_ROW) && (result_size)) {
        result_size=db_sqlite_get_size(pinfo,(char**)valarray);
        if(result_size) {
            presult=(unsigned char*)malloc(result_size);
            if(!presult)
                return 0;
            db_sqlite_build_dmap(pinfo,(char**)valarray,presult,result_size);
            *pdmap = presult;
            return result_size;
        }
    }

    if(err == SQLITE_DONE) {
        return -1;
    }

    db_sqlite_lock();
    sqlite_finalize(db_sqlite_pvm,&perr);
    DPRINTF(E_FATAL,L_DB,"sqlite_step: %s\n",perr);
    sqlite_freemem(perr);
    db_sqlite_unlock();

    return 0;
}

/**
 * start the enum again
 */
int db_sqlite_enum_reset(DBQUERYINFO *pinfo) {
    db_sqlite_enum_end();
    return db_sqlite_enum_start(pinfo);
}


/**
 * stop the enum
 */
int db_sqlite_enum_end(void) {
    char *perr=NULL;

    db_sqlite_lock();
    sqlite_finalize(db_sqlite_pvm,&perr);
    if(perr) {
        sqlite_freemem(perr);
    }
    db_sqlite_unlock();

    return 0;
}

int db_sqlite_get_size(DBQUERYINFO *pinfo, char **valarray) {
    int size;
    int transcode;

    switch(pinfo->query_type) {
    case queryTypeBrowseArtists: /* simple 'mlit' entry */
    case queryTypeBrowseAlbums:
    case queryTypeBrowseGenres:
    case queryTypeBrowseComposers:
        return valarray[0] ? (8 + strlen(valarray[0])) : 0;
    case queryTypePlaylists:
        size = 8;   /* mlit */
        size += 12; /* mimc - you get it whether you want it or not */
        if(db_wantsmeta(pinfo->meta, metaItemId))
            size += 12; /* miid */
        if(db_wantsmeta(pinfo->meta, metaItunesSmartPlaylist)) {
            if(valarray[plType] && (atoi(valarray[plType])==1))
                size += 9;  /* aeSP */
        }
        if(db_wantsmeta(pinfo->meta, metaItemName))
            size += (8 + strlen(valarray[plTitle])); /* minm */
        if(valarray[plType] && (atoi(valarray[plType])==1) && 
           db_wantsmeta(pinfo->meta, metaMPlaylistSpec))
            size += (8 + strlen(valarray[plQuery])); /* MSPS */
        if(db_wantsmeta(pinfo->meta, metaMPlaylistType)) 
            size += 9; /* MPTY */
        return size;
        break;
    case queryTypeItems:
    case queryTypePlaylistItems:  /* essentially the same query */
        /* see if this is going to be transcoded */
        transcode = server_side_convert(valarray[2]);
        
        /* Items that get changed by transcode:
         *
         * type:         item  8: changes to 'wav'
         * description:  item 29: changes to 'wav audio file'
         * bitrate:      item 15: guestimated, based on item 15, samplerate
         * 
         * probably file size should change as well, but currently doesn't
         */

        size = 8; /* mlit */
        if(db_wantsmeta(pinfo->meta, metaItemKind)) 
            /* mikd */
            size += 9; 
        if(db_wantsmeta(pinfo->meta, metaSongDataKind))
            /* asdk */
            size += 9;
        if(ISSTR(valarray[13]) && db_wantsmeta(pinfo->meta, metaSongDataURL)) 
            /* asul */
            size += (8 + strlen(valarray[13]));
        if(ISSTR(valarray[5]) && db_wantsmeta(pinfo->meta, metaSongAlbum))    
            /* asal */
            size += (8 + strlen(valarray[5]));
        if(ISSTR(valarray[4]) && db_wantsmeta(pinfo->meta, metaSongArtist))   
            /* asar */
            size += (8 + strlen(valarray[4]));
        if(valarray[23] && atoi(valarray[23]) && db_wantsmeta(pinfo->meta, metaSongBPM))      
            /* asbt */
            size += 10;
        if(db_wantsmeta(pinfo->meta, metaSongBitRate)) {
            /* asbr */
            if(transcode) {
                if(valarray[15] && atoi(valarray[15]))
                    size += 10;
            } else {
                if(valarray[14] && atoi(valarray[14]))
                    size += 10;
            }
        }
        if(ISSTR(valarray[7]) && db_wantsmeta(pinfo->meta, metaSongComment))  
            /* ascm */
            size += (8 + strlen(valarray[7]));
        if(valarray[24] && atoi(valarray[24]) && db_wantsmeta(pinfo->meta,metaSongCompilation)) 
            /* asco */
            size += 9;
        if(ISSTR(valarray[9]) && db_wantsmeta(pinfo->meta, metaSongComposer))
            /* ascp */
            size += (8 + strlen(valarray[9]));
        if(ISSTR(valarray[12]) && db_wantsmeta(pinfo->meta, metaSongGrouping))
            /* agrp */
            size += (8 + strlen(valarray[12]));
        if(valarray[30] && atoi(valarray[30]) && db_wantsmeta(pinfo->meta, metaSongDateAdded))
            /* asda */
            size += 12;
        if(valarray[31] && atoi(valarray[31]) && db_wantsmeta(pinfo->meta,metaSongDateModified))
            /* asdm */
            size += 12;
        if(valarray[22] && atoi(valarray[22]) && db_wantsmeta(pinfo->meta, metaSongDiscCount))
            /* asdc */
            size += 10;
        if(valarray[21] && atoi(valarray[21]) && db_wantsmeta(pinfo->meta, metaSongDiscNumber))
                size += 10;
                /* asdn */      
        if(ISSTR(valarray[6]) && db_wantsmeta(pinfo->meta, metaSongGenre))
            /* asgn */
            size += (8 + strlen(valarray[6]));
        if(db_wantsmeta(pinfo->meta,metaItemId))
            /* miid */
            size += 12;
        if(ISSTR(valarray[8]) && db_wantsmeta(pinfo->meta,metaSongFormat)) {
            /* asfm */
            if(transcode) {
                size += 11;   /* 'wav' */
            } else {
                size += (8 + strlen(valarray[8]));
            }
        }
        if(ISSTR(valarray[29]) && db_wantsmeta(pinfo->meta,metaSongDescription)) {
            /* asdt */
            if(transcode) {
                size += 22;  /* 'wav audio file' */
            } else {
                size += (8 + strlen(valarray[29]));
            }
        }
        if(ISSTR(valarray[3]) && db_wantsmeta(pinfo->meta,metaItemName))
            /* minm */
            size += (8 + strlen(valarray[3]));
        if(valarray[34] && atoi(valarray[34]) && db_wantsmeta(pinfo->meta,metaSongDisabled))
            /* asdb */
            size += 9;
        if(valarray[15] && atoi(valarray[15]) && db_wantsmeta(pinfo->meta,metaSongSampleRate))
            /* assr */
            size += 12;
        if(valarray[17] && atoi(valarray[17]) && db_wantsmeta(pinfo->meta,metaSongSize))
            /* assz */
            size += 12;

        /* In the old daap code, we always returned 0 for asst and assp
         * (song start time, song stop time).  I don't know if this
         * is required, so I'm going to disabled it
         */

        if(valarray[16] && atoi(valarray[16]) && db_wantsmeta(pinfo->meta, metaSongTime))
            /* astm */
            size += 12;
        if(valarray[20] && atoi(valarray[20]) && db_wantsmeta(pinfo->meta, metaSongTrackCount))
            /* astc */
            size += 10;
        if(valarray[19] && atoi(valarray[19]) && db_wantsmeta(pinfo->meta, metaSongTrackNumber))
            /* astn */
            size += 10;
        if(valarray[25] && atoi(valarray[25]) && db_wantsmeta(pinfo->meta, metaSongUserRating))
            /* asur */
            size += 9;
        if(valarray[18] && atoi(valarray[18]) && db_wantsmeta(pinfo->meta, metaSongYear))
            /* asyr */
            size += 10;
        if(db_wantsmeta(pinfo->meta, metaContainerItemId))
            /* mcti */
            size += 12;
        if(ISSTR(valarray[37]) && db_wantsmeta(pinfo->meta, metaSongCodecType))
            /* ascd */
            size += 12;
        return size;
        break;

    default:
        DPRINTF(E_LOG,L_DB|L_DAAP,"Unknown query type: %d\n",(int)pinfo->query_type);
        return 0;
    }
    return 0;
}

int db_sqlite_build_dmap(DBQUERYINFO *pinfo, char **valarray, unsigned char *presult, int len) {
    unsigned char *current = presult;
    int transcode;
    int samplerate=0;

    switch(pinfo->query_type) {
    case queryTypeBrowseArtists: /* simple 'mlit' entry */
    case queryTypeBrowseAlbums:
    case queryTypeBrowseGenres:
    case queryTypeBrowseComposers:
        return db_dmap_add_string(current,"mlit",valarray[0]);
    case queryTypePlaylists:
        /* do I want to include the mlit? */
        current += db_dmap_add_container(current,"mlit",len - 8);
        if(db_wantsmeta(pinfo->meta,metaItemId))
            current += db_dmap_add_int(current,"miid",atoi(valarray[plID]));
        current += db_dmap_add_int(current,"mimc",atoi(valarray[plItems]));
        if(db_wantsmeta(pinfo->meta,metaItunesSmartPlaylist)) {
            if(valarray[plType] && (atoi(valarray[plType]) == 1))
                current += db_dmap_add_char(current,"aeSP",1);
        }
        if(db_wantsmeta(pinfo->meta,metaItemName)) 
            current += db_dmap_add_string(current,"minm",valarray[plTitle]);
        if((valarray[plType]) && (atoi(valarray[plType])==1) && 
           db_wantsmeta(pinfo->meta, metaMPlaylistSpec))
            current += db_dmap_add_string(current,"MSPS",valarray[plQuery]);
        if(db_wantsmeta(pinfo->meta, metaMPlaylistType))
            current += db_dmap_add_char(current,"MPTY",atoi(valarray[plType]));
        break;
    case queryTypeItems:
    case queryTypePlaylistItems:  /* essentially the same query */
        /* see if this is going to be transcoded */
        transcode = server_side_convert(valarray[2]);
        
        /* Items that get changed by transcode:
         *
         * type:         item  8: changes to 'wav'
         * description:  item 29: changes to 'wav audio file'
         * bitrate:      item 15: guestimated, but doesn't change file size
         * 
         * probably file size should change as well, but currently doesn't
         */

        current += db_dmap_add_container(current,"mlit",len-8);
        if(db_wantsmeta(pinfo->meta, metaItemKind)) 
            current += db_dmap_add_char(current,"mikd",(char)atoi(valarray[28]));
        if(db_wantsmeta(pinfo->meta, metaSongDataKind))
            current += db_dmap_add_char(current,"asdk",(char)atoi(valarray[27]));
        if(ISSTR(valarray[13]) && db_wantsmeta(pinfo->meta, metaSongDataURL)) 
            current += db_dmap_add_string(current,"asul",valarray[13]);
        if(ISSTR(valarray[5]) && db_wantsmeta(pinfo->meta, metaSongAlbum))    
            current += db_dmap_add_string(current,"asal",valarray[5]);
        if(ISSTR(valarray[4]) && db_wantsmeta(pinfo->meta, metaSongArtist))   
            current += db_dmap_add_string(current,"asar",valarray[4]);
        if(valarray[23] && atoi(valarray[23]) && db_wantsmeta(pinfo->meta, metaSongBPM))      
            current += db_dmap_add_short(current,"asbt",(short)atoi(valarray[23]));
        if(valarray[14] && atoi(valarray[14]) && db_wantsmeta(pinfo->meta, metaSongBitRate)) {
            if(transcode) {
                if(valarray[15]) samplerate=atoi(valarray[15]);
                if(samplerate) {
                    current += db_dmap_add_short(current,"asbr",
                                                 (short)(samplerate * 4 * 8)/1000);
                }
            } else {
                current += db_dmap_add_short(current,"asbr",(short)atoi(valarray[14]));
            }
        }
        if(ISSTR(valarray[7]) && db_wantsmeta(pinfo->meta, metaSongComment))  
            current += db_dmap_add_string(current,"ascm",valarray[7]);
        if(valarray[24] && atoi(valarray[24]) && db_wantsmeta(pinfo->meta,metaSongCompilation)) 
            current += db_dmap_add_char(current,"asco",(char)atoi(valarray[24]));
        if(ISSTR(valarray[9]) && db_wantsmeta(pinfo->meta, metaSongComposer))
            current += db_dmap_add_string(current,"ascp",valarray[9]);
        if(ISSTR(valarray[12]) && db_wantsmeta(pinfo->meta, metaSongGrouping))
            current += db_dmap_add_string(current,"agrp",valarray[12]);
        if(valarray[30] && atoi(valarray[30]) && db_wantsmeta(pinfo->meta, metaSongDateAdded))
            current += db_dmap_add_int(current,"asda",(int)atoi(valarray[30]));
        if(valarray[31] && atoi(valarray[31]) && db_wantsmeta(pinfo->meta,metaSongDateModified))
            current += db_dmap_add_int(current,"asdm",(int)atoi(valarray[31]));
        if(valarray[22] && atoi(valarray[22]) && db_wantsmeta(pinfo->meta, metaSongDiscCount))
            current += db_dmap_add_short(current,"asdc",(short)atoi(valarray[22]));
        if(valarray[21] && atoi(valarray[21]) && db_wantsmeta(pinfo->meta, metaSongDiscNumber))
            current += db_dmap_add_short(current,"asdn",(short)atoi(valarray[21]));
        if(ISSTR(valarray[6]) && db_wantsmeta(pinfo->meta, metaSongGenre))
            current += db_dmap_add_string(current,"asgn",valarray[6]);
        if(db_wantsmeta(pinfo->meta,metaItemId))
            current += db_dmap_add_int(current,"miid",(int)atoi(valarray[0]));
        if(ISSTR(valarray[8]) && db_wantsmeta(pinfo->meta,metaSongFormat)) {
            if(transcode) {
                current += db_dmap_add_string(current,"asfm","wav");
            } else {
                current += db_dmap_add_string(current,"asfm",valarray[8]);
            }
        }
        if(ISSTR(valarray[29]) && db_wantsmeta(pinfo->meta,metaSongDescription)) {
            if(transcode) {
                current += db_dmap_add_string(current,"asdt","wav audio file");
            } else {
                current += db_dmap_add_string(current,"asdt",valarray[29]);
            }
        }
        if(ISSTR(valarray[3]) && db_wantsmeta(pinfo->meta,metaItemName))
            current += db_dmap_add_string(current,"minm",valarray[3]);
        if(valarray[34] && atoi(valarray[34]) && db_wantsmeta(pinfo->meta,metaSongDisabled))
            current += db_dmap_add_char(current,"asdb",(char)atoi(valarray[34]));
        if(valarray[15] && atoi(valarray[15]) && db_wantsmeta(pinfo->meta,metaSongSampleRate))
            current += db_dmap_add_int(current,"assr",atoi(valarray[15]));
        if(valarray[17] && atoi(valarray[17]) && db_wantsmeta(pinfo->meta,metaSongSize))
            current += db_dmap_add_int(current,"assz",atoi(valarray[17]));
        if(valarray[16] && atoi(valarray[16]) && db_wantsmeta(pinfo->meta, metaSongTime))
            current += db_dmap_add_int(current,"astm",atoi(valarray[16]));
        if(valarray[20] && atoi(valarray[20]) && db_wantsmeta(pinfo->meta, metaSongTrackCount))
            current += db_dmap_add_short(current,"astc",(short)atoi(valarray[20]));
        if(valarray[19] && atoi(valarray[19]) && db_wantsmeta(pinfo->meta, metaSongTrackNumber))
            current += db_dmap_add_short(current,"astn",(short)atoi(valarray[19]));
        if(valarray[25] && atoi(valarray[25]) && db_wantsmeta(pinfo->meta, metaSongUserRating))
            current += db_dmap_add_char(current,"asur",(char)atoi(valarray[25]));
        if(valarray[18] && atoi(valarray[18]) && db_wantsmeta(pinfo->meta, metaSongYear))
            current += db_dmap_add_short(current,"asyr",(short)atoi(valarray[18]));
        if(ISSTR(valarray[37]) && db_wantsmeta(pinfo->meta, metaSongCodecType))
            current += db_dmap_add_literal(current,"ascd",valarray[37],4);
        if(db_wantsmeta(pinfo->meta, metaContainerItemId))
            current += db_dmap_add_int(current,"mcti",atoi(valarray[0]));
        return 0;
        break;

    default:
        DPRINTF(E_LOG,L_DB|L_DAAP,"Unknown query type: %d\n",(int)pinfo->query_type);
        return 0;
    }
    return 0;
}

int db_sqlite_atoi(const char *what) {
    return what ? atoi(what) : 0;
}
char *db_sqlite_strdup(const char *what) {
    return what ? (strlen(what) ? strdup(what) : NULL) : NULL;
}

void db_sqlite_build_m3ufile(char **valarray, M3UFILE *pm3u) {
    memset(pm3u,0x00,sizeof(M3UFILE));

    pm3u->id=db_sqlite_atoi(valarray[0]);
    pm3u->title=db_sqlite_strdup(valarray[1]);
    pm3u->type=db_sqlite_atoi(valarray[2]);
    pm3u->items=db_sqlite_atoi(valarray[3]);
    pm3u->query=db_sqlite_strdup(valarray[4]);
    pm3u->db_timestamp=db_sqlite_atoi(valarray[5]);
    pm3u->path=db_sqlite_strdup(valarray[6]);
    pm3u->index=db_sqlite_atoi(valarray[7]);
    return;
}

void db_sqlite_build_mp3file(char **valarray, MP3FILE *pmp3) {
    memset(pmp3,0x00,sizeof(MP3FILE));
    pmp3->id=db_sqlite_atoi(valarray[0]);
    pmp3->path=db_sqlite_strdup(valarray[1]);
    pmp3->fname=db_sqlite_strdup(valarray[2]);
    pmp3->title=db_sqlite_strdup(valarray[3]);
    pmp3->artist=db_sqlite_strdup(valarray[4]);
    pmp3->album=db_sqlite_strdup(valarray[5]);
    pmp3->genre=db_sqlite_strdup(valarray[6]);
    pmp3->comment=db_sqlite_strdup(valarray[7]);
    pmp3->type=db_sqlite_strdup(valarray[8]);
    pmp3->composer=db_sqlite_strdup(valarray[9]);
    pmp3->orchestra=db_sqlite_strdup(valarray[10]);
    pmp3->conductor=db_sqlite_strdup(valarray[11]);
    pmp3->grouping=db_sqlite_strdup(valarray[12]);
    pmp3->url=db_sqlite_strdup(valarray[13]);
    pmp3->bitrate=db_sqlite_atoi(valarray[14]);
    pmp3->samplerate=db_sqlite_atoi(valarray[15]);
    pmp3->song_length=db_sqlite_atoi(valarray[16]);
    pmp3->file_size=db_sqlite_atoi(valarray[17]);
    pmp3->year=db_sqlite_atoi(valarray[18]);
    pmp3->track=db_sqlite_atoi(valarray[19]);
    pmp3->total_tracks=db_sqlite_atoi(valarray[20]);
    pmp3->disc=db_sqlite_atoi(valarray[21]);
    pmp3->total_discs=db_sqlite_atoi(valarray[22]);
    pmp3->bpm=db_sqlite_atoi(valarray[23]);
    pmp3->compilation=db_sqlite_atoi(valarray[24]);
    pmp3->rating=db_sqlite_atoi(valarray[25]);
    pmp3->play_count=db_sqlite_atoi(valarray[26]);
    pmp3->data_kind=db_sqlite_atoi(valarray[27]);
    pmp3->item_kind=db_sqlite_atoi(valarray[28]);
    pmp3->description=db_sqlite_strdup(valarray[29]);
    pmp3->time_added=db_sqlite_atoi(valarray[30]);
    pmp3->time_modified=db_sqlite_atoi(valarray[31]);
    pmp3->time_played=db_sqlite_atoi(valarray[32]);
    pmp3->db_timestamp=db_sqlite_atoi(valarray[33]);
    pmp3->disabled=db_sqlite_atoi(valarray[34]);
    pmp3->sample_count=db_sqlite_atoi(valarray[35]);
    pmp3->force_update=db_sqlite_atoi(valarray[36]);
    pmp3->codectype=db_sqlite_strdup(valarray[37]);
    pmp3->index=db_sqlite_atoi(valarray[38]);
}

/**
 * fetch a playlist by path
 *
 * \param path path to fetch
 */
M3UFILE *db_sqlite_fetch_playlist(char *path, int index) {
    int rows,cols;
    char **resarray;
    int result;
    M3UFILE *pm3u=NULL;

    result = db_sqlite_get_table(E_DBG,&resarray,&rows,&cols,
                                 "select * from playlists where path='%q' and idx=%d",
                                 path,index);

    if(result != DB_E_SUCCESS) 
        return NULL;

    if(rows != 0) {
        pm3u=(M3UFILE*)malloc(sizeof(M3UFILE));
        if(!pm3u)
            DPRINTF(E_FATAL,L_MISC,"malloc error: db_sqlite_fetch_playlist\n");
        
        db_sqlite_build_m3ufile((char**)&resarray[cols],pm3u);
    }
    
    db_sqlite_free_table(resarray);

    if((rows) && (db_sqlite_in_playlist_scan) && (!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"insert into plupdated values (%d)",pm3u->id);
    }

    return pm3u;
}

/**
 * fetch a MP3FILE for a specific id
 *
 * \param id id to fetch
 */
MP3FILE *db_sqlite_fetch_item(int id) {
    int rows,cols;
    char **resarray;
    MP3FILE *pmp3=NULL;

    db_sqlite_get_table(E_DBG,&resarray,&rows,&cols,
                        "SELECT * FROM songs WHERE id=%d",id);

    if(rows != 0) {
        pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
        if(!pmp3) 
            DPRINTF(E_FATAL,L_MISC,"Malloc error in db_sqlite_fetch_item\n");

        db_sqlite_build_mp3file((char **)&resarray[cols],pmp3);
    }

    db_sqlite_free_table(resarray);

    if ((rows) && (db_sqlite_in_scan) && (!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"INSERT INTO updated VALUES (%d)",id);
    }

    return pmp3;
}

/**
 * retrieve a MP3FILE struct for the song with a give path
 *
 * \param path path of the file to retreive
 */
MP3FILE *db_sqlite_fetch_path(char *path, int index) {
    int rows,cols;
    char **resarray;
    MP3FILE *pmp3=NULL;

    db_sqlite_get_table(E_DBG,&resarray,&rows,&cols,
                        "SELECT * FROM songs WHERE path='%q' and idx=%d",
                        path,index);

    if(rows != 0) {
        pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
        if(!pmp3) 
            DPRINTF(E_FATAL,L_MISC,"Malloc error in db_sqlite_fetch_item\n");

        db_sqlite_build_mp3file((char **)&resarray[cols],pmp3);
    }

    db_sqlite_free_table(resarray);

    if ((rows) && (db_sqlite_in_scan) && (!db_sqlite_reload)) {
        db_sqlite_exec(E_FATAL,"INSERT INTO updated VALUES (%d)",pmp3->id);
    }

    return pmp3;
}

/** 
 * dispose of a MP3FILE struct that was obtained 
 * from db_sqlite_fetch_item
 *
 * \param pmp3 item obtained from db_sqlite_fetch_item
 */
void db_sqlite_dispose_item(MP3FILE *pmp3) {
    if(!pmp3)
        return;

    MAYBEFREE(pmp3->path);
    MAYBEFREE(pmp3->fname);
    MAYBEFREE(pmp3->title);
    MAYBEFREE(pmp3->artist);
    MAYBEFREE(pmp3->album);
    MAYBEFREE(pmp3->genre);
    MAYBEFREE(pmp3->comment);
    MAYBEFREE(pmp3->type); 
    MAYBEFREE(pmp3->composer);
    MAYBEFREE(pmp3->orchestra);
    MAYBEFREE(pmp3->conductor);
    MAYBEFREE(pmp3->grouping);
    MAYBEFREE(pmp3->description);
    MAYBEFREE(pmp3->url);
    MAYBEFREE(pmp3->codectype);
    free(pmp3);
}

void db_sqlite_dispose_playlist(M3UFILE *pm3u) {
    if(!pm3u)
        return;

    MAYBEFREE(pm3u->title);
    MAYBEFREE(pm3u->query);
    MAYBEFREE(pm3u->path);
    free(pm3u);
}

/**
 * count either the number of playlists, or the number of
 * songs
 *
 * \param type either countPlaylists or countSongs (type to count)
 */
int db_sqlite_get_count(CountType_t type) {
    char *table;
    int rows, cols;
    char **resarray;
    int retval=0;
    
    switch(type) {
    case countPlaylists:
        table="playlists";
        break;

    case countSongs:
    default:
        table="songs";
        break;
    }

    db_sqlite_get_table(E_DBG,&resarray, &rows, &cols,
                        "SELECT COUNT(*) FROM %q", table);

    if(rows != 0) {
        retval=atoi(resarray[cols]);
    }

    db_sqlite_free_table(resarray);
    return retval;
}

/**
 * get the database version of the currently opened database
 */
int db_sqlite_get_version(void) {
    int rows, cols;
    char **resarray;
    int retval=0;
    
    db_sqlite_get_table(E_DBG,&resarray, &rows, &cols,
                        "select value from config where term='version'");

    if(rows != 0) {
        retval=atoi(resarray[cols]);
    }

    db_sqlite_free_table(resarray);
    return retval;
}


char *db_sqlite_upgrade_scripts[] = {
    /* version 0 -> version 1 -- initial update */
    "CREATE TABLE songs (\n"
    "   id              INTEGER PRIMARY KEY NOT NULL,\n"
    "   path            VARCHAR(4096) UNIQUE NOT NULL,\n"
    "   fname           VARCHAR(255) NOT NULL,\n"
    "   title           VARCHAR(1024) DEFAULT NULL,\n"
    "   artist          VARCHAR(1024) DEFAULT NULL,\n"
    "   album           VARCHAR(1024) DEFAULT NULL,\n"
    "   genre           VARCHAR(255) DEFAULT NULL,\n"
    "   comment         VARCHAR(4096) DEFAULT NULL,\n"
    "   type            VARCHAR(255) DEFAULT NULL,\n"
    "   composer        VARCHAR(1024) DEFAULT NULL,\n"
    "   orchestra       VARCHAR(1024) DEFAULT NULL,\n"
    "   conductor       VARCHAR(1024) DEFAULT NULL,\n"
    "   grouping        VARCHAR(1024) DEFAULT NULL,\n"
    "   url             VARCHAR(1024) DEFAULT NULL,\n"
    "   bitrate         INTEGER DEFAULT 0,\n"
    "   samplerate      INTEGER DEFAULT 0,\n"
    "   song_length     INTEGER DEFAULT 0,\n"
    "   file_size       INTEGER DEFAULT 0,\n"
    "   year            INTEGER DEFAULT 0,\n"
    "   track           INTEGER DEFAULT 0,\n"
    "   total_tracks    INTEGER DEFAULT 0,\n"
    "   disc            INTEGER DEFAULT 0,\n"
    "   total_discs     INTEGER DEFAULT 0,\n"
    "   bpm             INTEGER DEFAULT 0,\n"
    "   compilation     INTEGER DEFAULT 0,\n"
    "   rating          INTEGER DEFAULT 0,\n"
    "   play_count      INTEGER DEFAULT 0,\n"
    "   data_kind       INTEGER DEFAULT 0,\n"
    "   item_kind       INTEGER DEFAULT 0,\n"
    "   description     INTEGER DEFAULT 0,\n"
    "   time_added      INTEGER DEFAULT 0,\n"
    "   time_modified   INTEGER DEFAULT 0,\n"
    "   time_played     INTEGER DEFAULT 0,\n"
    "   db_timestamp    INTEGER DEFAULT 0,\n"
    "   disabled        INTEGER DEFAULT 0,\n"
    "   sample_count    INTEGER DEFAULT 0,\n"
    "   force_update    INTEGER DEFAULT 0\n"
    ");\n"
    "CREATE INDEX idx_path ON songs(path);\n" 
    "CREATE TABLE config (\n"
    "   term            VARCHAR(255)    NOT NULL,\n"
    "   subterm         VARCHAR(255)    DEFAULT NULL,\n"
    "   value           VARCHAR(1024)   NOT NULL\n"
    ");\n"
    "CREATE TABLE playlists (\n"
    "   id             INTEGER PRIMARY KEY NOT NULL,\n"
    "   title          VARCHAR(255) NOT NULL,\n"
    "   smart          INTEGER NOT NULL,\n"
    "   items          INTEGER NOT NULL,\n"
    "   query          VARCHAR(1024)\n"
    ");\n"
    "CREATE TABLE playlistitems (\n"
    "   id              INTEGER NOT NULL,\n"
    "   songid         INTEGER NOT NULL\n"
    ");\n"
    "INSERT INTO config VALUES ('version','','1');\n"
    "INSERT INTO playlists VALUES (1,'Library',1,0,'1');\n",

    /* version 1 -> version 2 */
    /* force rescan for invalid utf-8 data */
    "REPLACE INTO config VALUES('rescan',NULL,1);\n"
    "UPDATE config SET value=2 WHERE term='version';\n",

    /* version 2 -> version 3 */
    /* add daap.songcodectype, normalize daap.songformat and daap.songdescription */
    "drop index idx_path;\n"
    "create temp table tempsongs as select * from songs;\n"
    "drop table songs;\n"
    "CREATE TABLE songs (\n"
    "   id              INTEGER PRIMARY KEY NOT NULL,\n"
    "   path            VARCHAR(4096) UNIQUE NOT NULL,\n"
    "   fname           VARCHAR(255) NOT NULL,\n"
    "   title           VARCHAR(1024) DEFAULT NULL,\n"
    "   artist          VARCHAR(1024) DEFAULT NULL,\n"
    "   album           VARCHAR(1024) DEFAULT NULL,\n"
    "   genre           VARCHAR(255) DEFAULT NULL,\n"
    "   comment         VARCHAR(4096) DEFAULT NULL,\n"
    "   type            VARCHAR(255) DEFAULT NULL,\n"
    "   composer        VARCHAR(1024) DEFAULT NULL,\n"
    "   orchestra       VARCHAR(1024) DEFAULT NULL,\n"
    "   conductor       VARCHAR(1024) DEFAULT NULL,\n"
    "   grouping        VARCHAR(1024) DEFAULT NULL,\n"
    "   url             VARCHAR(1024) DEFAULT NULL,\n"
    "   bitrate         INTEGER DEFAULT 0,\n"
    "   samplerate      INTEGER DEFAULT 0,\n"
    "   song_length     INTEGER DEFAULT 0,\n"
    "   file_size       INTEGER DEFAULT 0,\n"
    "   year            INTEGER DEFAULT 0,\n"
    "   track           INTEGER DEFAULT 0,\n"
    "   total_tracks    INTEGER DEFAULT 0,\n"
    "   disc            INTEGER DEFAULT 0,\n"
    "   total_discs     INTEGER DEFAULT 0,\n"
    "   bpm             INTEGER DEFAULT 0,\n"
    "   compilation     INTEGER DEFAULT 0,\n"
    "   rating          INTEGER DEFAULT 0,\n"
    "   play_count      INTEGER DEFAULT 0,\n"
    "   data_kind       INTEGER DEFAULT 0,\n"
    "   item_kind       INTEGER DEFAULT 0,\n"
    "   description     INTEGER DEFAULT 0,\n"
    "   time_added      INTEGER DEFAULT 0,\n"
    "   time_modified   INTEGER DEFAULT 0,\n"
    "   time_played     INTEGER DEFAULT 0,\n"
    "   db_timestamp    INTEGER DEFAULT 0,\n"
    "   disabled        INTEGER DEFAULT 0,\n"
    "   sample_count    INTEGER DEFAULT 0,\n"
    "   force_update    INTEGER DEFAULT 0,\n"
    "   codectype       VARCHAR(5) DEFAULT NULL\n"
    ");\n"
    "begin transaction;\n"
    "insert into songs select *,NULL from tempsongs;\n"
    "commit transaction;\n"
    "update songs set type=lower(type);\n"
    "update songs set type='m4a' where type='aac' or type='mp4';\n"
    "update songs set type='flac' where type='fla';\n"
    "update songs set description='AAC audio file' where type='m4a';\n"
    "update songs set description='MPEG audio file' where type='mp3';\n"
    "update songs set description='WAV audio file' where type='wav';\n"
    "update songs set description='Playlist URL' where type='pls';\n"
    "update songs set description='Ogg Vorbis audio file' where type='ogg';\n"
    "update songs set description='FLAC audio file' where type='flac';\n"
    "update songs set codectype='mp4a' where type='m4a' or type='m4p';\n"
    "update songs set codectype='mpeg' where type='mp3';\n"
    "update songs set codectype='ogg' where type='ogg';\n"
    "update songs set codectype='flac' where type='flac';\n"
    "update songs set force_update=1 where type='m4a';\n"      /* look for alac */
    "create index idx_path on songs(path);\n"
    "drop table tempsongs;\n"
    "update config set value=3 where term='version';\n",
    
    /* version 3 -> version 4 */
    /* add db_timestamp and path to playlist table */
    "create temp table tempplaylists as select * from playlists;\n"
    "drop table playlists;\n"
    "CREATE TABLE playlists (\n"
    "   id             INTEGER PRIMARY KEY NOT NULL,\n"
    "   title          VARCHAR(255) NOT NULL,\n"
    "   type           INTEGER NOT NULL,\n"
    "   items          INTEGER NOT NULL,\n"
    "   query          VARCHAR(1024),\n"
    "   db_timestamp   INTEGER NOT NULL,\n"
    "   path           VARCHAR(4096)\n"
    ");\n"
    "insert into playlists select *,0,NULL from tempplaylists;\n"
    "drop table tempplaylists;\n"
    "update config set value=4 where term='version';\n",

    /* version 4 -> version 5 */
    /* add index to playlist table */
    "create temp table tempplaylists as select * from playlists;\n"
    "drop table playlists;\n"
    "CREATE TABLE playlists (\n"
    "   id             INTEGER PRIMARY KEY NOT NULL,\n"
    "   title          VARCHAR(255) NOT NULL,\n"
    "   type           INTEGER NOT NULL,\n"
    "   items          INTEGER NOT NULL,\n"
    "   query          VARCHAR(1024),\n"
    "   db_timestamp   INTEGER NOT NULL,\n"
    "   path           VARCHAR(4096),\n"
    "   idx            INTEGER NOT NULL\n"
    ");\n"
    "insert into playlists select *,0 from tempplaylists;\n"
    "drop table tempplaylists;\n"
    "update config set value=5 where term='version';\n",

    /* version 5 -> version 6 */
    "drop index idx_path;\n"
    "create temp table tempsongs as select * from songs;\n"
    "drop table songs;\n"
    "CREATE TABLE songs (\n"
    "   id              INTEGER PRIMARY KEY NOT NULL,\n"
    "   path            VARCHAR(4096) UNIQUE NOT NULL,\n"
    "   fname           VARCHAR(255) NOT NULL,\n"
    "   title           VARCHAR(1024) DEFAULT NULL,\n"
    "   artist          VARCHAR(1024) DEFAULT NULL,\n"
    "   album           VARCHAR(1024) DEFAULT NULL,\n"
    "   genre           VARCHAR(255) DEFAULT NULL,\n"
    "   comment         VARCHAR(4096) DEFAULT NULL,\n"
    "   type            VARCHAR(255) DEFAULT NULL,\n"
    "   composer        VARCHAR(1024) DEFAULT NULL,\n"
    "   orchestra       VARCHAR(1024) DEFAULT NULL,\n"
    "   conductor       VARCHAR(1024) DEFAULT NULL,\n"
    "   grouping        VARCHAR(1024) DEFAULT NULL,\n"
    "   url             VARCHAR(1024) DEFAULT NULL,\n"
    "   bitrate         INTEGER DEFAULT 0,\n"
    "   samplerate      INTEGER DEFAULT 0,\n"
    "   song_length     INTEGER DEFAULT 0,\n"
    "   file_size       INTEGER DEFAULT 0,\n"
    "   year            INTEGER DEFAULT 0,\n"
    "   track           INTEGER DEFAULT 0,\n"
    "   total_tracks    INTEGER DEFAULT 0,\n"
    "   disc            INTEGER DEFAULT 0,\n"
    "   total_discs     INTEGER DEFAULT 0,\n"
    "   bpm             INTEGER DEFAULT 0,\n"
    "   compilation     INTEGER DEFAULT 0,\n"
    "   rating          INTEGER DEFAULT 0,\n"
    "   play_count      INTEGER DEFAULT 0,\n"
    "   data_kind       INTEGER DEFAULT 0,\n"
    "   item_kind       INTEGER DEFAULT 0,\n"
    "   description     INTEGER DEFAULT 0,\n"
    "   time_added      INTEGER DEFAULT 0,\n"
    "   time_modified   INTEGER DEFAULT 0,\n"
    "   time_played     INTEGER DEFAULT 0,\n"
    "   db_timestamp    INTEGER DEFAULT 0,\n"
    "   disabled        INTEGER DEFAULT 0,\n"
    "   sample_count    INTEGER DEFAULT 0,\n"
    "   force_update    INTEGER DEFAULT 0,\n"
    "   codectype       VARCHAR(5) DEFAULT NULL,\n"
    "   idx             INTEGER NOT NULL\n"
    ");\n"
    "begin transaction;\n"
    "insert into songs select *,0 from tempsongs;\n"
    "commit transaction;\n"
    "create index idx_path on songs(path);\n"
    "drop table tempsongs;\n"
    "update config set value=6 where term='version';\n",

    /* version 6 -> version 7 */
    "create temp table tempitems as select * from playlistitems;\n"
    "drop table playlistitems;\n"
    "CREATE TABLE playlistitems (\n"
    "   id             INTEGER PRIMARY KEY NOT NULL,\n"
    "   playlistid     INTEGER NOT NULL,\n"
    "   songid         INTEGER NOT NULL\n"
    ");\n"
    "insert into playlistitems (playlistid, songid) select * from tempitems;\n"
    "drop table tempitems;\n"
    "update config set value=7 where term='version';\n",
    NULL /* No more versions! */
};

/**
 * Upgrade database from an older version of the database to the newest
 *
 * \param from_version the current version of the database
 */
int db_sqlite_update_version(int from_version) {
    char db_new_path[PATH_MAX + 1];
    int from_fd, to_fd;
    int copied=0;
    int result;

    if(from_version > (sizeof(db_sqlite_upgrade_scripts)/sizeof(char*))) {
        DPRINTF(E_FATAL,L_DB,"Database version too new (time machine, maybe?)\n");
    }

    while(db_sqlite_upgrade_scripts[from_version]) {
        DPRINTF(E_LOG,L_DB,"Upgrading database from version %d to version %d\n",from_version,
                from_version+1);

        if(!copied) {
            /* copy original version */
            sprintf(db_new_path,"%s.version-%02d",db_path,from_version);
            from_fd=r_open2(db_path,O_RDONLY);
            to_fd=r_open3(db_new_path,O_RDWR | O_CREAT,0666);

            if((from_fd == -1) || (to_fd == -1)) {
                DPRINTF(E_FATAL,L_DB,"Could not make backup copy of database "
                        "(%s).  Check write permissions for runas user.\n",
                        db_new_path);
            }

            while((result=readwrite(from_fd, to_fd) > 0));
            
            if(result == -1) {
                DPRINTF(E_FATAL,L_DB,"Could not make db backup (%s)\n",
                        strerror(errno));
            }

            r_close(from_fd);
            r_close(to_fd);
            
            copied=1;
        }

        if(db_sqlite_exec(E_LOG,db_sqlite_upgrade_scripts[from_version]) != DB_E_SUCCESS) {
            DPRINTF(E_FATAL,L_DB,"Error upgrading database.  A backup copy of "
                    "you original database is located at %s.  Please save it "
                    " somewhere and report to the forums at www.mt-daapd.org. "
                    " Thanks.\n",
                    db_new_path);
        }
        from_version++;
    }

    /* removed our backup file */
    if(copied)   
        unlink(db_new_path);

    return 0;
}

