/*
 * $Id$
 * daap handler functions and dispatch code
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <netinet/in.h>
#endif

#include "conf.h"
#include "db-generic.h"
#include "configfile.h"
#include "err.h"
#include "mp3-scanner.h"
#include "plugin.h"
#include "webserver.h"
#include "dynamic-art.h"
#include "restart.h"
#include "daapd.h"


/* Forwards */
static void dispatch_server_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_login(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_content_codes(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_update(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_dbinfo(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_playlistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_playlists(WS_CONNINFO *pqsc, DBQUERYINFO *pqi);
static void dispatch_addplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_addplaylistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_editplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_deleteplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_deleteplaylistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_items(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_logout(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_error(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *container, char *error);
static int dispatch_output_start(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, int content_length);
static int dispatch_output_write(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, unsigned char *block, int len);
static int dispatch_output_end(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);

static DAAP_ITEMS *dispatch_xml_lookup_tag(char *tag);
static char *dispatch_xml_encode(char *original, int len);
static int dispatch_output_xml_write(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, unsigned char *block, int len);

static void dispatch_cleanup(DBQUERYINFO *pqi);

/**
 * Hold the inf for the output serializer
 */
typedef struct tag_xml_stack {
    char tag[5];
    int bytes_left;
} XML_STACK;

typedef struct tag_output_info {
    int xml_output;
    int readable;
    int browse_response;
    int dmap_response_length;
    int stack_height;
    XML_STACK stack[10];
} OUTPUT_INFO;


/**
 * do cleanup on the pqi structure... free any allocated memory, etc
 */
void dispatch_cleanup(DBQUERYINFO *pqi) {
    if(!pqi)
        return;

    if(pqi->output_info)
        free(pqi->output_info);

    if(pqi->pt) {
        sp_dispose(pqi->pt);
    }
    free(pqi);
}


/**
 * Handles authentication for the daap server.  This isn't the
 * authenticator for the web admin page, but rather the iTunes
 * authentication when trying to connect to the server.  Note that most
 * of this is actually handled in the web server registration, which
 * decides when to apply the authentication or not.  If you mess with
 * when and where the webserver applies auth or not, you'll likely
 * break something.  It seems that some requests must be authed, and others
 * not.  If you apply authentication somewhere that iTunes doesn't expect
 * it, it happily disconnects.
 *
 * @param username The username passed by iTunes
 * @param password The password passed by iTunes
 * @returns 1 if auth successful, 0 otherwise
 */
int daap_auth(WS_CONNINFO *pwsc, char *username, char *password) {
    char *readpassword;

    readpassword = conf_alloc_string("general","password",NULL);

    if(password == NULL) {
        if((readpassword == NULL)||(strlen(readpassword) == 0)) {
            if(readpassword) free(readpassword);
            return TRUE;
        } else {
            free(readpassword);
            return FALSE;
        }
    } else {
        if(strcasecmp(password,readpassword)) {
            free(readpassword);
            return FALSE;
        } else {
            free(readpassword);
            return TRUE;
        }
    }

    return TRUE; /* not used */
}

/**
 * decodes the request and hands it off to the appropriate dispatcher
 *
 * @param pwsc the current web connection info
 */
void daap_handler(WS_CONNINFO *pwsc) {
    DBQUERYINFO *pqi;
    char *token, *string, *save;
    char *query, *index, *ptr;
    int l,h;

    pqi=(DBQUERYINFO*)malloc(sizeof(DBQUERYINFO));
    if(!pqi) {
        ws_returnerror(pwsc,500,"Internal server error: out of memory!");
        return;
    }

    memset(pqi,0x00,sizeof(DBQUERYINFO));
    pqi->zero_length = conf_get_int("daap","empty_strings",0);
    pqi->correct_order = conf_get_int("scan","correct_order",1);
    pqi->pwsc = pwsc;

    /* we could really pre-parse this to make sure it works */
    query=ws_getvar(pwsc,"query");
    if(!query) query=ws_getvar(pwsc,"filter");
    if(query) {
        pqi->pt = sp_init();
        if(!sp_parse(pqi->pt,query,SP_TYPE_QUERY)) {
            DPRINTF(E_LOG,L_DAAP,"Ignoring bad query/filter (%s): %s\n",
                    query,sp_get_error(pqi->pt));
            sp_dispose(pqi->pt);
            pqi->pt = NULL;
        }
        DPRINTF(E_DBG,L_DAAP,"Parsed query successfully\n");
    }

    /* set up the index stuff -- this will be in the format
     * index = l, index=l-h, index=l- or index=-h
     */

    pqi->index_type=indexTypeNone;
    l = h = 0;
    index=ws_getvar(pwsc,"index");
    if(index) {
        DPRINTF(E_DBG,L_DAAP,"Indexed query: %s\n",index);

        /* we have some kind of index string... */
        l=strtol(index,&ptr,10);
        if(l<0) { /* "-h"... tail range, last "h" entries */
            pqi->index_type = indexTypeLast;
            pqi->index_low = l * -1;
            DPRINTF(E_DBG,L_DAAP,"Index type last %d\n",l);
        } else if(*ptr == '\0') { /* single item */
            pqi->index_type = indexTypeSub;
            pqi->index_low = l;
            pqi->index_high = l;
            DPRINTF(E_DBG,L_DAAP,"Index type single item %d\n",l);
        } else if(*ptr == '-') {
            pqi->index_type = indexTypeSub;
            pqi->index_low = l;

            if(*++ptr == '\0') { /* l- */
                /* we don't handle this */
                DPRINTF(E_LOG,L_DAAP,"unhandled index: %s\n",index);
                pqi->index_high = 999999;
            } else {
                h = strtol(ptr, &ptr, 10);
                pqi->index_high=h;
            }
            DPRINTF(E_DBG,L_DAAP,"Index type range %d-%d\n",l,h);
        }
    }

    /* Add some default headers */
    ws_addresponseheader(pwsc,"Accept-Ranges","bytes");
    ws_addresponseheader(pwsc,"DAAP-Server","mt-daapd/" VERSION);
    ws_addresponseheader(pwsc,"Content-Type","application/x-dmap-tagged");
    ws_addresponseheader(pwsc,"Cache-Control","no-cache");  /* anti-ie defense */
    ws_addresponseheader(pwsc,"Expires","-1");

    if(ws_getvar(pwsc,"session-id"))
        pqi->session_id = atoi(ws_getvar(pwsc,"session-id"));

    /* tokenize the uri for easier decoding */
    string=(pwsc->uri)+1;
    while((pqi->uri_count < 9) && (token=strtok_r(string,"/",&save))) {
        string=NULL;
        pqi->uri_sections[pqi->uri_count++] = token;
    }

    /* Start dispatching */
    if(!strcasecmp(pqi->uri_sections[0],"server-info")) {
        dispatch_server_info(pwsc,pqi);
        dispatch_cleanup(pqi);
        return;
    }

    if(!strcasecmp(pqi->uri_sections[0],"content-codes")) {
        dispatch_content_codes(pwsc,pqi);
        dispatch_cleanup(pqi);
        return;
    }

    if(!strcasecmp(pqi->uri_sections[0],"login")) {
        dispatch_login(pwsc,pqi);
        dispatch_cleanup(pqi);
        return;
    }

    if(!strcasecmp(pqi->uri_sections[0],"update")) {
        dispatch_update(pwsc,pqi);
        dispatch_cleanup(pqi);
        return;
    }

    if(!strcasecmp(pqi->uri_sections[0],"logout")) {
        dispatch_logout(pwsc,pqi);
        dispatch_cleanup(pqi);
        return;
    }

    /*
     * /databases/id/items
     * /databases/id/containers
     * /databases/id/containers/id/items
     * /databases/id/browse/category
     * /databases/id/items/id.mp3
     */
    if(!strcasecmp(pqi->uri_sections[0],"databases")) {
        if(pqi->uri_count == 1) {
            dispatch_dbinfo(pwsc,pqi);
            dispatch_cleanup(pqi);
            return;
        }
        pqi->db_id=atoi(pqi->uri_sections[1]);
        if(pqi->uri_count == 3) {
            if(!strcasecmp(pqi->uri_sections[2],"items")) {
                /* /databases/id/items */
                dispatch_items(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if(!strcasecmp(pqi->uri_sections[2],"containers")) {
                /* /databases/id/containers */
                dispatch_playlists(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }

            pwsc->close=1;
            dispatch_cleanup(pqi);
            ws_returnerror(pwsc,404,"Page not found");
            return;
        }
        if(pqi->uri_count == 4) {
            if(!strcasecmp(pqi->uri_sections[2],"browse")) {
                /* /databases/id/browse/something */
                pqi->playlist_id=1; /* browse the library */
                dispatch_browse(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if(!strcasecmp(pqi->uri_sections[2],"items")) {
                /* /databases/id/items/id.mp3 */
                dispatch_stream(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
                (!strcasecmp(pqi->uri_sections[3],"add"))) {
                 /* /databases/id/containers/add */
                 dispatch_addplaylist(pwsc,pqi);
                 dispatch_cleanup(pqi);
                 return;
            }
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
                (!strcasecmp(pqi->uri_sections[3],"del"))) {
                 /* /databases/id/containers/del */
                dispatch_deleteplaylist(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
                (!strcasecmp(pqi->uri_sections[3],"edit"))) {
                /* /databases/id/contaienrs/edit */
                dispatch_editplaylist(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }

            pwsc->close=1;
            dispatch_cleanup(pqi);
            ws_returnerror(pwsc,404,"Page not found");
            return;
        }
        if(pqi->uri_count == 5) {
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
               (!strcasecmp(pqi->uri_sections[4],"items"))) {
                pqi->playlist_id=atoi(pqi->uri_sections[3]);
                dispatch_playlistitems(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
               (!strcasecmp(pqi->uri_sections[4],"del"))) {
                /* /databases/id/containers/id/del */
                pqi->playlist_id=atoi(pqi->uri_sections[3]);
                dispatch_deleteplaylistitems(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
        }
        if(pqi->uri_count == 6) {
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
               (!strcasecmp(pqi->uri_sections[4],"items")) &&
               (!strcasecmp(pqi->uri_sections[5],"add"))) {
                pqi->playlist_id=atoi(pqi->uri_sections[3]);
                dispatch_addplaylistitems(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
            if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
               (!strcasecmp(pqi->uri_sections[4],"browse"))) {
                pqi->playlist_id=atoi(pqi->uri_sections[3]);
                dispatch_browse(pwsc,pqi);
                dispatch_cleanup(pqi);
                return;
            }
        }
    }

    pwsc->close=1;
    dispatch_cleanup(pqi);
    ws_returnerror(pwsc,404,"Page not found");
    return;
}


/**
 * set up whatever necessary to begin streaming the output
 * to the client.
 *
 * @param pwsc pointer to the current conninfo struct
 * @param pqi pointer to the current dbquery struct
 * @param content_length content_length (assuming dmap) of the output
 */
int dispatch_output_start(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, int content_length) {
    OUTPUT_INFO *poi;

    poi=(OUTPUT_INFO*)calloc(1,sizeof(OUTPUT_INFO));
    if(!poi) {
        DPRINTF(E_LOG,L_DAAP,"Malloc error in dispatch_ouput_start\n");
        return -1;
    }

    pqi->output_info = (void*) poi;
    poi->dmap_response_length = content_length;

    if(ws_getvar(pwsc,"output")) {
        if(strcasecmp(ws_getvar(pwsc,"output"),"readable") == 0)
            poi->readable=1;

        poi->xml_output=1;
        ws_addresponseheader(pwsc,"Content-Type","text/xml");
        ws_addresponseheader(pwsc,"Connection","Close");
        pwsc->close=1;
        ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
        ws_emitheaders(pwsc);
        ws_writefd(pwsc,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
        if(poi->readable)
            ws_writefd(pwsc,"\n");
        return 0;
    }

    ws_addresponseheader(pwsc,"Content-Length","%d",poi->dmap_response_length);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    /* I guess now we would start writing the output */
    return 0;
}

/**
 * write the output to wherever it goes.  This expects to be fed
 * full dmap blocks.  In the simplest case, it just streams those
 * dmap blocks out to the client.  In more complex cases, it convert
 * them to xml, or compresses them.
 *
 * @param pqi pointer to the current dbquery info struct
 * @param pwsc pointer to the current conninfo struct
 * @param pblock block of data to write
 * @param len length of block to write
 */
int dispatch_output_write(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, unsigned char *block, int len) {
    OUTPUT_INFO *poi=(pqi->output_info);
    int result;

    if(poi->xml_output)
        return dispatch_output_xml_write(pwsc, pqi, block, len);

    result=r_write(pwsc->fd,block,len);

    if(result != len)
        return -1;

    return 0;
}

/**
 * this is the serializer for xml.  This assumes that (with the exception of
 * containers) blocks are complete dmap blocks
 *
 * @param pqi pointer to the current dbquery info struct
 * @param pwsc pointer to the current conninfo struct
 * @param pblock block of data to write
 * @param len length of block to write
 */
int dispatch_output_xml_write(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, unsigned char *block, int len) {
    OUTPUT_INFO *poi = pqi->output_info;
    unsigned char *current=block;
    char block_tag[5];
    int block_len;
    int len_left;
    DAAP_ITEMS *pitem;
    unsigned char *data;
    int ivalue;
    long long lvalue;
    int block_done=1;
    int stack_ptr;
    char *encoded_string;

    while(current < (block + len)) {
        block_done=1;
        len_left=(int)((block+len) - current);
        if(len_left < 8) {
            DPRINTF(E_FATAL,L_DAAP,"Badly formatted dmap block - frag size: %d",len_left);
        }

        /* set up block */
        memcpy(block_tag,current,4);
        block_tag[4] = '\0';
        block_len = current[4] << 24 | current[5] << 16 |
            current[6] << 8 | current[7];
        data = &current[8];

        if(strncmp(block_tag,"abro",4) ==0 ) {
            /* browse queries treat mlit as a string, not container */
            poi->browse_response=1;
        }

        /* lookup and serialize */
        DPRINTF(E_SPAM,L_DAAP,"%*s %s: %d\n",poi->stack_height,"",block_tag,block_len);
        pitem=dispatch_xml_lookup_tag(block_tag);
        if(poi->readable)
            r_fdprintf(pwsc->fd,"%*s",poi->stack_height,"");
        r_fdprintf(pwsc->fd,"<%s>",pitem->description);
        switch(pitem->type) {
        case 0x01: /* byte */
            if(block_len != 1) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 1\n",block_tag, block_len);
            }
            r_fdprintf(pwsc->fd,"%d",*((char *)data));
            break;

        case 0x02: /* unsigned byte */
            if(block_len != 1) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 1\n",block_tag, block_len);
            }
            r_fdprintf(pwsc->fd,"%ud",*((char *)data));
            break;

        case 0x03: /* short */
            if(block_len != 2) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 2\n",block_tag, block_len);
            }

            ivalue = data[0] << 8 | data[1];
            r_fdprintf(pwsc->fd,"%d",ivalue);
            break;

        case 0x05: /* int */
        case 0x0A: /* epoch */
            if(block_len != 4) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 4\n",block_tag, block_len);
            }
            ivalue = data[0] << 24 |
                data[1] << 16 |
                data[2] << 8 |
                data[3];
            r_fdprintf(pwsc->fd,"%d",ivalue);
            break;
        case 0x07: /* long long */
            if(block_len != 8) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 8\n",block_tag, block_len);
            }

            ivalue = data[0] << 24 |
                data[1] << 16 |
                data[2] << 8 |
                data[3];
            lvalue=ivalue;
            ivalue = data[4] << 24 |
                data[5] << 16 |
                data[6] << 8 |
                data[7];
            lvalue = (lvalue << 32) | ivalue;
            r_fdprintf(pwsc->fd,"%ll",ivalue);
            break;
        case 0x09: /* string */
            if(block_len) {
                encoded_string=dispatch_xml_encode((char*)data,block_len);
                r_fdprintf(pwsc->fd,"%s",encoded_string);
                free(encoded_string);
            }
            break;
        case 0x0B: /* version? */
            if(block_len != 4) {
                DPRINTF(E_FATAL,L_DAAP,"tag %s, size %d, wanted 4\n",block_tag, block_len);
            }

            ivalue=data[0] << 8 | data[1];
            r_fdprintf(pwsc->fd,"%d.%d.%d",ivalue,data[2],data[3]);
            break;

        case 0x0C:
            if((poi->browse_response)&&(strcmp(block_tag,"mlit") ==0)) {
                if(block_len) {
                    encoded_string=dispatch_xml_encode((char*)data,block_len);
                    r_fdprintf(pwsc->fd,"%s",encoded_string);
                    free(encoded_string);
                }
            } else {
                /* we'll need to stack this up and try and remember where we
                 * came from.  Make it an extra 8 so that it gets fixed to
                 * the *right* amount when the stacks are juggled below
                 */

                poi->stack[poi->stack_height].bytes_left=block_len + 8;
                memcpy(poi->stack[poi->stack_height].tag,block_tag,5);
                poi->stack_height++;
                if(poi->stack_height == 10) {
                    DPRINTF(E_FATAL,L_DAAP,"Stack overflow\n");
                }
                block_done=0;
            }
            break;

        default:
            DPRINTF(E_FATAL,L_DAAP,"Bad dmap type: %d, %s\n",
                    pitem->type, pitem->description);
            break;
        }

        if(block_done) {
            r_fdprintf(pwsc->fd,"</%s>",pitem->description);
            if(poi->readable)
                r_fdprintf(pwsc->fd,"\n");

            block_len += 8;
        } else {
            /* must be a container */
            block_len = 8;
            if(poi->readable)
                r_fdprintf(pwsc->fd,"\n");
        }

        current += block_len;

        if(poi->stack_height) {
            stack_ptr=poi->stack_height;
            while(stack_ptr--) {
                poi->stack[stack_ptr].bytes_left -= block_len;
                if(poi->stack[stack_ptr].bytes_left < 0) {
                    DPRINTF(E_FATAL,L_DAAP,"negative container\n");
                }

                if(!poi->stack[stack_ptr].bytes_left) {
                    poi->stack_height--;
                    pitem=dispatch_xml_lookup_tag(poi->stack[stack_ptr].tag);
                    if(poi->readable)
                        r_fdprintf(pwsc->fd,"%*s",poi->stack_height,"");
                    r_fdprintf(pwsc->fd,"</%s>",pitem->description);
                    if(poi->readable)
                        r_fdprintf(pwsc->fd,"\n");
                }
            }
        }
    }

    return 0;
}


/**
 * finish streaming output to the client, freeing any allocated
 * memory, and cleaning up
 *
 * @param pwsc current conninfo struct
 * @param pqi current dbquery struct
 */
int dispatch_output_end(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    OUTPUT_INFO *poi = pqi->output_info;

    if((poi) && (poi->xml_output) && (poi->stack_height)) {
        DPRINTF(E_LOG,L_DAAP,"Badly formed xml -- still stack\n");
    }

    config_set_status(pwsc,pqi->session_id,NULL);

    return 0;
}


DAAP_ITEMS *dispatch_xml_lookup_tag(char *tag) {
    DAAP_ITEMS *pitem;

    pitem=taglist;
    while((pitem->tag) && (strncmp(tag,pitem->tag,4))) {
        pitem++;
    }

    if(!pitem->tag)
        DPRINTF(E_FATAL,L_DAAP,"Unknown daap tag: %c%c%c%c\n",tag[0],tag[1],tag[2],tag[3]);

    return pitem;
}

/**
 * xml entity encoding, stupid style
 */
char *dispatch_xml_encode(char *original, int len) {
    char *new;
    char *s, *d;
    int destsize;
    int truelen;

    /* this is about stupid */
    if(len) {
        truelen=len;
    } else {
        truelen=(int) strlen(original);
    }

    destsize = 6*truelen+1;
    new=(char *)malloc(destsize);
    if(!new) return NULL;

    memset(new,0x00,destsize);

    s=original;
    d=new;

    while(s < (original+truelen)) {
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

void dispatch_stream_id(WS_CONNINFO *pwsc, int session, char *id) {
    MP3FILE *pmp3;
    int file_fd;
    int bytes_copied;
    off_t real_len;
    off_t file_len;
    off_t offset=0;
    long img_size;
    struct stat sb;
    int img_fd;
    int item;

    /* stream out the song */
    pwsc->close=1;

    item = atoi(id);

    if(ws_getrequestheader(pwsc,"range")) {
        offset=(off_t)atol(ws_getrequestheader(pwsc,"range") + 6);
    }

    /* FIXME: error handling */
    pmp3=db_fetch_item(NULL,item);
    if(!pmp3) {
        DPRINTF(E_LOG,L_DAAP|L_WS|L_DB,"Could not find requested item %lu\n",item);
        config_set_status(pwsc,session,NULL);
        ws_returnerror(pwsc,404,"File Not Found");
    } else if (plugin_ssc_should_transcode(pwsc,pmp3->codectype)) {
        /************************
         * Server side conversion
         ************************/
        config_set_status(pwsc,session,
                          "Transcoding '%s' (id %d)",
                          pmp3->title,pmp3->id);

        DPRINTF(E_WARN,L_WS,
                "Session %d: Streaming file '%s' to %s (offset %ld)\n",
                session,pmp3->fname, pwsc->hostname,(long)offset);

        bytes_copied =  plugin_ssc_transcode(pwsc,pmp3->path,pmp3->codectype,
                                             pmp3->song_length,offset,1);

        config_set_status(pwsc,session,NULL);
        db_dispose_item(pmp3);
    } else {
        /**********************
         * stream file normally
         **********************/
        if(pmp3->data_kind != 0) {
            ws_returnerror(pwsc,500,"Can't stream radio station");
            return;
        }
        file_fd=r_open2(pmp3->path,O_RDONLY);
        if(file_fd == -1) {
            pwsc->error=errno;
            DPRINTF(E_WARN,L_WS,"Thread %d: Error opening %s: %s\n",
                    pwsc->threadno,pmp3->path,strerror(errno));
            ws_returnerror(pwsc,404,"Not found");
            config_set_status(pwsc,session,NULL);
            db_dispose_item(pmp3);
        } else {
            real_len=lseek(file_fd,0,SEEK_END);
            lseek(file_fd,0,SEEK_SET);

            /* Re-adjust content length for cover art */
            if((conf_isset("general","art_filename")) &&
               ((img_fd=da_get_image_fd(pmp3->path)) != -1)) {
                fstat(img_fd, &sb);
                img_size = sb.st_size;
                r_close(img_fd);

                if (strncasecmp(pmp3->type,"mp3",4) ==0) {
                    /*PENDING*/
                } else if (strncasecmp(pmp3->type, "m4a", 4) == 0) {
                    real_len += img_size + 24;

                    if (offset > img_size + 24) {
                        offset -= img_size + 24;
                    }
                }
            }

            file_len = real_len - offset;

            DPRINTF(E_DBG,L_WS,"Thread %d: Length of file (remaining) is %ld\n",
                    pwsc->threadno,(long)file_len);

            // DWB:  fix content-type to correctly reflect data
            // content type (dmap tagged) should only be used on
            // dmap protocol requests, not the actually song data
            if(pmp3->type)
                ws_addresponseheader(pwsc,"Content-Type","audio/%s",pmp3->type);

            ws_addresponseheader(pwsc,"Content-Length","%ld",(long)file_len);
            ws_addresponseheader(pwsc,"Connection","Close");


            if(!offset)
                ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
            else {
                ws_addresponseheader(pwsc,"Content-Range","bytes %ld-%ld/%ld",
                                     (long)offset,(long)real_len,
                                     (long)real_len+1);
                ws_writefd(pwsc,"HTTP/1.1 206 Partial Content\r\n");
            }

            ws_emitheaders(pwsc);

            config_set_status(pwsc,session,"Streaming '%s' (id %d)",
                              pmp3->title, pmp3->id);
            DPRINTF(E_WARN,L_WS,"Session %d: Streaming file '%s' to %s (offset %d)\n",
                    session,pmp3->fname, pwsc->hostname,(long)offset);

            if(!offset)
                config.stats.songs_served++; /* FIXME: remove stat races */

            if((conf_isset("general","art_filename")) &&
               (!offset) &&
               ((img_fd=da_get_image_fd(pmp3->path)) != -1)) {
                if (strncasecmp(pmp3->type,"mp3",4) ==0) {
                    DPRINTF(E_INF,L_WS|L_ART,"Dynamic add artwork to %s (fd %d)\n",
                            pmp3->fname, img_fd);
                    da_attach_image(img_fd, pwsc->fd, file_fd, offset);
                } else if (strncasecmp(pmp3->type, "m4a", 4) == 0) {
                    DPRINTF(E_INF,L_WS|L_ART,"Dynamic add artwork to %s (fd %d)\n",
                            pmp3->fname, img_fd);
                    da_aac_attach_image(img_fd, pwsc->fd, file_fd, offset);
                }
            } else if(offset) {
                DPRINTF(E_INF,L_WS,"Seeking to offset %ld\n",(long)offset);
                lseek(file_fd,offset,SEEK_SET);
            }

            if((bytes_copied=copyfile(file_fd,pwsc->fd)) == -1) {
                DPRINTF(E_INF,L_WS,"Error copying file to remote... %s\n",
                        strerror(errno));
            } else {
                DPRINTF(E_INF,L_WS,"Finished streaming file to remote: %d bytes\n",
                        bytes_copied);
                /* update play counts */
                if(bytes_copied + 20 >= real_len) {
                    db_playcount_increment(NULL,pmp3->id);
                }
            }

            config_set_status(pwsc,session,NULL);
            r_close(file_fd);
            db_dispose_item(pmp3);
        }
    }

    //    free(pqi);
}

void dispatch_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    dispatch_stream_id(pwsc, pqi->session_id, pqi->uri_sections[3]);
}

/**
 * add songs to an existing playlist
 */
void dispatch_addplaylistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char playlist_response[20];
    unsigned char *current;
    char *tempstring;
    char *token;

    if(!ws_getvar(pwsc,"dmap.itemid")) {
        DPRINTF(E_LOG,L_DAAP,"Attempt to add playlist item w/o dmap.itemid\n");
        dispatch_error(pwsc,pqi,"MAPI","No item id specified (dmap.itemid)");
        return;
    }

    tempstring=strdup(ws_getvar(pwsc,"dmap.itemid"));
    current=(unsigned char*)tempstring;

    while((token=strsep((char**)(char*)&current,","))) {
        if(token) {
            /* FIXME:  error handling */
            db_add_playlist_item(NULL,pqi->playlist_id,atoi(token));
        }
    }

    free(tempstring);

    /* success(ish)... spool out a dmap block */
    current = playlist_response;
    current += db_dmap_add_container(current,"MAPI",12);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */

    dispatch_output_start(pwsc,pqi,20);
    dispatch_output_write(pwsc,pqi,playlist_response,20);
    dispatch_output_end(pwsc,pqi);

    pwsc->close=1;

    return;
}

/**
 * delete a playlist
 */
void dispatch_deleteplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char playlist_response[20];
    unsigned char *current;

    if(!ws_getvar(pwsc,"dmap.itemid")) {
        DPRINTF(E_LOG,L_DAAP,"Attempt to delete playlist w/o dmap.itemid\n");
        dispatch_error(pwsc,pqi,"MDPR","No playlist id specified");
        return;
    }

    /* FIXME: error handling */
    db_delete_playlist(NULL,atoi(ws_getvar(pwsc,"dmap.itemid")));

    /* success(ish)... spool out a dmap block */
    current = playlist_response;
    current += db_dmap_add_container(current,"MDPR",12);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */

    dispatch_output_start(pwsc,pqi,20);
    dispatch_output_write(pwsc,pqi,playlist_response,20);
    dispatch_output_end(pwsc,pqi);

    pwsc->close=1;

    return;
}

/**
 * delete a playlist item
 */
void dispatch_deleteplaylistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char playlist_response[20];
    unsigned char *current;
    char *tempstring;
    char *token;

    if(!ws_getvar(pwsc,"dmap.itemid")) {
        DPRINTF(E_LOG,L_DAAP,"Delete playlist item w/o dmap.itemid\n");
        dispatch_error(pwsc,pqi,"MDPI","No playlist item specified");
        return;
    }

    tempstring=strdup(ws_getvar(pwsc,"dmap.itemid"));
    current=(unsigned char *)tempstring;

    /* this looks strange, but gets rid of gcc 4 warnings */
    while((token=strsep((char**)(char*)&current,","))) {
        if(token) {
            /* FIXME: Error handling */
            db_delete_playlist_item(NULL,pqi->playlist_id,atoi(token));
        }
    }

    free(tempstring);

    /* success(ish)... spool out a dmap block */
    current = playlist_response;
    current += db_dmap_add_container(current,"MDPI",12);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */

    dispatch_output_start(pwsc,pqi,20);
    dispatch_output_write(pwsc,pqi,playlist_response,20);
    dispatch_output_end(pwsc,pqi);

    pwsc->close=1;

    return;
}

/**
 * add a playlist
 */
void dispatch_addplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char playlist_response[32];
    unsigned char *current=playlist_response;
    char *name, *query;
    int type;
    int retval, playlistid;
    char *estring = NULL;

    if((!ws_getvar(pwsc,"org.mt-daapd.playlist-type")) ||
       (!ws_getvar(pwsc,"dmap.itemname"))) {
        DPRINTF(E_LOG,L_DAAP,"attempt to add playlist with invalid type\n");
        dispatch_error(pwsc,pqi,"MAPR","bad playlist info specified");
        return;
    }

    type=atoi(ws_getvar(pwsc,"org.mt-daapd.playlist-type"));
    name=ws_getvar(pwsc,"dmap.itemname");
    query=ws_getvar(pwsc,"org.mt-daapd.smart-playlist-spec");

    retval=db_add_playlist(&estring,name,type,query,NULL,0,&playlistid);
    if(retval != DB_E_SUCCESS) {
        dispatch_error(pwsc,pqi,"MAPR",estring);
        DPRINTF(E_LOG,L_DAAP,"error adding playlist %s: %s\n",name,estring);
        free(estring);
        return;
    }

    /* success... spool out a dmap block */
    current += db_dmap_add_container(current,"MAPR",24);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_int(current,"miid",playlistid);  /* 12 */

    dispatch_output_start(pwsc,pqi,32);
    dispatch_output_write(pwsc,pqi,playlist_response,32);
    dispatch_output_end(pwsc,pqi);

    pwsc->close=1;
    return;
}

/**
 * edit an existing playlist (by id)
 */
void dispatch_editplaylist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char edit_response[20];
    unsigned char *current = edit_response;
    char *pe = NULL;
    char *name, *query;
    int id;

    int retval;

    if(!ws_getvar(pwsc,"dmap.itemid")) {
        DPRINTF(E_LOG,L_DAAP,"Missing itemid on playlist edit");
        dispatch_error(pwsc,pqi,"MEPR","No itemid specified");
        return;
    }

    name=ws_getvar(pwsc,"dmap.itemname");
    query=ws_getvar(pwsc,"org.mt-daapd.smart-playlist-spec");
    id=atoi(ws_getvar(pwsc,"dmap.itemid"));

    /* FIXME: Error handling */
    retval=db_edit_playlist(&pe,id,name,query);
    if(retval != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"error editing playlist.\n");
        dispatch_error(pwsc,pqi,"MEPR",pe);
        if(pe) free(pe);
        return;
    }

    current += db_dmap_add_container(current,"MEPR",12);
    current += db_dmap_add_int(current,"mstt",200);      /* 12 */

    dispatch_output_start(pwsc,pqi,20);
    dispatch_output_write(pwsc,pqi,edit_response,20);
    dispatch_output_end(pwsc,pqi);

    pwsc->close=1;
    return;
}


/**
 * enumerate and return playlistitems
 */
void dispatch_playlistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char items_response[61];
    unsigned char *current=items_response;
    int song_count;
    int list_length;
    unsigned char *block;
    char *pe = NULL;
    int mtco;

    if(ws_getvar(pwsc,"meta")) {
        pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
        pqi->meta = ((1ll << metaItemId) |
                     (1ll << metaItemName) |
                     (1ll << metaItemKind) |
                     (1ll << metaContainerItemId) |
                     (1ll << metaParentContainerId));
    }

    pqi->query_type = queryTypePlaylistItems;

    if(db_enum_start(&pe,pqi) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"Could not start enum: %s\n",pe);
        dispatch_error(pwsc,pqi,"apso",pe);
        if(pe) free(pe);
        return;
    }

    if(db_enum_size(&pe,pqi,&song_count,&list_length) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"Could not enum size: %s\n",pe);
        dispatch_error(pwsc,pqi,"apso",pe);
        if(pe) free(pe);
        return;
    }

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d songs, dmap size: %d\n",song_count,list_length);

    mtco = song_count;
    if(pqi->index_type != indexTypeNone)
        mtco = pqi->specifiedtotalcount;

    current += db_dmap_add_container(current,"apso",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",mtco);        /* 12 */
    current += db_dmap_add_int(current,"mrco",song_count);  /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);

    dispatch_output_start(pwsc,pqi,61+list_length);
    dispatch_output_write(pwsc,pqi,items_response,61);

    /* FIXME: Error checking */
    while((db_enum_fetch(NULL,pqi,&list_length,&block) == DB_E_SUCCESS) &&
          (list_length)) {
        DPRINTF(E_SPAM,L_DAAP,"Got block of size %d\n",list_length);
        dispatch_output_write(pwsc,pqi,block,list_length);
        free(block);
    }

    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");

    db_enum_end(NULL);

    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char browse_response[52];
    unsigned char *current=browse_response;
    int item_count;
    int list_length;
    unsigned char *block;
    char *response_type;
    int which_field=5;
    char *pe = NULL;
    int mtco;

    if(strcasecmp(pqi->uri_sections[2],"browse") == 0) {
        which_field = 3;
    }

    if(!strcmp(pqi->uri_sections[which_field],"artists")) {
        response_type = "abar";
        pqi->query_type=queryTypeBrowseArtists;
    } else if(!strcmp(pqi->uri_sections[which_field],"genres")) {
        response_type = "abgn";
        pqi->query_type=queryTypeBrowseGenres;
    } else if(!strcmp(pqi->uri_sections[which_field],"albums")) {
        response_type = "abal";
        pqi->query_type=queryTypeBrowseAlbums;
    } else if(!strcmp(pqi->uri_sections[which_field],"composers")) {
        response_type = "abcp";
        pqi->query_type=queryTypeBrowseComposers;
    } else {
        DPRINTF(E_WARN,L_DAAP|L_BROW,"Invalid browse request type %s\n",pqi->uri_sections[3]);
        dispatch_error(pwsc,pqi,"abro","Invalid browse type");
        config_set_status(pwsc,pqi->session_id,NULL);
        return;
    }

    if(db_enum_start(&pe,pqi) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP|L_BROW,"Could not start enum: %s\n",pe);
        dispatch_error(pwsc,pqi,"abro",pe);
        if(pe) free(pe);
        return;
    }

    DPRINTF(E_DBG,L_DAAP|L_BROW,"Getting enum size.\n");

    /* FIXME: Error handling */
    db_enum_size(NULL,pqi,&item_count,&list_length);

    DPRINTF(E_DBG,L_DAAP|L_BROW,"Item enum: got %d items, dmap size: %d\n",
            item_count,list_length);

    mtco = item_count;
    if(pqi->index_type != indexTypeNone)
        mtco = pqi->specifiedtotalcount;

    current += db_dmap_add_container(current,"abro",list_length + 44);
    current += db_dmap_add_int(current,"mstt",200);                    /* 12 */
    current += db_dmap_add_int(current,"mtco",mtco);                   /* 12 */
    current += db_dmap_add_int(current,"mrco",item_count);             /* 12 */
    current += db_dmap_add_container(current,response_type,list_length); /* 8+ */

    dispatch_output_start(pwsc,pqi,52+list_length);
    dispatch_output_write(pwsc,pqi,browse_response,52);

    while((db_enum_fetch(NULL,pqi,&list_length,&block) == DB_E_SUCCESS) &&
          (list_length))
    {
        DPRINTF(E_SPAM,L_DAAP|L_BROW,"Got block of size %d\n",list_length);
        dispatch_output_write(pwsc,pqi,block,list_length);
        free(block);
    }

    DPRINTF(E_DBG,L_DAAP|L_BROW,"Done enumerating\n");

    db_enum_end(NULL);

    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_playlists(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char playlist_response[61];
    unsigned char *current=playlist_response;
    int pl_count;
    int list_length;
    unsigned char *block;
    char *pe = NULL;
    int mtco;

    /* currently, this is ignored for playlist queries */
    if(ws_getvar(pwsc,"meta")) {
        pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
        pqi->meta = ((1ll << metaItemId) |
                     (1ll << metaItemName) |
                     (1ll << metaPersistentId) |
                     (1ll << metaItunesSmartPlaylist));
    }


    pqi->query_type = queryTypePlaylists;

    if(db_enum_start(&pe,pqi) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"Could not start enum: %s\n",pe);
        dispatch_error(pwsc,pqi,"aply",pe);
        if(pe) free(pe);
        return;
    }

    if(db_enum_size(NULL,pqi,&pl_count,&list_length) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"error in enumerating size: %s\n",pe);
        dispatch_error(pwsc,pqi,"aply",pe);
        if(pe) free(pe);
        return;
    }

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d playlists, dmap size: %d\n",pl_count,list_length);

    mtco = pl_count;
    if(pqi->index_type != indexTypeNone)
        mtco = pqi->specifiedtotalcount;

    current += db_dmap_add_container(current,"aply",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",mtco);        /* 12 */
    current += db_dmap_add_int(current,"mrco",pl_count);    /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);

    dispatch_output_start(pwsc,pqi,61+list_length);
    dispatch_output_write(pwsc,pqi,playlist_response,61);

    /* FIXME: error checking */
    while((db_enum_fetch(NULL,pqi,&list_length,&block) == DB_E_SUCCESS) &&
          (list_length))
    {
        DPRINTF(E_SPAM,L_DAAP,"Got block of size %d\n",list_length);
        dispatch_output_write(pwsc,pqi,block,list_length);
        free(block);
    }

    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");

    db_enum_end(NULL);

    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_items(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char items_response[61];
    unsigned char *current=items_response;
    int song_count;
    int list_length;
    unsigned char *block;
    char *pe = NULL;
    int mtco;

    if(ws_getvar(pwsc,"meta")) {
        pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
        pqi->meta = (MetaField_t) -1ll;
    }

    pqi->query_type = queryTypeItems;

    if(db_enum_start(&pe,pqi)) {
        DPRINTF(E_LOG,L_DAAP,"Could not start enum: %s\n",pe);
        dispatch_error(pwsc,pqi,"adbs",pe);
        if(pe) free(pe);
        return;
    }

    /* FIXME: Error handling */
    if(db_enum_size(&pe,pqi,&song_count,&list_length) != DB_E_SUCCESS) {
        DPRINTF(E_LOG,L_DAAP,"Error getting dmap size: %s\n",pe);
        dispatch_error(pwsc,pqi,"adbs",pe);
        if(pe) free(pe);
        return;
    }

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d songs, dmap size: %d\n",song_count,list_length);

    mtco = song_count;
    if(pqi->index_type != indexTypeNone)
        mtco = pqi->specifiedtotalcount;

    current += db_dmap_add_container(current,"adbs",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",mtco);        /* 12 */
    current += db_dmap_add_int(current,"mrco",song_count);  /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);

    dispatch_output_start(pwsc,pqi,61+list_length);
    dispatch_output_write(pwsc,pqi,items_response,61);

    /* FIXME: check errors */
    while((db_enum_fetch(NULL,pqi,&list_length,&block) == DB_E_SUCCESS) &&
          (list_length)) {
        DPRINTF(E_SPAM,L_DAAP,"Got block of size %d\n",list_length);
        dispatch_output_write(pwsc,pqi,block,list_length);
        free(block);
    }
    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");
    db_enum_end(NULL);
    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_update(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char update_response[32];
    unsigned char *current=update_response;
    int clientver=1;
    fd_set rset;
    struct timeval tv;
    int result;
    int lastver=0;

    DPRINTF(E_DBG,L_DAAP,"Preparing to send update response\n");
    config_set_status(pwsc,pqi->session_id,"Waiting for DB update");

    if(ws_getvar(pwsc,"revision-number")) {
        clientver=atoi(ws_getvar(pwsc,"revision-number"));
    }

    /* wait for db_version to be stable for 30 seconds */
    while((clientver == db_revision()) || 
          (lastver && (db_revision() != lastver))) {
	lastver = db_revision();

        FD_ZERO(&rset);
        FD_SET(pwsc->fd,&rset);

        tv.tv_sec=30;
        tv.tv_usec=0;

        result=select(pwsc->fd+1,&rset,NULL,NULL,&tv);
        if(FD_ISSET(pwsc->fd,&rset)) {
            /* can't be ready for read, must be error */
            DPRINTF(E_DBG,L_DAAP,"Update session stopped\n");
            return;
        }
    }

    /* otherwise, send the info about this version */
    current += db_dmap_add_container(current,"mupd",24);
    current += db_dmap_add_int(current,"mstt",200);       /* 12 */
    current += db_dmap_add_int(current,"musr",db_revision());   /* 12 */

    dispatch_output_start(pwsc,pqi,32);
    dispatch_output_write(pwsc,pqi,update_response,32);
    dispatch_output_end(pwsc,pqi);

    return;
}

void dispatch_dbinfo(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char dbinfo_response[255];  /* FIXME: servername limit 255-113 */
    unsigned char *current = dbinfo_response;
    int namelen;
    int count;
    char *servername;

    servername = conf_get_servername();
    namelen=(int) strlen(servername);

    current += db_dmap_add_container(current,"avdb",105 + namelen);
    current += db_dmap_add_int(current,"mstt",200);                    /* 12 */
    current += db_dmap_add_char(current,"muty",0);                     /*  9 */
    current += db_dmap_add_int(current,"mtco",1);                      /* 12 */
    current += db_dmap_add_int(current,"mrco",1);                      /* 12 */
    current += db_dmap_add_container(current,"mlcl",52 + namelen);
    current += db_dmap_add_container(current,"mlit",44 + namelen);
    current += db_dmap_add_int(current,"miid",1);                      /* 12 */
    current += db_dmap_add_string(current,"minm",servername); /* 8 + namelen */
    db_get_song_count(NULL,&count);
    current += db_dmap_add_int(current,"mimc",count);                  /* 12 */
    db_get_playlist_count(NULL,&count);
    current += db_dmap_add_int(current,"mctc",count);                  /* 12 */

    dispatch_output_start(pwsc,pqi,113+namelen);
    dispatch_output_write(pwsc,pqi,dbinfo_response,113+namelen);
    dispatch_output_end(pwsc,pqi);

    free(servername);
    return;
}

void dispatch_logout(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    config_set_status(pwsc,pqi->session_id,NULL);
    ws_returnerror(pwsc,204,"Logout Successful");
}


void dispatch_login(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char login_response[32];
    unsigned char *current = login_response;
    int session;

    session = config_get_next_session();

    current += db_dmap_add_container(current,"mlog",24);
    current += db_dmap_add_int(current,"mstt",200);       /* 12 */
    current += db_dmap_add_int(current,"mlid",session);   /* 12 */

    dispatch_output_start(pwsc,pqi,32);
    dispatch_output_write(pwsc,pqi,login_response,32);
    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_content_codes(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char content_codes[20];
    unsigned char *current=content_codes;
    unsigned char mdcl[256];  /* FIXME: Don't make this static */
    int len;
    DAAP_ITEMS *dicurrent;

    dicurrent=taglist;
    len=0;
    while(dicurrent->type) {
        len += (8 + 12 + 10 + 8 + (int) strlen(dicurrent->description));
        dicurrent++;
    }

    current += db_dmap_add_container(current,"mccr",len + 12);
    current += db_dmap_add_int(current,"mstt",200);

    dispatch_output_start(pwsc,pqi,len+20);
    dispatch_output_write(pwsc,pqi,content_codes,20);

    dicurrent=taglist;
    while(dicurrent->type) {
        current=mdcl;
        len = 12 + 10 + 8 + (int) strlen(dicurrent->description);
        current += db_dmap_add_container(current,"mdcl",len);
        current += db_dmap_add_string(current,"mcnm",dicurrent->tag);         /* 12 */
        current += db_dmap_add_string(current,"mcna",dicurrent->description); /* 8 + descr */
        current += db_dmap_add_short(current,"mcty",dicurrent->type);         /* 10 */
        dispatch_output_write(pwsc,pqi,mdcl,len+8);
        dicurrent++;
    }

    dispatch_output_end(pwsc,pqi);
    return;
}

void dispatch_server_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    unsigned char server_info[256];
    unsigned char *current = server_info;
    char *client_version;
    int mpro = 2 << 16;
    int apro = 3 << 16;
    char *servername;
    int actual_length;
    int supports_update=0;

    servername = conf_get_servername();
    //    supports_update = conf_get_int("daap","supports_update",1);

    actual_length=130 + (int) strlen(servername);
    if(!supports_update)
        actual_length -= 9;

    if(actual_length > sizeof(server_info)) {
        DPRINTF(E_FATAL,L_DAAP,"Server name too long.\n");
    }

    client_version=ws_getrequestheader(pwsc,"Client-DAAP-Version");

    current += db_dmap_add_container(current,"msrv",actual_length - 8);
    current += db_dmap_add_int(current,"mstt",200);        /* 12 */

    if((client_version) && (!strcmp(client_version,"1.0"))) {
        mpro = 1 << 16;
        apro = 1 << 16;
    }

    if((client_version) && (!strcmp(client_version,"2.0"))) {
        mpro = 1 << 16;
        apro = 2 << 16;
    }

    current += db_dmap_add_int(current,"mpro",mpro);       /* 12 */
    current += db_dmap_add_int(current,"apro",apro);       /* 12 */
    current += db_dmap_add_int(current,"mstm",1800);       /* 12 */
    current += db_dmap_add_string(current,"minm",servername); /* 8 + strlen(name) */

    current += db_dmap_add_char(current,"msau",            /* 9 */
                                conf_isset("general","password") ? 2 : 0);
    current += db_dmap_add_char(current,"msex",0);         /* 9 */
    current += db_dmap_add_char(current,"msix",0);         /* 9 */
    current += db_dmap_add_char(current,"msbr",0);         /* 9 */
    current += db_dmap_add_char(current,"msqy",0);         /* 9 */
    current += db_dmap_add_int(current,"msdc",1);          /* 12 */

    if(supports_update)
        current += db_dmap_add_char(current,"msup",0);         /* 9 */

    dispatch_output_start(pwsc,pqi,actual_length);
    dispatch_output_write(pwsc,pqi,server_info,actual_length);
    dispatch_output_end(pwsc,pqi);

    free(servername);
    return;
}

/**
 * throw out an error, xml style.  This throws  out a dmap block, but with a
 * mstt of 500, and a msts as specified
 */
void dispatch_error(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *container, char *error) {
    unsigned char *block, *current;
    int len;

    len = 12 + 8 + 8 + (int) strlen(error);
    block = (unsigned char *)malloc(len);

    if(!block)
        DPRINTF(E_FATAL,L_DAAP,"Malloc error\n");

    current = block;
    current += db_dmap_add_container(current,container,len - 8);
    current += db_dmap_add_int(current,"mstt",500);
    current += db_dmap_add_string(current,"msts",error);

    dispatch_output_start(pwsc,pqi,len);
    dispatch_output_write(pwsc,pqi,block,len);
    dispatch_output_end(pwsc,pqi);

    free(block);

    pwsc->close=1;
}



