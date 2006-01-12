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
    int index;
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

    int bitrate;
    int samplerate;
    int song_length;
    int file_size;
    int year;        /* TDRC */
    
    int track;       /* TRCK */
    int total_tracks;

    int disc;        /* TPOS */
    int total_discs;

    int time_added;
    int time_modified;
    int time_played;
    int play_count;
    int rating;
    int db_timestamp;
    int disabled;
    int bpm;         /* TBPM */

    int got_id3;
    unsigned int id;

    char *description;	/* long file type */
    char *codectype;          /* song.codectype */
    int item_kind;		/* song or movie */
    int data_kind;              /* dmap.datakind (asdk) */
    int force_update;
    int sample_count;
    char compilation;
    
    /* iTunes 5+ */
    int contentrating;
    
    /* iTunes 6.0.2 */
    int has_video;
} MP3FILE;

typedef struct tag_m3ufile {
    int id;              /**< integer id (miid) */
    char *title;       /**< playlist name as displayed in iTunes (minm) */
    int type;            /**< 0=static webmanaged, 1=smart, 2=static m3u (aeSP/MPTY) */
    int items;           /**< number of items (mimc) */
    char *query;       /**< where clause if type 1 (MSPS) */
    int db_timestamp;    /**< time last updated */
    char *path;        /**< path of underlying playlist (if type 2) */
    int index;           /**< index of playlist for paths with multiple playlists */
} M3UFILE;

#define PL_STATICWEB  0
#define PL_SMART      1
#define PL_STATICFILE 2
#define PL_STATICXML  3

#define WINAMP_GENRE_UNKNOWN 148

extern char *scan_winamp_genre[];
extern int scan_init(char *path);
extern void make_composite_tags(MP3FILE *song);

#ifndef TRUE
# define TRUE  1
# define FALSE 0
#endif


#endif /* _MP3_SCANNER_H_ */
