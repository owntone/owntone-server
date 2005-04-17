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

#ifndef _DBS_SQLITE_H_
#define _DBS_SQLITE_H_

extern int db_sqlite_open(char *parameters);
extern int db_sqlite_init(int reload);
extern int db_sqlite_deinit(void);
extern int db_sqlite_add(MP3FILE *pmp3);
extern int db_sqlite_enum_start(DBQUERYINFO *pinfo);
extern int db_sqlite_enum_size(DBQUERYINFO *pinfo, int *count);
extern int db_sqlite_enum_fetch(DBQUERYINFO *pinfo, unsigned char **pdmap);
extern int db_sqlite_enum_reset(DBQUERYINFO *pinfo);
extern int db_sqlite_enum_end(void);
extern int db_sqlite_start_scan(void);
extern int db_sqlite_end_scan(void);
extern int db_sqlite_get_count(CountType_t type);
extern MP3FILE *db_sqlite_fetch_item(int id);
extern MP3FILE *db_sqlite_fetch_path(char *path);
extern void db_sqlite_dispose_item(MP3FILE *pmp3);
extern int db_sqlite_add_playlist(char *name, int type, char *clause, char *path, int *playlistid);
extern int db_sqlite_add_playlist_item(int playlistid, int songid);


typedef enum {
    songID,
    songPath,
    songFname,
    songTitle,
    songArtist,
    songAlbum,
    songGenre,
    songComment,
    songType,
    songComposer,
    songOrchestra,
    songGrouping,
    songURL,
    songBitrate,
    songSampleRate,
    songLength,
    songFilesize,
    songYear,
    songTrack,
    songTotalTracks,
    songDisc,
    songTotalDiscs,
    songBPM,
    songCompilation,
    songRating,
    songPlayCount,
    songDataKind,
    songItemKind,
    songDescription,
    songTimeAdded,
    songTimeModified,
    songTimePlayed,
    songDBTimestamp,
    songDisabled,
    songSampleCount,
    songForceUpdate,
    songCodecType
} SongField_t;

typedef enum {
    plID,
    plTitle,
    plType,
    plItems,
    plQuery,
    plDBTimestamp,
    plPath
} PlaylistField_t;


#endif /* _DBS_SQLITE_H_ */
