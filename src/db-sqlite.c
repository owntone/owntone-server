/*
 * $Id$
 * Implementation for simple sqlite db
 *
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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

#define _XOPEN_SOURCE 600

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite.h>

#include "err.h"
#include "mp3-scanner.h"
#include "playlist.h"
#define RB_FREE
#include "redblack.h"

#include "db-memory.h"

/*
 * Defines
 */
#define DB_VERSION 8
#define STRLEN(a) (a) ? strlen((a)) + 1 : 1 
#define MAYBEFREE(a) { if((a)) free((a)); };

/* For old version of gdbm */
#ifndef GDBM_SYNC
# define GDBM_SYNC 0
#endif

#ifndef GDBM_NOLOCK
# define GDBM_NOLOCK 0
#endif


/*
 * Typedefs
 */
typedef struct tag_mp3record MP3RECORD;
struct tag_mp3record {
    MP3FILE mp3file;
    MP3RECORD* next;
};

typedef struct tag_playlistentry {
    unsigned long int	id;
    struct tag_playlistentry *next;
} DB_PLAYLISTENTRY;

typedef struct tag_playlist {
    unsigned long int	id;
    int songs;
    int is_smart;
    int found;
    char *name;
    int file_time;
    struct tag_playlistentry *nodes;
    struct tag_playlistentry *last_node;  /**< Make a tail add o(1) */
    struct tag_playlist *next;
} DB_PLAYLIST;

/*
 * Globals 
 */
static int db_version_no;  /**< db version, incremented every time add or delete */
static int db_update_mode=0; /**< Are we in the middle of a bulk update? */
static int db_song_count; /**< Number of songs in the db */
static int db_playlist_count=0; /**< Number of active playlists */
static int db_last_scan; /**< Dunno... */
static DB_PLAYLIST db_playlists; /**< The current playlists */
static pthread_rwlock_t db_rwlock; /**< pthread r/w sync for the database */
static pthread_once_t db_initlock=PTHREAD_ONCE_INIT; /**< to initialize the rwlock */
static sqlite *db_songs; /**< Database that holds the mp3 info */
static struct rbtree *db_removed; /**< rbtree to do quick searchs to do background scans */
static MP3FILE gdbm_mp3; /**< used during enumerations */
static int gdbm_mp3_mustfree=0;  /**< is the data in #gdbm_mp3 valid? Should it be freed? */
static pthread_mutex_t db_gdbm_mutex = PTHREAD_MUTEX_INITIALIZER; /**< gdbm not reentrant */
static sqlite_vm *gdbm_pvm;




/*
 * Forwards
 */
static void db_writelock(void);
static void db_readlock(void);
static int db_unlock(void);
static void db_gdbmlock(void);
static int db_gdbmunlock(void);

static DB_PLAYLIST	*db_playlist_find(unsigned long int playlistid);

int db_start_initial_update(void);
int db_end_initial_update(void);
int db_is_empty(void);
int db_open(char *parameters, int reload);
int db_init();
int db_deinit(void);
int db_version(void);
int db_add(MP3FILE *mp3file);
int db_delete(unsigned long int id);
int db_delete_playlist(unsigned long int playlistid);
int db_add_playlist(unsigned long int playlistid, char *name, int file_time, int is_smart);
int db_add_playlist_song(unsigned long int playlistid, unsigned long int itemid);
int db_scanning(void);

int db_get_song_count(void);
int db_get_playlist_count(void);
int db_get_playlist_is_smart(unsigned long int playlistid);
int db_get_playlist_entry_count(unsigned long int playlistid);
char *db_get_playlist_name(unsigned long int playlistid);

MP3FILE *db_find(unsigned long int id);

void db_dispose(MP3FILE *pmp3);
int db_compare_rb_nodes(const void *pa, const void *pb, const void *cfg);
void db_build_mp3file(const char **valarray, MP3FILE *pmp3);

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


/* 
 * db_gdbmlock
 * 
 * lock the gdbm functions
 */
void db_gdbmlock(void) {
    int err;

    if((err=pthread_mutex_lock(&db_gdbm_mutex))) {
	DPRINTF(E_FATAL,L_DB,"cannot lock gdbmlock: %s\n",strerror(err));
    }
}

/*
 * db_gdbmunlock
 * 
 * useless, but symmetrical 
 */
int db_gdbmunlock(void) {
    return pthread_mutex_unlock(&db_gdbm_mutex);
}


/*
 * db_compare_rb_nodes
 *
 * compare redblack nodes, which are just ints
 */
int db_compare_rb_nodes(const void *pa, const void *pb, const void *cfg) {
    if(*(unsigned long int*)pa < *(unsigned long int *)pb) return -1;
    if(*(unsigned long int*)pb < *(unsigned long int *)pa) return 1;
    return 0;
}


/*
 * db_init_once
 *
 * Must dynamically initialize the rwlock, as Mac OSX 10.3 (at least)
 * doesn't have a static initializer for rwlocks
 */
void db_init_once(void) {
    pthread_rwlock_init(&db_rwlock,NULL);
}

/*
 * db_open
 *
 * Open the database, so we can drop privs
 */
int db_open(char *parameters, int reload) {
    char db_path[PATH_MAX + 1];
    int current_db_version;
    char *perr;
    int err;

    if(pthread_once(&db_initlock,db_init_once))
	return -1;

    snprintf(db_path,sizeof(db_path),"%s/%s",parameters,"songs_sqlite.db");

    db_gdbmlock();
    db_songs=sqlite_open(db_path,0,&perr);
    if(!db_songs)
	DPRINTF(E_FATAL,L_DB,"db_open: %s\n",perr);

    if(reload) {
	err=sqlite_exec(db_songs,"DELETE FROM songs",NULL,NULL,&perr);
	if(err != SQLITE_OK)
	    DPRINTF(E_FATAL,L_DB,"Cannot reload tables: %s\n",perr);
    }

    db_gdbmunlock();
    return 0;
}

int db_atoi(const char *what) {
    return what ? atoi(what) : 0;
}
char *db_strdup(const char *what) {
    return what ? strdup(what) : NULL;
}

void db_build_mp3file(const char **valarray, MP3FILE *pmp3) {
    memset(pmp3,0x00,sizeof(MP3FILE));
    pmp3->id=db_atoi(valarray[0]);
    pmp3->path=db_strdup(valarray[1]);
    pmp3->fname=db_strdup(valarray[2]);
    pmp3->title=db_strdup(valarray[3]);
    pmp3->artist=db_strdup(valarray[4]);
    pmp3->album=db_strdup(valarray[5]);
    pmp3->genre=db_strdup(valarray[6]);
    pmp3->comment=db_strdup(valarray[7]);
    pmp3->type=db_strdup(valarray[8]);
    pmp3->composer=db_strdup(valarray[9]);
    pmp3->orchestra=db_strdup(valarray[10]);
    pmp3->conductor=db_strdup(valarray[11]);
    pmp3->grouping=db_strdup(valarray[12]);
    pmp3->url=db_strdup(valarray[13]);
    pmp3->bitrate=db_atoi(valarray[14]);
    pmp3->samplerate=db_atoi(valarray[15]);
    pmp3->song_length=db_atoi(valarray[16]);
    pmp3->file_size=db_atoi(valarray[17]);
    pmp3->year=db_atoi(valarray[18]);
    pmp3->track=db_atoi(valarray[19]);
    pmp3->total_tracks=db_atoi(valarray[20]);
    pmp3->disc=db_atoi(valarray[21]);
    pmp3->total_discs=db_atoi(valarray[22]);
    pmp3->time_added=db_atoi(valarray[23]);
    pmp3->time_modified=db_atoi(valarray[24]);
    pmp3->time_played=db_atoi(valarray[25]);
    pmp3->db_timestamp=db_atoi(valarray[26]);
    pmp3->bpm=db_atoi(valarray[27]);
    pmp3->compilation=db_atoi(valarray[28]);

    make_composite_tags(pmp3);
}


/*
 * db_init
 *
 * Initialize the database.  Parameter is the directory
 * of the database files
 */
int db_init(void) {
    MP3FILE mp3file;
    struct sqlite_vm *pvm;
    const char *ptail;
    char *perr;
    int err;
    int cols;
    const char **valarray;
    const char **colarray;

    pl_register();

    db_version_no=1;
    db_song_count=0;

    DPRINTF(E_DBG,L_DB|L_PL,"Building playlists\n");

    db_gdbmlock();

    /* count the actual songs and build the playlists */
    err=sqlite_compile(db_songs,"SELECT * FROM songs",&ptail,&pvm,&perr);
    if(err != SQLITE_OK)
	DPRINTF(E_FATAL,L_DB,"Cannot enum db: %s\n",perr);

    while((err=sqlite_step(pvm,&cols,&valarray,&colarray))==SQLITE_ROW) {
	db_build_mp3file(valarray,&mp3file);
	pl_eval(&mp3file);
	db_dispose(&mp3file);
	db_song_count++;
    }
    sqlite_finalize(pvm,&perr);
    db_gdbmunlock();

    DPRINTF(E_DBG,L_DB,"Loaded database... found %d songs\n",db_song_count);

    /* and the playlists */
    return 0;
}

/*
 * db_deinit
 *
 * Close the db, in this case freeing memory
 */
int db_deinit(void) {
    DB_PLAYLIST *plist;
    DB_PLAYLISTENTRY *pentry;

    db_gdbmlock();
    sqlite_close(db_songs);
    db_gdbmunlock();

    while(db_playlists.next) {
	plist=db_playlists.next;
	db_playlists.next=plist->next;
	free(plist->name);
	/* free all the nodes */
	while(plist->nodes) {
	    pentry=plist->nodes;
	    plist->nodes = pentry->next;
	    free(pentry);
	}
	free(plist);
    }

    return 0;
}

/*
 * db_scanning
 *
 * is the db currently in scanning mode?
 */
int db_scanning(void) {
    return db_update_mode;
}

/*
 * db_version
 *
 * return the db version
 */
int db_version(void) {
    int version;

    db_readlock();
    version=db_version_no;
    db_unlock();

    return version;
}

/*
 * db_start_initial_update
 *
 * Set the db to bulk import mode
 */
int db_start_initial_update(void) {
    int err;
    DB_PLAYLIST *current;
    char **resarray;
    int rows, cols;
    char *perr;
    int current_row;
    int *idx;

    /* we need a write lock on the db -- stop enums from happening */
    db_writelock();

    if((db_removed=rbinit(db_compare_rb_nodes,NULL)) == NULL) {
	errno=ENOMEM;
	db_unlock();
	return -1;
    }

    /* load up the red-black tree with all the current songs in the db */

    db_gdbmlock();
    sqlite_exec(db_songs,"PRAGMA synchronous=OFF;",NULL,NULL,&perr);
    err=sqlite_get_table(db_songs,"SELECT id FROM songs",&resarray,
			 &rows, &cols, &perr);
    db_gdbmunlock();

    if(err != SQLITE_OK) 
	DPRINTF(E_FATAL,L_DB,"db_start_initial_update: %s\n",perr);

    current_row=0;
    while(current_row < rows) {
	/* Add it to the rbtree */
	if(!(idx=(int*)malloc(sizeof(int))))DPRINTF(E_FATAL,L_DB,"malloc error\n");
	*idx=atoi(resarray[(current_row+1)*cols]);
	current_row++;

	if(!rbsearch((void*)idx,db_removed)) {
	    errno=ENOMEM;
	    db_unlock();
	    return -1;
	}
    }

    db_gdbmlock();
    sqlite_free_table(resarray);
    db_gdbmunlock();

    /* walk through the playlists and mark them as not found */
    current=db_playlists.next;
    while(current) {
	current->found=0;
	current=current->next;
    }

    db_update_mode=1;
    db_unlock();

    return 0;
}

/*
 * db_end_initial_update
 *
 * Take the db out of bulk import mode
 */
int db_end_initial_update(void) {
    const void *val;
    unsigned long int oldval;
    unsigned long int *oldptr;

    DB_PLAYLIST *current,*last;
    DB_PLAYLISTENTRY *pple;

    char *perr;

    db_gdbmlock();
    sqlite_exec(db_songs,"PRAGMA synchronous=NORMAL;",NULL,NULL,&perr);
    db_gdbmunlock();

    DPRINTF(E_DBG,L_DB|L_SCAN,"Initial update over.  Removing stale items\n");
    val=rblookup(RB_LUFIRST,NULL,db_removed);

    while(val) {
	oldval=(*((int*)val));
	oldptr=(unsigned long int*)rbdelete((void*)&oldval,db_removed);
	if(oldptr)
	    free(oldptr);
	db_delete(oldval);

	val=rblookup(RB_LUFIRST,NULL,db_removed);
    }

    DPRINTF(E_DBG,L_DB|L_SCAN,"Done removing stale items\n");

    rbdestroy(db_removed);

    DPRINTF(E_DBG,L_DB,"Reorganizing db\n");

    db_writelock();
    DPRINTF(E_DBG,L_DB|L_PL,"Finding deleted static playlists\n");
    
    current=db_playlists.next;
    last=&db_playlists;

    while(current) {
	if((!current->found)&&(!current->is_smart)) {
	    DPRINTF(E_DBG,L_DB|L_PL,"Deleting playlist %s\n",current->name);
	    last->next=current->next;
	    if(current->nodes)
		db_playlist_count--;
	    db_version_no++;

	    while(current->nodes) {
		pple=current->nodes;
		current->nodes=pple->next;
		free(pple);
	    }

	    if(current->name)
		free(current->name);
	    free(current);
	    
	    current=last;
	}
	current=current->next;
    }

    db_update_mode=0;
    db_unlock();

    return 0;
}

/*
 * db_is_empty
 *
 * See if the db is empty or not -- that is, should
 * the scanner start up in bulk update mode or in
 * background update mode
 */
int db_is_empty(void) {
    return !db_song_count;
}


/**
 * Get the pointer to a specific playlist.  MUST HAVE A
 * READLOCK TO CALL THIS!
 *
 * @param playlistid playlist to find
 * @returns DB_PLAYLIST of playlist, or null otherwise.
 */
DB_PLAYLIST *db_playlist_find(unsigned long int playlistid) {
    DB_PLAYLIST *current;

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	return NULL;
    }

    return current;
}

/**
 * delete a given playlist
 *
 * @param playlistid playlist to delete
 */
int db_delete_playlist(unsigned long int playlistid) {
    DB_PLAYLIST *plist;
    DB_PLAYLISTENTRY *pple;
    DB_PLAYLIST *last, *current;

    DPRINTF(E_DBG,L_PL,"Deleting playlist %ld\n",playlistid);

    db_writelock();

    current=db_playlists.next;
    last=&db_playlists;

    while(current && (current->id != playlistid)) {
	last=current;
	current=current->next;
    }

    if(!current) {
	db_unlock();
	return -1;
    }

    last->next=current->next;

    if(current->nodes)
	db_playlist_count--;

    db_version_no++;

    db_unlock();

    while(current->nodes) {
	pple=current->nodes;
	current->nodes=pple->next;
	free(pple);
    }

    if(current->name)
	free(current->name);

    free(current);
    return 0;
}

/**
 * find the last modified time of a specific playlist
 * 
 * @param playlistid playlist to check (inode)
 * @returns file_time of playlist, or 0 if no playlist
 */
int db_playlist_last_modified(unsigned long int playlistid) {
    DB_PLAYLIST *plist;
    int file_time;

    db_readlock();
    plist=db_playlist_find(playlistid);
    if(!plist) {
	db_unlock();
	return 0;
    }

    file_time=plist->file_time;
    
    /* mark as found, so deleted playlists can go away */
    plist->found=1;
    db_unlock();
    return file_time;
}

/*
 * db_add_playlist
 *
 * Add a new playlist
 */
int db_add_playlist(unsigned long int playlistid, char *name, int file_time, int is_smart) {
    int err;
    DB_PLAYLIST *pnew;

    pnew=(DB_PLAYLIST*)malloc(sizeof(DB_PLAYLIST));
    if(!pnew) 
	return -1;

    pnew->name=strdup(name);
    pnew->id=playlistid;
    pnew->nodes=NULL;
    pnew->last_node=NULL;
    pnew->songs=0;
    pnew->found=1;
    pnew->file_time=file_time;
    pnew->is_smart=is_smart;

    if(!pnew->name) {
	free(pnew);
	return -1;
    }

    DPRINTF(E_DBG,L_DB|L_PL,"Adding new playlist %s\n",name);

    db_writelock();

    pnew->next=db_playlists.next;
    db_playlists.next=pnew;

    db_version_no++;

    DPRINTF(E_DBG,L_DB|L_PL,"Added playlist\n");
    db_unlock();
    return 0;
}

/*
 * db_add_playlist_song
 *
 * Add a song to a particular playlist
 *
 * FIXME: as db_add playlist, this assumes we already have a writelock from
 * db_udpate_mode.
 */
int db_add_playlist_song(unsigned long int playlistid, unsigned long int itemid) {
    DB_PLAYLIST *current;
    DB_PLAYLISTENTRY *pnew;
    int err;

    pnew=(DB_PLAYLISTENTRY*)malloc(sizeof(DB_PLAYLISTENTRY));
    if(!pnew)
	return -1;

    pnew->id=itemid;
    pnew->next=NULL;

    DPRINTF(E_DBG,L_DB|L_PL,"Adding item %ld to %ld\n",itemid,playlistid); 

    db_writelock();

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	DPRINTF(E_WARN,L_DB|L_PL,"Could not find playlist attempting to add to\n");
	db_unlock();
	free(pnew);
	return -1;
    }

    if(!current->songs)
	db_playlist_count++;

    current->songs++;
    DPRINTF(E_DBG,L_DB|L_PL,"Playlist now has %d entries\n",current->songs);

    if(current->last_node) {
	current->last_node->next = pnew;
    } else {
	current->nodes = pnew;
    }
    current->last_node=pnew;

    db_version_no++;
    
    DPRINTF(E_DBG,L_DB|L_PL,"Added playlist item\n");

    db_unlock();
    return 0;
}

/*
 * db_add
 *
 * add an MP3 file to the database.
 *
 * FIXME: Like the playlist adds, this assumes that this will only be called
 * during a db_update... that the writelock is already held.  This wouldn't 
 * necessarily be that case if, say, we were doing an add from the web interface.
 *
 */
int db_add(MP3FILE *pmp3) {
    int err;
    unsigned long int id;
    int new=1;
    char *perr;

    DPRINTF(E_DBG,L_DB,"Adding %s\n",pmp3->path);

    if(db_exists(pmp3->id)) {
	db_delete(pmp3->id);
	new=0;
    }

    /* dummy this up in case the client didn't */
    if(!pmp3->time_added)
	pmp3->time_added=(int)time(NULL);

    if(!pmp3->time_modified)
      pmp3->time_modified=(int)time(NULL);
    pmp3->db_timestamp = (int)time(NULL);
    pmp3->time_played=0; /* do we want to keep track of this? */

    db_writelock();
    db_gdbmlock();
    err=sqlite_exec_printf(db_songs,"INSERT INTO songs VALUES"
			   "(%d,"   // id
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
			   "%d,"    // time_added
			   "%d,"    // time_modified
			   "%d,"    // time_played
			   "%d,"    // db_timestamp
			   "%d,"    // bpm
			   "%d)",   // compilation
			   NULL,NULL,
			   &perr,
			   pmp3->id,
			   pmp3->path,
			   pmp3->fname,
			   pmp3->title,
			   pmp3->artist,
			   pmp3->album,
			   pmp3->genre,
			   pmp3->comment,
			   pmp3->type,
			   pmp3->composer,
			   pmp3->orchestra,
			   pmp3->conductor,
			   pmp3->grouping,
			   pmp3->url,
			   pmp3->bitrate,
			   pmp3->samplerate,
			   pmp3->song_length,
			   pmp3->file_size,
			   pmp3->year,
			   pmp3->track,
			   pmp3->total_tracks,
			   pmp3->disc,
			   pmp3->total_discs,
			   pmp3->time_added,
			   pmp3->time_modified,
			   pmp3->time_played,
			   pmp3->db_timestamp,
			   pmp3->bpm,
			   pmp3->compilation);

    if(err != SQLITE_OK)
	DPRINTF(E_FATAL,L_DB,"Error inserting file %s in database: %s\n",
		pmp3->fname,perr);

    db_gdbmunlock();
    db_version_no++;
    db_song_count++;

    DPRINTF(E_DBG,L_DB,"%s file\n", new ? "Added" : "Updated");

    db_unlock();
    return 0;
}

/*
 * db_dispose
 *
 * free a complete mp3record
 */
void db_dispose(MP3FILE *pmp3) {
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
}

ENUMHANDLE db_enum_begin(void) {
    int err;
    const char *ptail;
    char *perr;
    static struct sqlite_vm *pvm;


    db_writelock();

    if(gdbm_mp3_mustfree) {
	db_dispose(&gdbm_mp3);
    }
    gdbm_mp3_mustfree=0;

    db_gdbmlock();
    err=sqlite_compile(db_songs,"SELECT * FROM songs",
		       &ptail,&pvm,
		       &perr);
    db_gdbmunlock();

    return (ENUMHANDLE)pvm;
}

MP3FILE *db_enum(ENUMHANDLE *current) {
    struct sqlite_vm *pvm = (struct sqlite_vm *)*current;
    int cols;
    const char **valarray;
    const char **colarray;
    int err;
    char *perr;

    if(gdbm_mp3_mustfree) {
	db_dispose(&gdbm_mp3);
    }
    gdbm_mp3_mustfree=0;

    if(pvm) {
	db_gdbmlock();
	err=sqlite_step(pvm,&cols,&valarray,&colarray);
	db_gdbmunlock();

	if(err == SQLITE_ROW) {
	    db_build_mp3file(valarray,&gdbm_mp3);
	    gdbm_mp3_mustfree=1;
	    return &gdbm_mp3;
	} else if(err == SQLITE_DONE) {
	    return NULL;
	}
    }

    sqlite_finalize(pvm,&perr);
    DPRINTF(E_FATAL,L_DB,"sqlite_step: %s",perr);

    return NULL;
}

int db_enum_end(ENUMHANDLE handle) {
    struct sqlite_vm *pvm = (struct sqlite_vm *)handle;
    char *perr;

    if(gdbm_mp3_mustfree) {
	db_dispose(&gdbm_mp3);
    }
    gdbm_mp3_mustfree=0;
    
    db_gdbmlock();
    sqlite_finalize(pvm,&perr);
    db_gdbmunlock();

    db_unlock();

    return 0;
}

/*
 * db_playlist_enum_begin
 *
 * Start enumerating playlists
 */
ENUMHANDLE db_playlist_enum_begin(void) {
    int err;
    DB_PLAYLIST *current;

    db_readlock();

    /* find first playlist with a song in it! */
    current=db_playlists.next;
    while(current && (!current->songs))
	current=current->next;

    return current;
}

/*
 * db_playlist_items_enum_begin
 *
 * Start enumerating playlist items
 */
ENUMHANDLE db_playlist_items_enum_begin(unsigned long int playlistid) {
    DB_PLAYLIST *current;
    int err;

    db_readlock();

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;
    
    if(!current) 
	return NULL;

    return current->nodes;
}

/*
 * db_playlist_enum
 *
 * walk to the next entry
 */
int db_playlist_enum(ENUMHANDLE* handle) {
    DB_PLAYLIST** current = (DB_PLAYLIST**) handle;
    int retval;
    DB_PLAYLIST *p;

    if(*current) {
	retval = (*current)->id;
	p=*current;
	p=p->next;
	while(p && (!p->songs))
	    p=p->next;

	*current=p;
	return retval;
    }
    return -1;
}

/*
 * db_playlist_items_enum
 *
 * walk to the next entry
 */
int db_playlist_items_enum(ENUMHANDLE* handle) {
    DB_PLAYLISTENTRY **current;
    int retval;

    if(!handle) 
	return -1;

    current = (DB_PLAYLISTENTRY**) handle;

    if(*current) {
	retval = (*current)->id;
	*current=(*current)->next;
	return retval;
    }

    return -1;
}


/*
 * db_playlist_enum_end
 *
 * quit walking the database
 */
int db_playlist_enum_end(ENUMHANDLE handle) {
    return db_unlock();
}

/*
 * db_playlist_items_enum_end
 *
 * Quit walking the database
 */
int db_playlist_items_enum_end(ENUMHANDLE handle) {
    return db_unlock();
}


/*
 * db_find
 *
 * Find a MP3FILE entry based on file id  
 */
MP3FILE *db_find(unsigned long int id) {  /* FIXME: Not reentrant */
    MP3FILE *pmp3=NULL;
    int err,rows,cols;
    char **resarray;
    char *perr;

    db_readlock();
    db_gdbmlock();

    err=sqlite_get_table_printf(db_songs,"SELECT * FROM songs WHERE id='%d'",
				&resarray, &rows, &cols, &perr, id);

    db_gdbmunlock();

    if(rows == 0) {
	DPRINTF(E_DBG,L_DB,"Could not find id %ld\n",id);
	db_unlock();
	return NULL;
    }

    pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
    if(!pmp3) {
	DPRINTF(E_LOG,L_MISC,"Malloc failed in db_find\n");
	db_unlock();
	return NULL;
    }

    db_unlock();
    db_build_mp3file((const char **)&resarray[cols],pmp3);

    db_gdbmlock();
    sqlite_free_table(resarray);
    db_gdbmunlock();

    return pmp3;
}

/*
 * db_get_playlist_count
 *
 * return the number of playlists
 */
int db_get_playlist_count(void) {
    int retval;

    db_readlock();
    retval=db_playlist_count;
    db_unlock();
    return retval;
}

/*
 * db_get_song_count
 *
 * return the number of songs in the database.  Used for the /database
 * request
 */
int db_get_song_count(void) {
    int retval;

    db_readlock();
    retval=db_song_count;
    db_unlock();
    
    return retval;
}


/*
 * db_get_playlist_is_smart
 *
 * return whether or not the playlist is a "smart" playlist
 */
int db_get_playlist_is_smart(unsigned long int playlistid) {
    DB_PLAYLIST *current;
    int err;
    int result;

    db_readlock();

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	result=0;
    } else {
	result=current->is_smart;
    }

    db_unlock();
    return result;
}

/*
 * db_get_playlist_entry_count
 *
 * return the number of songs in a particular playlist
 */
int db_get_playlist_entry_count(unsigned long int playlistid) {
    int count;
    DB_PLAYLIST *current;
    int err;

    db_readlock();

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	count = -1;
    } else {
	count = current->songs;
    }

    db_unlock();
    return count;
}

/*
 * db_get_playlist_name
 *
 * return the name of a playlist
 */
char *db_get_playlist_name(unsigned long int playlistid) {
    char *name;
    DB_PLAYLIST *current;
    int err;

    db_readlock();

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	name = NULL;
    } else {
	name = current->name;
    }

    db_unlock();
    return name;
}


/*
 * db_exists
 *
 * Check if a particular ID exists or not
 */
int db_exists(unsigned long int id) {
    MP3FILE *pmp3=NULL;
    int err,rows,cols;
    char **resarray;
    char *perr;
    unsigned long int *node;

    db_readlock();
    db_gdbmlock();

    err=sqlite_get_table_printf(db_songs,"SELECT * FROM songs WHERE id='%d'",
				&resarray, &rows, &cols, &perr, id);

    db_gdbmunlock();

    if(rows != 1) {
	DPRINTF(E_DBG,L_DB,"Nope, not in database\n");
	db_unlock();
	return 0;
    }

    if(db_update_mode) {
	/* knock it off the maybe list */
	node = (unsigned long int*)rbdelete((void*)&id,db_removed);
	if(node) {
	    DPRINTF(E_DBG,L_DB,"Knocked node %lu from the list\n",*node);
	    free(node);
	}
    }

    db_unlock();

    DPRINTF(E_DBG,L_DB,"Yup, in database\n");
    return 1;
}


/*
 * db_last_modified
 *
 * See when the file was last updated in the database
 */
int db_last_modified(unsigned long int id) {
    int retval;
    MP3FILE *pmp3;
//    int err;

    /* readlocked as part of db_find */
    pmp3=db_find(id);
    if(!pmp3) {
	retval=0;
    } else {
      retval = pmp3->db_timestamp;
    }

    if(pmp3) {
	db_dispose(pmp3);
	free(pmp3);
    }

    return retval;
}

/*
 * db_delete
 *
 * Delete an item from the database, and also remove it
 * from any playlists.
 */
int db_delete(unsigned long int id) {
    int err;
    char *perr;
    DB_PLAYLIST *pcurrent;
    DB_PLAYLISTENTRY *phead, *ptail;


    DPRINTF(E_DBG,L_DB,"Removing item %lu\n",id);

    if(db_exists(id)) {
	db_writelock();
	db_gdbmlock();
	err=sqlite_exec_printf(db_songs,"DELETE FROM songs WHERE id=%d",
			       NULL,NULL,&perr);
	db_gdbmunlock();

	if(err != SQLITE_OK) DPRINTF(E_FATAL,L_DB,"db_delete: %s\n",perr);

	db_song_count--;
	db_version_no++;

	/* walk the playlists and remove the item */
	pcurrent=db_playlists.next;
	while(pcurrent) {
	    phead=ptail=pcurrent->nodes;
	    while(phead && (phead->id != id)) {
		ptail=phead;
		phead=phead->next;
	    }

	    if(phead) { /* found it */
		DPRINTF(E_DBG,L_DB|L_PL,"Removing from playlist %lu\n",
			pcurrent->id);
		if(phead == pcurrent->nodes) {
		    pcurrent->nodes=phead->next;
		} else {
		    ptail->next=phead->next;
		}

		if(pcurrent->last_node == phead)
		    pcurrent->last_node = ptail;

		free(phead);

		if(pcurrent->nodes == NULL) {
		    pcurrent->last_node=NULL;
		    DPRINTF(E_DBG,L_DB|L_PL,"Empty Playlist!\n");
		    db_playlist_count--;
		}
	    }
	    pcurrent=pcurrent->next;
	}
	db_version_no++;
	db_unlock();
    }


    return 0;
}
