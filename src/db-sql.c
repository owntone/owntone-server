/*
 * $Id$
 * sql-specific db implementation
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
#include "db-sql.h"
#include "db-sql-sqlite2.h"
#include "restart.h"
#include "ssc.h"
#include "smart-parser.h"

/* Globals */
static int db_sql_reload=0;
static int db_sql_in_playlist_scan=0;
static int db_sql_in_scan=0;
static int db_sql_need_dispose=0;

/* Forwards */
int db_sql_get_size(DBQUERYINFO *pinfo, char **valarray);
int db_sql_build_dmap(DBQUERYINFO *pinfo, char **valarray, unsigned char *presult, int len);
void db_sql_build_mp3file(char **valarray, MP3FILE *pmp3);
int db_sql_update(char **pe, MP3FILE *pmp3);
int db_sql_update_playlists(char **pe);
char *db_sql_parse_smart(char *phrase);


#define STR(a) (a) ? (a) : ""
#define ISSTR(a) ((a) && strlen((a)))
#define MAYBEFREE(a) { if((a)) free((a)); };


/**
 * fetch a single row, using the underlying database enum
 * functions
 */
int db_sql_fetch_row(char **pe, SQL_ROW *row, char *fmt, ...) {
    int err;
    char *query;
    va_list ap;


    db_sql_need_dispose = 0;
    *row=NULL;

    va_start(ap,fmt);
    query=db_sqlite2_vmquery(fmt,ap);
    va_end(ap);

    err=db_sqlite2_enum_begin(pe,query);
    db_sqlite2_vmfree(query);

    if(err != DB_E_SUCCESS) {
        return err;
    }

    err=db_sqlite2_enum_fetch(pe, row);
    if(err != DB_E_SUCCESS) {
        db_sqlite2_enum_end(NULL);
        return err;
    }

    if(!(*row)) {
        db_sqlite2_enum_end(NULL);
        db_get_error(pe,DB_E_NOROWS);
        return DB_E_NOROWS;
    }

    db_sql_need_dispose = 1;
    return DB_E_SUCCESS;
}

int db_sql_fetch_int(char **pe, int *result, char *fmt, ...) {
    int err;
    char *query;
    va_list ap;
    SQL_ROW row;

    va_start(ap,fmt);
    query=db_sqlite2_vmquery(fmt,ap);
    va_end(ap);

    err = db_sql_fetch_row(pe, &row, query);
    if(err != DB_E_SUCCESS)
        return err;

    *result = atoi(row[0]);
    db_sql_dispose_row();
    return DB_E_SUCCESS;
}

int db_sql_fetch_char(char **pe, char **result, char *fmt, ...) {
    int err;
    char *query;
    va_list ap;
    SQL_ROW row;

    va_start(ap,fmt);
    query=db_sqlite2_vmquery(fmt,ap);
    va_end(ap);

    err = db_sql_fetch_row(pe, &row, query);
    if(err != DB_E_SUCCESS)
        return err;

    *result = strdup(row[0]);
    db_sql_dispose_row();
    return DB_E_SUCCESS;
}

int db_sql_dispose_row(void) {
    int err = DB_E_SUCCESS;

    /* don't really need the row */
    if(db_sql_need_dispose) {
        err=db_sqlite2_enum_end(NULL);
        db_sql_need_dispose=0;
    }

    return err;
}

/**
 * get the sql where clause for a smart playlist spec.  This
 * where clause must be freed by the caller
 *
 * @param phrase playlist spec to be converted
 * @returns sql where clause if successful, NULL otherwise
 */
char *db_sql_parse_smart(char *phrase) {
    PARSETREE pt;
    char *result = NULL;

    if(strcmp(phrase,"1") == 0)
        return strdup("1");

    pt=sp_init();
    if(!pt)
        return NULL;

    if(!sp_parse(pt,phrase)) {
        DPRINTF(E_LOG,L_DB,"Error parsing smart playlist: %s",sp_get_error(pt));
        sp_dispose(pt);
        return strdup("0");
    } else {
        result = sp_sql_clause(pt);
    }

    sp_dispose(pt);
    return result;
}

/**
 * open sqlite database
 *
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_open(char **pe, char *parameters) {
    return db_sqlite2_open(pe, parameters);
}

/**
 * initialize the sqlite database, reloading if requested
 *
 * @param reload whether or not to do a full reload on the db
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_init(int reload) {
    int items;
    int rescan = 0;
    int err;


    err=db_sql_get_count(NULL,&items, countSongs);
    if(err != DB_E_SUCCESS)
        items = 0;

    /* check if a request has been written into the db (by a db upgrade?) */
    if(db_sql_fetch_int(NULL,&rescan,"select value from config where "
                        "term='rescan'") == DB_E_SUCCESS)
    {
        if(rescan)
            reload=1;
    }



    if(reload || (!items)) {
        DPRINTF(E_LOG,L_DB,"Full reload...\n");
        db_sqlite2_event(DB_SQL_EVENT_FULLRELOAD);
        db_sql_reload=1;
    } else {
        db_sqlite2_event(DB_SQL_EVENT_STARTUP);
        db_sql_reload=0;
    }

    return DB_E_SUCCESS;
}

/**
 * close the database
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_deinit(void) {
    return db_sqlite2_close();
}

/**
 * start a background scan
 *
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_start_scan(void) {
    DPRINTF(E_DBG,L_DB,"Starting db scan\n");
    db_sqlite2_event(DB_SQL_EVENT_SONGSCANSTART);

    db_sql_in_scan=1;
    db_sql_in_playlist_scan=0;
    return DB_E_SUCCESS;
}

/**
 * end song scan -- start playlist scan
 *
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_end_song_scan(void) {
    DPRINTF(E_DBG,L_DB,"Ending song scan\n");

    db_sqlite2_event(DB_SQL_EVENT_SONGSCANEND);
    db_sqlite2_event(DB_SQL_EVENT_PLSCANSTART);

    db_sql_in_scan=0;
    db_sql_in_playlist_scan=1;

    return DB_E_SUCCESS;
}

/**
 * stop a db scan
 *
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_end_scan(void) {
    db_sqlite2_event(DB_SQL_EVENT_PLSCANEND);

    db_sql_update_playlists(NULL);
    db_sql_reload=0;
    db_sql_in_playlist_scan=0;

    return DB_E_SUCCESS;
}

/**
 * delete a playlist
 *
 * @param playlistid playlist to delete
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_delete_playlist(char **pe, int playlistid) {
    int type;
    int result;

    result=db_sql_fetch_int(pe,&type,"select type from playlists where id=%d",
                            playlistid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override the generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_PLAYLIST);
            return DB_E_INVALID_PLAYLIST;
        }

        return result;
    }

    /* got a good playlist, now do what we need to do */
    db_sqlite2_exec(pe,E_FATAL,"delete from playlists where id=%d",playlistid);
    db_sqlite2_exec(pe,E_FATAL,"delete from playlistitems where playlistid=%d",playlistid);

    return DB_E_SUCCESS;
}

/**
 * delete a song from a playlist
 *
 * @param playlistid playlist to delete item from
 * @param songid song to delete from playlist
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_delete_playlist_item(char **pe, int playlistid, int songid) {
    int result;
    int playlist_type;
    int count;

    /* first, check the playlist */
    result=db_sql_fetch_int(pe,&playlist_type,
                            "select type from playlists where id=%d",
                            playlistid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_PLAYLIST);
            return DB_E_INVALID_PLAYLIST;
        }
        return result;
    }

    if(playlist_type == PL_SMART) { /* can't delete from a smart playlist */
        db_get_error(pe,DB_E_INVALIDTYPE);
        return DB_E_INVALIDTYPE;
    }

    /* make sure the songid is valid */
    result=db_sql_fetch_int(pe,&count,"select count(*) from playlistitems "
                            "where playlistid=%d and songid=%d",
                            playlistid,songid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_SONGID);
            return DB_E_INVALID_SONGID;
        }
        return result;
    }

    /* looks valid, so lets add the item */
    result=db_sqlite2_exec(pe,E_DBG,"delete from playlistitems where "
                           "playlistid=%d and songid=%d",playlistid,songid);

    return result;
}

/**
 * edit a playlist.  The only things worth changing are the name
 * and the "where" clause.
 *
 * @param id id of the playlist to alter
 * @param name new name of the playlist
 * @param where new where clause
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_edit_playlist(char **pe, int id, char *name, char *clause) {
    int result;
    int playlist_type;

    /* first, check the playlist */
    result=db_sql_fetch_int(pe,&playlist_type,
                            "select type from playlists where id=%d",id);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_PLAYLIST);
            return DB_E_INVALID_PLAYLIST;
        }
        return result;
    }

    /* TODO: check for duplicate names here */

    if(playlist_type != PL_SMART) { /* Ignore the clause */
        return db_sqlite2_exec(pe,E_LOG,"update playlists set title='%q' "
                               "where id=%d",name,id);
    }

    return db_sqlite2_exec(pe,E_LOG,"update playlists set title='%q',"
                           "query='%q' where id=%d",name, clause, id);
}

/**
 * add a playlist
 *
 * @param name name of the playlist
 * @param type playlist type: 0 - static, 1 - smart, 2 - m3u
 * @param clause: "where" clause for smart playlist
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_add_playlist(char **pe, char *name, int type, char *clause, char *path, int index, int *playlistid) {
    int cnt=0;
    int result=DB_E_SUCCESS;
    char *criteria;

    result=db_sql_fetch_int(pe,&cnt,"select count(*) from playlists where "
                            "upper(title)=upper('%q')",name);

    if(result == DB_E_NOROWS) { /* good playlist name */
        if(pe) { free(*pe); };
    } else {
        if(result != DB_E_SUCCESS) {
            return result;
        } else {
            db_get_error(pe,DB_E_DUPLICATE_PLAYLIST);
            return DB_E_DUPLICATE_PLAYLIST;
        }
    }

    if((type == PL_SMART) && (!clause)) {
        db_get_error(pe,DB_E_NOCLAUSE);
        return DB_E_NOCLAUSE;
    }

    /* Let's throw it in  */
    switch(type) {
    case PL_STATICWEB: /* static, maintained in web interface */
    case PL_STATICFILE: /* static, from file */
    case PL_STATICXML: /* from iTunes XML file */
        result = db_sqlite2_exec(pe,E_LOG,"insert into playlists "
                                 "(title,type,items,query,db_timestamp,path,idx) "
                                 "values ('%q',%d,0,NULL,%d,'%q',%d)",
                                 name,type,time(NULL),path,index);
        break;
    case PL_SMART: /* smart */
        criteria = db_sql_parse_smart(clause);
        if(!criteria) {
            db_get_error(pe,DB_E_PARSE);
            return DB_E_PARSE;
        }
        free(criteria);

        result = db_sqlite2_exec(pe,E_LOG,"insert into playlists "
                                 "(title,type,items,query,db_timestamp,idx) "
                                 "values ('%q',%d,%d,'%q',%d,0)",
                                 name,PL_SMART,cnt,clause,time(NULL));
        break;
    }

    if(result)
        return result;

    result = db_sql_fetch_int(pe,playlistid,
                              "select id from playlists where title='%q'",
                              name);

    if(((type==PL_STATICFILE)||(type==PL_STATICXML))
        && (db_sql_in_playlist_scan) && (!db_sql_reload)) {
        db_sqlite2_exec(NULL,E_FATAL,"insert into plupdated values (%d)",*playlistid);
    }

    return result;
}

/**
 * add a song to a static playlist
 *
 * @param playlistid playlist to add song to
 * @param songid song to add
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_add_playlist_item(char **pe, int playlistid, int songid) {
    int result;
    int playlist_type;
    int count;

    /* first, check the playlist */
    result=db_sql_fetch_int(pe,&playlist_type,
                            "select type from playlists where id=%d",
                            playlistid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_PLAYLIST);
            return DB_E_INVALID_PLAYLIST;
        }
        return result;
    }

    if(playlist_type == 1) { /* can't add to smart playlists, or static */
        db_get_error(pe,DB_E_INVALIDTYPE);
        return DB_E_INVALIDTYPE;
    }

    /* make sure the songid is valid */
    result=db_sql_fetch_int(pe,&count,"select count(*) from songs where "
                            "id=%d",songid);

    if(result != DB_E_SUCCESS) {
        if(result == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_SONGID);
            return DB_E_INVALID_SONGID;
        }
        return result;
    }

    /* looks valid, so lets add the item */
    result=db_sqlite2_exec(pe,E_DBG,"insert into playlistitems "
                           "(playlistid, songid) values (%d,%d)",
                           playlistid,songid);
    return result;
}


/**
 * add a database item
 *
 * @param pmp3 mp3 file to add
 */
int db_sql_add(char **pe, MP3FILE *pmp3) {
    int err;
    int count;

    DPRINTF(E_SPAM,L_DB,"Entering db_sql_add\n");

    if(!pmp3->time_added)
        pmp3->time_added = (int)time(NULL);

    if(!pmp3->time_modified)
        pmp3->time_modified = (int)time(NULL);

    pmp3->db_timestamp = (int)time(NULL);


    if(!db_sql_reload) { /* if we are in a reload, then no need to check */
        err=db_sql_fetch_int(NULL,&count,"select count(*) from songs where "
                             "path='%q'",pmp3->path);

        if((err == DB_E_SUCCESS) && (count == 1)) { /* we should update */
            return db_sql_update(pe,pmp3);
        }
    }

    pmp3->play_count=0;
    pmp3->time_played=0;

    err=db_sqlite2_exec(pe,E_DBG,"INSERT INTO songs VALUES "
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

    if(err != DB_E_SUCCESS)
        DPRINTF(E_FATAL,L_DB,"Error inserting file %s in database\n",pmp3->fname);

    if((db_sql_in_scan)&&(!db_sql_reload)) {
        /* FIXME: this is sqlite-specific */
        db_sqlite2_exec(NULL,E_FATAL,"insert into updated values (last_insert_rowid())");
    }

    if((!db_sql_in_scan) && (!db_sql_in_playlist_scan))
        db_sql_update_playlists(NULL);

    DPRINTF(E_SPAM,L_DB,"Exiting db_sql_add\n");
    return DB_E_SUCCESS;
}

/**
 * update a database item
 *
 * @param pmp3 mp3 file to update
 */
int db_sql_update(char **pe, MP3FILE *pmp3) {
    int err;

    if(!pmp3->time_modified)
        pmp3->time_modified = (int)time(NULL);

    pmp3->db_timestamp = (int)time(NULL);

    err=db_sqlite2_exec(pe,E_LOG,"UPDATE songs SET "
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


    if(err != DB_E_SUCCESS)
        DPRINTF(E_FATAL,L_DB,"Error updating file: %s\n",pmp3->fname);

    if((db_sql_in_scan) && (!db_sql_reload)) {
        db_sqlite2_exec(NULL,E_FATAL,"INSERT INTO updated (id) select id from songs where path='%q'",
                       pmp3->path);
    }

    if((!db_sql_in_scan) && (!db_sql_in_playlist_scan))
        db_sql_update_playlists(NULL);

    return 0;
}


/**
 * Update the playlist item counts
 */
int db_sql_update_playlists(char **pe) {
    typedef struct tag_plinfo {
        char *plid;
        char *type;
        char *clause;
    } PLINFO;

    PLINFO *pinfo;
    int playlists;
    int err;
    int index;
    SQL_ROW row;
    char *where_clause;

    /* FIXME: There is a race here for externally added playlists */

    err = db_sql_fetch_int(pe,&playlists,"select count(*) from playlists");

    if(err != DB_E_SUCCESS) {
        return err;
    }

    pinfo = (PLINFO*)malloc(sizeof(PLINFO) * playlists);
    if(!pinfo) {
        DPRINTF(E_FATAL,L_DB,"Malloc error\n");
    }

    /* now, let's walk through the table */
    err = db_sqlite2_enum_begin(pe,"select * from playlistitems");
    if(err != DB_E_SUCCESS)
        return err;

    /* otherwise, walk the table */
    index=0;
    while((db_sqlite2_enum_fetch(pe, &row) == DB_E_SUCCESS) && (row) &&
          (index < playlists))
    {
        /* process row */
        pinfo[index].plid=strdup(STR(row[0]));
        pinfo[index].type=strdup(STR(row[1]));
        pinfo[index].clause=strdup(STR(row[2]));
        index++;
    }
    db_sqlite2_enum_end(pe);
    if(index != playlists) {
        DPRINTF(E_FATAL,L_DB,"Playlist count mismatch -- transaction problem?\n");
    }

    /* Now, update the playlists */
    for(index=0;index < playlists; index++) {
        if(atoi(pinfo[index].type) == 1) {
            /* smart */
            where_clause = db_sql_parse_smart(pinfo[index].clause);
            db_sqlite2_exec(NULL,E_FATAL,"update playlists set items=("
                            "select count(*) from songs where %s) "
                            "where id=%s",where_clause,pinfo[index].plid);
        } else {
            db_sqlite2_exec(NULL,E_FATAL,"update playlists set items=("
                            "select count(*) from playlistitems where "
                            "playlistid=%s) where id=%s",
                            pinfo[index].plid, pinfo[index].plid);
        }

        if(pinfo[index].plid) free(pinfo[index].plid);
        if(pinfo[index].type) free(pinfo[index].type);
        if(pinfo[index].clause) free(pinfo[index].clause);
    }

    free(pinfo);
    return DB_E_SUCCESS;
}


/**
 * start enum based on the DBQUERYINFO struct passed
 *
 * @param pinfo DBQUERYINFO struct detailing what to enum
 */
int db_sql_enum_start(char **pe, DBQUERYINFO *pinfo) {
    char scratch[4096];
    char query[4096];
    char query_select[255];
    char query_count[255];
    char query_rest[4096];
    char *where_clause;

    int is_smart;
    int have_clause=0;
    int err;
    int browse=0;
    int results=0;
    SQL_ROW temprow;

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
        err = db_sqlite2_enum_begin(pe, "select type,query from playlists "
                                   "where id=%d",pinfo->playlist_id);

        if(err != DB_E_SUCCESS)
            return err;

        err = db_sqlite2_enum_fetch(pe,&temprow);

        if(err != DB_E_SUCCESS) {
            db_sqlite2_enum_end(NULL);
            return err;
        }

        if(!temprow) { /* bad playlist */
            db_get_error(pe,DB_E_INVALID_PLAYLIST);
            db_sqlite2_enum_end(NULL);
            return DB_E_INVALID_PLAYLIST;
        }

        is_smart=(atoi(temprow[0]) == 1);
        have_clause=1;
        if(is_smart) {
            where_clause=db_sql_parse_smart(temprow[1]);
            if(!where_clause) {
                db_sqlite2_enum_end(NULL);
                db_get_error(pe,DB_E_PARSE);
                return DB_E_PARSE;
            }
            sprintf(query_select,"SELECT * FROM songs ");
            sprintf(query_count,"SELECT COUNT(id) FROM songs ");
            sprintf(query_rest,"WHERE (%s)",where_clause);
            free(where_clause);
        } else {
            sprintf(query_count,"SELECT COUNT(id) FROM songs ");

            /* We need to fix playlist queries to stop
             * pulling the whole song db... the performance
             * of these playlist queries sucks.
             */
#if 1
            sprintf(query_select,"select * from songs ");
            sprintf(query_rest,"where songs.id in (select songid from "
                               "playlistitems where playlistid=%d)",
                               pinfo->playlist_id);
#else
            sprintf(query_select,"select * from songs,playlistitems ");
            sprintf(query_rest,"where (songs.id=playlistitems.songid and "
                               "playlistitems.playlistid=%d) order by "
                               "playlistitems.id",pinfo->playlist_id);
#endif
        }

        db_sqlite2_enum_end(NULL);
        break;

        /* Note that sqlite doesn't support COUNT(DISTINCT x) */
    case queryTypeBrowseAlbums:
        strcpy(query_select,"select distinct album from songs ");
        strcpy(query_count,"select count(album) from (select "
                           "distinct album from songs ");
        browse=1;
        break;

    case queryTypeBrowseArtists:
        strcpy(query_select,"select distinct artist from songs ");
        strcpy(query_count,"select count(artist) from (select "
                           "distinct artist from songs ");
        browse=1;
        break;

    case queryTypeBrowseGenres:
        strcpy(query_select,"select distinct genre from songs ");
        strcpy(query_count,"select count(genre) from (select "
                           "distinct genre from songs ");
        browse=1;
        break;

    case queryTypeBrowseComposers:
        strcpy(query_select,"select distinct composer from songs ");
        strcpy(query_count,"select count(composer) from (select "
                           "distinct composer from songs ");
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


        err = db_sql_fetch_int(pe,&results,scratch);
        if(err != DB_E_SUCCESS)
            return err;

        DPRINTF(E_DBG,L_DB,"Number of results: %d\n",results);
    }

    strcpy(query,query_select);
    strcat(query,query_rest);

    /* FIXME: sqlite specific */
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
    err=db_sqlite2_enum_begin(pe,query);
    return err;
}

/**
 * find the size of the response by walking through the query and
 * sizing it
 *
 * @returns DB_E_SUCCESS on success, error code otherwise
 */
int db_sql_enum_size(char **pe, DBQUERYINFO *pinfo, int *count, int *total_size) {
    int err;
    int record_size;
    SQL_ROW row;

    DPRINTF(E_DBG,L_DB,"Enumerating size\n");

    *count=0;
    *total_size = 0;

    while(((err=db_sqlite2_enum_fetch(pe,&row)) == DB_E_SUCCESS) && (row)) {
        if((record_size = db_sql_get_size(pinfo,row))) {
            *total_size += record_size;
            *count = *count + 1;
        }
    }

    if(err != DB_E_SUCCESS) {
        db_sqlite2_enum_end(NULL);
        return err;
    }

    err=db_sqlite2_enum_restart(pe);

    DPRINTF(E_DBG,L_DB,"Got size: %d\n",*total_size);
    return err;
}


/**
 * fetch the next record from the enum
 */
int db_sql_enum_fetch(char **pe, DBQUERYINFO *pinfo, int *size, unsigned char **pdmap) {
    int err;
    int result_size=0;
    unsigned char *presult;
    SQL_ROW row;

    err=db_sqlite2_enum_fetch(pe, &row);
    if(err != DB_E_SUCCESS) {
        db_sqlite2_enum_end(NULL);
        return err;
    }

    if(row) {
        result_size = db_sql_get_size(pinfo,row);
        if(result_size) {
            presult = (unsigned char*)malloc(result_size);
            if(!presult) {
                DPRINTF(E_FATAL,L_DB,"Malloc error\n");
            }

            db_sql_build_dmap(pinfo,row,presult,result_size);
            *pdmap=presult;
            *size = result_size;
        }
    } else {
        *size = 0;
    }

    return DB_E_SUCCESS;
}

/**
 * start the enum again
 */
int db_sql_enum_reset(char **pe, DBQUERYINFO *pinfo) {
    return db_sqlite2_enum_restart(pe);
}


/**
 * stop the enum
 */
int db_sql_enum_end(char **pe) {
    return db_sqlite2_enum_end(pe);
}

/**
 * get the size of the generated dmap, given a specific meta
 */
int db_sql_get_size(DBQUERYINFO *pinfo, SQL_ROW valarray) {
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
        transcode = server_side_convert(valarray[37]);

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

int db_sql_build_dmap(DBQUERYINFO *pinfo, char **valarray, unsigned char *presult, int len) {
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
        transcode = server_side_convert(valarray[37]);

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

int db_sql_atoi(const char *what) {
    return what ? atoi(what) : 0;
}
char *db_sql_strdup(const char *what) {
    return what ? (strlen(what) ? strdup(what) : NULL) : NULL;
}

void db_sql_build_m3ufile(SQL_ROW valarray, M3UFILE *pm3u) {
    memset(pm3u,0x00,sizeof(M3UFILE));

    pm3u->id=db_sql_atoi(valarray[0]);
    pm3u->title=db_sql_strdup(valarray[1]);
    pm3u->type=db_sql_atoi(valarray[2]);
    pm3u->items=db_sql_atoi(valarray[3]);
    pm3u->query=db_sql_strdup(valarray[4]);
    pm3u->db_timestamp=db_sql_atoi(valarray[5]);
    pm3u->path=db_sql_strdup(valarray[6]);
    pm3u->index=db_sql_atoi(valarray[7]);
    return;
}

void db_sql_build_mp3file(SQL_ROW valarray, MP3FILE *pmp3) {
    memset(pmp3,0x00,sizeof(MP3FILE));
    pmp3->id=db_sql_atoi(valarray[0]);
    pmp3->path=db_sql_strdup(valarray[1]);
    pmp3->fname=db_sql_strdup(valarray[2]);
    pmp3->title=db_sql_strdup(valarray[3]);
    pmp3->artist=db_sql_strdup(valarray[4]);
    pmp3->album=db_sql_strdup(valarray[5]);
    pmp3->genre=db_sql_strdup(valarray[6]);
    pmp3->comment=db_sql_strdup(valarray[7]);
    pmp3->type=db_sql_strdup(valarray[8]);
    pmp3->composer=db_sql_strdup(valarray[9]);
    pmp3->orchestra=db_sql_strdup(valarray[10]);
    pmp3->conductor=db_sql_strdup(valarray[11]);
    pmp3->grouping=db_sql_strdup(valarray[12]);
    pmp3->url=db_sql_strdup(valarray[13]);
    pmp3->bitrate=db_sql_atoi(valarray[14]);
    pmp3->samplerate=db_sql_atoi(valarray[15]);
    pmp3->song_length=db_sql_atoi(valarray[16]);
    pmp3->file_size=db_sql_atoi(valarray[17]);
    pmp3->year=db_sql_atoi(valarray[18]);
    pmp3->track=db_sql_atoi(valarray[19]);
    pmp3->total_tracks=db_sql_atoi(valarray[20]);
    pmp3->disc=db_sql_atoi(valarray[21]);
    pmp3->total_discs=db_sql_atoi(valarray[22]);
    pmp3->bpm=db_sql_atoi(valarray[23]);
    pmp3->compilation=db_sql_atoi(valarray[24]);
    pmp3->rating=db_sql_atoi(valarray[25]);
    pmp3->play_count=db_sql_atoi(valarray[26]);
    pmp3->data_kind=db_sql_atoi(valarray[27]);
    pmp3->item_kind=db_sql_atoi(valarray[28]);
    pmp3->description=db_sql_strdup(valarray[29]);
    pmp3->time_added=db_sql_atoi(valarray[30]);
    pmp3->time_modified=db_sql_atoi(valarray[31]);
    pmp3->time_played=db_sql_atoi(valarray[32]);
    pmp3->db_timestamp=db_sql_atoi(valarray[33]);
    pmp3->disabled=db_sql_atoi(valarray[34]);
    pmp3->sample_count=db_sql_atoi(valarray[35]);
    pmp3->force_update=db_sql_atoi(valarray[36]);
    pmp3->codectype=db_sql_strdup(valarray[37]);
    pmp3->index=db_sql_atoi(valarray[38]);
}

/**
 * fetch a playlist by path and index
 *
 * @param path path to fetch
 */
M3UFILE *db_sql_fetch_playlist(char **pe, char *path, int index) {
    int result;
    M3UFILE *pm3u=NULL;
    SQL_ROW row;

    result = db_sqlite2_enum_begin(pe,"select * from playlists where "
                                   "path='%q' and idx=%d",path,index);

    if(result != DB_E_SUCCESS)
        return NULL;

    result = db_sqlite2_enum_fetch(pe, &row);
    if(result != DB_E_SUCCESS) {
        db_sqlite2_enum_end(NULL);
        return NULL;
    }

    if(!row) {
        db_sqlite2_enum_end(NULL);
        db_get_error(pe,DB_E_INVALID_PLAYLIST);
        return NULL;
    }

    pm3u=(M3UFILE*)malloc(sizeof(M3UFILE));
    if(!pm3u)
        DPRINTF(E_FATAL,L_MISC,"malloc error: db_sql_fetch_playlist\n");

    db_sql_build_m3ufile(row,pm3u);

    if((db_sql_in_playlist_scan) && (!db_sql_reload)) {
        db_sqlite2_exec(NULL,E_FATAL,"insert into plupdated values (%d)",
                        pm3u->id);
    }

    db_sqlite2_enum_end(NULL);
    return pm3u;
}


/* FIXME: bad error handling -- not like the rest */

/**
 * fetch a MP3FILE for a specific id
 *
 * @param id id to fetch
 */
MP3FILE *db_sql_fetch_item(char **pe, int id) {
    SQL_ROW row;
    MP3FILE *pmp3=NULL;
    int err;

    err=db_sql_fetch_row(pe,&row,"select * from songs where id=%d",id);
    if(err != DB_E_SUCCESS) {
        if(err == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_SONGID);
            return NULL;
        }
        return NULL;
    }

    pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
    if(!pmp3)
        DPRINTF(E_FATAL,L_MISC,"Malloc error in db_sql_fetch_item\n");

    db_sql_build_mp3file(row,pmp3);

    db_sql_dispose_row();

    if ((db_sql_in_scan) && (!db_sql_reload)) {
        db_sqlite2_exec(pe,E_FATAL,"INSERT INTO updated VALUES (%d)",id);
    }

    return pmp3;
}

/**
 * retrieve a MP3FILE struct for the song with a given path
 *
 * @param path path of the file to retreive
 */
MP3FILE *db_sql_fetch_path(char **pe, char *path, int index) {
    SQL_ROW row;
    MP3FILE *pmp3=NULL;
    int err;

    err=db_sql_fetch_row(pe,&row,"select * from songs where path='%q'",path);
    if(err != DB_E_SUCCESS) {
        if(err == DB_E_NOROWS) { /* Override generic error */
            if(pe) { free(*pe); };
            db_get_error(pe,DB_E_INVALID_SONGID);
            return NULL;
        }
        return NULL;
    }

    pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
    if(!pmp3)
        DPRINTF(E_FATAL,L_MISC,"Malloc error in db_sql_fetch_path\n");

    db_sql_build_mp3file(row,pmp3);

    db_sql_dispose_row();

    if ((db_sql_in_scan) && (!db_sql_reload)) {
        db_sqlite2_exec(pe,E_FATAL,"INSERT INTO updated VALUES (%d)",pmp3->id);
    }

    return pmp3;
}

/**
 * dispose of a MP3FILE struct that was obtained
 * from db_sql_fetch_item
 *
 * @param pmp3 item obtained from db_sql_fetch_item
 */
void db_sql_dispose_item(MP3FILE *pmp3) {
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

void db_sql_dispose_playlist(M3UFILE *pm3u) {
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
 * @param type either countPlaylists or countSongs (type to count)
 */
int db_sql_get_count(char **pe, int *count, CountType_t type) {
    char *table;
    int err;

    switch(type) {
    case countPlaylists:
        table="playlists";
        break;

    case countSongs:
    default:
        table="songs";
        break;
    }

    err=db_sql_fetch_int(pe,count,"SELECT COUNT(*) FROM '%q'", table);
    return err;
}

