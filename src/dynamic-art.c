/*
 * $Id$
 * Dynamically merge .jpeg data into an mp3 tag
 *
 * Copyright (C) 2004 Hiren Joshi (hirenj@mooh.org)
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


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "err.h"
#include "playlist.h"
#include "configfile.h"

int *da_get_current_tag_info(int file_fd);

/* For some reason, we need to lose 2 bytes from this image size
   This size is everything after the APIC text in this frame.
*/
/* APIC tag is made up from
   APIC		(4 bytes)
   xxxx		(4 bytes - Image size: raw + 16)
   \0\0\0		(3 bytes)
   image/jpeg	(10 bytes)
   \0\0\0		(3 bytes)
   Image-data
*/
#define id3v3_image_size(x) x + 14


/* This size is everything after the PIC in this frame
 */
/* PIC tag is made up of
   PIC			(3 bytes)
   xxx			(3 bytes - Image size: raw + 6)
   \0			(1 byte)
   JPG			(3 bytes)
   \0\0		(2 bytes)
   Image-data
*/

#define id3v2_image_size(x) x + 6


/* For some reason we need to fudge values - Why do we need 17 bytes here? */
#define id3v3_tag_size(x) id3v3_image_size(x) + 8
#define id3v2_tag_size(x) id3v2_image_size(x) + 6

/*
 * get_image_fd
 *
 * Get a file descriptor for a piece of cover art. 
 */
int da_get_image_fd(char *filename) {
    unsigned char buffer[255];
    char *path_end;
    int fd;
    strncpy(buffer,filename,255);
    path_end = strrchr(buffer,'/');
    strcpy(path_end+1,config.artfilename);
    fd = open(buffer,O_RDONLY);
    if(fd) 
	DPRINTF(ERR_INFO,"Found image file %s\n",buffer);

    return fd;
}

/*
 * get_current_tag_info
 *
 * Get the current tag from the file. We need to do this to determine whether we 
 * are dealing with an id3v2.2 or id3v2.3 tag so we can decide which artwork to 
 * attach to the song
 */
int *da_get_current_tag_info(int file_fd) {
    unsigned char buffer[10];
    int *tag_info;
	
    tag_info = (int *) calloc(2,sizeof(int));
	
    r_read(file_fd,buffer,10);
    if ( strncmp(buffer,"ID3", 3) == 0 ) {
	tag_info[0] = buffer[3];
	tag_info[1] = ( buffer[6] << 21 ) + ( buffer[7] << 14 ) + ( buffer[8] << 7 ) + buffer[9];
	return tag_info;
    } else {
	/* By default, we attach an id3v2.3 tag */
	lseek(file_fd,0,SEEK_SET);
	tag_info[0] = 2;
	tag_info[1] = 0;
	return tag_info;
    }
}

/*
 * attach_image
 *
 * Given an image, output and input mp3 file descriptor, attach the artwork found
 * at the image file descriptor to the mp3 and stream the new id3 header to the client
 * via the output file descriptor. If there is not id3 tag present, it will attach a tag
 * to contain the artwork.
 */
int da_attach_image(int img_fd, int out_fd, int mp3_fd, int offset)
{
    long img_size;
    int tag_size;
    int *tag_info;
    unsigned char buffer[4];
    struct stat sb;

    fstat(img_fd,&sb);
    img_size=sb.st_size;
    DPRINTF(ERR_INFO,"Image appears to be %ld bytes\n",img_size);

    if (offset > (img_size + 24) ) {
	lseek(mp3_fd,(offset - img_size - 24),SEEK_SET);
	r_close(img_fd);
	return 0;
    }

    tag_info = da_get_current_tag_info(mp3_fd);
    tag_size = tag_info[1];

    DPRINTF(ERR_INFO,"Current tag size is %d bytes\n",tag_size);

    if (tag_info[0] == 3) {
	r_write(out_fd,"ID3\x03\0\0",6);
	tag_size += id3v3_tag_size(img_size);
    } else {
	r_write(out_fd,"ID3\x02\0\0",6);
	tag_size += id3v2_tag_size(img_size);
    }

    buffer[3] = tag_size & 0x7F;
    buffer[2] = ( tag_size & 0x3F80 ) >> 7;
    buffer[1] = ( tag_size & 0x1FC000 ) >> 14;
    buffer[0] = ( tag_size & 0xFE00000 ) >> 21;
	
    r_write(out_fd,buffer,4);
	
    if (tag_info[0] == 3) {
	r_write(out_fd,"APIC\0",5);
	img_size = id3v3_image_size(img_size);
    } else {
	r_write(out_fd,"PIC",3);
	img_size = id3v2_image_size(img_size);
    }
    buffer[0] = ( img_size & 0xFF0000 ) >> 16;
    buffer[1] = ( img_size & 0xFF00 ) >> 8;
    buffer[2] = ( img_size & 0xFF );
    r_write(out_fd,buffer,3);
    if (tag_info[0] == 3) {
	r_write(out_fd,"\0\0\0image/jpeg\0\0\0",16);
    } else {
	r_write(out_fd,"\0JPG\x00\0",6);
    }
    lseek(img_fd,0,SEEK_SET);
    copyfile(img_fd,out_fd);
    DPRINTF(ERR_INFO,"Done copying IMG %ld\n",img_size);
    r_close(img_fd);
    free(tag_info);
    return 0;
}


