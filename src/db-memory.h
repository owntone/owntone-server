/*
 * $Id$
 * Header info for in-memory db
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

#ifndef _DB_MEMORY_H_
#define _DB_MEMORY_H_

#include "mp3-scanner.h"

typedef void* ENUMHANDLE;

extern int db_start_initial_update(void);
extern int db_end_initial_update(void);
extern int db_is_empty(void);
extern int db_init(char *parameters);
extern int db_deinit(void);
extern int db_version(void);
extern int db_add(MP3FILE *mp3file);
extern int db_add_playlist(unsigned int playlistid, char *name);
extern int db_add_playlist_song(unsigned int playlistid, unsigned int itemid);

extern ENUMHANDLE db_enum_begin(void);
extern MP3FILE *db_enum(ENUMHANDLE *current);
extern int db_enum_end(void);
extern MP3FILE *db_find(int id);

extern int db_get_song_count(void);
extern int db_get_playlist_count(void);
extern int db_get_playlist_entry_count(int playlistid);

extern ENUMHANDLE db_playlist_enum_begin(void);
extern int db_playlist_enum(ENUMHANDLE *current);
extern int db_playlist_enum_end(void);

extern ENUMHANDLE db_playlist_items_enum_begin(int playlistid);
extern int db_playlist_items_enum(ENUMHANDLE *current);
extern int db_playlist_items_enum_end(void);

extern char *db_get_playlist_name(int playlistid);

#endif /* _DB_MEMORY_H_ */
