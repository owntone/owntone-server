/*
 * $Id$
 * Implementation file for mp3 scanner and monitor
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <id3tag.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "db-memory.h"
#include "err.h"
#include "mp3-scanner.h"
#include "playlist.h"

/*
 * Typedefs
 */

typedef struct tag_scan_id3header {
    unsigned char id[3];
    unsigned char version[2];
    unsigned char flags;
    unsigned char size[4];
} SCAN_ID3HEADER;

#define MAYBEFREE(a) { if((a)) free((a)); };


/*
 * Globals
 */
int scan_br_table[] = {
    0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0
};

char *scan_winamp_genre[] = {
	"Blues",
	"Classic Rock",
	"Country",
	"Dance",
	"Disco",
	"Funk",
	"Grunge",
	"Hip-Hop",
	"Jazz",
	"Metal",
	"New Age",
	"Oldies",
	"Other",
	"Pop",
	"R&B",
	"Rap",
	"Reggae",
	"Rock",
	"Techno",
	"Industrial",
	"Alternative",
	"Ska",
	"Death Metal",
	"Pranks",
	"Soundtrack",
	"Euro-Techno",
	"Ambient",
	"Trip-Hop",
	"Vocal",
	"Jazz+Funk",
	"Fusion",
	"Trance",
	"Classical",
	"Instrumental",
	"Acid",
	"House",
	"Game",
	"Sound Clip",
	"Gospel",
	"Noise",
	"AlternRock",
	"Bass",
	"Soul",
	"Punk",
	"Space",
	"Meditative",
	"Instrumental Pop",
	"Instrumental Rock",
	"Ethnic",
	"Gothic",
	"Darkwave",
	"Techno-Industrial",
	"Electronic",
	"Pop-Folk",
	"Eurodance",
	"Dream",
	"Southern Rock",
	"Comedy",
	"Cult",
	"Gangsta",
	"Top 40",
	"Christian Rap",
	"Pop/Funk",
	"Jungle",
	"Native American",
	"Cabaret",
	"New Wave",
	"Psychadelic",
	"Rave",
	"Showtunes",
	"Trailer",
	"Lo-Fi",
	"Tribal",
	"Acid Punk",
	"Acid Jazz",
	"Polka",
	"Retro",
	"Musical",
	"Rock & Roll",
	"Hard Rock",
	"Folk",
	"Folk/Rock",
	"National folk",
	"Swing",
	"Fast-fusion",
	"Bebob",
	"Latin",
	"Revival",
	"Celtic",
	"Bluegrass",
	"Avantgarde",
	"Gothic Rock",
	"Progressive Rock",
	"Psychedelic Rock",
	"Symphonic Rock",
	"Slow Rock",
	"Big Band",
	"Chorus",
	"Easy Listening",
	"Acoustic",
	"Humour",
	"Speech",
	"Chanson",
	"Opera",
	"Chamber Music",
	"Sonata",
	"Symphony",
	"Booty Bass",
	"Primus",
	"Porn Groove",
	"Satire",
	"Slow Jam",
	"Club",
	"Tango",
	"Samba",
	"Folklore",
	"Ballad",
	"Powder Ballad",
	"Rhythmic Soul",
	"Freestyle",
	"Duet",
	"Punk Rock",
	"Drum Solo",
	"A Capella",
	"Euro-House",
	"Dance Hall",
	"Goa",
	"Drum & Bass",
	"Club House",
	"Hardcore",
	"Terror",
	"Indie",
	"BritPop",
	"NegerPunk",
	"Polsk Punk",
	"Beat",
	"Christian Gangsta",
	"Heavy Metal",
	"Black Metal",
	"Crossover",
	"Contemporary C",
	"Christian Rock",
	"Merengue",
	"Salsa",
	"Thrash Metal",
	"Anime",
	"JPop",
	"SynthPop",
	"Unknown"
};


/*
 * Forwards
 */
int scan_foreground(char *path);
int scan_gettags(char *file, MP3FILE *pmp3);
int scan_getfileinfo(char *file, MP3FILE *pmp3);
int scan_freetags(MP3FILE *pmp3);

/*
 * scan_init
 *
 * This assumes the database is already initialized.
 *
 * Ideally, this would check to see if the database is empty.
 * If it is, it sets the database into bulk-import mode, and scans
 * the MP3 directory.
 *
 * If not empty, it would start a background monitor thread
 * and update files on a file-by-file basis
 */

int scan_init(char *path) {
    int err;
    if(db_is_empty()) {
	if(db_start_initial_update()) 
	    return -1;


	DPRINTF(ERR_DEBUG,"Scanning for MP3s in %s\n",path);

	err=scan_foreground(path);

	if(db_end_initial_update())
	    return -1;
    } else {
	/* do deferred updating */
	return ENOTSUP;
    }

    return err;
}

/*
 * scan_foreground
 *
 * Do a brute force scan of a path, finding all the MP3 files there
 */
int scan_foreground(char *path) {
    MP3FILE mp3file;
    DIR *current_dir;
    struct dirent de;
    struct dirent *pde;
    int err;
    char mp3_path[PATH_MAX];
    char m3u_path[PATH_MAX];
    char linebuffer[PATH_MAX];
    int fd;
    int playlistid;
    struct stat sb;

    if((current_dir=opendir(path)) == NULL) {
	return -1;
    }

    while(1) {
	pde=&de;
	err=readdir_r(current_dir,&de,&pde);
	if(err == -1) {
	    DPRINTF(ERR_DEBUG,"Error on readdir_r: %s\n",strerror(errno));
	    err=errno;
	    closedir(current_dir);
	    errno=err;
	    return -1;
	}

	if(!pde)
	    break;
	
	if(de.d_name[0] == '.')
	    continue;

	sprintf(mp3_path,"%s/%s",path,de.d_name);
	DPRINTF(ERR_DEBUG,"Found %s\n",mp3_path);
	if(stat(mp3_path,&sb)) {
	    DPRINTF(ERR_WARN,"Error statting: %s\n",strerror(errno));
	}

	if(sb.st_mode & S_IFDIR) { /* dir -- recurse */
	    DPRINTF(ERR_DEBUG,"Found dir %s... recursing\n",de.d_name);
	    scan_foreground(mp3_path);
	} else {
	    DPRINTF(ERR_DEBUG,"Processing file\n");
	    /* process the file */
	    if(strlen(de.d_name) > 4) {
		if(strcasecmp(".m3u",(char*)&de.d_name[strlen(de.d_name) - 4]) == 0) {
		    /* we found an m3u file */

		    DPRINTF(ERR_DEBUG,"Found m3u: %s\n",de.d_name);
		    strcpy(m3u_path,de.d_name);
		    m3u_path[strlen(de.d_name) - 4] = '\0';
		    playlistid=sb.st_ino;
		    fd=open(mp3_path,O_RDONLY);
		    if(fd != -1) {
			db_add_playlist(playlistid,m3u_path);

			while(readline(fd,linebuffer,sizeof(linebuffer)) > 0) {
			    while(linebuffer[strlen(linebuffer)-1] == '\n')
				linebuffer[strlen(linebuffer)-1] = '\0';

			    if((linebuffer[0] == ';') || (linebuffer[0] == '#'))
				continue;

			    /* otherwise, assume it is a path */
			    if(linebuffer[0] == '/') {
				strcpy(m3u_path,linebuffer);
			    } else {
				snprintf(m3u_path,sizeof(m3u_path),"%s/%s",path,linebuffer);
			    }

			    DPRINTF(ERR_DEBUG,"Checking %s\n",m3u_path);

			    /* might be valid, might not... */
			    if(!stat(m3u_path,&sb)) {
				/* FIXME: check to see if valid inode! */
				db_add_playlist_song(playlistid,sb.st_ino);
			    } else {
				DPRINTF(ERR_WARN,"Playlist entry %s bad: %s\n",
					m3u_path,strerror(errno));
			    }
			}
			close(fd);
		    }
		} else if(strcasecmp(".mp3",(char*)&de.d_name[strlen(de.d_name) - 4]) == 0) {
		    /* we found an mp3 file */
		    DPRINTF(ERR_DEBUG,"Found mp3: %s\n",de.d_name);

		    memset((void*)&mp3file,0,sizeof(mp3file));
		    mp3file.path=mp3_path;
		    mp3file.fname=de.d_name;

#ifdef MAC /* wtf is this about? */
		    mp3file.mtime=sb.st_mtimespec.tv_sec;
		    mp3file.atime=sb.st_atimespec.tv_sec;
		    mp3file.ctime=sb.st_ctimespec.tv_sec;
#else
		    mp3file.mtime=sb.st_mtime;
		    mp3file.atime=sb.st_atime;
		    mp3file.ctime=sb.st_ctime;
#endif

		    /* FIXME; assumes that st_ino is a u_int_32 */
		    mp3file.id=sb.st_ino;
		    
		    /* Do the tag lookup here */
		    if(!scan_gettags(mp3file.path,&mp3file) && 
		       !scan_getfileinfo(mp3file.path,&mp3file)) {
			db_add(&mp3file);
			pl_eval(&mp3file);
		    } else {
			DPRINTF(ERR_INFO,"Skipping %s\n",de.d_name);
		    }
		    
		    scan_freetags(&mp3file);
		}
	    }
	}
    }

    closedir(current_dir);
}

/*
 * scan_gettags
 *
 * Scan an mp3 file for id3 tags using libid3tag
 */
int scan_gettags(char *file, MP3FILE *pmp3) {
    struct id3_file *pid3file;
    struct id3_tag *pid3tag;
    struct id3_frame *pid3frame;
    int err;
    int index;
    int used;
    unsigned char *utf8_text;
    int genre;

    pid3file=id3_file_open(file,ID3_FILE_MODE_READONLY);
    if(!pid3file) {
	DPRINTF(ERR_WARN,"Cannot open %s\n",file);
	return -1;
    }

    pid3tag=id3_file_tag(pid3file);
    
    if(!pid3tag) {
	err=errno;
	id3_file_close(pid3file);
	errno=err;
	DPRINTF(ERR_WARN,"Cannot get ID3 tag for %s\n",file);
	return -1;
    }

    index=0;
    while((pid3frame=id3_tag_findframe(pid3tag,"",index))) {
	used=0;
	utf8_text=NULL;

	if((pid3frame->id[0] == 'T')&&(id3_field_getnstrings(&pid3frame->fields[1]))) {
	    utf8_text=id3_ucs4_utf8duplicate(id3_field_getstrings(&pid3frame->fields[1],0));
	    MEMNOTIFY(utf8_text);
	}

	if(!strcmp(pid3frame->id,"TIT2")) { /* Title */
	    used=1;
	    pmp3->title = utf8_text;
	    DPRINTF(ERR_DEBUG," Title: %s\n",utf8_text);
	} else if(!strcmp(pid3frame->id,"TPE1")) {
	    used=1;
	    pmp3->artist = utf8_text;
	    DPRINTF(ERR_DEBUG," Artist: %s\n",utf8_text);
	} else if(!strcmp(pid3frame->id,"TALB")) {
	    used=1;
	    pmp3->album = utf8_text;
	    DPRINTF(ERR_DEBUG," Album: %s\n",utf8_text);
	} else if(!strcmp(pid3frame->id,"TCON")) {
	    used=1;
	    pmp3->genre = utf8_text;
	    DPRINTF(ERR_DEBUG," Genre: %s\n",utf8_text);
	    if((pmp3->genre) && (isdigit(pmp3->genre[0]))) {
		genre=atoi(pmp3->genre);
		free(pmp3->genre);
		pmp3->genre=strdup(scan_winamp_genre[genre]);
	    }
	} else if(!strcmp(pid3frame->id,"COMM")) {
	    used=1;
	    pmp3->comment = utf8_text;
	    DPRINTF(ERR_DEBUG," Comment: %s\n",utf8_text);
	} else if(!strcmp(pid3frame->id,"TDRC")) {
	    pmp3->year = atoi(utf8_text);
	    DPRINTF(ERR_DEBUG," Year: %d\n",pmp3->year);
	}

	if((!used) && (pid3frame->id[0]=='T') && (utf8_text))
	    free(utf8_text);

	index++;
    }

    pmp3->got_id3=1;
    id3_file_close(pid3file);
    DPRINTF(ERR_DEBUG,"Got id3 tag successfully\n");
    return 0;
}

/*
 * scan_freetags
 *
 * Free up the tags that were dynamically allocated
 */
int scan_freetags(MP3FILE *pmp3) {
    if(!pmp3->got_id3) 
	return 0;

    MAYBEFREE(pmp3->title);
    MAYBEFREE(pmp3->artist);
    MAYBEFREE(pmp3->album);
    MAYBEFREE(pmp3->genre);
    MAYBEFREE(pmp3->comment);

    return 0;
}

/*
 * scan_getfileinfo
 *
 * Get information from the file headers itself -- like
 * song length, bit rate, etc.
 */
int scan_getfileinfo(char *file, MP3FILE *pmp3) {
    FILE *infile;
    SCAN_ID3HEADER *pid3;
    unsigned int size=0;
    off_t fp_size=0;
    off_t file_size;
    unsigned char buffer[256];
    int time_seconds;

    int ver=0;
    int layer=0;
    int bitrate=0;
    int samplerate=0;

    if(!(infile=fopen(file,"rb"))) {
	DPRINTF(ERR_WARN,"Could not open %s for reading\n",file);
	return -1;
    }
    
    fread(buffer,1,sizeof(buffer),infile);
    pid3=(SCAN_ID3HEADER*)buffer;
    
    if(strncmp(pid3->id,"ID3",3)==0) {
	/* found an ID3 header... */
	DPRINTF(ERR_DEBUG,"Found ID3 header\n");
	size = (pid3->size[0] << 21 | pid3->size[1] << 14 | 
		pid3->size[2] << 7 | pid3->size[3]);
	fp_size=size + sizeof(SCAN_ID3HEADER);
	DPRINTF(ERR_DEBUG,"Header length: %d\n",size);
    }

    fseek(infile,0,SEEK_END);
    file_size=ftell(infile);
    file_size -= fp_size;

    fseek(infile,fp_size,SEEK_SET);
    fread(buffer,1,sizeof(buffer),infile);

    if((buffer[0] == 0xFF)&&(buffer[1] >= 224)) {
	ver=(buffer[1] & 0x18) >> 3;
	layer=(buffer[1] & 0x6) >> 1;
	if((ver==3) && (layer==1)) { /* MPEG1, Layer 3 */
	    bitrate=(buffer[2] & 0xF0) >> 4;
	    bitrate=scan_br_table[bitrate];
	    samplerate=(buffer[2] & 0x0C) >> 2;
	    switch(samplerate) {
	    case 0:
		samplerate=44100;
		break;
	    case 1:
		samplerate=48000;
		break;
	    case 2:
		samplerate=32000;
		break;
	    }
	    pmp3->bitrate=bitrate;
	    pmp3->samplerate=samplerate;
	} else {
	    /* not an mp3... */
	    DPRINTF(ERR_DEBUG,"File is not a MPEG-1/Layer III\n");
	    return -1;
	}

	/* guesstimate the file length */
	time_seconds = ((int)(file_size * 8)) / (bitrate * 1024);
	pmp3->song_length=time_seconds;
	pmp3->file_size=file_size;
    } else {
	/* should really scan forward to next sync frame */
	fclose(infile);
	DPRINTF(ERR_DEBUG,"Could not find sync frame\n");
	return -1;
    }
    

    fclose(infile);
    return 0;
}
