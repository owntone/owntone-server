/*
 * $Id: $
 * Public plug-in interface
 *
 * Copyright (C) 2006 Ron Pedde (ron@pedde.com)
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

#ifndef _FF_PLUGINS_H_
#define _FF_PLUGINS_H_

/* Plugin types */
#define PLUGIN_OUTPUT     1
#define PLUGIN_SCANNER    2
#define PLUGIN_DATABASE   4
#define PLUGIN_EVENT      8
#define PLUGIN_TRANSCODE 16

/* plugin event types */
#define PLUGIN_EVENT_LOG            0
#define PLUGIN_EVENT_FULLSCAN_START 1
#define PLUGIN_EVENT_FULLSCAN_END   2
#define PLUGIN_EVENT_STARTING       3
#define PLUGIN_EVENT_SHUTDOWN       4
#define PLUGIN_EVENT_STARTSTREAM    5
#define PLUGIN_EVENT_ABORTSTREAM    6
#define PLUGIN_EVENT_ENDSTREAM      7

#define PLUGIN_VERSION   1

#ifndef E_FATAL
# define E_FATAL 0
# define E_LOG   1
# define E_INF   5
# define E_DBG   9
#endif


struct tag_ws_conninfo;

/* Functions that must be exported by different plugin types */
typedef struct tag_plugin_output_fn {
    void(*handler)(struct tag_ws_conninfo *pwsc);
    int(*auth)(struct tag_ws_conninfo *pwsc, char *username, char *pw);
} PLUGIN_OUTPUT_FN;

typedef struct tag_plugin_event_fn {
    void(*handler)(int event_id, int intval, void *vp, int len);
} PLUGIN_EVENT_FN;

typedef struct tag_plugin_transcode_fn {
    void *(*ssc_init)(void);
    void (*ssc_deinit)(void*);
    int (*ssc_open)(void*, char *, char*, int);
    int (*ssc_close)(void*);
    int (*ssc_read)(void*, char*, int);
    char *(*ssc_error)(void*);
} PLUGIN_TRANSCODE_FN;

/* info for rendezvous advertising */
typedef struct tag_plugin_rend_info {
    char *type;
    char *txt;
} PLUGIN_REND_INFO;

/* main info struct that plugins must provide */
typedef struct tag_plugin_info {
    int version;                  /* PLUGIN_VERSION */
    int type;                     /* PLUGIN_OUTPUT, etc */
    char *server;                 /* Server/version format */
    char *url;                    /* regex match of web urls */
    PLUGIN_OUTPUT_FN *output_fns; /* functions for different plugin types */
    PLUGIN_EVENT_FN *event_fns;
    PLUGIN_TRANSCODE_FN *transcode_fns;
    PLUGIN_REND_INFO *rend_info;  /* array of rend announcements */
    char *codeclist;              /* comma separated list of codecs */
} PLUGIN_INFO;


#define QUERY_TYPE_ITEMS     0
#define QUERY_TYPE_PLAYLISTS 1
#define QUERY_TYPE_DISTINCT  2

#define FILTER_TYPE_FIREFLY  0
#define FILTER_TYPE_APPLE    1

typedef struct tag_db_query {
    int query_type;
    char *distinct_field;
    int filter_type;
    char *filter;

    int offset;
    int limit;

    int playlist_id;            /* for items query */
    int totalcount;             /* returned total count */
    void *private;
} DB_QUERY;


/* version 1 plugin imports */
typedef struct tag_plugin_input_fn {
    /* webserver helpers */
    char* (*ws_uri)(struct tag_ws_conninfo *);
    void (*ws_close)(struct tag_ws_conninfo *);
    int (*ws_returnerror)(struct tag_ws_conninfo *, int, char *);
    char* (*ws_getvar)(struct tag_ws_conninfo *, char *);
    int (*ws_writefd)(struct tag_ws_conninfo *, char *, ...);
    int (*ws_addresponseheader)(struct tag_ws_conninfo *, char *, char *, ...);
    void (*ws_emitheaders)(struct tag_ws_conninfo *);
    int (*ws_fd)(struct tag_ws_conninfo *);
    char* (*ws_getrequestheader)(struct tag_ws_conninfo *, char *);
    int (*ws_writebinary)(struct tag_ws_conninfo *, char *, int);

    /* misc helpers */
    char* (*server_ver)(void);
    int (*server_name)(char *, int *);
    void (*log)(int, char *, ...);

    int (*db_count)(void);
    int (*db_enum_start)(char **, DB_QUERY *);
    int (*db_enum_fetch_row)(char **, char ***, DB_QUERY *);
    int (*db_enum_end)(char **);
    void (*db_enum_dispose)(char **, DB_QUERY*);
    void (*stream)(struct tag_ws_conninfo *, char *);

    char *(*conf_alloc_string)(char *section, char *key, char *dflt);
    void (*conf_dispose_string)(char *str);
} PLUGIN_INPUT_FN;


#endif _FF_PLUGINS_
