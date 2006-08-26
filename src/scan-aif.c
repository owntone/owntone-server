/*
 * $Id: $
 *
 * Copyright (C) 2006 Ron Pedde (ron@pedde.com)
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
#  include "config.h"
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <sys/types.h>

#include "err.h"
#include "mp3-scanner.h"

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ > 4)
# define _PACKED __attribute((packed))
#else
# define _PACKED
#endif

#pragma pack(1)
typedef struct tag_aif_chunk_header {
    char       id[4];
    uint32_t   len;
} _PACKED AIF_CHUNK_HEADER;

typedef struct tag_iff_header {
    char       id[4];
    uint32_t   length;
    char       type[4];
} _PACKED AIF_IFF_HEADER;

typedef struct tag_aif_comm {
    int16_t     channels;
    uint32_t    sample_frames;
    int16_t     sample_size;
    uint8_t     sample_rate[10];
} _PACKED AIF_COMM;
#pragma pack()

uint32_t aif_from_be32(uint32_t *native) {
    uint32_t result;
    uint8_t *data = (uint8_t *)native;

    result = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
    return result;
}

uint16_t aif_from_be16(uint16_t *native) {
    uint16_t result;
    uint8_t *data = (uint8_t *)native;

    result = data[0] << 8 | data[1];
    return result;
}


/**
 * parse a COMM block -- should be sitting at the bottom half of
 * a comm block
 *
 * @param infile file to read
 * @return TRUE on success, FALSE otherwise
 */
int scan_aif_parse_comm(FILE *infile, MP3FILE *pmp3) {
    AIF_COMM comm;
    int sec;
    int ms;

    if(fread((void*)&comm,sizeof(AIF_COMM),1,infile) != 1) {
        DPRINTF(E_WARN,L_SCAN,"Error reading aiff file -- bad COMM block\n");
        return FALSE;
    }

    /* we'll brute the sample rate */
    
    pmp3->samplerate = aif_from_be32((uint32_t*)&comm.sample_rate[2]) >> 16;
    if(!pmp3->samplerate)
        return TRUE;

    pmp3->bitrate = pmp3->samplerate * comm.channels * 
        ((comm.sample_size + 7)/8)*8;

    sec = pmp3->file_size / (pmp3->bitrate / 8);
    ms = ((pmp3->file_size % (pmp3->bitrate / 8)) * 1000) / (pmp3->bitrate/8);
    pmp3->song_length = (sec * 1000) + ms;

    pmp3->bitrate /= 1000;

    return TRUE;
}

/**
 * Get info from the actual aiff headers.  Since there is no
 * metainfo in .wav files (or at least know standard I
 * know about), this merely gets duration, bitrate, etc.
 *
 * @param filename file to scan
 * @param pmp3 MP3FILE struct to be filled
 * @returns TRUE if song should be added to database, FALSE otherwise
 */
int scan_get_aifinfo(char *filename, MP3FILE *pmp3) {
    FILE *infile;
    int done=0;
    AIF_CHUNK_HEADER chunk;
    AIF_IFF_HEADER iff_header;
    long current_pos = 0;

    DPRINTF(E_DBG,L_SCAN,"Getting AIFF file info\n");

    if(!(infile=fopen(filename,"rb"))) {
        DPRINTF(E_WARN,L_SCAN,"Could not open %s for reading\n",filename);
        return FALSE;
    }

    /* first, verify we have a valid iff header */
    if(fread((void*)&iff_header,sizeof(AIF_IFF_HEADER),1,infile) != 1) {
        DPRINTF(E_WARN,L_SCAN,"Error reading %s -- bad iff header\n",filename);
        fclose(infile);
        return FALSE;
    }

    if((strncmp(iff_header.id,"FORM",4) != 0) ||
       (strncmp(iff_header.type,"AIFF",4) != 0)) {
        DPRINTF(E_WARN,L_SCAN,"File %s is not an AIFF file\n",filename);
        fclose(infile);
        return FALSE;
    }

    /* loop around, processing chunks */
    while(!done) {
        if(fread((void*)&chunk,sizeof(AIF_CHUNK_HEADER),1,infile) != 1) {
            done=1;
            break;
        }
        
        /* fixup */
        chunk.len = aif_from_be32(&chunk.len);

        DPRINTF(E_DBG,L_SCAN,"Got chunk %c%c%c%c\n",chunk.id[0],
                chunk.id[1],chunk.id[2],chunk.id[3]);

        current_pos = ftell(infile);

        /* process the chunk */
        if(strncmp(chunk.id,"COMM",4)==0) {
            if(!scan_aif_parse_comm(infile,pmp3)) {
                DPRINTF(E_INF,L_SCAN,"Error reading COMM block: %s\n",filename);
                fclose(infile);
                return FALSE;
            }
        }

        fseek(infile,current_pos,SEEK_SET);
        fseek(infile,chunk.len,SEEK_CUR);
    }

    fclose(infile);
    return TRUE;
}

