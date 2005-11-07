/*
 * $Id$
 *
 * This really isn't xmlrpc.  It's xmlrpc-ish.  Emphasis on -ish.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "configfile.h"
#include "daapd.h"
#include "err.h"
#include "mp3-scanner.h"
#include "webserver.h"

/* Forwards */
void xml_get_stats(WS_CONNINFO *pwsc);
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

    if(strcasecmp(method,"stats") == 0) {
        xml_get_stats(pwsc);
        return;
    }

    ws_returnerror(pwsc,500,"Invalid method");
    return;
}

/**
 * return xml file of all playlists
 */
void xml_get_stats(WS_CONNINFO *pwsc) {
    int r_secs, r_days, r_hours, r_mins;
    char buf[80];
    WS_CONNINFO *pci;
    SCAN_STATUS *pss;
    WSTHREADENUM wste;
    

    ws_addresponseheader(pwsc,"Content-Type","text/xml; charset=utf-8");
    ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
    ws_emitheaders(pwsc);

    ws_writefd(pwsc,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    ws_writefd(pwsc,"<status>");

    ws_writefd(pwsc,"<service_status>");
    /* enumerate services? */
    ws_writefd(pwsc,"</service_status>");
    
    ws_writefd(pwsc,"<thread_status>");
    /* enumerate thread status */

    pci = ws_thread_enum_first(config.server,&wste);
    while(pci) {
        pss = ws_get_local_storage(pci);
        if(pss) {
            ws_writefd(pwsc,"<thread><id>%d</id><sourceip>%s</sourceip><action>%s</action></thread>",
                       pss->thread,pss->host,pss->what);
        }
        pci=ws_thread_enum_next(config.server,&wste);
    }
    
    
    ws_writefd(pwsc,"</thread_status>");

    ws_writefd(pwsc,"<statistics>");
    /* dump stats */
    
    ws_writefd(pwsc,"<stat name=\"uptime\">");

    r_secs=time(NULL)-config.stats.start_time;

    r_days=r_secs/(3600 * 24);
    r_secs -= ((3600 * 24) * r_days);

    r_hours=r_secs/3600;
    r_secs -= (3600 * r_hours);

    r_mins=r_secs/60;
    r_secs -= 60 * r_mins;

    memset(buf,0x0,sizeof(buf));
    if(r_days) 
	sprintf((char*)&buf[strlen(buf)],"%d day%s, ", r_days,
		r_days == 1 ? "" : "s");

    if(r_days || r_hours) 
	sprintf((char*)&buf[strlen(buf)],"%d hour%s, ", r_hours,
		r_hours == 1 ? "" : "s");

    if(r_days || r_hours || r_mins)
	sprintf((char*)&buf[strlen(buf)],"%d minute%s, ", r_mins,
		r_mins == 1 ? "" : "s");

    sprintf((char*)&buf[strlen(buf)],"%d second%s ", r_secs,
	    r_secs == 1 ? "" : "s");
    
    ws_writefd(pwsc,"<name>Uptime</name>");    
    ws_writefd(pwsc,"<value>%s</value>",buf);
    ws_writefd(pwsc,"</stat>");
    ws_writefd(pwsc,"</statistics>");
    ws_writefd(pwsc,"</status>");
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
