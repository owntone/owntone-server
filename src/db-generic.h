/*
 * $Id$
 * Header info for generic database
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

#ifndef _DB_GENERIC_H_
#define _DB_GENERIC_H_

#include "mp3-scanner.h" /** for MP3FILE */

typedef enum {
    // generic meta data
    metaItemId,
    metaItemName,
    metaItemKind,
    metaPersistentId,
    metaContainerItemId,
    metaParentContainerId,
    
    firstTypeSpecificMetaId,

    // song meta data
    metaSongAlbum = firstTypeSpecificMetaId,
    metaSongArtist,
    metaSongBPM,
    metaSongBitRate,
    metaSongComment,
    metaSongCompilation,
    metaSongComposer,
    metaSongDataKind,
    metaSongDataURL,
    metaSongDateAdded,
    metaSongDateModified,
    metaSongDescription,
    metaSongDisabled,
    metaSongDiscCount,
    metaSongDiscNumber,
    metaSongEqPreset,
    metaSongFormat,
    metaSongGenre,
    metaSongGrouping,
    metaSongRelativeVolume,
    metaSongSampleRate,
    metaSongSize,
    metaSongStartTime,
    metaSongStopTime,
    metaSongTime,
    metaSongTrackCount,
    metaSongTrackNumber,
    metaSongUserRating,
    metaSongYear
} MetaFieldName_t;

typedef enum {
    queryTypeItems,
    queryTypePlaylists,
    queryTypePlaylistItems,
    queryTypeBrowseArtists,
    queryTypeBrowseAlbums,
    queryTypeBrowseGenres,
    queryTypeBrowseComposers
} QueryType_t;

typedef enum {
    indexTypeNone,
    indexTypeFirst,
    indexTypeLast,
    indexTypeSub
} IndexType_t;

typedef enum {
    countSongs,
    countPlaylists
} CountType_t;

typedef unsigned long long MetaField_t;

typedef struct tag_dbqueryinfo {
    QueryType_t query_type;
    IndexType_t index_type;
    MetaField_t meta;
    int index_low;
    int index_high;
    int playlist_id;
    int db_id;
    int session_id;
    int uri_count;
    char *uri_sections[10];
    char *whereclause;
    void *output_info;
} DBQUERYINFO;

typedef struct {
    const char*	tag;
    MetaFieldName_t bit;
} METAMAP;

typedef struct tag_daap_items {
    int type;
    char *tag;
    char *description;
} DAAP_ITEMS;

extern DAAP_ITEMS taglist[];

extern int db_set_backend(char *type);

extern int db_open(char *parameters); 
extern int db_init(int reload);
extern int db_deinit(void);

extern int db_revision(void);

extern int db_add(MP3FILE *pmp3);

extern int db_enum_start(DBQUERYINFO *pinfo);
extern int db_enum_size(DBQUERYINFO *pinfo, int *count);
extern int db_enum_fetch(DBQUERYINFO *pinfo, unsigned char **pdmap);
extern int db_enum_reset(DBQUERYINFO *pinfo);
extern int db_enum_end(void);
extern int db_start_scan(void);
extern int db_end_scan(void);
extern int db_exists(char *path);
extern int db_scanning(void);

extern MP3FILE *db_fetch_item(int id);
extern MP3FILE *db_fetch_path(char *path);


/* metatag parsing */
extern MetaField_t db_encode_meta(char *meta);
extern int db_wantsmeta(MetaField_t meta, MetaFieldName_t fieldNo);

/* dmap helper functions */
extern int db_dmap_add_char(char *where, char *tag, char value);
extern int db_dmap_add_short(char *where, char *tag, short value);
extern int db_dmap_add_int(char *where, char *tag, int value);
extern int db_dmap_add_string(char *where, char *tag, char *value);
extern int db_dmap_add_container(char *where, char *tag, int size);

/* Holdover functions from old db interface...
 * should these be removed?  Refactored?
 */
extern int db_get_song_count(void); 
extern int db_get_playlist_count(void);
extern void db_dispose_item(MP3FILE *pmp3);

#endif /* _DB_GENERIC_H_ */
