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
#include "smart-parser.h" /** for PARSETREE */

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
    metaSongYear,

    /* iTunes 4.5 + */
    metaSongCodecType,
    metaSongCodecSubType,
    metaItunesNormVolume,
    metaItmsSongId,
    metaItmsArtistId,
    metaItmsPlaylistId,
    metaItmsComposerId,
    metaItmsGenreId,
    metaItmsStorefrontId,
    metaItunesSmartPlaylist,

    /* iTunes 5.0 + */
    metaSongContentRating,
    metaHasChildContainers,

    /* iTunes 6.0.2+ */
    metaItunesHasVideo,

    /* mt-daapd specific */
    metaMPlaylistSpec,
    metaMPlaylistType
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
    int zero_length; /* emit zero-length strings? */
    int index_low;
    int index_high;
    int playlist_id;
    int db_id;
    int session_id;
    int uri_count;
    char *uri_sections[10];
    PARSETREE pt;
    void *output_info;
} DBQUERYINFO;

typedef struct {
    const char* tag;
    MetaFieldName_t bit;
} METAMAP;

typedef struct tag_daap_items {
    int type;
    char *tag;
    char *description;
} DAAP_ITEMS;

extern DAAP_ITEMS taglist[];

extern int db_open(char **pe, char *type, char *parameters);
extern int db_init(int reload);
extern int db_deinit(void);

extern int db_revision(void);

extern int db_add(char **pe, MP3FILE *pmp3, int *id);

extern int db_enum_start(char **pe, DBQUERYINFO *pinfo);
extern int db_enum_size(char **pe, DBQUERYINFO *pinfo, int *count, int *total_size);
extern int db_enum_fetch(char **pe, DBQUERYINFO *pinfo, int *size, unsigned char **pdmap);
extern int db_enum_reset(char **pe, DBQUERYINFO *pinfo);
extern int db_enum_end(char **pe);
extern int db_start_scan(void);
extern int db_end_song_scan(void);
extern int db_end_scan(void);
extern int db_exists(char *path);
extern int db_scanning(void);

extern int db_add_playlist(char **pe, char *name, int type, char *clause, char *path, int index, int *playlistid);
extern int db_add_playlist_item(char **pe, int playlistid, int songid);
extern int db_edit_playlist(char **pe, int id, char *name, char *clause);
extern int db_delete_playlist(char **pe, int playlistid);
extern int db_delete_playlist_item(char **pe, int playlistid, int songid);

extern void db_get_error(char **pe, int err, ...);

extern MP3FILE *db_fetch_item(char **pe, int id);
extern MP3FILE *db_fetch_path(char **pe, char *path, int index);
extern M3UFILE *db_fetch_playlist(char **pe, char *path, int index);


/* metatag parsing */
extern MetaField_t db_encode_meta(char *meta);
extern int db_wantsmeta(MetaField_t meta, MetaFieldName_t fieldNo);

/* dmap helper functions */
extern int db_dmap_add_char(unsigned char *where, char *tag, char value);
extern int db_dmap_add_short(unsigned char *where, char *tag, short value);
extern int db_dmap_add_int(unsigned char *where, char *tag, int value);
extern int db_dmap_add_string(unsigned char *where, char *tag, char *value);
extern int db_dmap_add_literal(unsigned char *where, char *tag, char *value, int size);
extern int db_dmap_add_container(unsigned char *where, char *tag, int size);

/* Holdover functions from old db interface...
 * should these be removed?  Refactored?
 */

extern int db_playcount_increment(char **pe, int id);
extern int db_get_song_count(char **pe, int *count);
extern int db_get_playlist_count(char **pe, int *count);
extern void db_dispose_item(MP3FILE *pmp3);
extern void db_dispose_playlist(M3UFILE *pm3u);


#define DB_E_SUCCESS                 0x00
#define DB_E_SQL_ERROR               0x01 /**< some kind of sql error - typically bad syntax */
#define DB_E_DUPLICATE_PLAYLIST      0x02 /**< playlist already exists when adding */
#define DB_E_NOCLAUSE                0x03 /**< adding smart playlist with no clause */
#define DB_E_INVALIDTYPE             0x04 /**< trying to add playlist items to invalid type */
#define DB_E_NOROWS                  0x05 /**< sql query returned no rows */
#define DB_E_INVALID_PLAYLIST        0x06 /**< bad playlist id */
#define DB_E_INVALID_SONGID          0x07 /**< bad song id */
#define DB_E_PARSE                   0x08 /**< could not parse result */
#define DB_E_BADPROVIDER             0x09 /**< requested db backend not there */
#define DB_E_PROC                    0x0A /**< could not start threadpool */
#define DB_E_SIZE                    0x0B /**< passed buffer too small */

/* describes the individual database handlers */
typedef struct tag_dbinfo {
    char *handler_name;
    char *description;
    int stores_playlists;
} DB_INFO;



#endif /* _DB_GENERIC_H_ */
