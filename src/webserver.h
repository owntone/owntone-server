/*
 * $Id$
 * Webserver library
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

#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

/*
 * Defines
 */

#define RT_GET       0
#define RT_POST      1

/* 
 * Typedefs
 */


typedef void* WSHANDLE;
typedef void* WSTHREADENUM;

typedef struct tag_wsconfig {
    char *web_root;
    char *id;
    unsigned short port;
} WSCONFIG;

typedef struct tag_arglist {
    char *key;
    char *value;
    struct tag_arglist *next;
} ARGLIST;

typedef struct tag_ws_conninfo {
    WSHANDLE pwsp;
    int threadno;
    int error;
    int fd;
    int request_type;
    char *uri;
    char *hostname;
    int close;
    void *local_storage;
    void (*storage_callback)(void*);
    ARGLIST request_headers;
    ARGLIST response_headers;
    ARGLIST request_vars;
} WS_CONNINFO;

/*
 * Externs
 */

#define WS_REQ_HANDLER void (*)(WS_CONNINFO *)
#define WS_AUTH_HANDLER int (*)(WS_CONNINFO*, char *, char *)

extern WSHANDLE ws_start(WSCONFIG *config);
extern int ws_stop(WSHANDLE ws);
extern int ws_registerhandler(WSHANDLE ws, char *regex, 
                              void(*handler)(WS_CONNINFO*),
                              int(*auth)(WS_CONNINFO*, char *, char *),
                              int addheaders);

extern void ws_lock_local_storage(WS_CONNINFO *pwsc);
extern void ws_unlock_local_storage(WS_CONNINFO *pwsc);
extern void *ws_get_local_storage(WS_CONNINFO *pwsc);
extern void ws_set_local_storage(WS_CONNINFO *pwsc, void *ptr, void (*callback)(void *));

extern WS_CONNINFO *ws_thread_enum_first(WSHANDLE, WSTHREADENUM *);
extern WS_CONNINFO *ws_thread_enum_next(WSHANDLE, WSTHREADENUM *);

/* for handlers */
extern void ws_close(WS_CONNINFO *pwsc);
extern int ws_returnerror(WS_CONNINFO *pwsc, int error, char *description);
extern int ws_addresponseheader(WS_CONNINFO *pwsc, char *header, char *fmt, ...);
extern int ws_writefd(WS_CONNINFO *pwsc, char *fmt, ...);
extern int ws_writebinary(WS_CONNINFO *pwsc, char *data, int len);
extern char *ws_getvar(WS_CONNINFO *pwsc, char *var);
extern char *ws_getrequestheader(WS_CONNINFO *pwsc, char *header);
extern int ws_testrequestheader(WS_CONNINFO *pwsc, char *header, char *value);
extern void ws_emitheaders(WS_CONNINFO *pwsc);

#endif /* _WEBSERVER_H_ */
