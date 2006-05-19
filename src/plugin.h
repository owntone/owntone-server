/*
 * $Id: $
 * Simple plug-in api for output, transcode, and scanning plug-ins
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

#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include "webserver.h"
#include "xml-rpc.h"
#include "db-generic.h"

extern int plugin_init(void);
extern int plugin_load(char **pe, char *path);
extern int plugin_deinit(void);

/* Interfaces for web */
extern int plugin_url_candispatch(WS_CONNINFO *pwsc);
extern void plugin_url_handle(WS_CONNINFO *pwsc);
extern int plugin_auth_handle(WS_CONNINFO *pwsc, char *username, char *pw);
extern int plugin_rend_register(char *name, int port, char *iface);
extern void plugin_event_dispatch(int event_id, int intval, void *vp, int len);



#define PLUGIN_E_SUCCESS     0
#define PLUGIN_E_NOLOAD      1
#define PLUGIN_E_BADFUNCS    2

#define PLUGIN_OUTPUT    1
#define PLUGIN_SCANNER   2
#define PLUGIN_DATABASE  4
#define PLUGIN_EVENT     8

#define PLUGIN_EVENT_LOG            0
#define PLUGIN_EVENT_FULLSCAN_START 1
#define PLUGIN_EVENT_FULLSCAN_END   2
#define PLUGIN_EVENT_STARTING       3
#define PLUGIN_EVENT_SHUTDOWN       4
#define PLUGIN_EVENT_STARTSTREAM    5
#define PLUGIN_EVENT_ABORTSTREAM    6
#define PLUGIN_EVENT_ENDSTREAM      7

#define PLUGIN_VERSION   1


typedef struct tag_plugin_output_fn {
    void(*handler)(WS_CONNINFO *pwsc);
    int(*auth)(WS_CONNINFO *pwsc, char *username, char *pw);
} PLUGIN_OUTPUT_FN;

typedef struct tag_plugin_event_fn {
    void(*handler)(int event_id, int intval, void *vp, int len);
} PLUGIN_EVENT_FN;

/* version 1 plugin info */
typedef struct tag_plugin_rend_info {
    char *type;
    char *txt;
} PLUGIN_REND_INFO;

typedef struct tag_plugin_info {
    int version;
    int type;
    char *server;
    char *url;      /* for output plugins */
    PLUGIN_OUTPUT_FN *output_fns;
    PLUGIN_EVENT_FN *event_fns;
    void *pi; /* exported functions */
    PLUGIN_REND_INFO *rend_info;
} PLUGIN_INFO;

/* version 1 plugin imports */
typedef struct tag_plugin_input_fn {
    /* webserver helpers */
    char* (*ws_uri)(WS_CONNINFO *);
    void (*ws_close)(WS_CONNINFO *);
    int (*ws_returnerror)(WS_CONNINFO *, int, char *);
    char* (*ws_getvar)(WS_CONNINFO *, char *);
    int (*ws_writefd)(WS_CONNINFO *, char *, ...);
    int (*ws_addresponseheader)(WS_CONNINFO *, char *, char *, ...);
    void (*ws_emitheaders)(WS_CONNINFO *);
    int (*ws_fd)(WS_CONNINFO *);
    char* (*ws_getrequestheader)(WS_CONNINFO *, char *);
    int (*ws_writebinary)(WS_CONNINFO *, char *, int);

    /* misc helpers */
    char* (*server_ver)(void);
    int (*server_name)(char *, int *);
    void (*log)(int, char *, ...);

    int (*db_count)(void);
    int (*db_enum_start)(char **, DBQUERYINFO *);
    int (*db_enum_fetch_row)(char **, char ***, DBQUERYINFO *);
    int (*db_enum_end)(char **);
    void (*stream)(WS_CONNINFO *, DBQUERYINFO *, char *);

    PARSETREE (*sp_init)(void);
    int (*sp_parse)(PARSETREE tree, char *term);
    int (*sp_dispose)(PARSETREE tree);
    char* (*sp_get_error)(PARSETREE tree);

    char *(*conf_alloc_string)(char *section, char *key, char *dflt);
    void (*conf_dispose_string)(char *str);
} PLUGIN_INPUT_FN;

#endif /* _PLUGIN_H_ */
