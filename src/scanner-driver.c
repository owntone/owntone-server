/*
 * $Id$
 * Simple driver to test tag scanners without the overhead
 * of all of mt-daapd
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "err.h"
#include "mp3-scanner.h"

/*
 * externs
 */
extern int scan_get_wmainfo(char *filename, MP3FILE *pmp3);
extern int scan_get_ogginfo(char *filename, MP3FILE *pmp3);
extern int scan_get_flacinfo(char *filename, MP3FILE *pmp3);


/* 
 * Typedefs
 */
typedef struct scannerlist_tag {
    char *ext;
    int (*scanner)(char *file, MP3FILE *pmp3);
} SCANNERLIST;

/*
 * Globals
 */
SCANNERLIST scanner_list[] = {
    { "wma",scan_get_wmainfo },
    { "flac",scan_get_flacinfo },
    { "fla",scan_get_flacinfo },
    { "ogg",scan_get_ogginfo },
    { NULL, NULL }
};
char *av0;


/**
 * dump a mp3 file
 *
 * \param pmp3 mp3 file to dump
 */
void dump_mp3(MP3FILE *pmp3) {
    int min,sec;

    min=(pmp3->song_length/1000) / 60;
    sec = (pmp3->song_length/1000) - (60 * min);

    printf("path..........:  %s\n",pmp3->path);
    printf("fname.........:  %s\n",pmp3->fname);
    printf("title.........:  %s\n",pmp3->title);
    printf("artist........:  %s\n",pmp3->artist);
    printf("album.........:  %s\n",pmp3->album);
    printf("genre.........:  %s\n",pmp3->genre);
    printf("comment.......:  %s\n",pmp3->comment);
    printf("type..........:  %s\n",pmp3->type);
    printf("composter.....:  %s\n",pmp3->composer);
    printf("orchestra.....:  %s\n",pmp3->orchestra);
    printf("conductor.....:  %s\n",pmp3->conductor);
    printf("grouping......:  %s\n",pmp3->grouping);
    printf("year..........:  %d\n",pmp3->year);
    printf("url...........:  %s\n",pmp3->url);

    printf("bitrate.......:  %dkb\n",pmp3->bitrate);
    printf("samplerate....:  %d\n",pmp3->samplerate);
    printf("length........:  %dms (%d:%02d)\n",pmp3->song_length,min,sec);
    printf("size..........:  %d\n",pmp3->file_size);

    printf("track.........:  %d of %d\n",pmp3->track,pmp3->total_tracks);
    printf("disc..........:  %d of %d\n",pmp3->disc,pmp3->total_discs);

    printf("compilation...:  %d\n",pmp3->compilation);
}


/*
 * dump suage
 */

void usage(int errorcode) {
    fprintf(stderr,"Usage: %s [options] input-file\n\n",av0);
    fprintf(stderr,"options:\n\n");
    fprintf(stderr,"  -d level    set debuglevel (9 is highest)\n");
    
    fprintf(stderr,"\n\n");
    exit(errorcode);
}

int main(int argc, char *argv[]) {
    MP3FILE mp3;
    SCANNERLIST *plist;
    int option;
    char *ext;
    
    memset((void*)&mp3,0x00,sizeof(MP3FILE));

    if(strchr(argv[0],'/')) {
	av0 = strrchr(argv[0],'/')+1;
    } else {
	av0 = argv[0];
    }
    
    while((option = getopt(argc, argv, "d:")) != -1) {
	switch(option) {
	case 'd':
	    err_debuglevel = atoi(optarg);
	    break;
	default:
	    fprintf(stderr,"Error: unknown option (%c)\n\n",option);
	    usage(-1);
	}
    }
    
    argc -= optind;
    argv += optind;

    if(argc == 0) {
	fprintf(stderr,"Error: Must specifiy file name\n\n");
	usage(-1);
    }

    printf("Getting info for %s\n",argv[0]);

    ext = strrchr(argv[0],'.')+1;
    plist=scanner_list;

    while(plist->ext && (strcasecmp(plist->ext,ext) != 0)) {
	plist++;
    }

    if(plist->ext) {
	fprintf(stderr,"dispatching as single-file metatag parser\n");
	plist->scanner(argv[0],&mp3);
	dump_mp3(&mp3);
    } else {
	fprintf(stderr,"unknown file extension: %s\n",ext);
	exit(-1);
    }
}
