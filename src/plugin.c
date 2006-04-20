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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "err.h"
#include "os.h"
#include "plugin.h"

typedef struct tag_pluginentry {
    void *phandle;
    int type;
    char *versionstring;
    struct tag_pluginentry *next;
} PLUGIN_ENTRY;

/* Globals */
static PLUGIN_ENTRY _plugin_list = { NULL, 0, NULL, NULL };

static pthread_mutex_t _plugin_mutex = PTHREAD_MUTEX_INITIALIZER;
static char* _plugin_error_list[] = {
    "Success.",
    "Could not load plugin: %s",
    "Plugin missing required export: plugin_type/plugin_ver"
};


/* Forwards */
void _plugin_lock(void);
void _plugin_unlock(void);
int _plugin_error(char **pe, int error, ...);

/**
 * lock the plugin_mutex
 */
void _plugin_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&_plugin_mutex))) {
        DPRINTF(E_FATAL,L_PLUG,"cannot lock plugin lock: %s\n",strerror(err));
    }
}

/**
 * unlock the plugin_mutex
 */
void _plugin_unlock(void) {
    int err;

    if((err=pthread_mutex_unlock(&_plugin_mutex))) {
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
    int (*type_func)(void);
    char* (*ver_func)(void);

    DPRINTF(E_DBG,L_PLUG,"Attempting to load plugin %s\n",path);
    
    phandle = os_loadlib(path);
    if(!phandle) {
        return _plugin_error(pe,PLUGIN_E_NOLOAD,strerror(errno));
    }

    ppi = (PLUGIN_ENTRY*)malloc(sizeof(PLUGIN_ENTRY));
    memset(ppi,0x00,sizeof(PLUGIN_ENTRY));

    ppi->phandle = phandle;

    type_func = (int(*)(void)) os_libfunc(phandle,"plugin_type");
    ver_func = (char*(*)(void)) os_libfunc(phandle,"plugin_ver");
    
    if((type_func == NULL) || (ver_func == NULL))
        return _plugin_error(pe,PLUGIN_E_BADFUNCS);
    
    ppi->type = type_func();
    ppi->versionstring = ver_func();
    
    DPRINTF(E_INF,L_PLUG,"Loaded plugin %s (%s)\n",path,ppi->versionstring);
    
    _plugin_lock();
    
    ppi->next = _plugin_list.next;
    _plugin_list.next = ppi;
    
    _plugin_unlock();
    
    return PLUGIN_E_SUCCESS;
}
 
