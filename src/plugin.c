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
# include "config.h"
#endif

#define _XOPEN_SOURCE 500  /** unix98?  pthread_once_t, etc */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

#include "conf.h"
#include "configfile.h"
#include "db-generic.h"
#include "dynamic-art.h"
#include "err.h"
#include "os.h"
#include "plugin.h"
#include "rend.h"
#include "restart.h"
#include "smart-parser.h"
#include "xml-rpc.h"
#include "webserver.h"
#include "ff-plugins.h"

typedef struct tag_pluginentry {
    void *phandle;
    PLUGIN_INFO *pinfo;
    struct tag_pluginentry *next;
} PLUGIN_ENTRY;

/* Globals */
static PLUGIN_ENTRY _plugin_list;
static int _plugin_initialized = 0;
static char *_plugin_ssc_codecs = NULL;

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
void _plugin_recalc_codecs(void);
int _plugin_ssc_transcode(WS_CONNINFO *pwsc, MP3FILE *pmp3, int offset, int headers);

/* webserver helpers */
char *pi_ws_uri(WS_CONNINFO *pwsc);
void pi_ws_will_close(WS_CONNINFO *pwsc);
int pi_ws_fd(WS_CONNINFO *pwsc);

/* misc helpers */
char *pi_server_ver(void);
int pi_server_name(char *, int *);
void pi_log(int, char *, ...);

/* db helpers */
int pi_db_count(void);
int pi_db_enum_start(char **pe, DB_QUERY *pinfo);
int pi_db_enum_fetch_row(char **pe, char ***row, DB_QUERY *pinfo);
int pi_db_enum_end(char **pe);
int pi_db_enum_restart(char **pe, DB_QUERY *pinfo);
void pi_db_enum_dispose(char **pe, DB_QUERY *pinfo);
void pi_stream(WS_CONNINFO *pwsc, char *id);
int pi_db_count_items(int what);
int pi_db_wait_update(WS_CONNINFO *pwsc);
void pi_conf_dispose_string(char *str);

/* transcode helpers */
int pi_ssc_should_transcode(WS_CONNINFO *pwsc, char *codec);

PLUGIN_INPUT_FN pi = {
    pi_ws_uri,
    pi_ws_will_close,
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
    pi_ssc_should_transcode,

    pi_db_count,
    pi_db_enum_start,
    pi_db_enum_fetch_row,
    pi_db_enum_end,
    pi_db_enum_restart,
    pi_db_enum_dispose,
    pi_stream,

    db_add_playlist,
    db_add_playlist_item,
    db_edit_playlist,
    db_delete_playlist,
    db_delete_playlist_item,
    db_revision,
    pi_db_count_items,
    pi_db_wait_update,

    conf_alloc_string,
    pi_conf_dispose_string,
    conf_get_int,

    config_set_status
};


/**
 * initialize stuff for plugins
 *
 * @returns TRUE on success, FALSE otherwise
 */
int plugin_init(void) {
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
 * walk through the installed plugins and recalculate
 * the codec string
 */
void _plugin_recalc_codecs(void) {
    PLUGIN_ENTRY *ppi;
    size_t size=0;

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_TRANSCODE) {
            if(size) size++;
            size += strlen(ppi->pinfo->codeclist);
        }
        ppi=ppi->next;
    }

    if(_plugin_ssc_codecs) {
        free(_plugin_ssc_codecs);
    }

    _plugin_ssc_codecs = (char*)malloc(size+1);
    if(!_plugin_ssc_codecs) {
        DPRINTF(E_FATAL,L_PLUG,"_plugin_recalc_codecs: malloc\n");
    }

    memset(_plugin_ssc_codecs,0,size+1);

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_TRANSCODE) {
            if(strlen(_plugin_ssc_codecs)) {
                strcat(_plugin_ssc_codecs,",");
            }
            strcat(_plugin_ssc_codecs,ppi->pinfo->codeclist);
        }
        ppi=ppi->next;
    }

    DPRINTF(E_DBG,L_PLUG,"New transcode codec list: %s\n",_plugin_ssc_codecs);
    return;

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
    PLUGIN_INFO *(*info_func)(PLUGIN_INPUT_FN *);
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

    info_func = (PLUGIN_INFO*(*)(PLUGIN_INPUT_FN*)) os_libfunc(pe, phandle,"plugin_info");
    if(info_func == NULL) {
        DPRINTF(E_INF,L_PLUG,"Couldn't get info_func for %s\n",path);
        os_unload(phandle);
        free(ppi);
        return PLUGIN_E_BADFUNCS;
    }

    pinfo = info_func(&pi);
    ppi->pinfo = pinfo;

    if(!pinfo) {
        if(pe) *pe = strdup("plugin declined to load");
        os_unload(phandle);
        free(ppi);
        return PLUGIN_E_NOLOAD;
    }

    DPRINTF(E_INF,L_PLUG,"Loaded plugin %s (%s)\n",path,pinfo->server);

    if(!_plugin_initialized) {
        _plugin_initialized = 1;
        memset((void*)&_plugin_list,0,sizeof(_plugin_list));
    }

    ppi->next = _plugin_list.next;
    _plugin_list.next = ppi;

    _plugin_recalc_codecs();
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

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_OUTPUT) {
            if((ppi->pinfo->output_fns)->can_handle(pwsc)) {
                return TRUE;
            }
        }
        ppi = ppi->next;
    }

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

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_OUTPUT) {
            if((ppi->pinfo->output_fns)->can_handle(pwsc)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->pinfo->server);

                /* so functions must be a tag_plugin_output_fn */
                disp_fn=(ppi->pinfo->output_fns)->handler;
                disp_fn(pwsc);
                return;
            }
        }
        ppi = ppi->next;
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
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

    ppi = _plugin_list.next;

    while(ppi) {
        DPRINTF(E_DBG,L_PLUG,"Checking %s\n",ppi->pinfo->server);
        if(ppi->pinfo->rend_info) {
            pri = ppi->pinfo->rend_info;
            while(pri->type) {
                supplied_txt = pri->txt;
                if(!pri->txt)
                    supplied_txt = txt;

                DPRINTF(E_DBG,L_PLUG,"Registering %s\n",pri->type);
                rend_register(name,pri->type,port,iface,supplied_txt);
                pri++;
            }
        }
        ppi=ppi->next;
    }

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

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_OUTPUT) {
            if((ppi->pinfo->output_fns)->can_handle(pwsc)) {
                /* we have a winner */
                DPRINTF(E_DBG,L_PLUG,"Dispatching %s to %s\n", pwsc->uri,
                        ppi->pinfo->server);

                /* so functions must be a tag_plugin_output_fn */
                auth_fn=(ppi->pinfo->output_fns)->auth;
                if(auth_fn) {
                    result=auth_fn(pwsc,username,pw);
                    return result;
                } else {
                    return TRUE;
                }
            }
        }
        ppi = ppi->next;
    }

    /* should 500 here or something */
    ws_returnerror(pwsc, 500, "Can't find plugin handler");
    return FALSE;
}

/**
 * send an event to a plugin... this can be a connection, disconnection, etc.
 */
void plugin_event_dispatch(int event_id, int intval, void *vp, int len) {
    PLUGIN_ENTRY *ppi;

    ppi = _plugin_list.next;
    while(ppi) {
        if(ppi->pinfo->type & PLUGIN_EVENT) {
/*            DPRINTF(E_DBG,L_PLUG,"Dispatching event %d to %s\n",
                event_id,ppi->versionstring); */

            if((ppi->pinfo->event_fns) && (ppi->pinfo->event_fns->handler)) {
                ppi->pinfo->event_fns->handler(event_id, intval, vp, len);
            }
        }
        ppi=ppi->next;
    }
}

/**
 * check to see if we can transcode
 *
 * @param codec the codec we are trying to serve
 * @returns TRUE if we can transcode, FALSE otherwise
 */
int pi_ssc_should_transcode(WS_CONNINFO *pwsc, char *codec) {
    int result;
    char *native_codecs=NULL;
    char *user_agent=NULL;
    char *never_transcode = NULL;

    if(!codec) {
        DPRINTF(E_LOG,L_PLUG,"testing transcode on null codec?\n");
        return FALSE;
    }


    never_transcode = conf_alloc_string("general","never_transcode",NULL);
    if(never_transcode) {
        if(strstr(never_transcode,codec)) {
            free(never_transcode);
            return FALSE;
        }
        free(never_transcode);
    }

    if(pwsc) {
        /* see if the headers give us any guidance */
        native_codecs = ws_getrequestheader(pwsc,"accept-codecs");
        if(!native_codecs) {
            user_agent = ws_getrequestheader(pwsc,"user-agent");
            if(user_agent) {
                if(strncmp(user_agent,"iTunes",6)==0) {
                    native_codecs = "mpeg,mp4a,wav,mp4v,alac";
                } else if(strncmp(user_agent,"Roku",4)==0) {
                    native_codecs = "mpeg,mp4a,wav,wma";
                }
            }
        }
    }

    if(!native_codecs) {
        native_codecs = "mpeg,wav";
    }

    /* can't transcode it if we can't transcode it */
    if(!_plugin_ssc_codecs)
        return FALSE;

    if(strstr(native_codecs,codec))
        return FALSE;

    result = FALSE;
    if(strstr(_plugin_ssc_codecs,codec)) {
        result = TRUE;
    }
    return result;
}


/**
 * stupid helper to copy transcode stream to the fd
 */
int _plugin_ssc_copy(WS_CONNINFO *pwsc, PLUGIN_TRANSCODE_FN *pfn,
                     void *vp,int offset) {
    int bytes_read;
    int bytes_to_read;
    int total_bytes_read = 0;
    char buffer[1024];

    /* first, skip past the offset */
    while(offset) {
        bytes_to_read = sizeof(buffer);
        if(bytes_to_read > offset)
            bytes_to_read = offset;

        bytes_read = pfn->ssc_read(vp,buffer,bytes_to_read);
        if(bytes_read <= 0)
            return bytes_read;

        offset -= bytes_read;
    }

    while((bytes_read=pfn->ssc_read(vp,buffer,sizeof(buffer))) > 0) {
        total_bytes_read += bytes_read;
        if(ws_writebinary(pwsc,buffer,bytes_read) != bytes_read) {
            return total_bytes_read;
        }
    }

    /*
    if(bytes_read < 0) {
        return bytes_read;
    }
    */

    return total_bytes_read;
}

/**
 * do the transcode, emitting the headers, content type,
 * and shoving the file down the wire
 *
 * @param pwsc connection to transcode to
 * @param file file to transcode
 * @param codec source codec
 * @param duration time in ms
 * @returns bytes transferred, or -1 on error
 */
int _plugin_ssc_transcode(WS_CONNINFO *pwsc, MP3FILE *pmp3, int offset, int headers) {
    PLUGIN_ENTRY *ppi, *ptc=NULL;
    PLUGIN_TRANSCODE_FN *pfn = NULL;
    void *vp_ssc;
    int post_error = 1;
    int result = -1;

    /* first, find the plugin that will do the conversion */
    ppi = _plugin_list.next;
    while((ppi) && (!pfn)) {
        if(ppi->pinfo->type & PLUGIN_TRANSCODE) {
            if(strstr(ppi->pinfo->codeclist,pmp3->codectype)) {
                ptc = ppi;
                pfn = ppi->pinfo->transcode_fns;
            }
        }
        ppi = ppi->next;
    }

    if(pfn) {
        DPRINTF(E_DBG,L_PLUG,"Transcoding %s with %s\n",pmp3->path,
                ptc->pinfo->server);

        vp_ssc = pfn->ssc_init();
        if(vp_ssc) {
            if(pfn->ssc_open(vp_ssc,pmp3)) {
                /* start reading and throwing */
                if(headers) {
                    ws_addresponseheader(pwsc,"Content-Type","audio/wav");
                    ws_addresponseheader(pwsc,"Connection","Close");
                    if(!offset) {
                        ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
                    } else {
                        ws_addresponseheader(pwsc,"Content-Range",
                                             "bytes %ld-*/*",
                                             (long)offset);
                        ws_writefd(pwsc,"HTTP/1.1 206 Partial Content\r\n");
                    }
                    ws_emitheaders(pwsc);
                }

                /* start reading/writing */
                result = _plugin_ssc_copy(pwsc,pfn,vp_ssc,offset);
                post_error = 0;
                pfn->ssc_close(vp_ssc);
            } else {
                DPRINTF(E_LOG,L_PLUG,"Error opening %s for ssc: %s\n",
                        pmp3->path,pfn->ssc_error(vp_ssc));
            }
            pfn->ssc_deinit(vp_ssc);
        } else {
            DPRINTF(E_LOG,L_PLUG,"Error initializing transcoder: %s\n",
                    ptc->pinfo->server);
        }
    }

    if(post_error) {
        pwsc->error = EPERM; /* ?? */
        ws_returnerror(pwsc,500,"Internal error");
    }

    return result;
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

void pi_ws_will_close(WS_CONNINFO *pwsc) {
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

int pi_db_enum_start(char **pe, DB_QUERY *pinfo) {
    DBQUERYINFO *pqi;
    int result;

    pqi = (DBQUERYINFO*)malloc(sizeof(DBQUERYINFO));
    if(!pqi) {
        if(pe) *pe = strdup("Malloc error");
        return DB_E_MALLOC;
    }
    memset(pqi,0,sizeof(DBQUERYINFO));
    pinfo->priv = (void*)pqi;

    if(pinfo->filter) {
        pqi->pt = sp_init();
        if(!sp_parse(pqi->pt,pinfo->filter,pinfo->filter_type)) {
            DPRINTF(E_LOG,L_PLUG,"Ignoring bad query (%s): %s\n",
                    pinfo->filter,sp_get_error(pqi->pt));
            sp_dispose(pqi->pt);
            pqi->pt = NULL;
        }
    }

    if((pinfo->limit) || (pinfo->offset)) {
        pqi->index_low = pinfo->offset;
        pqi->index_high = pinfo->offset + pinfo->limit - 1;
        if(pqi->index_high < pqi->index_low)
            pqi->index_high = 9999999;

        pqi->index_type = indexTypeSub;
    } else {
        pqi->index_type = indexTypeNone;
    }

    pqi->want_count = 1;

    switch(pinfo->query_type) {
    case QUERY_TYPE_PLAYLISTS:
        pqi->query_type = queryTypePlaylists;
        break;
    case QUERY_TYPE_DISTINCT:
        if((strcmp(pinfo->distinct_field,"artist") == 0)) {
            pqi->query_type = queryTypeBrowseArtists;
        } else if((strcmp(pinfo->distinct_field,"genre") == 0)) {
            pqi->query_type = queryTypeBrowseGenres;
        } else if((strcmp(pinfo->distinct_field,"album") == 0)) {
            pqi->query_type = queryTypeBrowseAlbums;
        } else if((strcmp(pinfo->distinct_field,"composer") == 0)) {
            pqi->query_type = queryTypeBrowseComposers;
        } else {
            if(pe) *pe = strdup("Unsupported browse type");
            if(pqi->pt)
                sp_dispose(pqi->pt);
            pqi->pt = NULL;
            return -1; /* not really a db error for this */
        }
        break;
    case QUERY_TYPE_ITEMS:
    default:
        pqi->query_type = queryTypePlaylistItems;
        pqi->correct_order = conf_get_int("scan","correct_order",1);
        break;
    }

    pqi->playlist_id = pinfo->playlist_id;
    result =  db_enum_start(pe, pqi);
    pinfo->totalcount = pqi->specifiedtotalcount;

    return DB_E_SUCCESS;
}

int pi_db_enum_fetch_row(char **pe, char ***row, DB_QUERY *pinfo) {
    return db_enum_fetch_row(pe, (PACKED_MP3FILE*)row,
                             (DBQUERYINFO*)pinfo->priv);
}

int pi_db_enum_end(char **pe) {
    return db_enum_end(pe);
}

/* FIXME: error checking */
int pi_db_count_items(int what) {
    int count=0;

    switch(what) {
    case COUNT_SONGS:
        db_get_song_count(NULL,&count);
        break;
    case COUNT_PLAYLISTS:
        db_get_playlist_count(NULL,&count);
        break;
    }

    return count;
}


int pi_db_enum_restart(char **pe, DB_QUERY *pinfo) {
    DBQUERYINFO *pqi;

    pqi = (DBQUERYINFO*)pinfo->priv;
    return db_enum_reset(pe,pqi);
}

void pi_db_enum_dispose(char **pe, DB_QUERY *pinfo) {
    DBQUERYINFO *pqi;

    if(!pinfo)
        return;

    if(pinfo->priv) {
        pqi = (DBQUERYINFO *)pinfo->priv;
        if(pqi->pt) {
            sp_dispose(pqi->pt);
            pqi->pt = NULL;
        }
    }
}

int pi_db_wait_update(WS_CONNINFO *pwsc) {
    int clientver=1;
    fd_set rset;
    struct timeval tv;
    int result;
    int lastver=0;

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
            return FALSE;
        }
    }

    return TRUE;
}

void pi_stream(WS_CONNINFO *pwsc, char *id) {
    int session = 0;
    MP3FILE *pmp3;
    int file_fd;
    int bytes_copied=0;
    off_t real_len=0;
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
    } else if (pi_ssc_should_transcode(pwsc,pmp3->codectype)) {
        /************************
         * Server side conversion
         ************************/
        config_set_status(pwsc,session,
                          "Transcoding '%s' (id %d)",
                          pmp3->title,pmp3->id);

        DPRINTF(E_WARN,L_WS,
                "Session %d: Streaming file '%s' to %s (offset %ld)\n",
                session,pmp3->fname, pwsc->hostname,(long)offset);

        /* estimate the real length of this thing */
        bytes_copied =  _plugin_ssc_transcode(pwsc,pmp3,offset,1);
        if(bytes_copied != -1)
            real_len = bytes_copied;

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
            }

            config_set_status(pwsc,session,NULL);
            r_close(file_fd);
            db_dispose_item(pmp3);
        }
        /* update play counts */
        if(bytes_copied  >= (real_len * 80 / 100)) {
            db_playcount_increment(NULL,pmp3->id);
        }
    }

    //    free(pqi);
}

void pi_conf_dispose_string(char *str) {
    free(str);
}
