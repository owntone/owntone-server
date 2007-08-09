#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"
#include "configfile.h"
#include "daapd.h"
#include "db-generic.h"
#include "err.h"
#include "ff-dbstruct.h"
#include "ff-plugins.h"
#include "mp3-scanner.h"
#include "plugin.h"
#include "util.h"
#include "webserver.h"

EXPORT char *pi_ws_uri(WS_CONNINFO *pwsc) {
    ASSERT(pwsc);

    if(!pwsc)
        return NULL;

    return ws_uri(pwsc);
}

EXPORT void pi_ws_will_close(WS_CONNINFO *pwsc) {
    ASSERT(pwsc);

    if(!pwsc)
        return;

    ws_should_close(pwsc,1);
}

EXPORT int pi_ws_returnerror(WS_CONNINFO *pwsc, int ecode, char *msg) {
    ASSERT(pwsc);

    if(!pwsc)
        return FALSE;

    return ws_returnerror(pwsc,ecode,msg);
}

EXPORT char *pi_ws_getvar(WS_CONNINFO *pwsc, char *var) {
    ASSERT(pwsc);
    ASSERT(var);

    if((!pwsc) || (!var))
        return NULL;

    return ws_getvar(pwsc,var);
}

EXPORT int pi_ws_writefd(WS_CONNINFO *pwsc, char *fmt, ...) {
    char *out;
    va_list ap;
    int result;

    ASSERT((pwsc) && (fmt));

    if((!pwsc) || (!fmt))
        return FALSE;

    va_start(ap,fmt);
    out = util_vasprintf(fmt,ap);
    va_end(ap);

    result = ws_writefd(pwsc, "%s", out);
    free(out);
    return result;
}

EXPORT int pi_ws_addresponseheader(WS_CONNINFO *pwsc, char *header, char *fmt, ...) {
    char *out;
    va_list ap;
    int result;

    ASSERT(pwsc && header && fmt);

    if((!pwsc) || (!header) || (!fmt))
        return FALSE;

    va_start(ap,fmt);
    out = util_vasprintf(fmt,ap);
    va_end(ap);

    result = ws_addresponseheader(pwsc, header, "%s", out);
    free(out);
    return result;

}

EXPORT void pi_ws_emitheaders(WS_CONNINFO *pwsc) {
    ASSERT(pwsc);

    if(!pwsc)
        return;

    ws_emitheaders(pwsc);
}

EXPORT char *pi_ws_getrequestheader(WS_CONNINFO *pwsc, char *header) {
    ASSERT((pwsc) && (header));

    if((!pwsc) || (!header))
        return NULL;

    return ws_getrequestheader(pwsc, header);
}

EXPORT int pi_ws_writebinary(WS_CONNINFO *pwsc, char *data, int len) {
    ASSERT((pwsc) && (data) && (len));

    if((!pwsc) || (!data) || (!len))
        return 0;

    return ws_writebinary(pwsc, data, len);
}

EXPORT char *pi_ws_gethostname(WS_CONNINFO *pwsc) {
    ASSERT(pwsc);

    if(!pwsc)
        return NULL;

    return ws_hostname(pwsc);
}

EXPORT int pi_ws_matchesrole(WS_CONNINFO *pwsc, char *username,
                             char *password, char *role) {
    ASSERT((pwsc) && (role));

    if((!pwsc) || (!role))
        return FALSE;

    return config_matches_role(pwsc, username, password, role);
}

/* misc helpers */
EXPORT char *pi_server_ver(void) {
    return VERSION;
}

EXPORT int pi_server_name(char *name, int *len) {
    char *servername;

    ASSERT((name) && (len));

    if((!name) || (!len))
        return FALSE;

    servername = conf_get_servername();

    /* FIXME: this is stupid */
    if((servername) && (strlen(servername) < (size_t)len)) {
        strcpy(name,servername);
    } else {
        if((size_t)len > strlen("Firefly Media Server"))
            strcpy(name,"Firefly Media Server");
    }

    free(servername);
    return CONF_E_SUCCESS;
}

EXPORT void pi_log(int level, char *fmt, ...) {
    char *out;
    va_list ap;

    va_start(ap,fmt);
    out=util_vasprintf(fmt,ap);
    va_end(ap);

    DPRINTF(level,L_PLUG,"%s",out);
    free(out);
}

/**
 * check to see if we can transcode
 *
 * @param codec the codec we are trying to serve
 * @returns TRUE if we can transcode, FALSE otherwise
 */
EXPORT int pi_should_transcode(WS_CONNINFO *pwsc, char *codec) {
    return plugin_ssc_should_transcode(pwsc,codec);
}


EXPORT int pi_db_count(void) {
    int count;
    db_get_song_count(NULL, &count);

    return count;
}

EXPORT int pi_db_enum_start(char **pe, DB_QUERY *pinfo) {
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

EXPORT int pi_db_enum_fetch_row(char **pe, char ***row, DB_QUERY *pinfo) {
    return db_enum_fetch_row(pe, (PACKED_MP3FILE*)row,
                             (DBQUERYINFO*)pinfo->priv);
}

EXPORT int pi_db_enum_end(char **pe) {
    return db_enum_end(pe);
}

EXPORT int pi_db_enum_restart(char **pe, DB_QUERY *pinfo) {
    DBQUERYINFO *pqi;

    pqi = (DBQUERYINFO*)pinfo->priv;
    return db_enum_reset(pe,pqi);
}

EXPORT void pi_db_enum_dispose(char **pe, DB_QUERY *pinfo) {
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

EXPORT void pi_stream(WS_CONNINFO *pwsc, char *id) {
    int session = 0;
    MP3FILE *pmp3;
    IOHANDLE hfile;
    uint64_t bytes_copied=0;
    uint64_t real_len;
    uint64_t file_len;
    uint64_t offset=0;
    int item;

    /* stream out the song */
    ws_should_close(pwsc,1);

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
    } else if (pi_should_transcode(pwsc,pmp3->codectype)) {
        /************************
         * Server side conversion
         ************************/
        config_set_status(pwsc,session,
                          "Transcoding '%s' (id %d)",
                          pmp3->title,pmp3->id);

        DPRINTF(E_WARN,L_WS,
                "Session %d: Streaming file '%s' to %s (offset %ld)\n",
                session,pmp3->fname, ws_hostname(pwsc),(long)offset);

        /* estimate the real length of this thing */
        bytes_copied =  plugin_ssc_transcode(pwsc,pmp3,offset,1);
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

        hfile = io_new();
        if(!hfile)
            DPRINTF(E_FATAL,L_WS,"Cannot allocate file handle\n");

        if(!io_open(hfile,"file://%U",pmp3->path)) {
            /* FIXME: ws_set_errstr */
            ws_set_err(pwsc,E_WS_NATIVE);
            DPRINTF(E_WARN,L_WS,"Thread %d: Error opening %s: %s\n",
                ws_threadno(pwsc),pmp3->path,io_errstr(hfile));
            ws_returnerror(pwsc,404,"Not found");
            config_set_status(pwsc,session,NULL);
            db_dispose_item(pmp3);
            io_dispose(hfile);
        } else {
            io_size(hfile,&real_len);
            file_len = real_len - offset;

            DPRINTF(E_DBG,L_WS,"Thread %d: Length of file (remaining): %lld\n",
                    ws_threadno(pwsc),file_len);

            // DWB:  fix content-type to correctly reflect data
            // content type (dmap tagged) should only be used on
            // dmap protocol requests, not the actually song data
            if(pmp3->type)
                ws_addresponseheader(pwsc,"Content-Type","audio/%s",pmp3->type);

            ws_addresponseheader(pwsc,"Content-Length","%ld",(long)file_len);

            if((ws_getrequestheader(pwsc,"user-agent")) &&
               (!strncmp(ws_getrequestheader(pwsc,"user-agent"),
                         "Hifidelio",9))) {
                ws_addresponseheader(pwsc,"Connection","Keep-Alive");
                ws_should_close(pwsc,0);
            } else {
                ws_addresponseheader(pwsc,"Connection","Close");
            }

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
                    session,pmp3->fname, ws_hostname(pwsc),(long)offset);

            if(offset) {
                DPRINTF(E_INF,L_WS,"Seeking to offset %ld\n",(long)offset);
                io_setpos(hfile,offset,SEEK_SET);
            }

            if(!ws_copyfile(pwsc,hfile,&bytes_copied)) {
                /* FIXME: Get webserver error string */
                DPRINTF(E_INF,L_WS,"Error copying file to remote...\n");
                ws_should_close(pwsc,1);
            } else {
                DPRINTF(E_INF,L_WS,"Finished streaming file to remote: %lld bytes\n",
                        bytes_copied);
            }

            config_set_status(pwsc,session,NULL);
            io_close(hfile);
            io_dispose(hfile);
            db_dispose_item(pmp3);
        }
        /* update play counts */
        if(bytes_copied  >= (real_len * 80 / 100)) {
            db_playcount_increment(NULL,pmp3->id);
            if(!offset)
                config.stats.songs_served++; /* FIXME: remove stat races */
        }
    }

    //    free(pqi);
}

EXPORT  int pi_db_add_playlist(char **pe, char *name, int type, char *clause,
                               char *path, int index, int *playlistid) {
    return db_add_playlist(pe, name, type, clause, path, index, playlistid);
}

EXPORT int pi_db_add_playlist_item(char **pe, int playlistid, int songid) {
    return db_add_playlist_item(pe, playlistid, songid);
}

EXPORT int pi_db_edit_playlist(char **pe, int id, char *name, char *clause) {
    return db_edit_playlist(pe, id, name, clause);
}

EXPORT int pi_db_delete_playlist(char **pe, int playlistid) {
    return db_delete_playlist(pe, playlistid);
}

EXPORT int pi_db_delete_playlist_item(char **pe, int playlistid, int songid) {
    return db_delete_playlist_item(pe, playlistid, songid);
}

EXPORT int pi_db_revision(void) {
    return db_revision();
}

/* FIXME: error checking */
EXPORT int pi_db_count_items(int what) {
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

EXPORT int pi_db_wait_update(WS_CONNINFO *pwsc) {
    int clientver=1;
    int lastver=0;
    IO_WAITHANDLE hwait;
    uint32_t ms;

    if(ws_getvar(pwsc,"revision-number")) {
        clientver=atoi(ws_getvar(pwsc,"revision-number"));
    }

    /* wait for db_version to be stable for 30 seconds */
    hwait = io_wait_new();
    if(!hwait)
        DPRINTF(E_FATAL,L_MISC,"Can't get wait handle in db_wait_update\n");

    /* FIXME: Move this up into webserver to avoid groping around
     * inside reserved data structures */

    io_wait_add(hwait,pwsc->hclient,IO_WAIT_ERROR);

    while((clientver == db_revision()) ||
          (lastver && (db_revision() != lastver))) {
        lastver = db_revision();

        if(!io_wait(hwait,&ms) && (ms != 0)) {
            /* can't be ready for read, must be error */
            DPRINTF(E_DBG,L_DAAP,"Update session stopped\n");
            io_wait_dispose(hwait);
            return FALSE;
        }
    }

    io_wait_dispose(hwait);

    return TRUE;
}

EXPORT char *pi_conf_alloc_string(char *section, char *key, char *dflt) {
    return conf_alloc_string(section, key, dflt);
}

EXPORT void pi_conf_dispose_string(char *str) {
    free(str);
}

EXPORT int pi_conf_get_int(char *section, char *key, int dflt) {
    return conf_get_int(section, key, dflt);
}

EXPORT void pi_config_set_status(WS_CONNINFO *pwsc, int session, char *fmt, ...) {
}
