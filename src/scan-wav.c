/*
 * $Id$
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "err.h"
#include "mp3-scanner.h"

#define GET_WAV_INT32(p) ((((unsigned long)((p)[3])) << 24) |   \
                          (((unsigned long)((p)[2])) << 16) |   \
                          (((unsigned long)((p)[1])) << 8) |    \
                          (((unsigned long)((p)[0]))))

#define GET_WAV_INT16(p) ((((unsigned long)((p)[1])) << 8) |    \
                          (((unsigned long)((p)[0]))))

/**
 * Get info from the actual wav headers.  Since there is no
 * metainfo in .wav files (or at least know standard I
 * know about), this merely gets duration, bitrate, etc.
 *
 * @param filename file to scan
 * @param pmp3 MP3FILE struct to be filled
 * @returns TRUE if song should be added to database, FALSE otherwise
 */
int scan_get_wavinfo(char *filename, MP3FILE *pmp3) {
    FILE *infile;
    size_t rl;
    unsigned char hdr[44];
    unsigned long chunk_data_length;
    unsigned long format_data_length;
    unsigned long compression_code;
    unsigned long channel_count;
    unsigned long sample_rate;
    unsigned long sample_bit_length;
    unsigned long bit_rate;
    unsigned long data_length;
    unsigned long sec, ms;

    DPRINTF(E_DBG,L_SCAN,"Getting WAV file info\n");

    if(!(infile=fopen(filename,"rb"))) {
        DPRINTF(E_WARN,L_SCAN,"Could not open %s for reading\n",filename);
        return FALSE;
    }

    rl = fread(hdr, 1, 44, infile);
    fclose(infile);
    if (rl != 44) {
        DPRINTF(E_WARN,L_SCAN,"Could not read wav header from %s\n",filename);
        return FALSE;
    }

    if (strncmp((char*)hdr + 0, "RIFF", 4) ||
        strncmp((char*)hdr + 8, "WAVE", 4) ||
        strncmp((char*)hdr + 12, "fmt ", 4) ||
        strncmp((char*)hdr + 36, "data", 4)) {
        DPRINTF(E_WARN,L_SCAN,"Invalid wav header in %s\n",filename);
        return FALSE;
    }

    chunk_data_length = GET_WAV_INT32(hdr + 4);
    format_data_length = GET_WAV_INT32(hdr + 16);
    compression_code = GET_WAV_INT16(hdr + 20);
    channel_count = GET_WAV_INT16(hdr + 22);
    sample_rate = GET_WAV_INT32(hdr + 24);
    sample_bit_length = GET_WAV_INT16(hdr + 34);
    data_length = GET_WAV_INT32(hdr + 40);

    if ((format_data_length != 16) ||
        (compression_code != 1) ||
        (channel_count < 1)) {
        DPRINTF(E_WARN,L_SCAN,"Invalid wav header in %s\n",filename);
        return FALSE;
    }

    bit_rate = sample_rate * channel_count * ((sample_bit_length + 7) / 8) * 8;
    pmp3->bitrate = bit_rate / 1000;
    pmp3->samplerate = sample_rate;
    sec = data_length / (bit_rate / 8);
    ms = ((data_length % (bit_rate / 8)) * 1000) / (bit_rate / 8);
    pmp3->song_length = (sec * 1000) + ms;

    return TRUE;
}

