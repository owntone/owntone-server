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
#include "mp3-scanner.h"
#include "rxml.h"

/* Forwards */
int scan_xml_playlist(char *filename);
void scan_xml_handler(int action,void* puser,char* info);
int scan_xml_preamble_section(int action,char *info);
int scan_xml_tracks_section(int action,char *info);
int scan_xml_playlists_section(int action,char *info);

/* Globals */
static char *scan_xml_itunes_version = NULL;
static char *scan_xml_itunes_base_path = NULL;
static char *scan_xml_itunes_decoded_base_path = NULL;
static char *scan_xml_real_base_path = NULL;

#define MAYBECOPY(a) if(!mp3.a) mp3.a = pmp3->a
#define MAYBEFREE(a) if((a)) free((a))

static char *scan_xml_track_tags[] = {
    "Name",
    "Artist",
    "Album",
    "Genre",
    "Total Time",
    "Track Number",
    "Track Count",
    "Year",
    "Bit Rate",
    "Sample Rate",
    "Play Count",
    "Rating",
    "Disabled",
    "Disc Number",
    "Disc Count",
    "Compilation",
    "Location",
    NULL
};

#define SCAN_XML_T_UNKNOWN     -1
#define SCAN_XML_T_NAME         0
#define SCAN_XML_T_ARTIST       1
#define SCAN_XML_T_ALBUM        2
#define SCAN_XML_T_GENRE        3
#define SCAN_XML_T_TOTALTIME    4
#define SCAN_XML_T_TRACKNUMBER  5
#define SCAN_XML_T_TRACKCOUNT   6
#define SCAN_XML_T_YEAR         7
#define SCAN_XML_T_BITRATE      8
#define SCAN_XML_T_SAMPLERATE   9
#define SCAN_XML_T_PLAYCOUNT   10
#define SCAN_XML_T_RATING      11
#define SCAN_XML_T_DISABLED    12
#define SCAN_XML_T_DISCNO      13
#define SCAN_XML_T_DISCCOUNT   14
#define SCAN_XML_T_COMPILATION 15
#define SCAN_XML_T_LOCATION    16

/**
 * get the tag index of a particular tag
 *
 * @param tag tag to determine tag index for
 */
int scan_xml_get_tagindex(char *tag) {
    char **ptag = scan_xml_track_tags;
    int index=0;

    while(*ptag && (strcasecmp(tag,*ptag) != 0)) {
	ptag++;
	index++;
    }

    if(*ptag) 
	return index;

    return SCAN_XML_T_UNKNOWN;
}

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
 * scan an iTunes xml music database file, augmenting
 * the metainfo with that found in the xml file
 */
int scan_xml_playlist(char *filename) {
    char *working_base;
    RXMLHANDLE xml_handle;

    MAYBEFREE(scan_xml_itunes_version);
    MAYBEFREE(scan_xml_itunes_base_path);
    MAYBEFREE(scan_xml_itunes_decoded_base_path);
    MAYBEFREE(scan_xml_real_base_path);


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

    if(!rxml_open(&xml_handle,filename,scan_xml_handler,NULL)) {
	DPRINTF(E_LOG,L_SCAN,"Error opening xml file %s: %s\n",
		filename,rxml_errorstring(xml_handle));
    } else {
	if(!rxml_parse(xml_handle)) {
	    DPRINTF(E_LOG,L_SCAN,"Error parsing xml file %s: %s\n",
		    filename,rxml_errorstring(xml_handle));
	}
    }

    rxml_close(xml_handle);
    return 0;
}


#define XML_STATE_PREAMBLE  0
#define XML_STATE_TRACKS    1
#define XML_STATE_PLAYLISTS 2
#define XML_STATE_ERROR     3


/**
 * handle new xml events, and dispatch it to the
 * appropriate handler.  This is a callback from the
 * xml parser.
 *
 * @param action what event (RXML_EVT_OPEN, etc)
 * @param puser opaqe data object passed in open (unused)
 * @param info char data associated with the event
 */
void scan_xml_handler(int action,void* puser,char* info) {
    static int state;

    switch(action) {
    case RXML_EVT_OPEN: /* file opened */
	state = XML_STATE_PREAMBLE;
	/* send this event to all dispatches to allow them
	 * to reset
	 */
	scan_xml_preamble_section(action,info);
	scan_xml_tracks_section(action,info);
	scan_xml_playlists_section(action,info);
	break;
    case RXML_EVT_BEGIN:
    case RXML_EVT_END:
    case RXML_EVT_TEXT:
	switch(state) {
	case XML_STATE_PREAMBLE:
	    state=scan_xml_preamble_section(action,info);
	    break;
	case XML_STATE_TRACKS:
	    state=scan_xml_tracks_section(action,info);
	    break;
	case XML_STATE_PLAYLISTS:
	    state=scan_xml_playlists_section(action,info);
	    break;
	default:
	    break;
	}
    default:
	break;
    }
}

#define SCAN_XML_PRE_NOTHING  0
#define SCAN_XML_PRE_VERSION  1
#define SCAN_XML_PRE_PATH     2
#define SCAN_XML_PRE_DONE     3

/**
 * collect preamble data... version, library id, etc.
 *
 * @param action xml action (RXML_EVT_TEXT, etc)
 * @param info text data associated with event
 */
int scan_xml_preamble_section(int action, char *info) {
    static int expecting_next;
    static int done;
    
    switch(action) {
    case RXML_EVT_OPEN: /* initialization */
	expecting_next=0;
	done=0;
	break;

    case RXML_EVT_END:
	if(expecting_next == SCAN_XML_PRE_DONE) /* end of tracks tag */
	    return XML_STATE_TRACKS;
	break;

    case RXML_EVT_TEXT: /* scan for the tags we expect */
	if(!expecting_next) {
	    if(strcmp(info,"Application Version") == 0) {
		expecting_next = SCAN_XML_PRE_VERSION;
	    } else if (strcmp(info,"Music Folder") == 0) {
		expecting_next = SCAN_XML_PRE_PATH;
	    } else if (strcmp(info,"Tracks") == 0) {
		expecting_next = SCAN_XML_PRE_DONE;
	    }
	} else {
	    /* we were expecting someting! */
	    switch(expecting_next) {
	    case SCAN_XML_PRE_VERSION:
		if(!scan_xml_itunes_version) {
		    scan_xml_itunes_version=strdup(info);
		    DPRINTF(E_DBG,L_SCAN,"iTunes Version: %s\n",info);
		}
		break;
	    case SCAN_XML_PRE_PATH:
		if(!scan_xml_itunes_base_path) {
		    scan_xml_itunes_base_path=strdup(info);
		    scan_xml_itunes_decoded_base_path=scan_xml_urldecode(info,0);
		    DPRINTF(E_DBG,L_SCAN,"iTunes base path: %s\n",info);
		}
		break;
	    default:
		break;
	    }
	    expecting_next=0;
	}
	break; /* RXML_EVT_TEXT */

    default:
	break;
    }

    return XML_STATE_PREAMBLE;
}


#define XML_TRACK_ST_INITIAL               0
#define XML_TRACK_ST_MAIN_DICT             1
#define XML_TRACK_ST_EXPECTING_TRACK_ID    2
#define XML_TRACK_ST_EXPECTING_TRACK_DICT  3
#define XML_TRACK_ST_TRACK_INFO            4
#define XML_TRACK_ST_TRACK_DATA            5
#define XML_TRACK_ST_EXPECTING_PLAYLISTS   6

/**
 * collect track data for each track in the itunes playlist
 *
 * @param action xml action (RXML_EVT_TEXT, etc)
 * @param info text data associated with event
 */
#define MAYBESETSTATE(a,b,c) { if((action==(a)) && \
                                  (strcmp(info,(b)) == 0)) { \
                                   state = (c); \
                                   DPRINTF(E_SPAM,L_SCAN,"New state: %d\n",state); \
                                   return XML_STATE_TRACKS; \
                             }}
                             
int scan_xml_tracks_section(int action, char *info) {
    static int state;
    static int current_track_id;
    static int current_field;
    static MP3FILE mp3;
    static char *song_path;
    char physical_path[PATH_MAX];
    char real_path[PATH_MAX];
    MP3FILE *pmp3;

    if(action == RXML_EVT_OPEN) {
	state = XML_TRACK_ST_INITIAL;
	memset((void*)&mp3,0,sizeof(MP3FILE));
	song_path = NULL;
	return 0;
    }

    /* walk through the states */
    switch(state) {
    case XML_TRACK_ST_INITIAL:
	/* expection only a <dict> */
	MAYBESETSTATE(RXML_EVT_BEGIN,"dict",XML_TRACK_ST_MAIN_DICT);
	return XML_STATE_ERROR;
	break;

    case XML_TRACK_ST_MAIN_DICT:
	/* either get a <key>, or a </dict> */
	MAYBESETSTATE(RXML_EVT_BEGIN,"key",XML_TRACK_ST_EXPECTING_TRACK_ID);
	MAYBESETSTATE(RXML_EVT_END,"dict",XML_TRACK_ST_EXPECTING_PLAYLISTS);
	return XML_STATE_ERROR;
	break;

    case XML_TRACK_ST_EXPECTING_TRACK_ID:
	/* this is somewhat loose  - <key>id</key> */
	MAYBESETSTATE(RXML_EVT_BEGIN,"key",XML_TRACK_ST_EXPECTING_TRACK_ID);
	MAYBESETSTATE(RXML_EVT_END,"key",XML_TRACK_ST_EXPECTING_TRACK_DICT);
	if (action == RXML_EVT_TEXT) {
	    current_track_id = atoi(info);
	    DPRINTF(E_DBG,L_SCAN,"Scanning iTunes id #%d\n",current_track_id);
	} else {
	    return XML_STATE_ERROR;
	}
	break;

    case XML_TRACK_ST_EXPECTING_TRACK_DICT:
	/* waiting for a dict */
	MAYBESETSTATE(RXML_EVT_BEGIN,"dict",XML_TRACK_ST_TRACK_INFO);
	return XML_STATE_ERROR;
	break;

    case XML_TRACK_ST_TRACK_INFO:
	/* again, kind of loose */
	MAYBESETSTATE(RXML_EVT_BEGIN,"key",XML_TRACK_ST_TRACK_INFO);
	MAYBESETSTATE(RXML_EVT_END,"key",XML_TRACK_ST_TRACK_DATA);
	if(action == RXML_EVT_TEXT) {
	    current_field=scan_xml_get_tagindex(info);
	    if(current_field == SCAN_XML_T_DISABLED) {
		mp3.disabled = 1;
	    } else if(current_field == SCAN_XML_T_COMPILATION) {
		mp3.compilation = 1;
	    }
	} else if((action == RXML_EVT_END) && (strcmp(info,"dict")==0)) {
	    state = XML_TRACK_ST_MAIN_DICT;
	    /* but more importantly, we gotta process the track */
	    if(song_path && (strlen(song_path) > 
			     strlen(scan_xml_itunes_decoded_base_path))) {
		sprintf(physical_path,"%siTunes Music/%s",
			scan_xml_real_base_path,
			(char*)&song_path[strlen(scan_xml_itunes_decoded_base_path)]);
		realpath(physical_path,real_path);
		pmp3=db_fetch_path(real_path);
		if(pmp3) {
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
	    MAYBEFREE(mp3.title);
	    MAYBEFREE(mp3.artist);
	    MAYBEFREE(mp3.album);
	    MAYBEFREE(mp3.genre);
	    MAYBEFREE(song_path);
	} else {
	    return XML_STATE_ERROR;
	}
	break;

    case XML_TRACK_ST_TRACK_DATA:
	if(action == RXML_EVT_BEGIN) {
	    break;
	} else if(action == RXML_EVT_TEXT) {
	    if(current_field == SCAN_XML_T_NAME) {
		mp3.title = strdup(info);
	    } else if(current_field == SCAN_XML_T_ARTIST) {
		mp3.artist = strdup(info);
	    } else if(current_field == SCAN_XML_T_ALBUM) {
		mp3.album = strdup(info);		
	    } else if(current_field == SCAN_XML_T_GENRE) {
		mp3.genre = strdup(info);
	    } else if(current_field == SCAN_XML_T_TOTALTIME) {
		mp3.song_length = atoi(info);
	    } else if(current_field == SCAN_XML_T_TRACKNUMBER) {
		mp3.track = atoi(info);
	    } else if(current_field == SCAN_XML_T_TRACKCOUNT) {
		mp3.total_tracks = atoi(info);
	    } else if(current_field == SCAN_XML_T_YEAR) {
		mp3.year = atoi(info);
	    } else if(current_field == SCAN_XML_T_BITRATE) {
		mp3.bitrate = atoi(info);
	    } else if(current_field == SCAN_XML_T_SAMPLERATE) {
		mp3.samplerate = atoi(info);
	    } else if(current_field == SCAN_XML_T_PLAYCOUNT) {
		mp3.play_count = atoi(info);
	    } else if(current_field == SCAN_XML_T_RATING) {
		mp3.rating = atoi(info);
	    } else if(current_field == SCAN_XML_T_DISCNO) {
		mp3.disc = atoi(info);
	    } else if(current_field == SCAN_XML_T_DISCCOUNT) {
		mp3.total_discs = atoi(info);
	    } else if(current_field == SCAN_XML_T_LOCATION) {
		song_path = scan_xml_urldecode(info,0);
	    }
	} else if(action == RXML_EVT_END) {
	    state = XML_TRACK_ST_TRACK_INFO;
	} else {
	    return XML_STATE_ERROR;
	}
	break;
    default:
	return XML_STATE_ERROR;
    }

    return XML_STATE_TRACKS;
}

/**
 * collect playlist data for each playlist in the itunes xml file
 *
 * @param action xml action (RXML_EVT_TEXT, etc)
 * @param info text data associated with event
 */
int scan_xml_playlists_section(int action, char *info) {
    return XML_STATE_PLAYLISTS;
}

