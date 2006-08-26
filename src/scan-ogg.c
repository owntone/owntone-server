/*
 * $Id$
 * Ogg parsing routines.
 *
 *
 * Copyright 2002 Michael Smith <msmith@xiph.org>
 * Licensed under the GNU GPL, distributed with this program.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <vorbis/vorbisfile.h>
#include "err.h"
#include "mp3-scanner.h"


/**
 * get ogg metainfo
 *
 * @param filename file to read metainfo for
 * @param pmp3 MP3FILE struct to fill with metainfo
 * @returns TRUE if file should be added to DB, FALSE otherwise
 */
int scan_get_ogginfo(char *filename, MP3FILE *pmp3) {
    FILE *f;
    OggVorbis_File vf;
    vorbis_comment *comment = NULL;
    vorbis_info *vi = NULL;
    char *val;

    f = fopen(filename, "rb");
    if (f == NULL) {
        DPRINTF(E_LOG, L_SCAN,
                "Error opening input file \"%s\": %s\n", filename,
                strerror(errno));
        return FALSE;
    }

    if (ov_open(f, &vf, NULL, 0) != 0) {
        fclose(f);
        DPRINTF(E_LOG, L_SCAN,
                "Error opening Vorbis stream in \"%s\"\n", filename);
        return FALSE;
    }

    vi=ov_info(&vf,-1);
    if(vi) {
        DPRINTF(E_DBG,L_SCAN," Bitrates: %d %d %d\n",vi->bitrate_upper,
                vi->bitrate_nominal,vi->bitrate_lower);
        if(vi->bitrate_nominal) {
            pmp3->bitrate=vi->bitrate_nominal / 1000;
        } else if(vi->bitrate_upper) {
            pmp3->bitrate=vi->bitrate_upper / 1000;
        } else if(vi->bitrate_lower) {
            pmp3->bitrate=vi->bitrate_lower / 1000;
        }

        DPRINTF(E_DBG,L_SCAN," Bitrates: %d",pmp3->bitrate);
        pmp3->samplerate=vi->rate;
    }

    pmp3->song_length=(int)ov_time_total(&vf,-1) * 1000;

    comment = ov_comment(&vf, -1);
    if (comment != NULL) {
        if ((val = vorbis_comment_query(comment, "artist", 0)) != NULL)
            pmp3->artist = strdup(val);
        if ((val = vorbis_comment_query(comment, "title", 0)) != NULL)
            pmp3->title = strdup(val);
        if ((val = vorbis_comment_query(comment, "album", 0)) != NULL)
            pmp3->album = strdup(val);
        if ((val = vorbis_comment_query(comment, "genre", 0)) != NULL)
            pmp3->genre = strdup(val);
        if ((val = vorbis_comment_query(comment, "composer", 0)) != NULL)
            pmp3->composer = strdup(val);
        if ((val = vorbis_comment_query(comment, "comment", 0)) != NULL)
            pmp3->comment = strdup(val);
        if ((val = vorbis_comment_query(comment, "tracknumber", 0)) != NULL)
            pmp3->track = atoi(val);
        if ((val = vorbis_comment_query(comment, "discnumber", 0)) != NULL)
            pmp3->disc = atoi(val);
        if ((val = vorbis_comment_query(comment, "year", 0)) != NULL)
            pmp3->year = atoi(val);
    }
    ov_clear(&vf);
    /*fclose(f);*/
    return TRUE;
}
