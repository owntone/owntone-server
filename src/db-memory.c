/*
 * $Id$
 * Implementation for simple in-memory linked list db
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

#include <errno.h>
#include <stdio.h>
#include <string.h>


#define __USE_UNIX98
#include <pthread.h>

#include "err.h"
#include "mp3-scanner.h"

/*
 * Typedefs
 */
typedef struct tag_mp3record {
    MP3FILE mp3file;
    struct tag_mp3record *next;
} MP3RECORD;

#define MAYBEFREE(a) { if((a)) free((a)); };

/*
 * Globals 
 */
MP3RECORD db_root;
int db_version_no;
int db_update_mode=0;
int db_song_count;
int db_song_id;
pthread_rwlock_t db_rwlock; /* OSX doesn't have PTHREAD_RWLOCK_INITIALIZER */
pthread_once_t db_initlock=PTHREAD_ONCE_INIT;
/*
 * Forwards
 */

int db_start_initial_update(void);
int db_end_initial_update(void);
int db_is_empty(void);
int db_init(char *parameters);
int db_deinit(void);
int db_version(void);
int db_add(MP3FILE *mp3file);

MP3RECORD *db_enum_begin(void);
MP3FILE *db_enum(MP3RECORD **current);
int db_enum_end(void);
int db_get_song_count(void);
MP3FILE *db_find(int id);

void db_freerecord(MP3RECORD *mp3record);

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
 * db_init
 *
 * Initialize the database.  For the in-memory db
 * the parameters are insignificant
 */
int db_init(char *parameters) {
    db_root.next=NULL;
    db_version_no=1;
    db_song_count=0;
    db_song_id=1;

    return pthread_once(&db_initlock,db_init_once);
}

/*
 * db_deinit
 *
 * Close the db, in this case freeing memory
 */
int db_deinit(void) {
    /* free used memory here */
    return 0;
}

/*
 * db_version
 *
 * return the db version
 */
int db_version(void) {
    return db_version_no;
}

/*
 * db_start_initial_update
 *
 * Set the db to bulk import mode
 */
int db_start_initial_update(void) {
    db_update_mode=1;
    return 0;
}

/*
 * db_end_initial_update
 *
 * Take the db out of bulk import mode
 */
int db_end_initial_update(void) {
    db_update_mode=0;
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
    return !db_root.next;
}

/*
 * db_add
 *
 * add an MP3 file to the database.
 */

int db_add(MP3FILE *mp3file) {
    int err;
    int g;
    MP3RECORD *pnew;

    DPRINTF(ERR_DEBUG,"Adding %s\n",mp3file->path);

    if((pnew=(MP3RECORD*)malloc(sizeof(MP3RECORD))) == NULL) {
	free(pnew);
	errno=ENOMEM;
	return -1;
    }

    memset(pnew,0,sizeof(MP3RECORD));

    memcpy(&pnew->mp3file,mp3file,sizeof(MP3FILE));

    g=(int) pnew->mp3file.path=strdup(mp3file->path);
    g = g && (pnew->mp3file.fname=strdup(mp3file->fname));

    if(mp3file->artist)
	g = g && (pnew->mp3file.artist=strdup(mp3file->artist));

    if(mp3file->album)
	g = g && (pnew->mp3file.album=strdup(mp3file->album));

    if(mp3file->genre)
	g = g && (pnew->mp3file.genre=strdup(mp3file->genre));

    if(!g) {
	DPRINTF(ERR_WARN,"Malloc error in db_add\n");
	db_freerecord(pnew);
	errno=ENOMEM;
	return -1;
    }

    if(err=pthread_rwlock_wrlock(&db_rwlock)) {
	DPRINTF(ERR_WARN,"cannot lock wrlock in db_add\n");
	db_freerecord(pnew);
	errno=err;
	return -1;
    }

    pnew->mp3file.id=db_song_id++;
    pnew->next=db_root.next;
    db_root.next=pnew;

    if(!db_update_mode) {
	db_version_no++;
    }

    db_song_count++;
    
    pthread_rwlock_unlock(&db_rwlock);
    DPRINTF(ERR_DEBUG,"Added file\n");
    return 0;
}

/*
 * db_freerecord
 *
 * free a complete mp3record
 */
void db_freerecord(MP3RECORD *mp3record) {
    MAYBEFREE(mp3record->mp3file.path);
    MAYBEFREE(mp3record->mp3file.fname);
    MAYBEFREE(mp3record->mp3file.artist);
    MAYBEFREE(mp3record->mp3file.album);
    MAYBEFREE(mp3record->mp3file.genre);
    free(mp3record);
}

/*
 * db_enum_begin
 *
 * Begin to walk through an enum of 
 * the database.
 *
 * this should be done quickly, as we'll be holding
 * a reader lock on the db
 */
MP3RECORD *db_enum_begin(void) {
    int err;

    if(err=pthread_rwlock_wrlock(&db_rwlock)) {
	log_err(0,"Cannot lock rwlock\n");
	errno=err;
	return NULL;
    }

    return db_root.next;
}


/*
 * db_enum
 *
 * Walk to the next entry
 */
MP3FILE *db_enum(MP3RECORD **current) {
    MP3FILE *retval;

    if(*current) {
	retval=&((*current)->mp3file);
	*current=(*current)->next;
	return retval;
    }
    return NULL;
}

/*
 * db_enum_end
 *
 * quit walking the database (and give up reader lock)
 */
int db_enum_end(void) {
    return pthread_rwlock_unlock(&db_rwlock);
}

/*
 * db_find
 *
 * Find a MP3FILE entry based on file id
 */
MP3FILE *db_find(int id) {
    MP3RECORD *current=db_root.next;
    while((current) && (current->mp3file.id != id)) {
	current=current->next;
    }

    if(!current)
	return NULL;

    return &current->mp3file;
}

/*
 * db_get_song_count
 *
 * return the number of songs in the database.  Used for the /database
 * request
 */
int db_get_song_count(void) {
    return db_song_count;
}
