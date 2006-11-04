/*
 * $Id$
 * Header file for mp3 scanner and monitor
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

#ifndef _MP3_SCANNER_H_
#define _MP3_SCANNER_H_

#include <sys/types.h>

typedef struct tag_mp3file {
    char *path;
    uint32_t index;
    char *fname;
    char *title;     /* TIT2 */
    char *artist;    /* TPE1 */
    char *album;     /* TALB */
    char *genre;     /* TCON */
    char *comment;   /* COMM */
    char *type;
    char *composer;  /* TCOM */
    char *orchestra; /* TPE2 */
    char *conductor; /* TPE3 */
    char *grouping;  /* TIT1 */
    char *url;       /* daap.songdataurl (asul) */

    uint32_t bitrate;
    uint32_t samplerate;
    uint32_t song_length;
    uint32_t file_size; /* ?? */
    uint32_t year;        /* TDRC */

    uint32_t track;       /* TRCK */
    uint32_t total_tracks;

    uint32_t disc;        /* TPOS */
    uint32_t total_discs;

    uint32_t time_added; /* really should be a time_t */
    uint32_t time_modified;
    uint32_t time_played;

    uint32_t play_count;
    uint32_t rating;
    uint32_t db_timestamp;
 
    uint32_t disabled;
    uint32_t bpm;         /* TBPM */

    uint32_t got_id3;
    uint32_t id;

    char *description;  /* long file type */
    char *codectype;          /* song.codectype */

    uint32_t item_kind;              /* song or movie */
    uint32_t data_kind;              /* dmap.datakind (asdk) */
    uint32_t force_update;
    uint64_t sample_count;
    char compilation;

    /* iTunes 5+ */
    uint32_t contentrating;

    /* iTunes 6.0.2 */
    uint32_t has_video;
    uint32_t bits_per_sample;

    char *album_artist;
} MP3FILE;

typedef struct tag_m3ufile {
    uint32_t id;              /**< integer id (miid) */
    char *title;       /**< playlist name as displayed in iTunes (minm) */
    uint32_t type;            /**< 0=static webmanaged, 1=smart, 2=static m3u (aeSP/MPTY) */
    uint32_t items;           /**< number of items (mimc) */
    char *query;       /**< where clause if type 1 (MSPS) */
    uint32_t db_timestamp;    /**< time last updated */
    char *path;        /**< path of underlying playlist (if type 2) */
    uint32_t index;           /**< index of playlist for paths with multiple playlists */
} M3UFILE;

typedef struct tag_packed_m3ufile {
    char *id;
    char *title;
    char *type;
    char *items;
    char *query;
    char *db_timestamp;
    char *path;
    char *index;
} PACKED_M3UFILE;

typedef struct tag_packed_mp3file {
    char *id;
    char *path;
    char *fname;
    char *title;
    char *artist;
    char *album;
    char *genre;
    char *comment;
    char *type;
    char *composer;
    char *orchestra;
    char *conductor;
    char *grouping;
    char *url;
    char *bitrate;
    char *samplerate;
    char *song_length;
    char *file_size;
    char *year;
    char *track;
    char *total_tracks;
    char *disc;
    char *total_discs;
    char *bpm;
    char *compilation;
    char *rating;
    char *play_count;
    char *data_kind;
    char *item_kind;
    char *description;
    char *time_added;
    char *time_modified;
    char *time_played;
    char *db_timestamp;
    char *disabled;
    char *sample_count;
    char *force_update;
    char *codectype;
    char *idx;
    char *has_video;
    char *contentrating;
    char *bits_per_sample;
} PACKED_MP3FILE;

#define PL_STATICWEB  0
#define PL_SMART      1
#define PL_STATICFILE 2
#define PL_STATICXML  3

#define SCAN_NOT_COMPDIR  0
#define SCAN_IS_COMPDIR   1
#define SCAN_TEST_COMPDIR 2

#define WINAMP_GENRE_UNKNOWN 148

extern void scan_filename(char *path, int compdir, char *extensions);

extern char *scan_winamp_genre[];
extern int scan_init(char **patharray);
extern void make_composite_tags(MP3FILE *song);

#ifndef TRUE
# define TRUE  1
# define FALSE 0
#endif


#endif /* _MP3_SCANNER_H_ */
