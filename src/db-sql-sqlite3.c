/*
 * $Id$
 * sqlite3-specific db implementation
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

/*
 * This file handles sqlite3 databases.  SQLite3 databases
 * should have a dsn of:
 *
 * sqlite3:/path/to/folder
 *
 * The actual db will be appended to the passed path.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "err.h"
#include "db-generic.h"
#include "db-sql.h"
#include "db-sql-sqlite3.h"

#ifndef TRUE
#  define TRUE 1
#  define FALSE 0
#endif

#define DB_SQLITE3_JOB_DONE     0
#define DB_SQLITE3_JOB_NOP      1
#define DB_SQLITE3_JOB_OPEN     2
#define DB_SQLITE3_JOB_CLOSE    3
#define DB_SQLITE3_JOB_EXEC     4
#define DB_SQLITE3_JOB_CHANGES  5
#define DB_SQLITE3_JOB_EBEGIN   6
#define DB_SQLITE3_JOB_EFETCH   7
#define DB_SQLITE3_JOB_ESTEP    8
#define DB_SQLITE3_JOB_FINALIZE 9
#define DB_SQLITE3_JOB_ROWID    10
#define DB_SQLITE3_JOB_QUIT     99


/* Globals */
static pthread_mutex_t _db_sqlite3_mutex = PTHREAD_MUTEX_INITIALIZER; /**< sqlite not reentrant */
static sqlite3_stmt *_db_sqlite3_stmt;
static int db_sqlite3_reload=0;
static char *_db_sqlite3_enum_query=NULL;
static char **_db_sqlite3_row = NULL;

static char db_sqlite3_path[PATH_MAX + 1];
static pthread_t _db_sqlite3_tid;
static pthread_cond_t _db_sqlite3_start = PTHREAD_COND_INITIALIZER;
static pthread_cond_t _db_sqlite3_done = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t _db_sqlite3_mutex_job = PTHREAD_MUTEX_INITIALIZER;
/* Job info */
static volatile int _db_sqlite3_job = DB_SQLITE3_JOB_DONE;
static sqlite3 *_db_sqlite3_songs; /**< Database that holds the mp3 info */
static int _db_sqlite3_err;
static char *_db_sqlite3_perr=NULL;
static char *_db_sqlite3_query;

#define DB_SQLITE3_VERSION 9


/* Forwards */
void _db_sqlite3_lock(void);
void _db_sqlite3_unlock(void);
extern char *db_sqlite3_initial1;
extern char *db_sqlite3_initial2;
int _db_sqlite3_enum_begin_helper(char **pe);


/**
 * throw a job at the worker thread.  
 *
 * @param pe error buffer
 * @param job job (DB_SQLITE3_JOB_*)
 * @returns DB_E_SUCCESS on success, DB_E_* otherwise
 */
int _db_sqlite3_start_job(int job) {
    int err;
    
    DPRINTF(E_SPAM,L_DB,"About to submit job (%d).. waiting for mutex\n",job);
    if((err=pthread_mutex_lock(&_db_sqlite3_mutex_job))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock sqlite job lock: %s\n",strerror(err));
    }
    
    /* we'll assume that all the other stuff is set correctly */
    _db_sqlite3_job = job;
    
    pthread_cond_signal(&_db_sqlite3_start);
    
    DPRINTF(E_SPAM,L_DB,"Submitting sqlite job type: %d\n",job);
    /* now wait for the job to be done */
    while(_db_sqlite3_job != DB_SQLITE3_JOB_DONE) {
        pthread_cond_wait(&_db_sqlite3_done,&_db_sqlite3_mutex_job);
    }
    DPRINTF(E_SPAM,L_DB,"Job done: status %d, unlocking mutex\n",_db_sqlite3_err);

    pthread_mutex_unlock(&_db_sqlite3_mutex_job);
    return _db_sqlite3_err;
}

/**
 * worker thread main loop.  since sqlite3 is picky about only
 * using handles from the thread that opened, I'm going to make
 * a single worker thread pool to handle all db stuff from one
 * thread.
 */
void *_db_sqlite3_threadproc(void *arg) {
    int err;
    char *perr;
    const char *ptail;
    int cols;
    int idx;
    static int done=0;
    
    /* we'll just sit on the "start" cond */

    DPRINTF(E_INF,L_DB,"sqlite3 worker: starting\n");
    if((err=pthread_mutex_lock(&_db_sqlite3_mutex_job))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock sqlite job lock: %s\n",strerror(err));
    }
    
    while(!done) {
        while(_db_sqlite3_job == DB_SQLITE3_JOB_DONE) {
            DPRINTF(E_SPAM,L_DB,"sqlite3 worker: about to cond_wait...\n");
            pthread_cond_wait(&_db_sqlite3_start, &_db_sqlite3_mutex_job);
        }
        
        DPRINTF(E_SPAM,L_DB,"sqlite3 worker: Found job type %d\n",_db_sqlite3_job);
        _db_sqlite3_err = SQLITE_OK;
        
        /* case takes up too much horizontal space */
        if(_db_sqlite3_job == DB_SQLITE3_JOB_OPEN) {
            err=sqlite3_open(db_sqlite3_path,&_db_sqlite3_songs);
            if(err == SQLITE_OK) {
                sqlite3_busy_timeout(_db_sqlite3_songs,30000);
            }
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_CLOSE) {
            err=sqlite3_close(_db_sqlite3_songs);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_EXEC) {
            err = sqlite3_exec(_db_sqlite3_songs,_db_sqlite3_query,NULL,NULL,&perr);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_CHANGES) {
            err = sqlite3_changes(_db_sqlite3_songs);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_EBEGIN) {
            err=sqlite3_prepare(_db_sqlite3_songs,_db_sqlite3_enum_query,0,
                &_db_sqlite3_stmt,&ptail);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_ESTEP) {
            err=sqlite3_step(_db_sqlite3_stmt);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_EFETCH) {
            cols = sqlite3_column_count(_db_sqlite3_stmt);
            if(!_db_sqlite3_row) {
                /* alloc space */
                _db_sqlite3_row = (char**)malloc((sizeof(char*)) * cols);
                if(!_db_sqlite3_row) 
                    DPRINTF(E_FATAL,L_DB,"Malloc error\n");
            }
            for(idx=0; idx < cols; idx++) {
                _db_sqlite3_row[idx] = (char*)sqlite3_column_text(_db_sqlite3_stmt,idx);
                DPRINTF(E_SPAM,L_DB,"Fetched %s\n",_db_sqlite3_row[idx]);
            }
            err = SQLITE_OK;
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_FINALIZE) {
            err = sqlite3_finalize(_db_sqlite3_stmt);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_ROWID) {
            err = (int)sqlite3_last_insert_rowid(_db_sqlite3_songs);
        } else if(_db_sqlite3_job == DB_SQLITE3_JOB_QUIT) {
            done = 1;
        }

        _db_sqlite3_err = err;
        DPRINTF(E_SPAM,L_DB,"sqlite3 worker: finished job with %d\n",err);
        
        /* hand it back to the client */

        _db_sqlite3_job = DB_SQLITE3_JOB_DONE;
        pthread_cond_signal(&_db_sqlite3_done);
    }
    pthread_mutex_unlock(&_db_sqlite3_mutex_job);
    DPRINTF(E_INF,L_DB,"sqlite3 worker exiting\n");

    return NULL;
}


/**
 * lock the db_mutex
 */
void _db_sqlite3_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&_db_sqlite3_mutex))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock sqlite lock: %s\n",strerror(err));
    }
}

/**
 * unlock the db_mutex
 */
void _db_sqlite3_unlock(void) {
    int err;

    if((err=pthread_mutex_unlock(&_db_sqlite3_mutex))) {
        DPRINTF(E_FATAL,L_DB,"cannot unlock sqlite3 lock: %s\n",strerror(err));
    }
}

/**
 *
 */
char *db_sqlite3_vmquery(char *fmt,va_list ap) {
    return sqlite3_vmprintf(fmt,ap);
}

/**
 *
 */
void db_sqlite3_vmfree(char *query) {
    sqlite3_free(query);
}


/**
 * open a sqlite3 database
 *
 * @param dsn the full dns to the database
 *        (sqlite3:/path/to/database)
 *
 * @returns DB_E_SUCCESS on success
 */
int db_sqlite3_open(char **pe, char *dsn) {
    int ver;
    int err;
    char *perr;

    snprintf(db_sqlite3_path,sizeof(db_sqlite3_path),"%s/songs3.db",dsn);

    _db_sqlite3_lock();
    
    if((err=pthread_create(&_db_sqlite3_tid,NULL,
                           _db_sqlite3_threadproc,NULL))) {
        DPRINTF(E_LOG,L_DB,"Could not spawn thread: %s\n",strerror(err));
        return DB_E_PROC;
    }
    
    if(_db_sqlite3_start_job(DB_SQLITE3_JOB_OPEN) != SQLITE_OK) {
        perr = _db_sqlite3_perr;
        db_get_error(pe,DB_E_SQL_ERROR,perr);
        DPRINTF(E_LOG,L_DB,"db_sqlite3_open: %s (%s)\n",*pe,
            db_sqlite3_path);
        _db_sqlite3_unlock();
        sqlite3_free(perr);
        return DB_E_SQL_ERROR;
    }
    _db_sqlite3_unlock();

    err = db_sql_fetch_int(pe,&ver,"select value from config where "
                           "term='version'");
    if(err != DB_E_SUCCESS) {
        if(pe) { free(*pe); }
        /* we'll catch this on the init */
        DPRINTF(E_LOG,L_DB,"Can't get db version. New database?\n");
    } else if(ver != DB_SQLITE3_VERSION) {
        DPRINTF(E_LOG,L_DB,"Old database version -- forcing rescan\n");
        err=db_sqlite3_exec(pe,E_FATAL,"insert into config (term,value) values "
                        "('rescan','1')");
        if(err != DB_E_SUCCESS)
            return err;
    }

    return DB_E_SUCCESS;
}

/**
 * close the database
 */
int db_sqlite3_close(void) {
    _db_sqlite3_lock();
    _db_sqlite3_start_job(DB_SQLITE3_JOB_CLOSE);
    _db_sqlite3_start_job(DB_SQLITE3_JOB_QUIT);
    pthread_join(_db_sqlite3_tid,NULL);
    _db_sqlite3_unlock();
    return DB_E_SUCCESS;
}

/**
 * execute a throwaway query against the database, disregarding
 * the outcome
 *
 * @param pe db error structure
 * @param loglevel error level to return if the query fails
 * @param fmt sprintf-style arguements
 *
 * @returns DB_E_SUCCESS on success
 */
int db_sqlite3_exec(char **pe, int loglevel, char *fmt, ...) {
    va_list ap;
    int err;
    char *perr;

    _db_sqlite3_lock();

    va_start(ap,fmt);
    _db_sqlite3_query=sqlite3_vmprintf(fmt,ap);
    va_end(ap);

    DPRINTF(E_DBG,L_DB,"Executing: %s\n",_db_sqlite3_query);

    err=_db_sqlite3_start_job(DB_SQLITE3_JOB_EXEC);
    perr = _db_sqlite3_perr;
    
    if(err != SQLITE_OK) {
        db_get_error(pe,DB_E_SQL_ERROR,perr);

        DPRINTF(loglevel == E_FATAL ? E_LOG : loglevel,L_DB,"Query: %s\n",
                _db_sqlite3_query);
        DPRINTF(loglevel,L_DB,"Error: %s\n",perr);
        sqlite3_free(perr);
    } else {
        DPRINTF(E_DBG,L_DB,"Rows: %d\n",_db_sqlite3_start_job(DB_SQLITE3_JOB_CHANGES));
    }
    sqlite3_free(_db_sqlite3_query);

    _db_sqlite3_unlock();

    if(err != SQLITE_OK)
        return DB_E_SQL_ERROR;
    return DB_E_SUCCESS;
}

/**
 * start enumerating rows in a select
 */
int db_sqlite3_enum_begin(char **pe, char *fmt, ...) {
    va_list ap;

    _db_sqlite3_lock();
    va_start(ap, fmt);
    _db_sqlite3_enum_query = sqlite3_vmprintf(fmt,ap);
    va_end(ap);

    DPRINTF(E_SPAM,L_DB,"Starting enum_begin: %s\n",_db_sqlite3_enum_query);
    return _db_sqlite3_enum_begin_helper(pe);
}

int _db_sqlite3_enum_begin_helper(char **pe) {
    int err;

    if(!_db_sqlite3_enum_query)
        *((int*)NULL) = 1;
        
    
    DPRINTF(E_DBG,L_DB,"Executing: %s\n",_db_sqlite3_enum_query);
    err=_db_sqlite3_start_job(DB_SQLITE3_JOB_EBEGIN);

    if(err != SQLITE_OK) {
        DPRINTF(E_SPAM,L_DB,"Error: %s, enum exiting\n",_db_sqlite3_perr);
        db_get_error(pe,DB_E_SQL_ERROR,_db_sqlite3_perr);
        sqlite3_free(_db_sqlite3_perr);
        sqlite3_free(_db_sqlite3_enum_query);
        _db_sqlite3_enum_query = NULL;
        _db_sqlite3_unlock();
        return DB_E_SQL_ERROR;
    }

    /* otherwise, we leave the db locked while we walk through the enums */
    if(_db_sqlite3_row)
        free(_db_sqlite3_row);
    _db_sqlite3_row=NULL;

    return DB_E_SUCCESS;

}

/**
 * fetch the next row.  This will return DB_E_SUCCESS if it got a 
 * row, or it's done.  If it's done, the row will be empty, otherwise
 * it will be full of data.  Either way, if fetch fails, you must close.
 *
 * @param pe error string, if result isn't DB_E_SUCCESS
 * @param pr pointer to a row struct
 *
 * @returns DB_E_SUCCESS with *pr=NULL when end of table,
 *          DB_E_SUCCESS with a valid row when more data,
 *          DB_E_* on error
 */
int db_sqlite3_enum_fetch(char **pe, SQL_ROW *pr) {
    int err;
    int counter=10;

    DPRINTF(E_SPAM,L_DB,"Fetching row for %s\n",_db_sqlite3_enum_query);
    
    if(!_db_sqlite3_enum_query)
        *((int*)NULL) = 1;

    while(counter--) {
        err=_db_sqlite3_start_job(DB_SQLITE3_JOB_ESTEP);
        if(err != SQLITE_BUSY)
            break;
        usleep(100);
    }

    if(err == SQLITE_DONE) {
        *pr = NULL;
        if(_db_sqlite3_row)
            free(_db_sqlite3_row);
        _db_sqlite3_row = NULL;
        return DB_E_SUCCESS;
    }

    if(err == SQLITE_ROW) {
        err = _db_sqlite3_start_job(DB_SQLITE3_JOB_EFETCH);
        *pr = _db_sqlite3_row;
        return DB_E_SUCCESS;
    }

    if(_db_sqlite3_row)
        free(_db_sqlite3_row);
    _db_sqlite3_row = NULL;

    db_get_error(pe,DB_E_SQL_ERROR,_db_sqlite3_perr);
    sqlite3_free(_db_sqlite3_perr);
    _db_sqlite3_start_job(DB_SQLITE3_JOB_FINALIZE);

    return DB_E_SQL_ERROR;
}

/**
 * end the db enumeration
 */
int db_sqlite3_enum_end(char **pe) {
    int err;
    char *perr;
    
    DPRINTF(E_SPAM,L_DB,"Finishing enum for %s\n",_db_sqlite3_enum_query);

    if(!_db_sqlite3_enum_query)
        *((int*)NULL) = 1;

    if(_db_sqlite3_row)
        free(_db_sqlite3_row);
    _db_sqlite3_row = NULL;
    sqlite3_free(_db_sqlite3_enum_query);
    _db_sqlite3_enum_query = NULL;

    err = _db_sqlite3_start_job(DB_SQLITE3_JOB_FINALIZE);
    if(err != SQLITE_OK) {
        perr = _db_sqlite3_perr;
        db_get_error(pe,DB_E_SQL_ERROR,perr);
        DPRINTF(E_LOG,L_DB,"Error in enum_end: %s\n",perr);
        sqlite3_free(perr);
        _db_sqlite3_unlock();
        return DB_E_SQL_ERROR;
    }

    _db_sqlite3_unlock();
    return DB_E_SUCCESS;
}

/**
 * restart the enumeration
 */
int db_sqlite3_enum_restart(char **pe) {
    return _db_sqlite3_enum_begin_helper(pe);
}


int db_sqlite3_event(int event_type) {
    switch(event_type) {

    case DB_SQL_EVENT_STARTUP: /* this is a startup with existing songs */
        db_sqlite3_exec(NULL,E_FATAL,"vacuum");
        db_sqlite3_reload=0;
        break;

    case DB_SQL_EVENT_FULLRELOAD: /* either a fresh load or force load */
        db_sqlite3_exec(NULL,E_DBG,"drop index idx_path");
        db_sqlite3_exec(NULL,E_DBG,"drop index idx_songid");
        db_sqlite3_exec(NULL,E_DBG,"drop index idx_playlistid");

        db_sqlite3_exec(NULL,E_DBG,"drop table songs");
//        db_sqlite3_exec(NULL,E_DBG,"drop table playlists");
        db_sqlite3_exec(NULL,E_DBG,"delete from playlists where not type=1");
        db_sqlite3_exec(NULL,E_DBG,"drop table playlistitems");
        db_sqlite3_exec(NULL,E_DBG,"drop table config");

        db_sqlite3_exec(NULL,E_DBG,"vacuum");

        db_sqlite3_exec(NULL,E_DBG,db_sqlite3_initial1);
        db_sqlite3_exec(NULL,E_DBG,db_sqlite3_initial2);
        db_sqlite3_reload=1;
        break;

    case DB_SQL_EVENT_SONGSCANSTART:
        if(db_sqlite3_reload) {
            db_sqlite3_exec(NULL,E_FATAL,"pragma synchronous = off");
            db_sqlite3_exec(NULL,E_FATAL,"begin transaction");
        } else {
            db_sqlite3_exec(NULL,E_DBG,"drop table updated");
            db_sqlite3_exec(NULL,E_FATAL,"create temp table updated (id int)");
            db_sqlite3_exec(NULL,E_DBG,"drop table plupdated");
            db_sqlite3_exec(NULL,E_FATAL,"create temp table plupdated(id int)");
        }
        break;

    case DB_SQL_EVENT_SONGSCANEND:
        if(db_sqlite3_reload) {
            db_sqlite3_exec(NULL,E_FATAL,"commit transaction");
            db_sqlite3_exec(NULL,E_FATAL,"create index idx_path on songs(path)");
            db_sqlite3_exec(NULL,E_DBG,"delete from config where term='rescan'");
        } else {
            db_sqlite3_exec(NULL,E_FATAL,"delete from songs where id not in (select id from updated)");
            db_sqlite3_exec(NULL,E_FATAL,"update songs set force_update=0");
            db_sqlite3_exec(NULL,E_FATAL,"drop table updated");
        }
        break;

    case DB_SQL_EVENT_PLSCANSTART:
        if(db_sqlite3_reload)
            db_sqlite3_exec(NULL,E_FATAL,"begin transaction");
        break;

    case DB_SQL_EVENT_PLSCANEND:
        if(db_sqlite3_reload) {
            db_sqlite3_exec(NULL,E_FATAL,"end transaction");
            db_sqlite3_exec(NULL,E_FATAL,"pragma synchronous=normal");
            db_sqlite3_exec(NULL,E_FATAL,"create index idx_songid on playlistitems(songid)");
            db_sqlite3_exec(NULL,E_FATAL,"create index idx_playlistid on playlistitems(playlistid)");

        } else {
            db_sqlite3_exec(NULL,E_FATAL,"delete from playlists where "
                                         "((type=%d) OR (type=%d)) and "
                                         "id not in (select id from plupdated)",
                                         PL_STATICFILE,PL_STATICXML);
            db_sqlite3_exec(NULL,E_FATAL,"delete from playlistitems where "
                                         "playlistid not in (select distinct "
                                         "id from playlists)");
            db_sqlite3_exec(NULL,E_FATAL,"drop table plupdated");
        }
        db_sqlite3_reload=0;
        break;

    default:
        break;
    }

    return DB_E_SUCCESS;
}

/**
 * get the id of the last auto_update inserted item
 *
 * @returns autoupdate value
 */

int db_sqlite3_insert_id(void) {
    int result;
    
    _db_sqlite3_lock();
    result=_db_sqlite3_start_job(DB_SQLITE3_JOB_ROWID);
    _db_sqlite3_unlock();
    
    return result;
}



char *db_sqlite3_initial1 =
"create table songs (\n"
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
"   idx             INTEGER NOT NULL,\n"
"   has_video       INTEGER DEFAULT 0,\n"
"   contentrating   INTEGER DEFAULT 0\n"
");\n"
"create table playlistitems (\n"
"   id             INTEGER PRIMARY KEY NOT NULL,\n"
"   playlistid     INTEGER NOT NULL,\n"
"   songid         INTEGER NOT NULL\n"
");\n"
"create table config (\n"
"   term            VARCHAR(255)    NOT NULL,\n"
"   subterm         VARCHAR(255)    DEFAULT NULL,\n"
"   value           VARCHAR(1024)   NOT NULL\n"
");\n"
"insert into config values ('version','','9');\n";

char *db_sqlite3_initial2 =
"create table playlists (\n"
"   id             INTEGER PRIMARY KEY NOT NULL,\n"
"   title          VARCHAR(255) NOT NULL,\n"
"   type           INTEGER NOT NULL,\n"
"   items          INTEGER NOT NULL,\n"
"   query          VARCHAR(1024),\n"
"   db_timestamp   INTEGER NOT NULL,\n"
"   path           VARCHAR(4096),\n"
"   idx            INTEGER NOT NULL\n"
");\n"
"insert into playlists values (1,'Library',1,0,'1',0,'',0);\n";


