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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "db-generic.h"
#include "dispatch.h"
#include "err.h"
#include "os.h"
#include "plugin.h"
#include "smart-parser.h"
#include "xml-rpc.h"
#include "webserver.h"

typedef struct tag_pluginentry {
    void *phandle;
    int type;
    char *versionstring;
    regex_t regex;
    void *functions;
    struct tag_pluginentry *next;
} PLUGIN_ENTRY;

/* Globals */
static PLUGIN_ENTRY _plugin_list;
static int _plugin_initialized = 0;

static pthread_rwlock_t _plugin_lock;

static char* _plugin_error_list[] = {
    "Success.",
    "Could not load plugin: %s",
    "Plugin missing required export: plugin_type/plugin_ver"
};

/* Forwards */
void _plugin_readlock(void);
void _plugin_writelock(void);
void _plugin_unlock(void);
int _plugin_error(char **pe, int error, ...);

/* webserver helpers */
char *pi_ws_uri(WS_CONNINFO *pwsc);
void pi_ws_close(WS_CONNINFO *pwsc);
int pi_ws_fd(WS_CONNINFO *pwsc);

/* misc helpers */
char *pi_server_ver(void);
int pi_server_name(char *, int *);
void pi_log(int, char *, ...);

/* db helpers */
int pi_db_count(void);
int pi_db_enum_start(char **pe, DBQUERYINFO *pinfo);
int pi_db_enum_fetch_row(char **pe, char ***row, DBQUERYINFO *pinfo);
int pi_db_enum_end(char **pe);
void pi_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *id);

int pi_sp_parse(PARSETREE tree, char *term);

PLUGIN_INPUT_FN pi = {
    pi_ws_uri,
    pi_ws_close,
    ws_returnerror,
    ws_getvar,
    ws_writefd,
    ws_addresponseheader,
    ws_emitheaders,
    pi_ws_fd,
    ws_getrequestheader,
    ws_writebinary,

    pi_server_ver,
    pi_server_name,
    pi_log,

    pi_db_count,
    pi_db_enum_start,
    pi_db_enum_fetch_row,
    pi_db_enum_end,
    pi_stream,

    sp_init,
    pi_sp_parse,
    sp_dispose,
    sp_get_error
};

/**
 * initialize stuff for plugins
 *
 * @returns TRUE on success, FALSE otherwise
 */
int plugin_init(void) {
    pthread_rwlock_init(&_plugin_lock,NULL);
    return TRUE;
}

/**
 * deinitialize stuff for plugins
 *
 * @returns TRUE on success, FALSE otherwise
 */
int plugin_deinit(void) {
    return TRUE;
}


/**
 * lock the plugin_mutex
 */
void _plugin_readlock(void) {
    int err;

    if((err=pthread_rwlock_rdlock(&_plugin_lock))) {
        DPRINTF(E_FATAL,L_PLUG,"cannot lock plugin lock: %s\n",strerror(err));
    }
}

/**
 * lock the plugin_mutex
 */
void _plugin_writelock(void) {
    int err;

    if((err=pthread_rwlock_wrlock(&_plugin_lock))) {
        DPRINTF(E_FATAL,L_PLUG,"cannot lock plugin lock: %s\n",strerror(err));
    }
}

/**
 * unlock the plugin_mutex
 */
void _plugin_unlock(void) {
    int err;

    if((err=pthread_rwlock_unlock(&_plugin_lock))) {
        DPRINTF(E_FATAL,L_PLUG,"cannot unlock plugin lock: %s\n",strerror(err));
    }
}

/**
 * return the error 
 * 
 * @param pe buffer to store the error string in
 * @param error error to return
 * @returns the specified error,to thelp with returns
 */
int _plugin_error(char **pe, int error, ...) {
    va_list ap;
    char errbuf[1024];

    if(!pe)
        return error;

    va_start(ap, error);
    vsnprintf(errbuf, sizeof(errbuf), _plugin_error_list[error], ap);
    va_end(ap);

    DPRINTF(E_SPAM,L_PLUG,"Raising error: %s\n",errbuf);

    *pe = strdup(errbuf);
    return error;
}

/**
 * load a specified plugin.
 *
 * @param pe pointer to error string returned (if error)
 * @param plugin path to plugin to load
 *
 * return PLUGIN_E_SUCCESS, or not, with pe set
 */
int plugin_load(char **pe, char *path) {
    PLUGIN_ENTRY *ppi;
    void *phandle;
    PLUGIN_INFO *(*info_func)(void);
    PLUGIN_INFO *pinfo;

    DPRINTF(E_DBG,L_PLUG,"Attempting to load plugin %s\n",path);
    
    phandle = os_loadlib(pe, path);
    if(!phandle) {
        DPRINTF(E_INF,L_PLUG,"Couldn't get lib handle for %s\n",path);
        return PLUGIN_E_NOLOAD;
    }

    ppi = (PLUGIN_ENTRY*)malloc(sizeof(PLUGIN_ENTRY));
    memset(ppi,0x00,sizeof(PLUGIN_ENTRY));

    ppi->phandle = phandle;

    info_func = (PLUGIN_INFO*(*)(void)) os_libfunc(pe, phandle,"plugin_info");
    if(info_func == NULL) {
        DPRINTF(E_INF,L_PLUG,"Couldn't get info_func for %s\n",path);
        return PLUGIN_E_BADFUNCS;
    }
    
    pinfo = info_func();

    ppi->type = pinfo->type;
    ppi->versionstring = pinfo->server;
    if(ppi->type == PLUGIN_OUTPUT) {
        /* build the regex */
        if(regcomp(&ppi->regex,pinfo->url,REG_EXTENDED | REG_NOSUB)) {
            DPRINTF(E_LOG,L_PLUG,"Bad regex in %s: %s\n",path,pinfo->url);
        }
    }
    ppi->functions = pinfo->handler_functions;

    DPRINTF(E_INF,L_PLUG,"Loaded plugin %s (%s)\n",path,ppi->versionstring);
    pinfo->pi = (void*)&pi;
    
    _plugin_writelock();
    if(!_plugin_initialized) {
        _plugin_initialized = 1;
        memset((void*)&_plugin_list,0,sizeof(_plugin_list));
    }

    ppi->next = _plugin_list.next;
    _plugin_list.next = ppi;
    
    _plugin_unlock();
    
    return PLUGIN_E_SUCCESS;
}

/**
 * check to see if we want to dispatch a particular url
 *
 * @param pwsc the connection info (including uri) to check
 * @returns TRUE if we want to handle it 
 */
int plugin_url_candispatch(WS_CONNINFO *pwsc) {
    PLUGIN_ENTRY *ppi;

    _plugin_readlock();
    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->type == PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                _plugin_unlock();
                return TRUE;
            }
            ppi = ppi->next;
        }
    }
    _plugin_unlock();
    return FALSE;
}


/**
 * actually DISPATCH the hander we said we wanted
 *
 * @param pwsc the connection info (including uri) to check
 * @returns TRUE if we want to handle it 
 */
void plugin_url_handle(WS_CONNINFO *pwsc) {
    PLUGIN_ENTRY *ppi;
    void (*disp_fn)(WS_CONNINFO *pwsc);

    _plugin_readlock();
    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->type == PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->versionstring);

                /* so functions must be a tag_plugin_output_fn */
                disp_fn=(((PLUGIN_OUTPUT_FN*)ppi->functions)->handler);
                disp_fn(pwsc);
                _plugin_unlock();
                return;
            }
            ppi = ppi->next;
        }
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
    _plugin_unlock();
    return;
} 

/**
 * Test password for the handled namespace
 *
 * @param pwsc the connection info (including uri) to check
 * @param username user attempting to login
 * @param pw password attempting
 * @returns TRUE if we want to handle it 
 */
int plugin_auth_handle(WS_CONNINFO *pwsc, char *username, char *pw) {
    PLUGIN_ENTRY *ppi;
    int (*auth_fn)(WS_CONNINFO *pwsc, char *username, char *pw);
    int result;

    _plugin_readlock();
    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->type == PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->versionstring);

                /* so functions must be a tag_plugin_output_fn */
                auth_fn=(((PLUGIN_OUTPUT_FN*)ppi->functions)->auth);
		if(auth_fn) {
		    result=auth_fn(pwsc,username,pw);
		    _plugin_unlock();
		    return result;
		} else {
		    _plugin_unlock();
		    return TRUE;
		}
            }
            ppi = ppi->next;
        }
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
    _plugin_unlock();
    return FALSE;
}


/* plugin wrappers for utility functions & stuff
 * 
 * these functions need to be wrapped so we can maintain a stable
 * interface to older plugins even if we get newer functions or apis
 * upstream... it's a binary compatibility layer.
 */
char *pi_ws_uri(WS_CONNINFO *pwsc) {
    return pwsc->uri;
}

void pi_ws_close(WS_CONNINFO *pwsc) {
    pwsc->close=1;
}

int pi_ws_fd(WS_CONNINFO *pwsc) {
    return pwsc->fd;
}

void pi_log(int level, char *fmt, ...) {
    char buf[256];
    va_list ap;

    va_start(ap,fmt);
    vsnprintf(buf,sizeof(buf),fmt,ap);
    va_end(ap);
    
    DPRINTF(level,L_PLUG,"%s",buf);
}

char *pi_server_ver(void) {
    return VERSION;
}

int pi_server_name(char *name, int *len) {
    return conf_get_string("general","servername","unknown",name, len);
}

int pi_db_count(void) {
    int count;
    db_get_song_count(NULL, &count);

    return count;
}

int pi_db_enum_start(char **pe, DBQUERYINFO *pinfo) {
    return db_enum_start(pe, pinfo);
}

int pi_db_enum_fetch_row(char **pe, char ***row, DBQUERYINFO *pinfo) {
    return db_enum_fetch_row(pe, (PACKED_MP3FILE*)row, pinfo);
}

int pi_db_enum_end(char **pe) {
    return db_enum_end(pe);
}

void pi_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *id) {
    dispatch_stream_id(pwsc, pqi,id);
    return;
}

int pi_sp_parse(PARSETREE tree, char *term) {
    return sp_parse(tree, term, 0);
}
