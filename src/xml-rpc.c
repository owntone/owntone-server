/*
 * $Id$
 *
 * This really isn't xmlrpc.  It's xmlrpc-ish.  Emphasis on -ish.
 */

#include <stdio.h>
#include <stdlib.h>

#include "err.h"
#include "db-memory.h"
#include "webserver.h"

/* Forwards */
void xml_get_playlists(WS_CONNINFO *pwsc);
void xml_get_playlistinfo(WS_CONNINFO *pwsc);
char *xml_entity_encode(char *original);

/**
 * main entrypoint for the xmlrpc functions.
 *
 * @arg pwsc Pointer to the web request structure
 */
void xml_handle(WS_CONNINFO *pwsc) {
    char *method;

    if((method=ws_getvar(pwsc,"method")) == NULL) {
	ws_returnerror(pwsc,500,"no method specified");
	return;
    }

    if(strcasecmp(method,"getPlaylists") == 0) {
	xml_get_playlists(pwsc);
	return;
    } else if(strcasecmp(method,"getPlaylistInfo") == 0) {
	xml_get_playlistinfo(pwsc);
    }

    ws_returnerror(pwsc,500,"Invalid method");
    return;
}

/**
 * return xml file of all playlists
 */
void xml_get_playlists(WS_CONNINFO *pwsc) {
    ENUMHANDLE henum;
    int playlistid;
    char *temp;

    ws_addresponseheader(pwsc,"Content-type","text/xml");
    ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
    ws_emitheaders(pwsc);

    ws_writefd(pwsc,"<?xml version=\"1.0\" standalone=\"yes\" encoding=\"UTF-8\"?>\n");
    ws_writefd(pwsc,"<playlists>\n");

    /* enumerate all the playlists */
    henum=db_playlist_enum_begin();
    while(henum) {
	playlistid=db_playlist_enum(&henum);
	ws_writefd(pwsc," <item>\n");
	ws_writefd(pwsc,"  <id>%d</id>\n",playlistid);
	temp=xml_entity_encode(db_get_playlist_name(playlistid));
	ws_writefd(pwsc,"  <name>%s</name>\n",temp);
	if(temp) free(temp);
	ws_writefd(pwsc," </item>\n");
    }

    ws_writefd(pwsc,"</playlists>\n");

    return;
}


/**
 * return xml file of playlist info
 */
void xml_get_playlistinfo(WS_CONNINFO *pwsc) {
    ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
    ws_emitheaders(pwsc);
}

/**
 * xml entity encoding, stupid style
 */
char *xml_entity_encode(char *original) {
    char *new;
    char *s, *d;
    int destsize;

    destsize = 6*strlen(original)+1;
    new=(char *)malloc(destsize);
    if(!new) return NULL;

    memset(new,0x00,destsize);

    s=original;
    d=new;

    while(*s) {
	switch(*s) {
	case '>':
	    strcat(d,"&gt;");
	    d += 4;
	    s++;
	    break;
	case '<':
	    strcat(d,"&lt;");
	    d += 4;
	    s++;
	    break;
	case '"':
	    strcat(d,"&quot;");
	    d += 6;
	    s++;
	    break;
	case '\'':
	    strcat(d,"&apos;");
	    d += 6;
	    s++;
	    break;
	case '&':
	    strcat(d,"&amp;");
	    d += 5;
	    s++;
	    break;
	default:
	    *d++ = *s++;
	}
    }

    return new;
}
