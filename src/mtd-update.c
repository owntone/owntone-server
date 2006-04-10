/*
 * $Id: $
 * Update a database from an old version to a new version.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "daapd.h"
#include "db-generic.h"
#include "db-sql.h"
#include "err.h"

//int (*db_sql_exec_fn)(char **pe, int loglevel, char *fmt, ...) = NULL;

CONFIG config;

int db_error_exec(char **pe, int loglevel, char *fmt, ...);
int db_error_close(void);
int db_error_open(char **pe, char *dsn);
extern int db_sql_fetch_int(char **pe, int *result, char *fmt, ...);

#ifdef HAVE_LIBSQLITE
# include "db-sql-sqlite2.h"
# define SQLITE_EXEC db_sqlite2_exec
# define SQLITE_OPEN db_sql_open_sqlite2
# define SQLITE_CLOSE db_sqlite2_close
# define SQLITE_UPDATES db_sqlite_updates
#else
# define SQLITE_EXEC db_error_exec
# define SQLITE_OPEN db_error_open
# define SQLITE_CLOSE db_error_close
# define SQLITE_UPDATES db_error_updates
#endif

#ifdef HAVE_LIBSQLITE3
# include "db-sql-sqlite3.h"
# define SQLITE3_EXEC db_sqlite3_exec
# define SQLITE3_OPEN db_sql_open_sqlite3
# define SQLITE3_CLOSE db_sqlite3_close
# define SQLITE3_UPDATES db_sqlite_updates
#else
# define SQLITE3_EXEC db_error_exec
# define SQLITE3_OPEN db_error_open
# define SQLITE3_CLOSE db_error_close
# define SQLITE3_UPDATES db_error_updates
#endif


extern char *db_sqlite_updates[];
char *db_error_updates[] = {
    NULL
};

int (*dbu_sql_open_fn)(char **pe, char *dsn) = NULL;
int (*dbu_sql_close_fn)(void) = NULL;
int (*dbu_sql_exec_fn)(char **pe, int loglevel, char *fmt, ...) = NULL;
char **dbu_updates;

int db_error_exec(char **pe, int loglevel, char *fmt, ...) {
    char *err="Unsupported DB type";

    db_get_error(pe,DB_E_SQL_ERROR,err);
    return DB_E_SQL_ERROR;
}

int db_error_close(void) {
    return DB_E_SQL_ERROR;
}

int db_error_open(char **pe, char *dsn) {
    char *err="Unsupported DB type";

    db_get_error(pe,DB_E_SQL_ERROR,err);
    return DB_E_SQL_ERROR;
}

int main(int argc, char *argv[]) {
    char *configfile = CONFFILE;
    char *pe;
    int result;
    int option;
    char *db_type;
    char *db_parms;
    int version;
    int max_version;

    err_setlevel(1);

    while((option=getopt(argc,argv,"d:c:")) != -1) {
        switch(option) {
        case 'd':
            err_setlevel(atoi(optarg));
            break;
        case 'c':
            configfile = optarg;
            break;
        default:
            break;
        }
    }

    printf("Loading config file: %s\n",configfile);
    if(conf_read(configfile) != CONF_E_SUCCESS) {
        fprintf(stderr,"Error loading config file!\n");
        exit(EXIT_FAILURE);
    }

    db_type = conf_alloc_string("general","db_type",NULL);
    db_parms = conf_alloc_string("general","db_parms",NULL);

    if((!db_type) || (!db_parms)) {
        fprintf(stderr,"Bad config: missing db_type or db_parms\n");
        exit(EXIT_FAILURE);
    }
    printf("Opening database (type: %s, parms: %s)\n",db_type,db_parms);
    if(strcasecmp(db_type,"sqlite") == 0) {
        dbu_sql_open_fn = SQLITE_OPEN;
        dbu_sql_close_fn = SQLITE_CLOSE;
        dbu_sql_exec_fn = SQLITE_EXEC;
        dbu_updates = SQLITE_UPDATES;
    } else if(strcasecmp(db_type,"sqlite3") == 0) {
        dbu_sql_open_fn = SQLITE3_OPEN;
        dbu_sql_close_fn = SQLITE3_CLOSE;
        dbu_sql_exec_fn = SQLITE3_EXEC;
        dbu_updates = SQLITE3_UPDATES;
    } else {
        fprintf(stderr,"Error: unknown database type: %s\n",db_type);
        exit(EXIT_FAILURE);
    }

    pe=NULL;
    result = dbu_sql_open_fn(&pe,db_parms);
    if((result != DB_E_SUCCESS)&&(result != DB_E_WRONGVERSION)) {
        fprintf(stderr,"Error: %s\n",pe);
        exit(EXIT_FAILURE);
    }

    if(result == DB_E_SUCCESS) {
        printf("Database is already up-to-date\n");
    } else {
        /* update the db */
        result = db_sql_fetch_int(&pe,&version,"select value from config "
                                  "where term='version'");
        if(result != DB_E_SUCCESS) {
            fprintf(stderr,"Error: %s\n",pe);
            exit(EXIT_FAILURE);
        }

        max_version=0;
        while(dbu_updates[max_version] != NULL) {
            max_version++;
        }

        printf("Current database version: %d\n",version);
        printf("Target version: %d\n",max_version);

        while(version < max_version) {
            printf("Upgrading db: %d --> %d\n",version,version+1);
            result = dbu_sql_exec_fn(&pe,E_LOG,"%s",dbu_updates[version]);
            if(result != DB_E_SUCCESS) {
                fprintf(stderr,"Could not upgrade db.  Aborting.\n");
                dbu_sql_close_fn();
                exit(EXIT_FAILURE);
            }
            version++;
        }
    }

    dbu_sql_close_fn();
    printf("Success!\n");
    return EXIT_SUCCESS;
}


char *db_sqlite_updates[] = {
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
    "update songs set type='mpc' where type='mpp';\n"
    "update songs set type='mpc' where type='mp+';\n"
    "update songs set description='AAC audio file' where type='m4a';\n"
    "update songs set description='MPEG audio file' where type='mp3';\n"
    "update songs set description='WAV audio file' where type='wav';\n"
    "update songs set description='Playlist URL' where type='pls';\n"
    "update songs set description='Ogg Vorbis audio file' where type='ogg';\n"
    "update songs set description='FLAC audio file' where type='flac';\n"
    "update songs set description='Musepack audio file' where type='mpc';\n"
    "update songs set codectype='mp4a' where type='m4a' or type='m4p';\n"
    "update songs set codectype='mpeg' where type='mp3';\n"
    "update songs set codectype='ogg' where type='ogg';\n"
    "update songs set codectype='flac' where type='flac';\n"
    "update songs set codectype='mpc' where type='mpc';\n"
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
    
    /* version 7 -> version 8 */
    "create index idx_songid on playlistitems(songid);\n"
    "create index idx_playlistid on playlistitems(playlistid);\n"
    "update config set value=8 where term='version';\n",

    /* version 8 -> version 9 */ 
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
    "   idx             INTEGER NOT NULL,\n"
    "   has_video       INTEGER DEFAULT 0,\n"
    "   contentrating   INTEGER DEFAULT 0\n"
    ");\n"
    "begin transaction;\n"
    "insert into songs select *,0,0 from tempsongs;\n"
    "commit transaction;\n"
    "update songs set has_video=1 where fname like '%.m4v';\n"
    "create index idx_path on songs(path);\n"
    "drop table tempsongs;\n"
    "update config set value=9 where term='version';\n",
    NULL /* No more versions! */
};
