/*
 * $Id$
 *
 * This really isn't xmlrpc.  It's xmlrpc-ish.  Emphasis on -ish.
 */

#include <stdio.h>
#include <stdlib.h>

#include "err.h"
#include "db-memory.h"
#include "mp3-scanner.h"
#include "webserver.h"

/* Forwards */
void xml_get_playlists(WS_CONNINFO *pwsc);
void xml_get_playlistitems(WS_CONNINFO *pwsc);
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
    } else if(strcasecmp(method,"getPlaylistItems") == 0) {
	xml_get_playlistitems(pwsc);
	return;
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

    ws_addresponseheader(pwsc,"Content-Type","text/xml; charset=utf-8");
    ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
    ws_emitheaders(pwsc);

    ws_writefd(pwsc,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    ws_writefd(pwsc,"<playlists>");

    /* enumerate all the playlists */
    henum=db_playlist_enum_begin();
    while(henum) {
	playlistid=db_playlist_enum(&henum);
	ws_writefd(pwsc,"<item>");
	ws_writefd(pwsc,"<id>%d</id>",playlistid);
	temp=xml_entity_encode(db_get_playlist_name(playlistid));
	ws_writefd(pwsc,"<name>%s</name>",temp);
	if(temp) free(temp);
	ws_writefd(pwsc,"<smart>%d</smart>",db_get_playlist_is_smart(playlistid));
	ws_writefd(pwsc,"<entries>%d</entries>",db_get_playlist_entry_count(playlistid));
	ws_writefd(pwsc," </item>");
    }

    ws_writefd(pwsc,"</playlists>\n");

    return;
}


/**
 * return xml file of playlist info
 */
void xml_get_playlistitems(WS_CONNINFO *pwsc) {
    char *playlistnum;
    int playlistid;
    ENUMHANDLE henum;
    unsigned long int itemid;
    char *temp;
    MP3FILE *current;

    if((playlistnum=ws_getvar(pwsc,"playlistid")) == NULL) {
	ws_returnerror(pwsc,500,"no playlistid specified");
	return;
    }

    ws_addresponseheader(pwsc,"Content-Type","text/xml; charset=utf-8");
    ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
    ws_emitheaders(pwsc);

    playlistid=atoi(playlistnum);

    ws_writefd(pwsc,"<playlist>");

    henum=db_playlist_items_enum_begin(playlistid);
    while((itemid=db_playlist_items_enum(&henum)) != -1) {
	current=db_find(itemid);
	if(0 != current) {
	    ws_writefd(pwsc,"<item>%lu</item>",itemid);
	    temp=xml_entity_encode(current->title);
	    ws_writefd(pwsc,"<name>%s</name>",temp);
	    free(temp);
	    db_dispose(current);
	    free(current);
	}
    }
    
    ws_writefd(pwsc,"</playlist>");
    return;
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
