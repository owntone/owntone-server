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
#include "mtd-plugins.h"
#include "rsp.h"
#include "xml-rpc.h"

/* Forwards */
PLUGIN_INFO *plugin_info(void);
void plugin_handler(WS_CONNINFO *pwsc);
int plugin_auth(WS_CONNINFO *pwsc, char *username, char *pw);
void rsp_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
void rsp_db(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
void rsp_playlist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
void rsp_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
void rsp_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
void rsp_error(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, int eno, char *estr);

/* Globals */
PLUGIN_OUTPUT_FN _pofn = { plugin_handler, plugin_auth };
PLUGIN_REND_INFO _pri[] = {
    { "_rsp._tcp", NULL },
    { NULL, NULL }
};

PLUGIN_INFO _pi = { 
    PLUGIN_VERSION, 
    PLUGIN_OUTPUT, 
    "rsp/" RSP_VERSION, 
    "/rsp/.*",
    &_pofn,
    NULL,
    _pri
};

typedef struct tag_response {
    char *uri[10];
    void (*dispatch)(WS_CONNINFO *, DBQUERYINFO *);
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
    DBQUERYINFO *pqi;
    int elements;
    int index, part;
    int found;


    string = infn->ws_uri(pwsc);
    string++;

    pqi = (DBQUERYINFO *)malloc(sizeof(DBQUERYINFO));
    if(!pqi) {
        infn->ws_returnerror(pwsc,500,"Malloc error in plugin_handler");
        return;
    }
    memset(pqi,0,sizeof(DBQUERYINFO));

    infn->log(E_DBG,"Tokenizing url\n");
    while((pqi->uri_count < 10) && (token=strtok_r(string,"/",&save))) {
        string=NULL;
        pqi->uri_sections[pqi->uri_count++] = token;
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
            if((rsp_uri_map[index].uri[part]) && (!pqi->uri_sections[part]))
                break;
            if((pqi->uri_sections[part]) && (!rsp_uri_map[index].uri[part]))
                break;

            if((rsp_uri_map[index].uri[part]) && 
               (strcmp(rsp_uri_map[index].uri[part],"*") != 0)) {
                if(strcmp(rsp_uri_map[index].uri[part],
                          pqi->uri_sections[part])!= 0)
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
        rsp_uri_map[index].dispatch(pwsc, pqi);
        infn->ws_close(pwsc);
        return;
    }

    rsp_error(pwsc, pqi, 1, "Bad path");
    infn->ws_close(pwsc);
    return;
}

/**
 * get server info 
 */
void rsp_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
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
void rsp_db(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int rowindex;

    pqi->want_count = 1;
    pqi->query_type = queryTypePlaylists;
    if((err=infn->db_enum_start(&pe,pqi)) != 0) {
        rsp_error(pwsc, pqi, err | E_DB, pe);
        return;
    }

    pxml = xml_init(pwsc,1);

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring","");
    xml_output(pxml,"records","%d",pqi->specifiedtotalcount);
    xml_output(pxml,"totalrecords","%d",pqi->specifiedtotalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"playlists");

    while((infn->db_enum_fetch_row(NULL,&row,pqi) == 0) && (row)) {
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

    xml_pop(pxml); /* playlists */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);

}

/**
 * get all items under the playlist
 */
void rsp_playlist(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int rowindex;
    int returned;
    char *browse_type;
    int type;
    char *query;
    char *estr = NULL;

    query = infn->ws_getvar(pwsc,"query");
    if(query) {
        pqi->pt = infn->sp_init();
        if(!infn->sp_parse(pqi->pt,query)) {
            estr = strdup(infn->sp_get_error(pqi->pt));
            infn->log(E_LOG,"Ignoring bad query (%s): %s\n",query,estr);
            infn->sp_dispose(pqi->pt);
            pqi->pt = NULL;
        }
    }

    pqi->index_type = indexTypeNone;
    if(infn->ws_getvar(pwsc,"offset")) {
        pqi->index_low = atoi(infn->ws_getvar(pwsc,"offset"));
    }
    if(infn->ws_getvar(pwsc,"limit")) {
        pqi->index_high = pqi->index_low + atoi(infn->ws_getvar(pwsc,"limit")) -1;
        if(pqi->index_high < pqi->index_low) {
            pqi->index_high = 999999;
        }
    } else {
        pqi->index_high = 999999; /* FIXME */
    }
    pqi->index_type = indexTypeSub;
    
    browse_type = infn->ws_getvar(pwsc,"type");
    type = F_FULL;

    if(browse_type) {
        if(strcasecmp(browse_type,"browse") == 0) {
            type = F_BROWSE;
        } else if(strcasecmp(browse_type,"id") == 0) {
            type = F_ID;
        }
    }

    pqi->want_count = 1;
    pqi->query_type = queryTypePlaylistItems;
    pqi->playlist_id = atoi(pqi->uri_sections[2]);

    if((err=infn->db_enum_start(&pe,pqi)) != 0) {
        if(pqi->pt) infn->sp_dispose(pqi->pt);
        if(estr) free(estr);
        rsp_error(pwsc, pqi, err | E_DB, pe);
        return;
    }

    pxml = xml_init(pwsc,1);

    if(pqi->index_low > pqi->specifiedtotalcount) {
        returned = 0;
    } else {
        returned = pqi->index_high - pqi->index_low + 1;
        if(returned > (pqi->specifiedtotalcount - pqi->index_low)) 
            returned = pqi->specifiedtotalcount - pqi->index_low;
    }

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring",estr ? estr : "");
    xml_output(pxml,"records","%d",returned);
    xml_output(pxml,"totalrecords","%d",pqi->specifiedtotalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"items");

    while((infn->db_enum_fetch_row(NULL,&row,pqi) == 0) && (row)) {
        xml_push(pxml,"item");
        rowindex=0;
        while(rsp_fields[rowindex].name) {
            if((rsp_fields[rowindex].flags & type) &&
               (row[rowindex] && strlen(row[rowindex]))) {
                xml_output(pxml,rsp_fields[rowindex].name,"%s",
                              row[rowindex]);
            }
            rowindex++;
        }
        xml_pop(pxml); /* item */
    }

    infn->db_enum_end(NULL);

    xml_pop(pxml); /* items */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);

    if(pqi->pt) infn->sp_dispose(pqi->pt);
    if(estr) free(estr);
}

void rsp_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    XMLSTRUCT *pxml;
    char *pe;
    int err;
    char **row;
    int returned;
    char *query;
    char *estr = NULL;

    /* FIXME */
    if(!strcmp(pqi->uri_sections[3],"artist")) {
        pqi->query_type = queryTypeBrowseArtists;
    } else if(!strcmp(pqi->uri_sections[3],"genre")) {
        pqi->query_type = queryTypeBrowseGenres;
    } else if(!strcmp(pqi->uri_sections[3],"album")) {
        pqi->query_type = queryTypeBrowseAlbums;
    } else if(!strcmp(pqi->uri_sections[3],"composer")) {
        pqi->query_type = queryTypeBrowseComposers;
    } else {
        rsp_error(pwsc, pqi, 0 | E_RSP, "Unsupported browse type");        
        return;
    }
    
    query = infn->ws_getvar(pwsc,"query");
    if(query) {
        pqi->pt = infn->sp_init();
        if(!infn->sp_parse(pqi->pt,query)) {
            estr = strdup(infn->sp_get_error(pqi->pt));
            infn->log(E_LOG,"Ignoring bad query (%s): %s\n",query,estr);
            infn->sp_dispose(pqi->pt);
            pqi->pt = NULL;
        }
    }

    pqi->index_type = indexTypeNone;
    if(infn->ws_getvar(pwsc,"offset")) {
        pqi->index_low = atoi(infn->ws_getvar(pwsc,"offset"));
    }
    if(infn->ws_getvar(pwsc,"limit")) {
        pqi->index_high = pqi->index_low + atoi(infn->ws_getvar(pwsc,"limit")) -1;
        if(pqi->index_high < pqi->index_low) {
            pqi->index_high = 999999;
        }
    } else {
        pqi->index_high = 999999; /* FIXME */
    }
    pqi->index_type = indexTypeSub;
    pqi->want_count = 1;
    pqi->playlist_id = atoi(pqi->uri_sections[2]);

    if((err=infn->db_enum_start(&pe,pqi)) != 0) {
        if(pqi->pt) infn->sp_dispose(pqi->pt);
        if(estr) free(estr);
        rsp_error(pwsc, pqi, err | E_DB, pe);
        return;
    }

    pxml = xml_init(pwsc,1);

    if(pqi->index_low > pqi->specifiedtotalcount) {
        returned = 0;
    } else {
        returned = pqi->index_high - pqi->index_low + 1;
        if(returned > (pqi->specifiedtotalcount - pqi->index_low)) 
            returned = pqi->specifiedtotalcount - pqi->index_low;
    }

    xml_push(pxml,"response");
    xml_push(pxml,"status");
    xml_output(pxml,"errorcode","0");
    xml_output(pxml,"errorstring",estr ? estr : "");
    xml_output(pxml,"records","%d",returned);
    xml_output(pxml,"totalrecords","%d",pqi->specifiedtotalcount);
    xml_pop(pxml); /* status */

    xml_push(pxml,"items");

    while((infn->db_enum_fetch_row(NULL,&row,pqi) == 0) && (row)) {
        xml_output(pxml,"item",row[0]);
    }

    infn->db_enum_end(NULL);

    xml_pop(pxml); /* items */
    xml_pop(pxml); /* response */
    xml_deinit(pxml);

    if(pqi->pt) infn->sp_dispose(pqi->pt);
    if(estr) free(estr);
}

void rsp_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    infn->stream(pwsc, pqi, pqi->uri_sections[2]);
    return;
}

void rsp_error(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, int eno, char *estr) {
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

