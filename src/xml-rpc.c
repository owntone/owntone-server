/*
 * $Id$
 *
 * This really isn't xmlrpc.  It's xmlrpc-ish.  Emphasis on -ish.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "configfile.h"
#include "conf.h"
#include "db-generic.h"
#include "daapd.h"
#include "err.h"
#include "mp3-scanner.h"
#include "rend.h"
#include "webserver.h"
#include "xml-rpc.h"

/* typedefs */
typedef struct tag_xmlstack {
    char *tag;
    struct tag_xmlstack *next;
} XMLSTACK;

struct tag_xmlstruct {
    WS_CONNINFO *pwsc;
    int stack_level;
    XMLSTACK stack;
};

/* Forwards */
void xml_get_stats(WS_CONNINFO *pwsc);
void xml_set_config(WS_CONNINFO *pwsc);
void xml_return_error(WS_CONNINFO *pwsc, int errno, char *errstr);
char *xml_entity_encode(char *original);

void xml_return_error(WS_CONNINFO *pwsc, int errno, char *errstr) {
    XMLSTRUCT *pxml;

    pxml=xml_init(pwsc,TRUE);
    xml_push(pxml,"results");
    
    xml_output(pxml,"status","%d",errno);
    xml_output(pxml,"statusstring","%s",errstr);

    xml_pop(pxml); /* results */
    xml_deinit(pxml);
    return;
}


/**
 * create an xml response structure, a helper struct for
 * building xml responses.
 *
 * @param pwsc the pwsc we are emitting to
 * @param emit_header whether or not to throw out html headers and xml header
 * @returns XMLSTRUCT on success, or NULL if failure
 */
XMLSTRUCT *xml_init(WS_CONNINFO *pwsc, int emit_header) {
    XMLSTRUCT *pxml;

    pxml=(XMLSTRUCT*)malloc(sizeof(XMLSTRUCT));
    if(!pxml) {
        DPRINTF(E_FATAL,L_XML,"Malloc error\n");
    }

    memset(pxml,0,sizeof(XMLSTRUCT));

    pxml->pwsc = pwsc;

    /* the world would be a wonderful place without ie */
    ws_addresponseheader(pwsc,"Cache-Control","no-cache");
    ws_addresponseheader(pwsc,"Expires","-1");

    if(emit_header) {
        ws_addresponseheader(pwsc,"Content-Type","text/xml; charset=utf-8");
        ws_writefd(pwsc,"HTTP/1.0 200 OK\r\n");
        ws_emitheaders(pwsc);

        ws_writefd(pwsc,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    }

    return pxml;
}


/**
 * post settings back to the config file
 *
 * @param pwsc connection do dump results back to
 */
void xml_set_config(WS_CONNINFO *pwsc) {
    char *section;
    char *key;
    char *value;
    int err;

    section = ws_getvar(pwsc,"section");
    key = ws_getvar(pwsc,"key");
    value = ws_getvar(pwsc,"value");

    if((!section) || (!key) || (!value)) {
        xml_return_error(pwsc,500,"Missing section, key, or value");
        return;
    }

    if((err=conf_set_string(section,key,value) != CONF_E_SUCCESS)) {
        /* should return text error from conf_ */
        xml_return_error(pwsc,500,"conf_set_string: error");
        return;
    }

    xml_return_error(pwsc,200,"Success");
    return;
}

/**
 * push a new term on the stack
 *
 * @param pxml xml struct obtained from xml_init
 * @param term next xlm section to start
 */
void xml_push(XMLSTRUCT *pxml, char *term) {
    XMLSTACK *pstack;

    pstack = (XMLSTACK *)malloc(sizeof(XMLSTACK));
    pstack->next=pxml->stack.next;
    pstack->tag=strdup(term);
    pxml->stack.next=pstack;

    pxml->stack_level++;

    ws_writefd(pxml->pwsc,"<%s>",term);
}

/**
 * end an xml section
 *
 * @param pxml xml struct we are working with
 */
void xml_pop(XMLSTRUCT *pxml) {
    XMLSTACK *pstack;

    pstack=pxml->stack.next;
    if(!pstack) {
        DPRINTF(E_LOG,L_XML,"xml_pop: tried to pop an empty stack\n");
        return;
    }

    pxml->stack.next = pstack->next;

    ws_writefd(pxml->pwsc,"</%s>",pstack->tag);
    free(pstack->tag);
    free(pstack);

    pxml->stack_level--;
}

/**
 * output a string
 */
void xml_output(XMLSTRUCT *pxml, char *section, char *fmt, ...) {
    va_list ap;
    char buf[256];
    char *output;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    output = xml_entity_encode(buf);
    if(section) {
        xml_push(pxml,section);
    }
    ws_writefd(pxml->pwsc,"%s",output);
    free(output);
    if(section) {
        xml_pop(pxml);
    }
}

/**
 * clean up an xml struct
 *
 * @param pxml xml struct to clean up
 */
void xml_deinit(XMLSTRUCT *pxml) {
    XMLSTACK *pstack;

    if(pxml->stack.next) {
        DPRINTF(E_LOG,L_XML,"xml_deinit: entries still on stack (%s)\n",
                pxml->stack.next->tag);
    }

    while((pstack=pxml->stack.next)) {
        pxml->stack.next=pstack->next;
        free(pstack->tag);
        free(pstack);
    }
    free(pxml);
}

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

    if(strcasecmp(method,"config") == 0) {
        conf_xml_dump(pwsc);
        return;
    }

    if(strcasecmp(method,"setconfig") == 0) {
        xml_set_config(pwsc);
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
    int count;
    XMLSTRUCT *pxml;

    pxml=xml_init(pwsc,1);
    xml_push(pxml,"status");

    xml_push(pxml,"service_status");

    xml_push(pxml,"service");

    xml_output(pxml,"name","Rendezvous");

#ifndef WITHOUT_MDNS
    if(config.use_mdns) {
        xml_output(pxml,"status",rend_running() ? "Stopped" : "Running"); /* ??? */
    } else {
        xml_output(pxml,"status","Disabled");
    }
#else

    ws_writefd(pwsc,"<td>No Support</td><td>&nbsp;</td></tr>\n");
#endif
    xml_pop(pxml); /* service */

    xml_push(pxml,"service");
    xml_output(pxml,"name","DAAP Server");
    xml_output(pxml,"status",config.stop ? "Stopping" : "Running");
    xml_pop(pxml); /* service */

    xml_push(pxml,"service");
    xml_output(pxml,"name","File Scanner");
    xml_output(pxml,"status",config.reload ? "Running" : "Idle");
    xml_pop(pxml); /* service */

    xml_pop(pxml); /* service_status */

    xml_push(pxml,"thread_status");

    pci = ws_thread_enum_first(config.server,&wste);
    while(pci) {
        pss = ws_get_local_storage(pci);
        if(pss) {
            xml_push(pxml,"thread");
            xml_output(pxml,"id","%d",pss->thread);
            xml_output(pxml,"sourceip","%s",pss->host);
            xml_output(pxml,"action","%s",pss->what);
            xml_pop(pxml); /* thread */
        }
        pci=ws_thread_enum_next(config.server,&wste);
    }

    xml_pop(pxml); /* thread_status */

    xml_push(pxml,"statistics");

    r_secs=(int)(time(NULL)-config.stats.start_time);

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

    xml_push(pxml,"stat");
    xml_output(pxml,"name","Uptime");
    xml_output(pxml,"value","%s",buf);
    xml_pop(pxml); /* stat */

    xml_push(pxml,"stat");
    xml_output(pxml,"name","Songs");
    db_get_song_count(NULL,&count);
    xml_output(pxml,"value","%d",count);
    xml_pop(pxml); /* stat */

    xml_push(pxml,"stat");
    xml_output(pxml,"name","Songs Served");
    xml_output(pxml,"value","%d",config.stats.songs_served);
    xml_pop(pxml); /* stat */

    xml_pop(pxml); /* statistics */


    xml_push(pxml,"misc");
    xml_output(pxml,"writable_config","%d",conf_iswritable());
    xml_pop(pxml); /* misc */

    xml_pop(pxml); /* status */

    xml_deinit(pxml);
    return;
}

/**
 * xml entity encoding, stupid style
 */
char *xml_entity_encode(char *original) {
    char *new;
    char *s, *d;
    int destsize;

    destsize = 6*(int)strlen(original)+1;
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
