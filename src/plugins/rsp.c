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
PLUGIN_INFO *plugin_info(void);
void plugin_handler(WS_CONNINFO *pwsc);
int plugin_auth(WS_CONNINFO *pwsc, char *username, char *pw);
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

PLUGIN_INFO _pi = { 
    PLUGIN_VERSION,      /* version */
    PLUGIN_OUTPUT,       /* type */
    "rsp/" VERSION,      /* server */
    "/rsp/.*",           /* url */
    &_pofn,              /* output fns */
    NULL,                /* event fns */
    NULL,                /* transcode fns */
    NULL,                /* ff functions */
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

#define F_FULL   1
#define F_BROWSE 2
#define F_ID     4


typedef struct tag_fieldspec {
    char *name;
    int flags; /* 1: full, 2: browse, 4: id */
    int type;  /* 0: string, 1: int, 2: date */
} FIELDSPEC;

FIELDSPEC rsp_playlist_fields[] = {
    { "id"           , 7, T_INT    },
    { "title"        , 3, T_STRING },
    { "type"         , 0, T_INT    },
    { "items"        , 3, T_INT    },
    { "query"        , 0, T_STRING },
    { "db_timestamp" , 0, T_DATE   },
    { "path"         , 0, T_STRING },
    { "index"        , 0, T_INT    },
    { NULL           , 0, 0        }
};

FIELDSPEC rsp_fields[] = {
    { "id"           , 7, T_INT    },
    { "path"         , 0, T_STRING },
    { "fname"        , 0, T_STRING },
    { "title"        , 7, T_STRING },
    { "artist"       , 3, T_STRING },
    { "album"        , 3, T_STRING },
    { "genre"        , 1, T_STRING },
    { "comment"      , 0, T_STRING },
    { "type"         , 1, T_STRING },
    { "composer"     , 1, T_STRING },
    { "orchestra"    , 1, T_STRING },
    { "conductor"    , 1, T_STRING },
    { "grouping"     , 0, T_STRING },
    { "url"          , 1, T_STRING },
    { "bitrate"      , 1, T_INT    },
    { "samplerate"   , 1, T_INT    },
    { "song_length"  , 1, T_INT    },
    { "file_size"    , 1, T_INT    },
    { "year"         , 1, T_INT    },
    { "track"        , 3, T_INT    },
    { "total_tracks" , 1, T_INT    },
    { "disc"         , 3, T_INT    },
    { "total_discs"  , 1, T_INT    },
    { "bpm"          , 1, T_INT    },
    { "compilation"  , 1, T_INT    },
    { "rating"       , 1, T_INT    },
    { "play_count"   , 1, T_INT    },
    { "data_kind"    , 0, T_INT    },
    { "item_kind"    , 0, T_INT    },
    { "description"  , 1, T_STRING },
    { "time_added"   , 1, T_DATE   },
    { "time_modified", 1, T_DATE   },
    { "time_played"  , 1, T_DATE   },
    { "db_timestamp" , 0, T_DATE   },
    { "disabled"     , 1, T_INT    },
    { "sample_count" , 0, T_INT    },
    { "force_update" , 0, T_INT    },
    { "codectype"    , 7, T_INT    },
    { "idx"          , 0, T_INT    },
    { "has_video"    , 0, T_INT    },
    { "contentrating", 0, T_INT    },
    { NULL           , 0 }
};

/**
 * return info about this plugin module
 */
PLUGIN_INFO *plugin_info(void) {
    return &_pi;
}


/**
 * check for auth
 */
int plugin_auth(WS_CONNINFO *pwsc, char *username, char *pw) {
    /* disable passwords for now */
    return 1;
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

    string = infn->ws_uri(pwsc);
    string++;
    
    ppi = (PRIVINFO *)malloc(sizeof(PRIVINFO));
    if(ppi) {
        memset(ppi,0,sizeof(PRIVINFO));
    }

    if(!ppi) {
        infn->ws_returnerror(pwsc,500,"Malloc error in plugin_handler");
        return;
    }

    memset((void*)&ppi->dq,0,sizeof(DB_QUERY));

    infn->log(E_DBG,"Tokenizing url\n");
    while((ppi->uri_count < 10) && (token=strtok_r(string,"/",&save))) {
        string=NULL;
        ppi->uri_sections[ppi->uri_count++] = token;
    }

    elements = sizeof(rsp_uri_map) / sizeof(PLUGIN_RESPONSE);
    infn->log(E_DBG,"Found %d elements\n",elements);

    index = 0;
    found = 0;

    while((!found) && (index < elements)) {
        /* test this set */
        infn->log(E_DBG,"Checking reponse %d\n",index);
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
            infn->log(E_DBG,"Found it! Index: %d\n",index);
        } else {
            index++;
        }
    }

    if(found) {
        rsp_uri_map[index].dispatch(pwsc, ppi);
        infn->ws_close(pwsc);
        free(ppi);
        return;
    }

    rsp_error(pwsc, ppi, 1, "Bad path");
    infn->ws_close(pwsc);
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

    infn->log(E_DBG,"Starting rsp_info\n");

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
    xml_output(pxml,"count","%d",infn->db_count());
    xml_output(pxml,"rsp-version",RSP_VERSION);

    xml_output(pxml,"server-version",infn->server_ver());

    size = sizeof(servername);
    infn->server_name(servername,&size);
    xml_output(pxml,"name",servername);
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

    if((err=infn->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        infn->db_enum_dispose(NULL,&ppi->dq);
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

    while((infn->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
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

    infn->db_enum_end(NULL);
    infn->db_enum_dispose(NULL,&ppi->dq);

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
    char *transcode_codecs;
    int transcode;
    int samplerate;
    //    char *user_agent;

    /*
    user_agent = infn->ws_getrequestheader(pwsc,"user-agent");
    if(user_agent) {
        if(strncmp(user_agent,"iTunes",6)==0) {
            trancode_codecs = "wma,ogg,flac,mpc";
        } else if(strncmp(user_agent,"Roku",4)==0) {
            transcode_codecs = "ogg,flac,mpc,alac";
        } else {
            transcode_codecs = "wma,ogg,flac,mpc,alac";
        }
    }
    */

    ppi->dq.filter = infn->ws_getvar(pwsc,"query");
    ppi->dq.filter_type = FILTER_TYPE_FIREFLY;

    if(infn->ws_getvar(pwsc,"offset")) {
        ppi->dq.offset = atoi(infn->ws_getvar(pwsc,"offset"));
    }
    if(infn->ws_getvar(pwsc,"limit")) {
        ppi->dq.limit = atoi(infn->ws_getvar(pwsc,"limit"));
    }
    
    browse_type = infn->ws_getvar(pwsc,"type");
    type = F_FULL;

    if(browse_type) {
        if(strcasecmp(browse_type,"browse") == 0) {
            type = F_BROWSE;
        } else if(strcasecmp(browse_type,"id") == 0) {
            type = F_ID;
        }
    }
    ppi->dq.query_type = QUERY_TYPE_ITEMS;
    ppi->dq.playlist_id = atoi(ppi->uri_sections[2]);

    if((err=infn->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        infn->db_enum_dispose(NULL,&ppi->dq);
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

    transcode_codecs = infn->conf_alloc_string("general","ssc_codectypes","");

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","%d",returned);
    xml_output(pxml,"totalrecords","%d",ppi->dq.totalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"items");

    while((infn->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
        xml_push(pxml,"item");
        rowindex=0;
        transcode = 0;
        if(strstr(transcode_codecs,row[37])) /* FIXME: ticket #21 */
            transcode = 1;

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

    infn->db_enum_end(NULL);
    infn->conf_dispose_string(transcode_codecs);

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
    ppi->dq.filter = infn->ws_getvar(pwsc,"query");
    ppi->dq.filter_type = FILTER_TYPE_FIREFLY;

    if(infn->ws_getvar(pwsc,"offset")) {
        ppi->dq.offset = atoi(infn->ws_getvar(pwsc,"offset"));
    }
    
    if(infn->ws_getvar(pwsc,"limit")) {
        ppi->dq.limit = atoi(infn->ws_getvar(pwsc,"limit"));
    }

    ppi->dq.playlist_id = atoi(ppi->uri_sections[2]);

    if((err=infn->db_enum_start(&pe,&ppi->dq)) != 0) {
        rsp_error(pwsc, ppi, err | E_DB, pe);
        infn->db_enum_dispose(NULL,&ppi->dq);
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

    while((infn->db_enum_fetch_row(NULL,&row,&ppi->dq) == 0) && (row)) {
        xml_output(pxml,"item",row[0]);
    }

    infn->db_enum_end(NULL);
    infn->db_enum_dispose(NULL,&ppi->dq);

    xml_pop(pxml); /* items */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);
}

void rsp_stream(WS_CONNINFO *pwsc, PRIVINFO *ppi) {
    infn->stream(pwsc, ppi->uri_sections[2]);
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
    infn->ws_close(pwsc);
}

