/*
 * $Id: $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"
#include "ff-plugins.h"
#include "rsp.h"
#include "xml-rpc.h"

typedef struct tag_rsp_privinfo {
    DB_QUERY dq;
    int uri_count;
    char *uri_sections[10];
} PRIVINFO;

/* Forwards */
PLUGIN_INFO *plugin_info(PLUGIN_INPUT_FN *);
void plugin_handler(WS_CONNINFO *pwsc);
int plugin_auth(WS_CONNINFO *pwsc, char *username, char *password);
void rsp_info(WS_CONNINFO *pwsc, PRIVINFO *ppi);
void rsp_db(WS_CONNINFO *pwsc, PRIVINFO *ppi);
void rsp_playlist(WS_CONNINFO *pwsc, PRIVINFO *ppi);
void rsp_browse(WS_CONNINFO *pwsc, PRIVINFO *ppi);
void rsp_stream(WS_CONNINFO *pwsc, PRIVINFO *ppi);
void rsp_error(WS_CONNINFO *pwsc, PRIVINFO *ppi, int eno, char *estr);

/* Globals */
PLUGIN_OUTPUT_FN _pofn = { plugin_handler, plugin_auth };
PLUGIN_REND_INFO _pri[] = {
    { "_rsp._tcp", NULL },
    { NULL, NULL }
};

PLUGIN_INPUT_FN *_ppi;

PLUGIN_INFO _pi = { 
    PLUGIN_VERSION,      /* version */
    PLUGIN_OUTPUT,       /* type */
    "rsp/" VERSION,      /* server */
    "/rsp/.*",           /* url */
    &_pofn,              /* output fns */
    NULL,                /* event fns */
    NULL,                /* transcode fns */
    _pri,                /* rend info */
    NULL                 /* transcode info */
};

typedef struct tag_response {
    char *uri[10];
    void (*dispatch)(WS_CONNINFO *, PRIVINFO *);
} PLUGIN_RESPONSE;
    

PLUGIN_RESPONSE rsp_uri_map[] = {
    {{"rsp",  "info",NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL }, rsp_info },
    {{"rsp",  "db"  ,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL }, rsp_db },
    {{"rsp",  "db"  , "*",NULL,NULL,NULL,NULL,NULL,NULL,NULL }, rsp_playlist },
    {{"rsp",  "db"  , "*", "*",NULL,NULL,NULL,NULL,NULL,NULL }, rsp_browse },
    {{"rsp","stream", "*",NULL,NULL,NULL,NULL,NULL,NULL,NULL }, rsp_stream }
};

#define E_RSP    0x0000
#define E_DB     0x1000


#define T_STRING 0
#define T_INT    1
#define T_DATE   2

#define F_FULL     1
#define F_BROWSE   2
#define F_ID       4
#define F_DETAILED 8

typedef struct tag_fieldspec {
    char *name;
    int flags; /* 1: full, 2: browse, 4: id */
    int type;  /* 0: string, 1: int, 2: date */
} FIELDSPEC;

FIELDSPEC rsp_playlist_fields[] = {
    { "id"           , 15, T_INT    },
    { "title"        , 11, T_STRING },
    { "type"         , 8, T_INT    },
    { "items"        , 11, T_INT    },
    { "query"        , 8, T_STRING },
    { "db_timestamp" , 8, T_DATE   },
    { "path"         , 8, T_STRING },
    { "index"        , 8, T_INT    },
    { NULL           , 0, 0        }
};

FIELDSPEC rsp_fields[] = {
    { "id"           , 15, T_INT    },
    { "path"         , 8, T_STRING },
    { "fname"        , 8, T_STRING },
    { "title"        , 15, T_STRING },
    { "artist"       , 11, T_STRING },
    { "album"        , 11, T_STRING },
    { "genre"        , 9, T_STRING },
    { "comment"      , 9, T_STRING },
    { "type"         , 9, T_STRING },
    { "composer"     , 9, T_STRING },
    { "orchestra"    , 9, T_STRING },
    { "conductor"    , 9, T_STRING },
    { "grouping"     , 0, T_STRING },
    { "url"          , 9, T_STRING },
    { "bitrate"      , 9, T_INT    },
    { "samplerate"   , 9, T_INT    },
    { "song_length"  , 9, T_INT    },
    { "file_size"    , 9, T_INT    },
    { "year"         , 9, T_INT    },
    { "track"        , 11, T_INT    },
    { "total_tracks" , 9, T_INT    },
    { "disc"         , 11, T_INT    },
    { "total_discs"  , 9, T_INT    },
    { "bpm"          , 9, T_INT    },
    { "compilation"  , 9, T_INT    },
    { "rating"       , 9, T_INT    },
    { "play_count"   , 9, T_INT    },
    { "data_kind"    , 8, T_INT    },
    { "item_kind"    , 8, T_INT    },
    { "description"  , 9, T_STRING },
    { "time_added"   , 9, T_DATE   },
    { "time_modified", 9, T_DATE   },
    { "time_played"  , 9, T_DATE   },
    { "db_timestamp" , 8, T_DATE   },
    { "disabled"     , 9, T_INT    },
    { "sample_count" , 8, T_INT    },
    { "force_update" , 8, T_INT    },
    { "codectype"    , 15, T_INT    },
    { "idx"          , 8, T_INT    },
    { "has_video"    , 8, T_INT    },
    { "contentrating", 8, T_INT    },
    { NULL           , 0 }
};

/**
 * return info about this plugin module
 */
PLUGIN_INFO *plugin_info(PLUGIN_INPUT_FN *ppi) {
    _ppi = ppi;
    return &_pi;
}


/**
 * check for auth.  Kind of a ham-handed implementation, but
 * works.
 */
int plugin_auth(WS_CONNINFO *pwsc, char *username, char *password) {
    char *readpassword;

    readpassword = _ppi->conf_alloc_string("general","password",NULL);
    if(password == NULL) { /* testing to see if we need a pw */
        if((readpassword == NULL) || (strlen(readpassword)==0)) {
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
    return TRUE; /* ?? */
}

/**
 * dispatch handler for web stuff
 */
void plugin_handler(WS_CONNINFO *pwsc) {
    char *string, *save, *token;
    PRIVINFO *ppi;
    int elements;
    int index, part;
    int found;

    string = _ppi->ws_uri(pwsc);
    string++;
    
    ppi = (PRIVINFO *)malloc(sizeof(PRIVINFO));
    if(ppi) {
        memset(ppi,0,sizeof(PRIVINFO));
    }

    if(!ppi) {
        _ppi->ws_returnerror(pwsc,500,"Malloc error in plugin_handler");
        return;
    }

    memset((void*)&ppi->dq,0,sizeof(DB_QUERY));

    _ppi->log(E_DBG,"Tokenizing url\n");
    while((ppi->uri_count < 10) && (token=strtok_r(string,"/",&save))) {
        string=NULL;
        ppi->uri_sections[ppi->uri_count++] = token;
    }

    elements = sizeof(rsp_uri_map) / sizeof(PLUGIN_RESPONSE);
    _ppi->log(E_DBG,"Found %d elements\n",elements);

    index = 0;
    found = 0;

    while((!found) && (index < elements)) {
        /* test this set */
        _ppi->log(E_DBG,"Checking reponse %d\n",index);
        part=0;
        while(part < 10) {
            if((rsp_uri_map[index].uri[part]) && (!ppi->uri_sections[part]))
                break;
            if((ppi->uri_sections[part]) && (!rsp_uri_map[index].uri[part]))
                break;

            if((rsp_uri_map[index].uri[part]) && 
               (strcmp(rsp_uri_map[index].uri[part],"*") != 0)) {
                if(strcmp(rsp_uri_map[index].uri[part],
                          ppi->uri_sections[part])!= 0)
                    break;
            }
            part++;
        }

        if(part == 10) {
            found = 1;
            _ppi->log(E_DBG,"Found it! Index: %d\n",index);
        } else {
            index++;
        }
    }

    if(found) {
        rsp_uri_map[index].dispatch(pwsc, ppi);
        _ppi->ws_close(pwsc);
        free(ppi);
        return;
    }

    rsp_error(pwsc, ppi, 1, "Bad path");
    _ppi->ws_close(pwsc);
    free(ppi);
    return;
}

/**
 * get server info 
 */
void rsp_info(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    XMLSTRUCT *pxml;
    char servername[256];
    int size;

    _ppi->log(E_DBG,"Starting rsp_info\n");

    pxml = xml_init(pwsc,1);

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","0");
    xml_output(pxml,"totalrecords","0");
    xml_pop(pxml); /* status */

    /* info block */
    xml_push(pxml,"info");
    xml_output(pxml,"count","%d",_ppi->db_count());
    xml_output(pxml,"rsp-version","%s",RSP_VERSION);

    xml_output(pxml,"server-version","%s",_ppi->server_ver());

    size = sizeof(servername);
    _ppi->server_name(servername,&size);
    xml_output(pxml,"name","%s",servername);
    xml_pop(pxml); /* info */

    xml_pop(pxml); /* response */
    xml_deinit(pxml);
}

/**
 * /rsp/db
 *
 * dump details about all playlists 
 */
void rsp_db(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int rowindex;

    ppi->dq.query_type = QUERY_TYPE_PLAYLISTS;

    if((err=_ppi->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        _ppi->db_enum_dispose(NULL,&ppi->dq);
        return;
    }

    pxml = xml_init(pwsc,1);

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","%d",ppi->dq.totalcount);
    xml_output(pxml,"totalrecords","%d",ppi->dq.totalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"playlists");

    while((_ppi->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
        xml_push(pxml,"playlist");
        rowindex=0;
        while(rsp_playlist_fields[rowindex].name) {
            if(rsp_playlist_fields[rowindex].flags & F_FULL) {
                xml_output(pxml,rsp_playlist_fields[rowindex].name,"%s",
                              row[rowindex]);
            }
            rowindex++;
        }
        xml_pop(pxml); /* playlist */
    }

    _ppi->db_enum_end(NULL);
    _ppi->db_enum_dispose(NULL,&ppi->dq);

    xml_pop(pxml); /* playlists */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);

}

/**
 * get all items under the playlist
 */
void rsp_playlist(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int rowindex;
    int returned;
    char *browse_type;
    int type;
    int transcode;
    int samplerate;

    ppi->dq.filter = _ppi->ws_getvar(pwsc,"query");
    ppi->dq.filter_type = FILTER_TYPE_FIREFLY;

    if(_ppi->ws_getvar(pwsc,"offset")) {
        ppi->dq.offset = atoi(_ppi->ws_getvar(pwsc,"offset"));
    }
    if(_ppi->ws_getvar(pwsc,"limit")) {
        ppi->dq.limit = atoi(_ppi->ws_getvar(pwsc,"limit"));
    }
    
    browse_type = _ppi->ws_getvar(pwsc,"type");
    type = F_FULL;

    if(browse_type) {
        if(strcasecmp(browse_type,"browse") == 0) {
            type = F_BROWSE;
        } else if(strcasecmp(browse_type,"id") == 0) {
            type = F_ID;
        } else if(strcasecmp(browse_type,"detailed") ==0) {
            type = F_DETAILED;
        }
    }

    ppi->dq.query_type = QUERY_TYPE_ITEMS;
    ppi->dq.playlist_id = atoi(ppi->uri_sections[2]);

    if((err=_ppi->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        _ppi->db_enum_dispose(NULL,&ppi->dq);
        free(pe);
        return;
    }

    pxml = xml_init(pwsc,1);

    if(ppi->dq.offset > ppi->dq.totalcount) {
        returned = 0;
    } else {
        returned = ppi->dq.limit;
        if(returned > (ppi->dq.totalcount - ppi->dq.offset)) 
            returned = ppi->dq.totalcount - ppi->dq.offset;
    }

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","%d",returned);
    xml_output(pxml,"totalrecords","%d",ppi->dq.totalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"items");

    while((_ppi->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
        xml_push(pxml,"item");
        rowindex=0;
        transcode = 0;

        transcode = _ppi->should_transcode(pwsc,row[37]);

        _ppi->log(E_DBG,"Transcode: %d, %s: %s\n",transcode,row[37],row[2]);

        while(rsp_fields[rowindex].name) {
            if((rsp_fields[rowindex].flags & type) &&
               (row[rowindex] && strlen(row[rowindex]))) {
                if(transcode) {
                    switch(rowindex) {
                    case 8:
                        xml_output(pxml,rsp_fields[rowindex].name,"%s","wav");
                        break;
                    case 29:
                        xml_output(pxml,rsp_fields[rowindex].name,"%s",
                                   "wav audio file");
                        break;
                    case 15:
                        samplerate = atoi(row[15]);
                        if(samplerate) {
                            samplerate = (samplerate * 4 * 8)/1000;
                        }
                        xml_output(pxml,rsp_fields[rowindex].name,"%d",
                                   samplerate);
                        break;
                    case 37:
                        xml_output(pxml,rsp_fields[rowindex].name,"%s","wav");
                        xml_output(pxml,"original_codec","%s",row[37]);
                        break;
                    default:
                        xml_output(pxml,rsp_fields[rowindex].name,"%s",
                                   row[rowindex]);
                        break;
                    }
                } else {
                    xml_output(pxml,rsp_fields[rowindex].name,"%s",
                               row[rowindex]);
                }

            }
            rowindex++;
        }
        xml_pop(pxml); /* item */
    }

    _ppi->db_enum_end(NULL);

    xml_pop(pxml); /* items */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);
}

void rsp_browse(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int returned;

    /* this might fail if an unsupported browse type */
    ppi->dq.query_type = QUERY_TYPE_DISTINCT;
    ppi->dq.distinct_field = ppi->uri_sections[3];
    ppi->dq.filter = _ppi->ws_getvar(pwsc,"query");
    ppi->dq.filter_type = FILTER_TYPE_FIREFLY;

    if(_ppi->ws_getvar(pwsc,"offset")) {
        ppi->dq.offset = atoi(_ppi->ws_getvar(pwsc,"offset"));
    }
    
    if(_ppi->ws_getvar(pwsc,"limit")) {
        ppi->dq.limit = atoi(_ppi->ws_getvar(pwsc,"limit"));
    }

    ppi->dq.playlist_id = atoi(ppi->uri_sections[2]);

    if((err=_ppi->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        _ppi->db_enum_dispose(NULL,&ppi->dq);
        return;
    }

    pxml = xml_init(pwsc,1);

    if(ppi->dq.offset > ppi->dq.totalcount) {
        returned = 0;
    } else {
        returned = ppi->dq.limit;
        if(returned > (ppi->dq.totalcount - ppi->dq.offset)) 
            returned = ppi->dq.totalcount - ppi->dq.offset;
    }

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","%d",returned);
    xml_output(pxml,"totalrecords","%d",ppi->dq.totalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"items");

    while((_ppi->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
        xml_output(pxml,"item","%s",row[0]);
    }

    _ppi->db_enum_end(NULL);
    _ppi->db_enum_dispose(NULL,&ppi->dq);

    xml_pop(pxml); /* items */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);
}

void rsp_stream(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    _ppi->stream(pwsc, ppi->uri_sections[2]);
    return;
}

void rsp_error(WS_CONNINFO *pwsc, PRIVINFO *ppi, int eno, char *estr) {
    XMLSTRUCT *pxml;

    pxml = xml_init(pwsc, 1);
    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","%d",eno);
    xml_output(pxml,"errorstring","%s",estr);
    xml_output(pxml,"records","0");
    xml_output(pxml,"totalrecords","0");
    xml_pop(pxml); /* status */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);
    _ppi->ws_close(pwsc);
}

