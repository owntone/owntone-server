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

typedef struct tag_mp3file {
    char *path;
    char *fname;
    char *title;
    char *artist;
    char *album;
    char *genre;
    char *comment;
    char *type;

    int bitrate;
    int samplerate;
    int song_length;
    int file_size;
    int year;
    
    int track;
    int total_tracks;

    int disc;
    int total_discs;

    int mtime;
    int atime;
    int ctime;

    int got_id3;
    unsigned int id;
} MP3FILE;

extern int scan_init(char *path);

#endif /* _MP3_SCANNER_H_ */
