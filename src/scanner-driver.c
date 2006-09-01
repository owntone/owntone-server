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
#include <string.h>
#include <unistd.h>

#include "conf.h"
#include "daapd.h"
#include "err.h"
#include "mp3-scanner.h"

/*
 * externs
 */
extern int scan_get_wmainfo(char *filename, MP3FILE *pmp3);
extern int scan_get_ogginfo(char *filename, MP3FILE *pmp3);
extern int scan_get_flacinfo(char *filename, MP3FILE *pmp3);
extern int scan_get_mpcinfo(char *filename, MP3FILE *pmp3);
extern int scan_get_wavinfo(char *filename, MP3FILE *pmp3);
extern int scan_get_aacinfo(char *filename, MP3FILE *pmp3);
extern int scan_get_mp3info(char *filename, MP3FILE *pmp3);
extern int scan_get_urlinfo(char *filename, MP3FILE *pmp3);
extern int scan_get_aifinfo(char *filename, MP3FILE *pmp3);

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
    { "mpc",scan_get_mpcinfo },
    { "mpp",scan_get_mpcinfo },
    { "mp+",scan_get_mpcinfo },
    { "ogg",scan_get_ogginfo },
    { "m4a",scan_get_aacinfo },
    { "m4p",scan_get_aacinfo },
    { "mp4",scan_get_aacinfo },
    { "wav",scan_get_wavinfo },
    { "url",scan_get_urlinfo },
    { "mp3",scan_get_mp3info },
    { "aif",scan_get_aifinfo },
    { "aiff",scan_get_aifinfo },
    { NULL, NULL }
};
char *av0;

CONFIG config;


/**
 * dump a mp3 file
 *
 * \param pmp3 mp3 file to dump
 */
void dump_mp3(MP3FILE *pmp3) {
    int min,sec,msec;

    min=(pmp3->song_length/1000) / 60;
    sec = (pmp3->song_length/1000) - (60 * min);
    msec = pmp3->song_length - ((pmp3->song_length/1000)*1000);

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
    printf("url...........:  %s\n",pmp3->url);
    printf("description...:  %s\n",pmp3->description);
    printf("codectype.....:  %s\n",pmp3->codectype);
    printf("year..........:  %d\n",pmp3->year);

    printf("bitrate.......:  %dkb\n",pmp3->bitrate);
    printf("samplerate....:  %d\n",pmp3->samplerate);
    printf("length........:  %dms (%d:%02d.%03d)\n",pmp3->song_length,min,sec,msec);
    printf("size..........:  %d\n",pmp3->file_size);

    printf("track.........:  %d of %d\n",pmp3->track,pmp3->total_tracks);
    printf("disc..........:  %d of %d\n",pmp3->disc,pmp3->total_discs);

    printf("compilation...:  %d\n",pmp3->compilation);

    printf("rating........:  %d\n",pmp3->rating);
    printf("disabled......:  %d\n",pmp3->disabled);
    printf("bpm...........:  %d\n",pmp3->bpm);
    printf("has_video.....:  %d\n",pmp3->has_video);
}


/*
 * dump suage
 */

void usage(int errorcode) {
    fprintf(stderr,"Usage: %s [options] input-file\n\n",av0);
    fprintf(stderr,"options:\n\n");
    fprintf(stderr,"  -d level    set debuglevel (9 is highest)\n");
    fprintf(stderr,"  -c config   read config file\n");
    
    fprintf(stderr,"\n\n");
    exit(errorcode);
}

int main(int argc, char *argv[]) {
    MP3FILE mp3;
    SCANNERLIST *plist;
    int option;
    char *ext;
    char *configfile = "mt-daapd.conf";
    int debuglevel=1;
    FILE *fin;

    memset((void*)&mp3,0x00,sizeof(MP3FILE));

    if(strchr(argv[0],'/')) {
        av0 = strrchr(argv[0],'/')+1;
    } else {
        av0 = argv[0];
    }
    
    while((option = getopt(argc, argv, "d:c:")) != -1) {
        switch(option) {
        case 'd':
            debuglevel = atoi(optarg);
            break;
        case 'c':
            configfile=optarg;
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

    printf("Reading config file %s\n",configfile);
    if(conf_read(configfile) != CONF_E_SUCCESS) {
        fprintf(stderr,"Bummer.\n");
        exit(EXIT_FAILURE);
    }

    err_setdest(LOGDEST_STDERR);
    err_setlevel(debuglevel);
    printf("Getting info for %s\n",argv[0]);

    fin=fopen(argv[0],"r");
    if(!fin) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fseek(fin,0,SEEK_END);
    mp3.file_size = ftell(fin);
    fclose(fin);

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
