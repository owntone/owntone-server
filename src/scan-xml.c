/*
 * $Id$
 * Implementation file iTunes metainfo scanning
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
#  include "config.h"
#endif

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db-generic.h"
#include "err.h"
#include "ezxml.h"
#include "mp3-scanner.h"

/* Forwards */
int scan_xml_playlist(char *filename);


/* Globals */
static char *scan_xml_itunes_version = NULL;
static char *scan_xml_itunes_base_path = NULL;
static char *scan_xml_real_base_path = NULL;

#define MAYBECOPY(a) if(!mp3.a) mp3.a = pmp3->a

/**
 * urldecode a string, returning a string pointer which must
 * be freed by the calling function or NULL on error (ENOMEM)
 *
 * \param string string to convert
 * \param space as plus whether to convert '+' chars to spaces (no, for iTunes)
 */
char *scan_xml_urldecode(char *string, int space_as_plus) {
    char *pnew;
    char *src,*dst;
    int val=0;

    pnew=(char*)malloc(strlen(string)+1);
    if(!pnew)
	return NULL;

    src=string;
    dst=pnew;

    while(*src) {
	switch(*src) {
	case '+':
	    if(space_as_plus) {
		*dst++=' ';
	    } else {
		*dst++=*src;
	    }
	    src++;
	    break;
	case '%':
	    /* this is hideous */
	    src++;
	    if(*src) {
		if((*src <= '9') && (*src >='0'))
		    val=(*src - '0');
		else if((tolower(*src) <= 'f')&&(tolower(*src) >= 'a'))
		    val=10+(tolower(*src) - 'a');
		src++;
	    }
	    if(*src) {
		val *= 16;
		if((*src <= '9') && (*src >='0'))
		    val+=(*src - '0');
		else if((tolower(*src) <= 'f')&&(tolower(*src) >= 'a'))
		    val+=(10+(tolower(*src) - 'a'));
		src++;
	    }
	    *dst++=val;
	    break;
	default:
	    *dst++=*src++;
	    break;
	}
    }

    *dst='\0';
    return pnew;
}

/**
 * give an ezxml_t node for the Tracks dictionary, walk though
 * and update metainfo
 *
 * \param tracksdict ezxml node for the tracks dictionary
 */
int scan_xml_process_tracks(ezxml_t tracksdict) {
    char *base_path;
    char *song_path=NULL;
    char physical_path[PATH_MAX];
    char real_path[PATH_MAX];
    ezxml_t track,songdict,songkey,value;
    MP3FILE mp3;
    MP3FILE *pmp3;

    /* urldecode the base_path */
    base_path=scan_xml_urldecode(scan_xml_itunes_base_path,0);

    for(track=ezxml_child(tracksdict,"key"); track; track=track->next) {
	memset((void*)&mp3,0x00,sizeof(mp3));
	if(song_path)
	    free(song_path);
	song_path = NULL;

	songdict=track->ordered;
	DPRINTF(E_DBG,L_SCAN,"Found track %s\n",track->txt);

	for(songkey=ezxml_child(songdict,"key"); songkey; songkey=songkey->next) {
	    /* walking through the song elements, hanging the data on the mp3 file */
	    value=songkey->ordered;
	    if(!strcasecmp(songkey->txt,"Name")) {
		mp3.title = value->txt;
	    } else if(!strcasecmp(songkey->txt,"Artist")) {
		mp3.artist = value->txt;
	    } else if(!strcasecmp(songkey->txt,"Album")) {
		mp3.album = value->txt;
	    } else if(!strcasecmp(songkey->txt,"Genre")) {
		mp3.genre = value->txt;
	    } else if(!strcasecmp(songkey->txt,"Total Time")) {
		mp3.song_length = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Track Number")) {
		mp3.track = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Track Count")) {
		mp3.total_tracks = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Year")) {
		mp3.year = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Bit Rate")) {
		mp3.bitrate = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Sample Rate")) {
		mp3.samplerate = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Play Count")) {
		mp3.play_count = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Rating")) {
		mp3.rating = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Disabled")) {
		mp3.disabled=1;
	    } else if(!strcasecmp(songkey->txt,"Disc Number")) {
		mp3.disc = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Disc Count")) {
		mp3.total_discs = atoi(value->txt);
	    } else if(!strcasecmp(songkey->txt,"Compilation")) {
		mp3.compilation=1;
	    } else if(!strcasecmp(songkey->txt,"Location")) {
		song_path = scan_xml_urldecode(value->txt,0);
	    }
	}
	
	DPRINTF(E_DBG,L_SCAN,"Found track at path %s\n",song_path);

	/* song is parsed, now see if we can find a corresponding song */
	if(song_path && (strlen(song_path) > strlen(base_path))) {
	    sprintf(physical_path,"%siTunes Music/%s",scan_xml_real_base_path,
		    (char*)&song_path[strlen(base_path)]);
	    realpath(physical_path,real_path);

	    DPRINTF(E_DBG,L_SCAN,"Real Path: %s\n", real_path);

	    /* now we have the real path -- is it in the database? */
	    pmp3=db_fetch_path(real_path);
	    if(pmp3) {
		/* yup... let's update it with the iTunes info */
		mp3.path = pmp3->path;
		mp3.fname = pmp3->fname;

		MAYBECOPY(title);
		MAYBECOPY(artist);
		MAYBECOPY(album);
		MAYBECOPY(genre);
		MAYBECOPY(comment);
		MAYBECOPY(type);
		MAYBECOPY(composer);
		MAYBECOPY(orchestra);
		MAYBECOPY(conductor);
		MAYBECOPY(grouping);
		MAYBECOPY(url);
		MAYBECOPY(bitrate);
		MAYBECOPY(samplerate);
		MAYBECOPY(song_length);
		MAYBECOPY(file_size);
		MAYBECOPY(year);
		MAYBECOPY(track);
		MAYBECOPY(total_tracks);
		MAYBECOPY(disc);
		MAYBECOPY(total_discs);
		MAYBECOPY(time_added);
		MAYBECOPY(time_modified);
		MAYBECOPY(time_played);
		MAYBECOPY(play_count);
		MAYBECOPY(rating);
		MAYBECOPY(db_timestamp);
		MAYBECOPY(disabled);
		MAYBECOPY(bpm);
		MAYBECOPY(id);
		MAYBECOPY(description);
		MAYBECOPY(codectype);
		MAYBECOPY(item_kind);
		MAYBECOPY(data_kind);
		MAYBECOPY(force_update);
		MAYBECOPY(sample_count);
		MAYBECOPY(compilation);

		db_add(&mp3);
		db_dispose_item(pmp3);
	    }
	}
    }

    free(base_path);
    if(song_path)
	free(song_path);
    return 0;
}

/**
 * scan an iTunes xml music database file, augmenting
 * the metainfo with that found in the xml file
 */
int scan_xml_playlist(char *filename) {
    ezxml_t itpl,maindict,key,value;
    char *working_base;

    if(scan_xml_itunes_version) {
	free(scan_xml_itunes_version);
	scan_xml_itunes_version = NULL;
    }
  
    if(scan_xml_itunes_base_path) {
	free(scan_xml_itunes_base_path);
	scan_xml_itunes_base_path = NULL;
    }

    if(scan_xml_real_base_path) {
	free(scan_xml_real_base_path);
	scan_xml_real_base_path = NULL;
    }

    /* find the base dir of the itunes playlist itself */
    working_base = strdup(filename);
    if(strrchr(working_base,'/')) {
	*(strrchr(working_base,'/') + 1) = '\x0';
	scan_xml_real_base_path = strdup(working_base);
    } else {
	scan_xml_real_base_path = strdup("/");
    }
    free(working_base);

    DPRINTF(E_SPAM,L_SCAN,"Parsing xml file: %s\n",filename);

    itpl = ezxml_parse_file(filename);

    if(itpl == NULL) {
	return -1;
    }

    DPRINTF(E_SPAM,L_SCAN,"File parsed... processing\n");
    maindict = ezxml_child(itpl,"dict");

    /* we are parsing a dict entry -- this will be a seriese of key/value pairs */
    for(key = ezxml_child(maindict,"key"); key; key=key->next) {
	DPRINTF(E_SPAM,L_SCAN,"Found key %s\n",key->txt);
	value = key->ordered;
	if(!value) {  /* badly formed xml file */
	    ezxml_free(itpl);
	    return -1;
	}

	if(!scan_xml_itunes_version && (strcasecmp(key->txt,"Application Version") == 0)) {
	    scan_xml_itunes_version=strdup(value->txt);
	    DPRINTF(E_DBG,L_SCAN,"iTunes Version %s\n",scan_xml_itunes_version);
	} else if (!scan_xml_itunes_base_path && (strcasecmp(key->txt,"Music Folder") == 0)) {
	    scan_xml_itunes_base_path=strdup(value->txt);
	    DPRINTF(E_DBG,L_SCAN,"iTunes base path: %s\n",scan_xml_itunes_base_path);
	} else if (strcasecmp(key->txt,"Tracks") == 0) {
	    scan_xml_process_tracks(value);
	} else if (strcasecmp(key->txt,"Playlists") == 0) {
	    DPRINTF(E_DBG,L_SCAN,"Skipping iTunes playlists.... for now\n");
	}
    }

    ezxml_free(itpl);
    return 0;
}

