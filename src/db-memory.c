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
#include <pthread.h>
#include <stdio.h>
#include <string.h>

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
int db_version;
int db_update_mode=0;
pthread_rwlock_t db_rwlock=PTHREAD_RWLOCK_INITIALIZER;

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

void db_freerecord(MP3RECORD *mp3record);

/*
 * db_init
 *
 * Initialize the database.  For the in-memory db
 * the parameters are insignificant
 */
int db_init(char *parameters) {
    db_root.next=NULL;
    return 0;
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
    return db_version;
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
    return db_root.next;
}

/*
 * db_add
 *
 * add an MP3 file to the database.
 */

int db_add(MP3FILE *mp3file) {
    int err;
    MP3RECORD *pnew;

    if(!pnew=(MP3RECORD*)malloc(sizeof(MP3RECORD))) {
	free(pnew);
	errno=ENOMEM;
	return -1;
    }

    memset(pnew,0,sizeof(MP3RECORD));

    memcpy(pnew->mp3file,mp3file,sizeof(MP3FILE));
    err=(int) pnew->mp3file.path=strdup(mp3file->path);
    err = err | pnew->mp3file.fname=strdup(mp3file->fname);
    err = err | pnew->mp3file.artist=strdup(mp3file->artist);
    err = err | pnew->mp3file.album=strdup(mp3file->album);
    err = err | pnew->mp3file.genre=strdup(mp3file->genre);

    if(err) {
	db_freerecord(pnew);
	errno=ENOMEM;
	return -1;
    }

    if(err=pthread_rwlock_wrlock(&db_wrlock)) {
	db_freerecord(pnew);
	errno=err;
	return -1;
    }

    pnew->next=root.next;
    root.next=pnew->next;

    pthread_rwlock_unlock(&db_wrlock);
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

