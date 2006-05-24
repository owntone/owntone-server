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

#define _XOPEN_SOURCE 500  /** unix98?  pthread_once_t, etc */

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
#include "rend.h"
#include "smart-parser.h"
#include "xml-rpc.h"
#include "webserver.h"

typedef struct tag_pluginentry {
    void *phandle;
    int type;
    char *versionstring;
    regex_t regex;
    PLUGIN_OUTPUT_FN *output_fns;
    PLUGIN_EVENT_FN *event_fns;
    PLUGIN_REND_INFO *rend_info;
    struct tag_pluginentry *next;
} PLUGIN_ENTRY;

/* Globals */
static pthread_key_t _plugin_lock_key;
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
void _plugin_free(int *pi);

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
void pi_conf_dispose_string(char *str);
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
    sp_get_error,

    conf_alloc_string,
    pi_conf_dispose_string
};


/**
 * initialize stuff for plugins
 *
 * @returns TRUE on success, FALSE otherwise
 */
int plugin_init(void) {
    pthread_rwlock_init(&_plugin_lock,NULL);
    pthread_key_create(&_plugin_lock_key, (void*)_plugin_free);

    return TRUE;
}

/**
 * free the tls
 */
void _plugin_free(int *pi) {
    if(pi)
        free(pi);
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
 * lock the plugin_mutex.  As it turns out, there might be one thread that calls
 * multiple plug-ins.  So we need to be able to just get one readlock, rather than
 * multiple.  so we'll keep a tls counter.
 *
 * NO DPRINTFING IN HERE!
 */
void _plugin_readlock(void) {
    int err;
    int *current_count;

    current_count = pthread_getspecific(_plugin_lock_key);
    if(!current_count) {
        current_count = (int*)malloc(sizeof(int));
        if(!current_count) {
            /* hrm */
            DPRINTF(E_FATAL,L_PLUG,"Malloc error in _plugin_readlock\n");
        }

        *current_count = 0;
    }

    DPRINTF(E_DBG,L_PLUG,"Current lock level: %d\n",*current_count);
    if(!(*current_count)) {
        (*current_count)++;
        pthread_setspecific(_plugin_lock_key,(void*)current_count);

        if((err=pthread_rwlock_rdlock(&_plugin_lock))) {
            DPRINTF(E_FATAL,L_PLUG,"cannot lock plugin lock: %s\n",strerror(err));
        }
    } else {
        (*current_count)++;
        pthread_setspecific(_plugin_lock_key,(void*)current_count);
    }
}

/**
 * lock the plugin_mutex
 */
void _plugin_writelock(void) {
    int err;
    int *current_count;

    current_count = pthread_getspecific(_plugin_lock_key);
    if(!current_count) {
        current_count = (int*)malloc(sizeof(int));
        if(!current_count) {
            DPRINTF(E_FATAL,L_PLUG,"Malloc error in _plugin_readlock\n");
        }

        *current_count = 0;
    }
    
    DPRINTF(E_DBG,L_PLUG,"Current lock level: %d\n",*current_count);

    if(!(*current_count)) {
        (*current_count)++;
        pthread_setspecific(_plugin_lock_key,(void*)current_count);

        if((err=pthread_rwlock_wrlock(&_plugin_lock))) {
            DPRINTF(E_FATAL,L_PLUG,"cannot lock plugin lock: %s\n",strerror(err));
        }
    } else {
        (*current_count)++;
        pthread_setspecific(_plugin_lock_key,(void*)current_count);
    }
}

/**
 * unlock the plugin_mutex
 */
void _plugin_unlock(void) {
    int err;
    int *current_count;

    current_count = pthread_getspecific(_plugin_lock_key);
    if(!current_count) {
        DPRINTF(E_FATAL,L_PLUG,"_plug_unlock without tls.  wtf?\n");
    }

    (*current_count)--;

    if(!(*current_count)) {
        pthread_setspecific(_plugin_lock_key,(void*)current_count);
        if((err=pthread_rwlock_unlock(&_plugin_lock))) {
            DPRINTF(E_FATAL,L_PLUG,"cannot unlock plugin lock: %s\n",strerror(err));
        }
    } else {
        pthread_setspecific(_plugin_lock_key,(void*)current_count);
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
    if(ppi->type & PLUGIN_OUTPUT) {
        /* build the regex */
        if(regcomp(&ppi->regex,pinfo->url,REG_EXTENDED | REG_NOSUB)) {
            DPRINTF(E_LOG,L_PLUG,"Bad regex in %s: %s\n",path,pinfo->url);
        }
    }
    ppi->output_fns = pinfo->output_fns;
    ppi->event_fns = pinfo->event_fns;
    ppi->rend_info = pinfo->rend_info;

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
    
    DPRINTF(E_DBG,L_PLUG,"Entering candispatch\n");

    _plugin_readlock();
    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->type & PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                _plugin_unlock();
                return TRUE;
            }
        }
        ppi = ppi->next;
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
        if(ppi->type & PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->versionstring);

                /* so functions must be a tag_plugin_output_fn */
                disp_fn=(ppi->output_fns)->handler;
                disp_fn(pwsc);
                _plugin_unlock();
                return;
            }
        }
        ppi = ppi->next;
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
    _plugin_unlock();
    return;
} 

/**
 * walk through the plugins and register whatever rendezvous
 * names the clients want
 */
int plugin_rend_register(char *name, int port, char *iface, char *txt) {
    PLUGIN_ENTRY *ppi;
    PLUGIN_REND_INFO *pri;
    char *supplied_txt;
    char *new_name;
    int name_len;


    _plugin_readlock();
    ppi = _plugin_list.next;

    while(ppi) {
        DPRINTF(E_DBG,L_PLUG,"Checking %s\n",ppi->versionstring);
        if(ppi->rend_info) {
            pri = ppi->rend_info;
            while(pri->type) {
                supplied_txt = pri->txt;
                if(!pri->txt)
                    supplied_txt = txt;

                DPRINTF(E_DBG,L_PLUG,"Registering %s\n",pri->type);

                name_len = (int)strlen(name) + 4 + (int)strlen(ppi->versionstring);
                new_name=(char*)malloc(name_len);
                if(!new_name)
                    DPRINTF(E_FATAL,L_PLUG,"plugin_rend_register: malloc");

                memset(new_name,0,name_len);

                if(conf_get_int("plugins","mangle_rendezvous",1)) {
                    snprintf(new_name,name_len,"%s (%s)",name,ppi->versionstring);
                } else {
                    snprintf(new_name,name_len,"%s",name);
                }
                rend_register(new_name,pri->type,port,iface,supplied_txt);
                free(new_name);

                pri++;
            }
        }
        ppi=ppi->next;
    }

    _plugin_unlock();

    return TRUE;
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
        if(ppi->type & PLUGIN_OUTPUT) {
            if(!regexec(&ppi->regex,pwsc->uri,0,NULL,0)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->versionstring);

                /* so functions must be a tag_plugin_output_fn */
                auth_fn=(ppi->output_fns)->auth;
		if(auth_fn) {
		    result=auth_fn(pwsc,username,pw);
		    _plugin_unlock();
		    return result;
		} else {
		    _plugin_unlock();
		    return TRUE;
		}
            }
        }
        ppi = ppi->next;
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
    _plugin_unlock();
    return FALSE;
}

/**
 * send an event to a plugin... this can be a connection, disconnection, etc.
 */
void plugin_event_dispatch(int event_id, int intval, void *vp, int len) {
    PLUGIN_ENTRY *ppi;

    fprintf(stderr,"entering plugin_event_dispatch\n");

//    _plugin_readlock();
    ppi = _plugin_list.next;
    while(ppi) {
        fprintf(stderr,"Checking %s\n",ppi->versionstring);
        if(ppi->type & PLUGIN_EVENT) {
/*            DPRINTF(E_DBG,L_PLUG,"Dispatching event %d to %s\n",
                event_id,ppi->versionstring); */

            if((ppi->event_fns) && (ppi->event_fns->handler)) {
                ppi->event_fns->handler(event_id, intval, vp, len);
            }
        }
        ppi=ppi->next;
    }
//    _plugin_unlock();
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
    char *servername;

    servername = conf_get_servername();
    if((servername) && (strlen(servername) < (size_t)len)) {
        strcpy(name,servername);
    } else {
        if((size_t)len > strlen("Firefly Media Server"))
            strcpy(name,"Firefly Media Server");
    }

    free(servername);
    return CONF_E_SUCCESS;
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

void pi_conf_dispose_string(char *str) {
    free(str);
}
