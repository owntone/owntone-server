/*
 * $Id$
 * Implementation for simple gdbm db
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
#include <gdbm.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>


#include "err.h"
#include "mp3-scanner.h"
#include "playlist.h"
#include "redblack.h"

#define DB_VERSION 1
#define STRLEN(a) (a) ? strlen((a)) + 1 : 1 
/*
 * Typedefs
 */
typedef struct tag_mp3record {
    MP3FILE mp3file;
    datum key;
} MP3RECORD;

typedef struct tag_playlistentry {
    unsigned int id;
    struct tag_playlistentry *next;
} DB_PLAYLISTENTRY;

typedef struct tag_playlist {
    unsigned int id;
    int songs;
    int is_smart;
    char *name;
    struct tag_playlistentry *nodes;
    struct tag_playlist *next;
} DB_PLAYLIST;

typedef struct tag_mp3packed {
    int version;
    int bitrate;
    int samplerate;
    int song_length;
    int file_size;
    int year;
    
    int track;
    int total_tracks;

    int disc;
    int total_discs;

    int time_added;
    int time_modified;
    int time_played;

    unsigned int id; /* inode */

    int path_len;
    int fname_len;
    int title_len;
    int artist_len;
    int album_len;
    int genre_len;
    int comment_len;
    int type_len;

    char data[1];
} MP3PACKED;


#define MAYBEFREE(a) { if((a)) free((a)); };

/*
 * Globals 
 */
int db_version_no;
int db_update_mode=0;
int db_song_count;
int db_playlist_count=0;
DB_PLAYLIST db_playlists;
pthread_rwlock_t db_rwlock; /* OSX doesn't have PTHREAD_RWLOCK_INITIALIZER */
pthread_once_t db_initlock=PTHREAD_ONCE_INIT;
MP3RECORD db_enum_helper;
GDBM_FILE db_songs;
struct rbtree *db_removed;

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
int db_delete(int id);
int db_add_playlist(unsigned int playlistid, char *name, int is_smart);
int db_add_playlist_song(unsigned int playlistid, unsigned int itemid);
int db_unpackrecord(datum *pdatum, MP3FILE *pmp3);
datum *db_packrecord(MP3FILE *pmp3);

MP3RECORD *db_enum_begin(void);
MP3FILE *db_enum(MP3RECORD **current);
int db_enum_end(void);

DB_PLAYLIST *db_playlist_enum_begin(void);
int db_playlist_enum(DB_PLAYLIST **current);
int db_playlist_enum_end(void);

DB_PLAYLISTENTRY *db_playlist_items_enum_begin(int playlistid);
int db_playlist_items_enum(DB_PLAYLISTENTRY **current);
int db_playlist_items_enum_end(void);

int db_get_song_count(void);
int db_get_playlist_count(void);
int db_get_playlist_is_smart(int playlistid); 
int db_get_playlist_entry_count(int playlistid);
char *db_get_playlist_name(int playlistid);

MP3FILE *db_find(int id);

void db_freefile(MP3FILE *pmp3);
int db_compare_rb_nodes(const void *pa, const void *pb, const void *cfg);

/*
 * db_compare_rb_nodes
 *
 * compare redblack nodes, which are just ints
 */
int db_compare_rb_nodes(const void *pa, const void *pb, const void *cfg) {
    if(*(int*)pa < *(int *)pb) return -1;
    if(*(int*)pb < *(int *)pa) return 1;
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
 * db_init
 *
 * Initialize the database.  Parameter is the directory
 * of the database files
 */
int db_init(char *parameters) {
    MP3FILE mp3file;
    datum tmp_key,tmp_nextkey,song_data;
    char db_path[PATH_MAX + 1];
    
    if(pthread_once(&db_initlock,db_init_once))
	return -1;

    if((db_removed=rbinit(db_compare_rb_nodes,NULL)) == NULL) {
	errno=ENOMEM;
	return -1;
    }

    pl_register();

    snprintf(db_path,sizeof(db_path),"%s/%s",parameters,"songs.gdb");
    db_songs=gdbm_open(db_path,0,GDBM_WRCREAT | GDBM_SYNC | GDBM_NOLOCK,
		       0600,NULL);
    if(!db_songs) {
	DPRINTF(ERR_FATAL,"Could not open songs database (%s)\n",
		gdbm_strerror(errno));
	return -1;
    }

    db_version_no=1;
    db_song_count=0;

    DPRINTF(ERR_DEBUG,"Building playlists\n");

    /* count the actual songs and build the playlists */
    tmp_key=gdbm_firstkey(db_songs);

    MEMNOTIFY(tmp_key.dptr);

    while(tmp_key.dptr) {
	/* Add it to the rbtree */
	if(!rbsearch((void*)tmp_key.dptr,db_removed)) {
	    errno=ENOMEM;
	    return -1;
	}

	/* Fetch that key */
	song_data=gdbm_fetch(db_songs,tmp_key);
	MEMNOTIFY(song_data.dptr);
	if(song_data.dptr) {
	    if(!db_unpackrecord(&song_data,&mp3file)) {
		/* Check against playlist */
		pl_eval(&mp3file);
		db_freefile(&mp3file);
	    }
	    free(song_data.dptr);
	}
	
	tmp_nextkey=gdbm_nextkey(db_songs,tmp_key);
	MEMNOTIFY(tmp_nextkey.dptr);
	// free(tmp_key.dptr); /* we'll free it in update mode */
	tmp_key=tmp_nextkey;
	db_song_count++;
    }

    DPRINTF(ERR_DEBUG,"Loaded database... found %d songs\n",db_song_count);

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

    gdbm_close(db_songs);

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
    const void *val;
    int inode;

    db_update_mode=0;

    DPRINTF(ERR_DEBUG,"Initial update over.  Removing stale items\n");
    for(val=rblookup(RB_LUFIRST,NULL,db_removed); val != NULL; val=rblookup(RB_LUNEXT,val,db_removed)) {
	db_delete(*((int*)val));
	free(val);
    }

    rbdestroy(db_removed);

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


/*
 * db_add_playlist
 *
 * Add a new playlist
 */
int db_add_playlist(unsigned int playlistid, char *name, int is_smart) {
    int err;
    DB_PLAYLIST *pnew;
    
    pnew=(DB_PLAYLIST*)malloc(sizeof(DB_PLAYLIST));
    if(!pnew)
	return -1;

    pnew->name=strdup(name);
    pnew->id=playlistid;
    pnew->nodes=NULL;
    pnew->songs=0;
    pnew->is_smart=is_smart;

    if(!pnew->name) {
	free(pnew);
	return -1;
    }

    DPRINTF(ERR_DEBUG,"Adding new playlist %s\n",name);

    if((err=pthread_rwlock_wrlock(&db_rwlock))) {
	DPRINTF(ERR_WARN,"cannot lock wrlock in db_add\n");
	free(pnew->name);
	free(pnew);
	errno=err;
	return -1;
    }

    /* we'll update playlist count when we add a song! */
    //    db_playlist_count++;
    pnew->next=db_playlists.next;
    db_playlists.next=pnew;

    if(!db_update_mode) {
	db_version_no++;
    }

    pthread_rwlock_unlock(&db_rwlock);
    DPRINTF(ERR_DEBUG,"Added playlist\n");
    return 0;
}

/*
 * db_add_playlist_song
 *
 * Add a song to a particular playlist
 */
int db_add_playlist_song(unsigned int playlistid, unsigned int itemid) {
    DB_PLAYLIST *current;
    DB_PLAYLISTENTRY *pnew;
    int err;
    
    pnew=(DB_PLAYLISTENTRY*)malloc(sizeof(DB_PLAYLISTENTRY));
    if(!pnew)
	return -1;

    pnew->id=itemid;
    pnew->next=NULL;

    DPRINTF(ERR_DEBUG,"Adding item %d to %d\n",itemid,playlistid); 

    if((err=pthread_rwlock_wrlock(&db_rwlock))) {
	DPRINTF(ERR_WARN,"cannot lock wrlock in db_add\n");
	free(pnew);
	errno=err;
	return -1;
    }

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	DPRINTF(ERR_WARN,"Could not find playlist attempting to add to\n");
	pthread_rwlock_unlock(&db_rwlock);
	free(pnew);
	return -1;
    }

    if(!current->songs)
	db_playlist_count++;


    current->songs++;
    DPRINTF(ERR_DEBUG,"Playlist now has %d entries\n",current->songs);
    pnew->next = current->nodes;
    current->nodes = pnew;

    if(!db_update_mode) {
	db_version_no++;
    }

    pthread_rwlock_unlock(&db_rwlock);
    DPRINTF(ERR_DEBUG,"Added playlist item\n");
    return 0;
}

/*
 * db_packrecord 
 *
 * Given an MP3 record, turn it into a datum
 */
datum *db_packrecord(MP3FILE *pmp3) {
    int len;
    datum *result;
    MP3PACKED *ppacked;
    int offset;

    len=sizeof(MP3PACKED)-1;  /* minus the data char... */
    len += STRLEN(pmp3->path);
    len += STRLEN(pmp3->fname);
    len += STRLEN(pmp3->title);
    len += STRLEN(pmp3->artist);
    len += STRLEN(pmp3->album);
    len += STRLEN(pmp3->genre);
    len += STRLEN(pmp3->comment);
    len += STRLEN(pmp3->type);

    result = (datum*) malloc(sizeof(datum));
    if(!result)
	return NULL;

    result->dptr = (void*)malloc(len);
    result->dsize=len;

    if(!result->dptr) {
	free(result);
	return NULL;
    }

    memset(result->dptr,0x00,len);

    /* start packing! */
    ppacked=(MP3PACKED *)result->dptr;

    ppacked->version=DB_VERSION;
    ppacked->bitrate=pmp3->bitrate;
    ppacked->samplerate=pmp3->samplerate;
    ppacked->song_length=pmp3->song_length;
    ppacked->file_size=pmp3->file_size;
    ppacked->year=pmp3->year;
    ppacked->track=pmp3->track;
    ppacked->total_tracks=pmp3->total_tracks;
    ppacked->disc=pmp3->disc;
    ppacked->total_discs=pmp3->total_discs;
    ppacked->time_added=pmp3->time_added;
    ppacked->time_modified=pmp3->time_modified;
    ppacked->time_played=pmp3->time_played;
    ppacked->id=pmp3->id;

    ppacked->path_len=STRLEN(pmp3->path);
    ppacked->fname_len=STRLEN(pmp3->fname);
    ppacked->title_len=STRLEN(pmp3->title);
    ppacked->artist_len=STRLEN(pmp3->artist);
    ppacked->album_len=STRLEN(pmp3->album);
    ppacked->genre_len=STRLEN(pmp3->genre);
    ppacked->comment_len=STRLEN(pmp3->comment);
    ppacked->type_len=STRLEN(pmp3->type);

    offset=0;
    if(pmp3->path)
	strncpy(&ppacked->data[offset],pmp3->path,ppacked->path_len);
    offset+=ppacked->path_len;

    if(pmp3->fname)
	strncpy(&ppacked->data[offset],pmp3->fname,ppacked->fname_len);
    offset+=ppacked->fname_len;

    if(pmp3->title)
	strncpy(&ppacked->data[offset],pmp3->title,ppacked->title_len);
    offset+=ppacked->title_len;

    if(pmp3->artist)
	strncpy(&ppacked->data[offset],pmp3->artist,ppacked->artist_len);
    offset+=ppacked->artist_len;

    if(pmp3->album)
	strncpy(&ppacked->data[offset],pmp3->album,ppacked->album_len);
    offset+=ppacked->album_len;

    if(pmp3->genre)
	strncpy(&ppacked->data[offset],pmp3->genre,ppacked->genre_len);
    offset+=ppacked->genre_len;

    if(pmp3->comment)
	strncpy(&ppacked->data[offset],pmp3->comment,ppacked->comment_len);
    offset+=ppacked->comment_len;

    if(pmp3->type)
	strncpy(&ppacked->data[offset],pmp3->type,ppacked->type_len);
    offset+=ppacked->type_len;

    /* whew */
    return result;
}


/*
 * db_unpackrecord
 *
 * Given a datum, return an MP3 record
 */
int db_unpackrecord(datum *pdatum, MP3FILE *pmp3) {
    MP3PACKED *ppacked;
    int offset;

    /* should check minimum length (for v1) */

    memset(pmp3,0x0,sizeof(MP3FILE));

    /* VERSION 1 */
    ppacked=(MP3PACKED*)pdatum->dptr;
    pmp3->bitrate=ppacked->bitrate;
    pmp3->samplerate=ppacked->samplerate;
    pmp3->song_length=ppacked->song_length;
    pmp3->file_size=ppacked->file_size;
    pmp3->year=ppacked->year;
    pmp3->track=ppacked->track;
    pmp3->total_tracks=ppacked->total_tracks;
    pmp3->disc=ppacked->disc;
    pmp3->total_discs=ppacked->total_discs;
    pmp3->time_added=ppacked->time_added;
    pmp3->time_modified=ppacked->time_modified;
    pmp3->time_played=ppacked->time_played;
    pmp3->id=ppacked->id;

    offset=0;
    if(ppacked->path_len > 1)
	pmp3->path=strdup(&ppacked->data[offset]);
    offset += ppacked->path_len;

    if(ppacked->fname_len > 1)
	pmp3->fname=strdup(&ppacked->data[offset]);
    offset += ppacked->fname_len;

    if(ppacked->title_len > 1)
	pmp3->title=strdup(&ppacked->data[offset]);
    offset += ppacked->title_len;

    if(ppacked->artist_len > 1)
	pmp3->artist=strdup(&ppacked->data[offset]);
    offset += ppacked->artist_len;

    if(ppacked->album_len > 1)
	pmp3->album=strdup(&ppacked->data[offset]);
    offset += ppacked->album_len;

    if(ppacked->genre_len > 1)
	pmp3->genre=strdup(&ppacked->data[offset]);
    offset += ppacked->genre_len;

    if(ppacked->comment_len > 1)
	pmp3->comment=strdup(&ppacked->data[offset]);
    offset += ppacked->comment_len;

    if(ppacked->type_len > 1)
	pmp3->type=strdup(&ppacked->data[offset]);
    offset += ppacked->type_len;
    
    return 0;
}

/*
 * db_add
 *
 * add an MP3 file to the database.
 */
int db_add(MP3FILE *pmp3) {
    int err;
    datum *pnew;
    datum dkey;
    MP3PACKED *ppacked;
    unsigned int id;

    DPRINTF(ERR_DEBUG,"Adding %s\n",pmp3->path);

    if(!(pnew=db_packrecord(pmp3))) {
	errno=ENOMEM;
	return -1;
    }

    if((err=pthread_rwlock_wrlock(&db_rwlock))) {
	DPRINTF(ERR_WARN,"cannot lock wrlock in db_add\n");
	free(pnew->dptr);
	free(pnew);
	errno=err;
	return -1;
    }

    /* insert the datum into the underlying database */
    dkey.dptr=(void*)&(pmp3->id);
    dkey.dsize=sizeof(unsigned int);

    /* dummy this up in case the client didn't */
    ppacked=(MP3PACKED *)pnew->dptr;
    ppacked->time_added=(int)time(NULL);
    ppacked->time_modified=ppacked->time_added;
    ppacked->time_played=0;

    if(gdbm_store(db_songs,dkey,*pnew,GDBM_REPLACE)) {
	log_err(1,"Error inserting file %s in database\n",pmp3->fname);
    }

    DPRINTF(ERR_DEBUG,"Testing for %d\n",pmp3->id);
    id=pmp3->id;
    dkey.dptr=(void*)&id;
    dkey.dsize=sizeof(unsigned int);

    if(!gdbm_exists(db_songs,dkey)) {
	log_err(1,"Error.. could not find just added file\n");
    }

    free(pnew->dptr);
    free(pnew);

    if(!db_update_mode) {
	db_version_no++;
    }

    db_song_count++;
    
    pthread_rwlock_unlock(&db_rwlock);
    DPRINTF(ERR_DEBUG,"Added file\n");
    return 0;
}

/*
 * db_freefile
 *
 * free a complete mp3record
 */
void db_freefile(MP3FILE *pmp3) {
    MAYBEFREE(pmp3->path);
    MAYBEFREE(pmp3->fname);
    MAYBEFREE(pmp3->title);
    MAYBEFREE(pmp3->artist);
    MAYBEFREE(pmp3->album);
    MAYBEFREE(pmp3->genre);
    MAYBEFREE(pmp3->comment);
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

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return NULL;
    }

    memset((void*)&db_enum_helper,0x00,sizeof(db_enum_helper));
    db_enum_helper.key=gdbm_firstkey(db_songs);
    MEMNOTIFY(db_enum_helper.key.dptr);
    if(!db_enum_helper.key.dptr)
	return NULL;

    return &db_enum_helper;
}

/*
 * db_playlist_enum_begin
 *
 * Start enumerating playlists
 */
DB_PLAYLIST *db_playlist_enum_begin(void) {
    int err;
    DB_PLAYLIST *current;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return NULL;
    }

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
DB_PLAYLISTENTRY *db_playlist_items_enum_begin(int playlistid) {
    DB_PLAYLIST *current;
    int err;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return NULL;
    }

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;
    
    if(!current)
	return NULL;

    return current->nodes;
}


/*
 * db_enum
 *
 * Walk to the next entry
 */
MP3FILE *db_enum(MP3RECORD **current) {
    datum nextkey;
    datum data;

    if(db_enum_helper.key.dptr) {
	db_freefile(&db_enum_helper.mp3file);

	/* Got the key, let's fetch it */
	data=gdbm_fetch(db_songs,db_enum_helper.key);
	MEMNOTIFY(data.dptr);
	if(!data.dptr) {
	    log_err(1,"Inconsistant database.\n");
	}

	if(db_unpackrecord(&data,&db_enum_helper.mp3file)) {
	    log_err(1,"Cannot unpack item.. Corrupt database?\n");
	}

	if(data.dptr)
	    free(data.dptr);

	nextkey=gdbm_nextkey(db_songs,db_enum_helper.key);
	MEMNOTIFY(nextkey.dptr);

	if(db_enum_helper.key.dptr) {
	    free(db_enum_helper.key.dptr);
	    db_enum_helper.key.dptr=NULL;
	}

	db_enum_helper.key=nextkey;
	return &db_enum_helper.mp3file;
    }

    return NULL;
}

/*
 * db_playlist_enum
 *
 * walk to the next entry
 */
int db_playlist_enum(DB_PLAYLIST **current) {
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
int db_playlist_items_enum(DB_PLAYLISTENTRY **current) {
    int retval;

    if(*current) {
	retval = (*current)->id;
	*current=(*current)->next;
	return retval;
    }

    return -1;
}

/*
 * db_enum_end
 *
 * quit walking the database (and give up reader lock)
 */
int db_enum_end(void) {
    db_freefile(&db_enum_helper.mp3file);
    if(db_enum_helper.key.dptr)
	free(db_enum_helper.key.dptr);

    return pthread_rwlock_unlock(&db_rwlock);
}

/*
 * db_playlist_enum_end
 *
 * quit walking the database
 */
int db_playlist_enum_end(void) {
    return pthread_rwlock_unlock(&db_rwlock);
}

/*
 * db_playlist_items_enum_end
 *
 * Quit walking the database
 */
int db_playlist_items_enum_end(void) {
    return pthread_rwlock_unlock(&db_rwlock);
}


/*
 * db_find
 *
 * Find a MP3FILE entry based on file id  
 */
MP3FILE *db_find(int id) {  /* FIXME: Not reentrant */
    static MP3FILE *pmp3=NULL;
    datum key, content;

    key.dptr=(char*)&id;
    key.dsize=sizeof(int);

    content=gdbm_fetch(db_songs,key);
    MEMNOTIFY(content.dptr);
    if(!content.dptr)
	return NULL;

    if(pmp3) {
	db_freefile(pmp3);
	free(pmp3);
    }

    pmp3=(MP3FILE*)malloc(sizeof(MP3FILE));
    if(!pmp3)
	return NULL;

    db_unpackrecord(&content,pmp3);
    return pmp3;
}


/*
 * db_get_playlist_count
 *
 * return the number of playlists
 */
int db_get_playlist_count(void) {
    return db_playlist_count;
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


/*
 * db_get_playlist_is_smart
 *
 * return whether or not the playlist is a "smart" playlist
 */
int db_get_playlist_is_smart(int playlistid) {
    DB_PLAYLIST *current;
    int err;
    int result;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return -1;	
    }

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	result=0;
    } else {
	result=current->is_smart;
    }

    pthread_rwlock_unlock(&db_rwlock);
    return result;
}

/*
 * db_get_playlist_entry_count
 *
 * return the number of songs in a particular playlist
 */
int db_get_playlist_entry_count(int playlistid) {
    int count;
    DB_PLAYLIST *current;
    int err;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return -1;	
    }

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	count = -1;
    } else {
	count = current->songs;
    }

    pthread_rwlock_unlock(&db_rwlock);
    return count;
}

/*
 * db_get_playlist_name
 *
 * return the name of a playlist
 *
 * FIXME: Small race here
 */
char *db_get_playlist_name(int playlistid) {
    char *name;
    DB_PLAYLIST *current;
    int err;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return NULL;
    }

    current=db_playlists.next;
    while(current && (current->id != playlistid))
	current=current->next;

    if(!current) {
	name = NULL;
    } else {
	name = current->name;
    }

    pthread_rwlock_unlock(&db_rwlock);
    return name;
}


/*
 * db_exists
 *
 * Check if a particular ID exists or not
 */
int db_exists(int id) {
    int *node;
    int err;
    MP3FILE *pmp3;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return -1;	
    }

    /* this is wrong and expensive */

    pmp3=db_find(id);

    if(db_update_mode) {
	/* knock it off the maybe list */
	(void*)node = rbdelete((void*)&id,db_removed);
	if(node) {
	    DPRINTF(ERR_DEBUG,"Knocked node %d from the list\n",*node);
	    free(node);
	}
    }
    
    pthread_rwlock_unlock(&db_rwlock);
    return pmp3 ? 1 : 0;
}


/*
 * db_last_modified
 *
 * See when the file was last updated in the database
 */
int db_last_modified(int id) {
    int retval;
    MP3FILE *pmp3;
    int err;

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return -1;	
    }

    pmp3=db_find(id);
    if(!pmp3) {
	retval=0;
    } else {
	retval=pmp3->time_modified;
    }


    pthread_rwlock_unlock(&db_rwlock);
    return retval;
}

/*
 * db_delete
 *
 * Delete an item from the database, and also remove it
 * from any playlists.
 */
int db_delete(int id) {
    int err;
    int retval;
    datum key;
    DB_PLAYLIST *pcurrent;
    DB_PLAYLISTENTRY *phead, *ptail;

    DPRINTF(ERR_DEBUG,"Removing item %d\n",id);

    if((err=pthread_rwlock_rdlock(&db_rwlock))) {
	log_err(1,"Cannot lock rwlock\n");
	errno=err;
	return -1;	
    }

    if(db_exists(id)) {
	key.dptr=(void*)&id;
	key.dsize=sizeof(int);
	gdbm_delete(db_songs,key);
	db_song_count--;
	if(!db_update_mode) 
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
		DPRINTF(ERR_DEBUG,"Removing from playlist %d\n",
			pcurrent->id);
		if(phead == pcurrent->nodes) {
		    pcurrent->nodes=phead->next;
		} else {
		    ptail->next=phead->next;
		}
		free(phead);
	    }
	    pcurrent=pcurrent->next;
	}
    }

    pthread_rwlock_unlock(&db_rwlock);
    return 0;
}
