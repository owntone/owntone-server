/*
 * $Id$
 * Implementation file for mp3 scanner and monitor
 *
 * Ironically, this now scans file types other than mp3 files,
 * but the name is the same for historical purposes, not to mention
 * the fact that sf.net makes it virtually impossible to manage a cvs
 * root reasonably.  Perhaps one day soon they will move to subversion.
 * 
 * /me crosses his fingers
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

#define _POSIX_PTHREAD_SEMANTICS
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <id3tag.h>
#include <limits.h>
#include <restart.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>  /* htons and friends */
#include <sys/stat.h>
#include <dirent.h>      /* why here?  For osx 10.2, of course! */

#include "daapd.h"
#include "db-memory.h"
#include "err.h"
#include "mp3-scanner.h"
#include "playlist.h"

#ifndef HAVE_STRCASESTR
# include "strcasestr.h"
#endif

/*
 * Typedefs
 */

typedef struct tag_scan_id3header {
    unsigned char id[3];
    unsigned char version[2];
    unsigned char flags;
    unsigned char size[4];
} __attribute((packed)) SCAN_ID3HEADER;

#define MAYBEFREE(a) { if((a)) free((a)); };


/*
 * Globals
 */
int scan_br_table[5][16] = {
    { 0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0 }, /* MPEG1, Layer 1 */
    { 0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0 },    /* MPEG1, Layer 2 */
    { 0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0 },     /* MPEG1, Layer 3 */
    { 0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0 },    /* MPEG2/2.5, Layer 1 */
    { 0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0 }          /* MPEG2/2.5, Layer 2/3 */
};

int scan_sample_table[3][4] = {
    { 44100, 48000, 32000, 0 },  /* MPEG 1 */
    { 22050, 24000, 16000, 0 },  /* MPEG 2 */
    { 11025, 12000, 8000, 0 }    /* MPEG 2.5 */
};



int scan_mode_foreground=1;

char *scan_winamp_genre[] = {
    "Blues",              // 0
    "Classic Rock",
    "Country",
    "Dance",
    "Disco",
    "Funk",               // 5
    "Grunge",
    "Hip-Hop",
    "Jazz",
    "Metal",
    "New Age",            // 10
    "Oldies",
    "Other",
    "Pop",
    "R&B",
    "Rap",                // 15
    "Reggae",
    "Rock",
    "Techno",
    "Industrial",
    "Alternative",        // 20
    "Ska",
    "Death Metal",
    "Pranks",
    "Soundtrack",
    "Euro-Techno",        // 25
    "Ambient",
    "Trip-Hop",
    "Vocal",
    "Jazz+Funk",
    "Fusion",             // 30
    "Trance",
    "Classical",
    "Instrumental",
    "Acid",
    "House",              // 35
    "Game",
    "Sound Clip",
    "Gospel",
    "Noise",
    "AlternRock",         // 40
    "Bass",
    "Soul",
    "Punk",
    "Space",
    "Meditative",         // 45
    "Instrumental Pop",
    "Instrumental Rock",
    "Ethnic",
    "Gothic",
    "Darkwave",           // 50
    "Techno-Industrial",
    "Electronic",
    "Pop-Folk",
    "Eurodance",
    "Dream",              // 55
    "Southern Rock",
    "Comedy",
    "Cult",
    "Gangsta",
    "Top 40",             // 60
    "Christian Rap",
    "Pop/Funk",
    "Jungle",
    "Native American",
    "Cabaret",            // 65
    "New Wave",
    "Psychadelic",
    "Rave",
    "Showtunes",
    "Trailer",            // 70
    "Lo-Fi",
    "Tribal",
    "Acid Punk",
    "Acid Jazz",
    "Polka",              // 75
    "Retro",
    "Musical",
    "Rock & Roll",
    "Hard Rock",
    "Folk",               // 80
    "Folk/Rock",
    "National folk",
    "Swing",
    "Fast-fusion",
    "Bebob",              // 85
    "Latin",
    "Revival",
    "Celtic",
    "Bluegrass",
    "Avantgarde",         // 90
    "Gothic Rock",
    "Progressive Rock",
    "Psychedelic Rock",
    "Symphonic Rock",
    "Slow Rock",          // 95
    "Big Band",
    "Chorus",
    "Easy Listening",
    "Acoustic",
    "Humour",             // 100
    "Speech",
    "Chanson",
    "Opera",
    "Chamber Music",
    "Sonata",             // 105 
    "Symphony",
    "Booty Bass",
    "Primus",
    "Porn Groove",
    "Satire",             // 110
    "Slow Jam",
    "Club",
    "Tango",
    "Samba",
    "Folklore",           // 115
    "Ballad",
    "Powder Ballad",
    "Rhythmic Soul",
    "Freestyle",
    "Duet",               // 120
    "Punk Rock",
    "Drum Solo",
    "A Capella",
    "Euro-House",
    "Dance Hall",         // 125
    "Goa",
    "Drum & Bass",
    "Club House",
    "Hardcore",
    "Terror",             // 130
    "Indie",
    "BritPop",
    "NegerPunk",
    "Polsk Punk",
    "Beat",               // 135
    "Christian Gangsta",
    "Heavy Metal",
    "Black Metal",
    "Crossover",
    "Contemporary C",     // 140
    "Christian Rock",
    "Merengue",
    "Salsa",
    "Thrash Metal",
    "Anime",              // 145
    "JPop",
    "SynthPop",
    "Unknown"
};

#define WINAMP_GENRE_UNKNOWN 148


/*
 * Forwards
 */
int scan_path(char *path);
int scan_gettags(char *file, MP3FILE *pmp3);
int scan_get_mp3tags(char *file, MP3FILE *pmp3);
int scan_get_aactags(char *file, MP3FILE *pmp3);
int scan_get_fileinfo(char *file, MP3FILE *pmp3);
int scan_get_mp3fileinfo(char *file, MP3FILE *pmp3);
int scan_get_aacfileinfo(char *file, MP3FILE *pmp3);

int scan_freetags(MP3FILE *pmp3);
void scan_static_playlist(char *path, struct dirent *pde, struct stat *psb);
void scan_music_file(char *path, struct dirent *pde, struct stat *psb);

void make_composite_tags(MP3FILE *pmp3);

typedef struct {
    char*	suffix;
    int		(*tags)(char* file, MP3FILE* pmp3);
    int		(*files)(char* file, MP3FILE* pmp3);
} taghandler;
static taghandler taghandlers[] = {
    { "aac", scan_get_aactags, scan_get_aacfileinfo },
    { "mp4", scan_get_aactags, scan_get_aacfileinfo },
    { "m4a", scan_get_aactags, scan_get_aacfileinfo },
    { "m4p", scan_get_aactags, scan_get_aacfileinfo },
    { "mp3", scan_get_mp3tags, scan_get_mp3fileinfo },
    { NULL, 0 }
};

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

    scan_mode_foreground=0;
    if(db_is_empty()) {
	scan_mode_foreground=1;
    }

    if(db_start_initial_update()) 
	return -1;

    DPRINTF(ERR_DEBUG,"%s scanning for MP3s in %s\n",
	    scan_mode_foreground ? "Foreground" : "Background",
	    path);

    err=scan_path(path);

    if(db_end_initial_update())
	return -1;

    scan_mode_foreground=0;

    return err;
}

/*
 * scan_path
 *
 * Do a brute force scan of a path, finding all the MP3 files there
 */
int scan_path(char *path) {
    DIR *current_dir;
    char de[sizeof(struct dirent) + MAXNAMLEN + 1]; /* overcommit for solaris */
    struct dirent *pde;
    int err;
    char mp3_path[PATH_MAX];
    struct stat sb;
    int modified_time;

    if((current_dir=opendir(path)) == NULL) {
	return -1;
    }

    while(1) {
	if(config.stop) {
	    DPRINTF(ERR_WARN,"Stop detected.  Aborting scan of %s.\n",path);
	    return 0;
	}

	pde=(struct dirent *)&de;

	err=readdir_r(current_dir,(struct dirent *)de,&pde);
	if(err == -1) {
	    DPRINTF(ERR_DEBUG,"Error on readdir_r: %s\n",strerror(errno));
	    err=errno;
	    closedir(current_dir);
	    errno=err;
	    return -1;
	}

	if(!pde)
	    break;
	
	if(pde->d_name[0] == '.') /* skip hidden and directories */
	    continue;

	snprintf(mp3_path,PATH_MAX,"%s/%s",path,pde->d_name);
	DPRINTF(ERR_DEBUG,"Found %s\n",mp3_path);
	if(stat(mp3_path,&sb)) {
	    DPRINTF(ERR_WARN,"Error statting: %s\n",strerror(errno));
	} else {
	    if(sb.st_mode & S_IFDIR) { /* dir -- recurse */
		DPRINTF(ERR_DEBUG,"Found dir %s... recursing\n",pde->d_name);
		scan_path(mp3_path);
	    } else {
		/* process the file */
		if(strlen(pde->d_name) > 4) {
		    if((strcasecmp(".m3u",(char*)&pde->d_name[strlen(pde->d_name) - 4]) == 0) &&
		       config.process_m3u){
			/* we found an m3u file */
			scan_static_playlist(path, pde, &sb);
		    } else if (strcasestr(config.extensions,
					  (char*)&pde->d_name[strlen(pde->d_name) - 4])) {
			
			/* only scan if it's been changed, or empty db */
			modified_time=sb.st_mtime;
			DPRINTF(ERR_DEBUG,"FS Modified time: %d\n",modified_time);
			DPRINTF(ERR_DEBUG,"DB Modified time: %d\n",db_last_modified(sb.st_ino));
			if((scan_mode_foreground) || 
			   !db_exists(sb.st_ino) ||
			   db_last_modified(sb.st_ino) < modified_time) {
			    scan_music_file(path,pde,&sb);
			} else {
			    DPRINTF(ERR_DEBUG,"Skipping file... not modified\n");
			}
		    }
		}
	    }
	}
    }

    closedir(current_dir);
    return 0;
}

/*
 * scan_static_playlist
 *
 * Scan a file as a static playlist
 */
void scan_static_playlist(char *path, struct dirent *pde, struct stat *psb) {
    char playlist_path[PATH_MAX];
    char m3u_path[PATH_MAX];
    char linebuffer[PATH_MAX];
    int fd;
    int playlistid;
    struct stat sb;

    DPRINTF(ERR_WARN,"Processing static playlist: %s\n",pde->d_name);
    strcpy(m3u_path,pde->d_name);
    snprintf(playlist_path,sizeof(playlist_path),"%s/%s",path,pde->d_name);
    m3u_path[strlen(pde->d_name) - 4] = '\0';
    playlistid=psb->st_ino;
    fd=open(playlist_path,O_RDONLY);
    if(fd != -1) {
	db_add_playlist(playlistid,m3u_path,0);
	DPRINTF(ERR_INFO,"Added playlist as id %d\n",playlistid);

	memset(linebuffer,0x00,sizeof(linebuffer));
	while(readline(fd,linebuffer,sizeof(linebuffer)) > 0) {
	    while((linebuffer[strlen(linebuffer)-1] == '\n') ||
		  (linebuffer[strlen(linebuffer)-1] == '\r'))   /* windows? */
		linebuffer[strlen(linebuffer)-1] = '\0';

	    if((linebuffer[0] == ';') || (linebuffer[0] == '#'))
		continue;

	    /* FIXME - should chomp trailing comments */

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

    DPRINTF(ERR_WARN,"Done processing playlist\n");
}

/*
 * scan_music_file
 *
 * scan a particular file as a music file
 */
void scan_music_file(char *path, struct dirent *pde, struct stat *psb) {
    MP3FILE mp3file;
    char mp3_path[PATH_MAX];

    snprintf(mp3_path,sizeof(mp3_path),"%s/%s",path,pde->d_name);

    /* we found an mp3 file */
    DPRINTF(ERR_INFO,"Found music file: %s\n",pde->d_name);
    
    memset((void*)&mp3file,0,sizeof(mp3file));
    mp3file.path=mp3_path;
    mp3file.fname=pde->d_name;
    if(strlen(pde->d_name) > 4)
	mp3file.type=strdup(strrchr(pde->d_name, '.') + 1);
    
    /* FIXME; assumes that st_ino is a u_int_32 
       DWB: also assumes that the library is contained entirely within
       one file system 
    */
    mp3file.id=psb->st_ino;
    
    /* Do the tag lookup here */
    if(!scan_gettags(mp3file.path,&mp3file) && 
       !scan_get_fileinfo(mp3file.path,&mp3file)) {
	make_composite_tags(&mp3file);
	/* fill in the time_added.  I'm not sure of the logic in this.
	   My thinking is to use time created, but what is that?  Best
	   guess would be earliest of st_mtime and st_ctime...
	*/
	mp3file.time_added=psb->st_mtime;
	if(psb->st_ctime < mp3file.time_added)
	    mp3file.time_added=psb->st_ctime;
	mp3file.time_modified=time(NULL);

	DPRINTF(ERR_DEBUG," Date Added: %d\n",mp3file.time_added);

	db_add(&mp3file);
	pl_eval(&mp3file); /* FIXME: move to db_add? */
    } else {
	DPRINTF(ERR_WARN,"Skipping %s - scan_gettags failed\n",pde->d_name);
    }
    
    scan_freetags(&mp3file);
}

/*
 * scan_aac_findatom
 *
 * Find an AAC atom
 */
long scan_aac_findatom(FILE *fin, long max_offset, char *which_atom, int *atom_size) {
    long current_offset=0;
    int size;
    char atom[4];

    while(current_offset < max_offset) {
	if(fread((void*)&size,1,sizeof(int),fin) != sizeof(int))
	    return -1;

	size=ntohl(size);

	if(size <= 7) /* something not right */
	    return -1;

	if(fread(atom,1,4,fin) != 4) 
	    return -1;

	if(strncasecmp(atom,which_atom,4) == 0) {
	    *atom_size=size;
	    return current_offset;
	}

	fseek(fin,size-8,SEEK_CUR);
	current_offset+=size;
    }

    return -1;
}

/*
 * scan_get_aactags
 *
 * Get tags from an AAC (m4a) file
 */
int scan_get_aactags(char *file, MP3FILE *pmp3) {
    FILE *fin;
    long atom_offset;
    int atom_length;

    long current_offset=0;
    int current_size;
    char current_atom[4];
    char *current_data;
    unsigned short us_data;
    int genre;
    int len;

    if(!(fin=fopen(file,"rb"))) {
	DPRINTF(ERR_INFO,"Cannot open file %s for reading\n",file);
	return -1;
    }

    fseek(fin,0,SEEK_SET);

    atom_offset = aac_drilltoatom(fin, "moov:udta:meta:ilst", &atom_length);
    if(atom_offset != -1) {
	/* found the tag section - need to walk through now */
      
	while(current_offset < atom_length) {
	    if(fread((void*)&current_size,1,sizeof(int),fin) != sizeof(int))
		break;
			
	    current_size=ntohl(current_size);
			
	    if(current_size <= 7) /* something not right */
		break;

	    if(fread(current_atom,1,4,fin) != 4) 
		break;
			
	    len=current_size-7;  /* for ill-formed too-short tags */
	    if(len < 22)
		len=22;

	    current_data=(char*)malloc(len);  /* extra byte */
	    memset(current_data,0x00,len);

	    if(fread(current_data,1,current_size-8,fin) != current_size-8) 
		break;

	    if(!memcmp(current_atom,"\xA9" "nam",4)) { /* Song name */
		pmp3->title=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "ART",4)) {
		pmp3->artist=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "alb",4)) {
		pmp3->album=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "cmt",4)) {
		pmp3->comment=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "wrt",4)) {
		pmp3->composer=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "grp",4)) {
		pmp3->grouping=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"\xA9" "gen",4)) {
		/* can this be a winamp genre??? */
		pmp3->genre=strdup((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"tmpo",4)) {
		us_data=*((unsigned short *)&current_data[16]);
		us_data=ntohs(us_data);
		pmp3->bpm=us_data;
	    } else if(!memcmp(current_atom,"trkn",4)) {
		us_data=*((unsigned short *)&current_data[18]);
		us_data=ntohs(us_data);

		pmp3->track=us_data;

		us_data=*((unsigned short *)&current_data[20]);
		us_data=ntohs(us_data);

		pmp3->total_tracks=us_data;
	    } else if(!memcmp(current_atom,"disk",4)) {
		us_data=*((unsigned short *)&current_data[18]);
		us_data=ntohs(us_data);

		pmp3->disc=us_data;

		us_data=*((unsigned short *)&current_data[20]);
		us_data=ntohs(us_data);

		pmp3->total_discs=us_data;
	    } else if(!memcmp(current_atom,"\xA9" "day",4)) {
		pmp3->year=atoi((char*)&current_data[16]);
	    } else if(!memcmp(current_atom,"gnre",4)) {
		genre=(int)(*((char*)&current_data[17]));
		genre--;
			    
		if((genre < 0) || (genre > WINAMP_GENRE_UNKNOWN))
		    genre=WINAMP_GENRE_UNKNOWN;

		pmp3->genre=strdup(scan_winamp_genre[genre]);
	    } else if (!memcmp(current_atom, "cpil", 4)) {
		pmp3->compilation = current_data[16];
	    }

	    free(current_data);
	    current_offset+=current_size;
	}
    }

    fclose(fin);
    return 0;  /* we'll return as much as we got. */
}


/*
 * scan_gettags
 *
 * Scan an mp3 file for id3 tags using libid3tag
 */
int scan_gettags(char *file, MP3FILE *pmp3) {
    taghandler *hdl;

    /* dispatch to appropriate tag handler */
    for(hdl = taghandlers ; hdl->suffix ; ++hdl)
	if(!strcasecmp(hdl->suffix, pmp3->type))
	    break;

    if(hdl->tags)
	return hdl->tags(file, pmp3);

    /* maybe this is an extension that we've manually
     * specified in the config file, but don't know how
     * to extract tags from.  Ogg, maybe.
     */

    return 0;
}


int scan_get_mp3tags(char *file, MP3FILE *pmp3) {
    struct id3_file *pid3file;
    struct id3_tag *pid3tag;
    struct id3_frame *pid3frame;
    int err;
    int index;
    int used;
    unsigned char *utf8_text;
    int genre=WINAMP_GENRE_UNKNOWN;
    int have_utf8;
    int have_text;
    id3_ucs4_t const *native_text;
    char *tmp;
    int got_numeric_genre;

    if(strcasecmp(pmp3->type,"mp3"))  /* can't get tags for non-mp3 */
	return 0;

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
	native_text=NULL;
	have_utf8=0;
	have_text=0;

	if(!strcmp(pid3frame->id,"YTCP")) { /* for id3v2.2 */
	    pmp3->compilation = 1;
	    DPRINTF(ERR_DEBUG, "Compilation: %d\n", pmp3->compilation);
	}

	if(((pid3frame->id[0] == 'T')||(strcmp(pid3frame->id,"COMM")==0)) &&
	   (id3_field_getnstrings(&pid3frame->fields[1]))) 
	    have_text=1;

	if(have_text) {
	    native_text=id3_field_getstrings(&pid3frame->fields[1],0);

	    if(native_text) {
		have_utf8=1;
		utf8_text=id3_ucs4_utf8duplicate(native_text);
		MEMNOTIFY(utf8_text);

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
		} else if(!strcmp(pid3frame->id,"TCOM")) {
		    used=1;
		    pmp3->composer = utf8_text;
		    DPRINTF(ERR_DEBUG," Composer: %s\n",utf8_text);
		} else if(!strcmp(pid3frame->id,"TIT1")) {
		    used=1;
		    pmp3->grouping = utf8_text;
		    DPRINTF(ERR_DEBUG," Grouping: %s\n",utf8_text);
		} else if(!strcmp(pid3frame->id,"TPE2")) {
		    used=1;
		    pmp3->orchestra = utf8_text;
		    DPRINTF(ERR_DEBUG," Orchestra: %s\n",utf8_text);
		} else if(!strcmp(pid3frame->id,"TPE3")) {
		    used=1;
		    pmp3->conductor = utf8_text;
		    DPRINTF(ERR_DEBUG," Conductor: %s\n",utf8_text);
		} else if(!strcmp(pid3frame->id,"TCON")) {
		    used=1;
		    pmp3->genre = utf8_text;
		    got_numeric_genre=0;
		    DPRINTF(ERR_DEBUG," Genre: %s\n",utf8_text);
		    if(pmp3->genre) {
			if(!strlen(pmp3->genre)) {
			    genre=WINAMP_GENRE_UNKNOWN;
			    got_numeric_genre=1;
			} else if (isdigit(pmp3->genre[0])) {
			    genre=atoi(pmp3->genre);
			    got_numeric_genre=1;
			} else if ((pmp3->genre[0] == '(') && (isdigit(pmp3->genre[1]))) {
			    genre=atoi((char*)&pmp3->genre[1]);
			    got_numeric_genre=1;
			} 

			if(got_numeric_genre) {
			    if((genre < 0) || (genre > WINAMP_GENRE_UNKNOWN))
				genre=WINAMP_GENRE_UNKNOWN;
			    free(pmp3->genre);
			    pmp3->genre=strdup(scan_winamp_genre[genre]);
			}
		    }
		} else if(!strcmp(pid3frame->id,"COMM")) {
		    used=1;
		    pmp3->comment = utf8_text;
		    DPRINTF(ERR_DEBUG," Comment: %s\n",pmp3->comment);
		} else if(!strcmp(pid3frame->id,"TPOS")) {
		    tmp=(char*)utf8_text;
		    strsep(&tmp,"/");
		    if(tmp) {
			pmp3->total_discs=atoi(tmp);
		    }
		    pmp3->disc=atoi((char*)utf8_text);
		    DPRINTF(ERR_DEBUG," Disc %d of %d\n",pmp3->disc,pmp3->total_discs);
		} else if(!strcmp(pid3frame->id,"TRCK")) {
		    tmp=(char*)utf8_text;
		    strsep(&tmp,"/");
		    if(tmp) {
			pmp3->total_tracks=atoi(tmp);
		    }
		    pmp3->track=atoi((char*)utf8_text);
		    DPRINTF(ERR_DEBUG," Track %d of %d\n",pmp3->track,pmp3->total_tracks);
		} else if(!strcmp(pid3frame->id,"TDRC")) {
		    pmp3->year = atoi(utf8_text);
		    DPRINTF(ERR_DEBUG," Year: %d\n",pmp3->year);
		} else if(!strcmp(pid3frame->id,"TLEN")) {
		    pmp3->song_length = atoi(utf8_text) / 1000;
		    DPRINTF(ERR_DEBUG, " Length: %d\n", pmp3->song_length);
		} else if(!strcmp(pid3frame->id,"TBPM")) {
		    pmp3->bpm = atoi(utf8_text);
		    DPRINTF(ERR_DEBUG, "BPM: %d\n", pmp3->bpm);
		} else if(!strcmp(pid3frame->id,"TCMP")) { /* for id3v2.3 */
                    pmp3->compilation = (char)atoi(utf8_text);
                    DPRINTF(ERR_DEBUG, "Compilation: %d\n", pmp3->compilation);
                }
	    }
	}

	/* can check for non-text tags here */
	if((!used) && (have_utf8) && (utf8_text))
	    free(utf8_text);

	index++;
    }

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
    MAYBEFREE(pmp3->title);
    MAYBEFREE(pmp3->artist);
    MAYBEFREE(pmp3->album);
    MAYBEFREE(pmp3->genre);
    MAYBEFREE(pmp3->comment);
    MAYBEFREE(pmp3->type);
    MAYBEFREE(pmp3->composer);
    MAYBEFREE(pmp3->orchestra);
    MAYBEFREE(pmp3->conductor);
    MAYBEFREE(pmp3->grouping);
    MAYBEFREE(pmp3->description);

    return 0;
}


/*
 * scan_get_fileinfo
 *
 * Dispatch to actual file info handlers
 */
int scan_get_fileinfo(char *file, MP3FILE *pmp3) {
    FILE *infile;
    off_t file_size;

    taghandler *hdl;

    /* dispatch to appropriate tag handler */
    for(hdl = taghandlers ; hdl->suffix ; ++hdl)
	if(!strcmp(hdl->suffix, pmp3->type))
	    break;

    if(hdl->files)
	return hdl->files(file, pmp3);

    /* a file we don't know anything about... ogg or aiff maybe */
    if(!(infile=fopen(file,"rb"))) {
	DPRINTF(ERR_WARN,"Could not open %s for reading\n",file);
	return -1;
    }

    /* we can at least get this */
    fseek(infile,0,SEEK_END);
    file_size=ftell(infile);
    fseek(infile,0,SEEK_SET);

    pmp3->file_size=file_size;

    fclose(infile);
    return 0;
}

/*
 * aac_drilltoatom
 *
 * Returns the offset of the atom specified by the given path or -1 if
 * not found. atom_path is a colon separated list of atoms specifying
 * a path from parent node to the target node. All paths must be specified
 * from the root.
 */
off_t aac_drilltoatom(FILE *aac_fp, char *atom_path, unsigned int *atom_length)
{
    long          atom_offset;
    off_t         file_size;
    char          *cur_p, *end_p;
    char          atom_name[5];

    fseek(aac_fp, 0, SEEK_END);
    file_size = ftell(aac_fp);
    rewind(aac_fp);

    end_p = atom_path;
    while (*end_p != '\0')
	{
	    end_p++;
	}
    atom_name[4] = '\0';
    cur_p = atom_path;

    while (cur_p != NULL)
	{
	    if ((end_p - cur_p) < 4)
		{
		    return -1;
		}
	    strncpy(atom_name, cur_p, 4);
	    atom_offset = scan_aac_findatom(aac_fp, file_size, atom_name, atom_length);
	    if (atom_offset == -1)
		{
		    return -1;
		}
	    DPRINTF(ERR_DEBUG, "Found %s atom at offset %ld.\n", atom_name, ftell(aac_fp) - 8);
	    cur_p = strchr(cur_p, ':');
	    if (cur_p != NULL)
		{
		    cur_p++;

		    /* PENDING: Hack to deal with atoms that have extra data in addition
		       to having child atoms. This should be dealt in a better fashion
		       than this (table with skip offsets or an actual real mp4 parser.) */
		    if (!strcmp(atom_name, "meta")) {
			fseek(aac_fp, 4, SEEK_CUR);
		    } else if (!strcmp(atom_name, "stsd")) {
			fseek(aac_fp, 8, SEEK_CUR);
		    } else if (!strcmp(atom_name, "mp4a")) {
			fseek(aac_fp, 28, SEEK_CUR);
		    }
		}
	}

    return ftell(aac_fp) - 8;
}

/*
 * scan_get_aacfileinfo
 *
 * Get info from the actual aac headers
 */
int scan_get_aacfileinfo(char *file, MP3FILE *pmp3) {
    FILE *infile;
    long atom_offset;
    int atom_length;
    int sample_size;
    int samples;
    unsigned int bit_rate;
    off_t file_size;
    int ms;
    unsigned char buffer[2];

    DPRINTF(ERR_DEBUG,"Getting AAC file info\n");

    if(!(infile=fopen(file,"rb"))) {
	DPRINTF(ERR_WARN,"Could not open %s for reading\n",file);
	return -1;
    }

    fseek(infile,0,SEEK_END);
    file_size=ftell(infile);
    fseek(infile,0,SEEK_SET);

    pmp3->file_size=file_size;

    /* now, hunt for the mvhd atom */
    atom_offset = aac_drilltoatom(infile, "moov:mvhd", &atom_length);
    if(atom_offset != -1) {
	fseek(infile,12,SEEK_CUR);
	fread((void*)&sample_size,1,sizeof(int),infile);
	fread((void*)&samples,1,sizeof(int),infile);

	sample_size=ntohl(sample_size);
	samples=ntohl(samples);

	/* avoid overflowing on large sample_sizes (90000) */
	ms=1000;
	while((ms > 9) && (!(sample_size % 10))) {
	    sample_size /= 10;
	    ms /= 10;
	}

	/* DWB: use ms time instead of sec */
	pmp3->song_length=(int)((samples * ms) / sample_size);

	DPRINTF(ERR_DEBUG,"Song length: %d seconds\n", pmp3->song_length / 1000);
    }

    pmp3->bitrate = 0;

    /* Get the sample rate from the 'mp4a' atom (timescale). This is also
       found in the 'mdhd' atom which is a bit closer but we need to 
       navigate to the 'mp4a' atom anyways to get to the 'esds' atom. */
    atom_offset = aac_drilltoatom(infile, "moov:trak:mdia:minf:stbl:stsd:mp4a", &atom_length);
    if (atom_offset != -1) {
	fseek(infile, atom_offset + 32, SEEK_SET);

	/* Timescale here seems to be 2 bytes here (the 2 bytes before it are
	   "reserved") though the timescale in the 'mdhd' atom is 4. Not sure how
	   this is dealt with when sample rate goes higher than 64K. */
	fread(buffer, sizeof(unsigned char), 2, infile);

	pmp3->samplerate = (buffer[0] << 8) | (buffer[1]);

	/* Seek to end of atom. */
	fseek(infile, 2, SEEK_CUR);

	/* Get the bit rate from the 'esds' atom. We are already positioned
	   in the parent atom so just scan ahead. */
	atom_offset = scan_aac_findatom(infile, atom_length - (ftell(infile) - atom_offset), "esds", &atom_length);

	if (atom_offset != -1) {
	    fseek(infile, atom_offset + 22, SEEK_CUR);

	    fread((void *)&bit_rate, sizeof(unsigned int), 1, infile);

	    pmp3->bitrate = ntohl(bit_rate) / 1000;
	} else {
	    DPRINTF(ERR_DEBUG, "Could not find 'esds' atom to determine bit rate.\n");
	}
      
    } else {
	DPRINTF(ERR_DEBUG, "Could not find 'mp4a' atom to determine sample rate.\n");
    }

    /* Fallback if we can't find the info in the atoms. */
    if (pmp3->bitrate == 0) {
	/* calculate bitrate from song length... Kinda cheesy */
	DPRINTF(ERR_DEBUG, "Could not find 'esds' atom. Calculating bit rate.\n");

	atom_offset=aac_drilltoatom(infile,"mdat",&atom_length);

	if (atom_offset != -1) {
	    pmp3->bitrate = atom_length / ((pmp3->song_length / 1000) * 128);
	}
    }

    fclose(infile);
    return 0;
}


/*
 * scan_get_mp3fileinfo
 *
 * Get information from the file headers itself -- like
 * song length, bit rate, etc.
 */
int scan_get_mp3fileinfo(char *file, MP3FILE *pmp3) {
    FILE *infile;
    SCAN_ID3HEADER *pid3;
    unsigned int size=0;
    off_t fp_size=0;
    off_t file_size;
    unsigned char buffer[1024];
    int index;
    int layer_index;
    int sample_index;

    int ver=0;
    int layer=0;
    int bitrate=0;
    int samplerate=0;
    int stereo=0;

    if(!(infile=fopen(file,"rb"))) {
	DPRINTF(ERR_WARN,"Could not open %s for reading\n",file);
	return -1;
    }

    fseek(infile,0,SEEK_END);
    file_size=ftell(infile);
    fseek(infile,0,SEEK_SET);

    pmp3->file_size=file_size;

    if(fread(buffer,1,sizeof(buffer),infile) != sizeof(buffer)) {
	if(ferror(infile)) {
	    DPRINTF(ERR_LOG,"Error reading: %s\n",strerror(errno));
	} else {
	    DPRINTF(ERR_LOG,"Short file: %s\n",file);
	}
	return -1;
    }

    pid3=(SCAN_ID3HEADER*)buffer;
    
    if(strncmp(pid3->id,"ID3",3)==0) {
	/* found an ID3 header... */
	DPRINTF(ERR_DEBUG,"Found ID3 header\n");
	size = (pid3->size[0] << 21 | pid3->size[1] << 14 | 
		pid3->size[2] << 7 | pid3->size[3]);
	fp_size=size + sizeof(SCAN_ID3HEADER);
	DPRINTF(ERR_DEBUG,"Header length: %d\n",size);
    }

    file_size -= fp_size;

    fseek(infile,fp_size,SEEK_SET);
    if(fread(buffer,1,sizeof(buffer),infile) < sizeof(buffer)) {
	DPRINTF(ERR_LOG,"Short file: %s\n",file);
	return -1;
    }

    index=0;
    while(((buffer[index] != 0xFF) || (buffer[index+1] < 224)) &&
	  (index < (sizeof(buffer)-10))) {
	index++;
    }

    if(index) {
	DPRINTF(ERR_DEBUG,"Scanned forward %d bytes to find frame header\n",index);
    }

    if((buffer[index] == 0xFF)&&(buffer[index+1] >= 224)) {
	ver=(buffer[index+1] & 0x18) >> 3;
	layer=(buffer[index+1] & 0x6) >> 1;

	layer_index=-1;
	sample_index=-1;

	switch(ver) {
	case 0: /* MPEG Version 2.5 */
	    sample_index=2;
	    if(layer == 3)
		layer_index=3;
	    else
		layer_index=4;
	    break;
	case 2: /* MPEG Version 2 */
	    sample_index=1;
	    if(layer == 3)
		layer_index=3;
	    else
		layer_index=4;
	    break;
	case 3: /* MPEG Version 1 */
	    sample_index=0;
	    if(layer == 3) /* layer 1 */
		layer_index=0;
	    if(layer == 2) /* layer 2 */
		layer_index=1;
	    if(layer == 1) /* layer 3 */
		layer_index=2;
	    break;
	}

	if((layer_index < 0) || (layer_index > 4)) {
	    DPRINTF(ERR_LOG,"Bad mp3 header in %s: bad layer_index\n",file);
	    return -1;
	}

	if((sample_index < 0) || (sample_index > 2)) {
	    DPRINTF(ERR_LOG,"Bad mp3 header in %s: bad sample_index\n",file);
	    return -1;
	}

	bitrate=(buffer[index+2] & 0xF0) >> 4;
	bitrate=scan_br_table[layer_index][bitrate];
	samplerate=(buffer[index+2] & 0x0C) >> 2;  /* can only be 0-3 */
	samplerate=scan_sample_table[sample_index][samplerate];
	pmp3->bitrate=bitrate;
	pmp3->samplerate=samplerate;
	stereo=buffer[index+3] & 0xC0 >> 6;
	if(stereo == 3)
	    stereo=0;
	else
	    stereo=1;
	DPRINTF(ERR_DEBUG," MPEG Version: %s\n",ver == 3 ? "1" : (ver == 2 ? "2" : "2.5"));
	DPRINTF(ERR_DEBUG," Layer: %d\n",4-layer);
	DPRINTF(ERR_DEBUG," Sample Rate: %d\n",samplerate);
	DPRINTF(ERR_DEBUG," Bit Rate: %d\n",bitrate);

	/* guesstimate the file length */
	if(!pmp3->song_length) /* could have gotten it from the tag */
	    {
		/* DWB: use ms time instead of seconds, use doubles to
		   avoid overflow */
		if(bitrate)
		    {
			pmp3->song_length = (int) ((double) file_size * 8000. /
						   (double) bitrate /
						   1024.);
		    }
	    }
    } else {
	/* FIXME: should really scan forward to next sync frame */
	fclose(infile);
	DPRINTF(ERR_DEBUG,"Could not find sync frame\n");
	return 0;
    }
    

    fclose(infile);
    return 0;
}

void make_composite_tags(MP3FILE *song)
{
    int len;

    len=0;

    if(!song->artist && (song->orchestra || song->conductor)) {
	if(song->orchestra)
	    len += strlen(song->orchestra);
	if(song->conductor)
	    len += strlen(song->conductor);

	len += 3;

	song->artist=(char*)calloc(len, 1);
	if(song->artist) {
	    if(song->orchestra)
		strcat(song->artist,song->orchestra);

	    if(song->orchestra && song->conductor)
		strcat(song->artist," - ");

	    if(song->conductor)
		strcat(song->artist,song->conductor);
	}
    }

    if(!strcasecmp(song->type,"ogg")) {
	song->description = strdup("QuickTime movie file");
    } else {
	char fdescr[50];

	sprintf(fdescr,"%s audio file",song->type);
	song->description = strdup(fdescr);
    }

    if(!song->title)
	song->title = strdup(song->fname);

    if(!strcmp(song->type, "ogg"))
	song->item_kind = 4;
    else
	song->item_kind = 2;
}
