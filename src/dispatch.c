/*
 * $Id$
 * daap handler functions and dispatch code
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "db-generic.h"
#include "configfile.h"
#include "err.h"
#include "mp3-scanner.h"
#include "webserver.h"
#include "ssc.h"
#include "dynamic-art.h"
#include "restart.h"
#include "daapd.h"

/* Forwards */
static void dispatch_server_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_login(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_content_codes(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_update(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_dbinfo(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_playlistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_playlists(WS_CONNINFO *pqsc, DBQUERYINFO *pqi);
static void dispatch_items(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);
static void dispatch_logout(WS_CONNINFO *pwsc, DBQUERYINFO *pqi);





/**
 * Handles authentication for the daap server.  This isn't the
 * authenticator for the web admin page, but rather the iTunes 
 * authentication when trying to connect to the server.  Note that most
 * of this is actually handled in the web server registration, which
 * decides when to apply the authentication or not.  If you mess with
 * when and where the webserver applies auth or not, you'll likely 
 * break something.  It seems that some requests must be authed, and others
 * not.  If you apply authentication somewhere that iTunes doesn't expect
 * it, it happily disconnects.
 *
 * \param username The username passed by iTunes
 * \param password The password passed by iTunes
 * \returns 1 if auth successful, 0 otherwise
 */
int daap_auth(char *username, char *password) {
    if((password == NULL) && 
       ((config.readpassword == NULL) || (strlen(config.readpassword)==0)))
	return 1;

    if(password == NULL)
	return 0;

    return !strcasecmp(password,config.readpassword);
}

void daap_handler(WS_CONNINFO *pwsc) {
    DBQUERYINFO *pqi;
    char *token, *string, *save;

    pqi=(DBQUERYINFO*)malloc(sizeof(DBQUERYINFO));
    if(!pqi) {
	ws_returnerror(pwsc,500,"Internal server error: out of memory!");
	return;
    }

    memset(pqi,0x00,sizeof(DBQUERYINFO));

    /* Add some default headers */
    ws_addresponseheader(pwsc,"Accept-Ranges","bytes");
    ws_addresponseheader(pwsc,"DAAP-Server","mt-daapd/" VERSION);
    ws_addresponseheader(pwsc,"Content-Type","application/x-dmap-tagged");
    
    if(ws_getvar(pwsc,"session-id"))
	pqi->session_id = atoi(ws_getvar(pwsc,"session-id"));
    
    /* tokenize the uri for easier decoding */
    string=(pwsc->uri)+1;
    while((token=strtok_r(string,"/",&save))) {
	string=NULL;
	pqi->uri_sections[pqi->uri_count++] = token;
    }
    
    /* Start dispatching */
    if(!strcasecmp(pqi->uri_sections[0],"server-info"))
	return dispatch_server_info(pwsc,pqi);
    
    if(!strcasecmp(pqi->uri_sections[0],"content-codes"))
	return dispatch_content_codes(pwsc,pqi);

    if(!strcasecmp(pqi->uri_sections[0],"login"))
     	return dispatch_login(pwsc,pqi);

    if(!strcasecmp(pqi->uri_sections[0],"update"))
	return dispatch_update(pwsc,pqi);

    if(!strcasecmp(pqi->uri_sections[0],"logout"))
	return dispatch_logout(pwsc,pqi);

    /*
     * /databases/id/items
     * /databases/id/containers
     * /databases/id/containers/id/items
     * /databases/id/browse/category
     * /databases/id/items/id.mp3
     */
    if(!strcasecmp(pqi->uri_sections[0],"databases")) {
	if(pqi->uri_count == 1) {
	    return dispatch_dbinfo(pwsc,pqi);
	}
	pqi->db_id=atoi(pqi->uri_sections[1]);
	if(pqi->uri_count == 3) {
	    if(!strcasecmp(pqi->uri_sections[2],"items"))
		return dispatch_items(pwsc,pqi);
	    if(!strcasecmp(pqi->uri_sections[2],"containers"))
		return dispatch_playlists(pwsc,pqi);
	    
	    pwsc->close=1;
	    free(pqi);
	    ws_returnerror(pwsc,404,"Page not found");
	    return;
	}
	if(pqi->uri_count == 4) {
	    if(!strcasecmp(pqi->uri_sections[2],"browse"))
		return dispatch_browse(pwsc,pqi);
	    if(!strcasecmp(pqi->uri_sections[2],"items"))
		return dispatch_stream(pwsc,pqi);
	    
	    pwsc->close=1;
	    free(pqi);
	    ws_returnerror(pwsc,404,"Page not found");
	    return;
	}
	if(pqi->uri_count == 5) {
	    if((!strcasecmp(pqi->uri_sections[2],"containers")) &&
	       (!strcasecmp(pqi->uri_sections[4],"items"))) {
		pqi->playlist_id=atoi(pqi->uri_sections[3]);
		return dispatch_playlistitems(pwsc,pqi);
	    }
	}
    }
    
    pwsc->close=1;
    free(pqi);
    ws_returnerror(pwsc,404,"Page not found");
    return;
}

void dispatch_stream(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    MP3FILE *pmp3;
    FILE *file_ptr;
    int file_fd;
    char *real_path;
    int bytes_copied;
    off_t real_len;
    off_t file_len;
    off_t offset=0;
    long img_size;
    struct stat sb;
    int img_fd;
    int item;

    /* stream out the song */
    pwsc->close=1;

    item=atoi(pqi->uri_sections[3]);

    if(ws_getrequestheader(pwsc,"range")) { 
	offset=(off_t)atol(ws_getrequestheader(pwsc,"range") + 6);
    }

    pmp3=db_fetch_item(item);
    if(!pmp3) {
	DPRINTF(E_LOG,L_DAAP|L_WS|L_DB,"Could not find requested item %lu\n",item);
	ws_returnerror(pwsc,404,"File Not Found");
    } else if ((real_path=server_side_convert_path(pmp3->path)) != NULL) {
	/************************
	 * Server side conversion
	 ************************/
	DPRINTF(E_WARN,L_WS,"Thread %d: Autoconvert file %s for client\n",
		pwsc->threadno,real_path);
	file_ptr = server_side_convert_open(real_path,
					    offset,
					    pmp3->song_length);
	if (file_ptr) {
	    file_fd = fileno(file_ptr);
	} else {
	    file_fd = -1;
	}
	if(file_fd == -1) {
	    if (file_ptr) {
		server_side_convert_close(file_ptr);
	    }
	    pwsc->error=errno;
	    DPRINTF(E_WARN,L_WS,
		    "Thread %d: Error opening %s for conversion\n",
		    pwsc->threadno,real_path);
	    ws_returnerror(pwsc,404,"Not found");
	    config_set_status(pwsc,pqi->session_id,NULL);
	    db_dispose_item(pmp3);
	    free(pmp3);
	    free(real_path);
	} else {
	    if(pmp3->type)
		ws_addresponseheader(pwsc,"Content-Type","audio/%s",
				     pmp3->type);
	    // Also content-length -heade would be nice, but since
	    // we don't really know it here, so let's leave it out.
	    ws_addresponseheader(pwsc,"Connection","Close");

	    if(!offset)
		ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
	    else {
		// This is actually against the protocol, since
		// range MUST be explicit according to HTTP-standard
		// Seems to work at least with iTunes.
		ws_addresponseheader(pwsc,
				     "Content-Range","bytes %ld-*/*",
				     (long)offset);
		ws_writefd(pwsc,"HTTP/1.1 206 Partial Content\r\n");
	    }

	    ws_emitheaders(pwsc);

	    config_set_status(pwsc,pqi->session_id,
			      "Streaming file via convert filter '%s'",
			      pmp3->fname);
	    DPRINTF(E_LOG,L_WS,
		    "Session %d: Streaming file '%s' to %s (offset %ld)\n",
		    pqi->session_id,pmp3->fname, pwsc->hostname,(long)offset);
		
	    if(!offset)
		config.stats.songs_served++; /* FIXME: remove stat races */
	    if((bytes_copied=copyfile(file_fd,pwsc->fd)) == -1) {
		DPRINTF(E_INF,L_WS,
			"Error copying converted file to remote... %s\n",
			strerror(errno));
	    } else {
		DPRINTF(E_INF,L_WS,
			"Finished streaming converted file to remote\n");
	    }
	    server_side_convert_close(file_ptr);
	    config_set_status(pwsc,pqi->session_id,NULL);
	    db_dispose_item(pmp3);
	    free(pmp3);
	    free(real_path);
	}
    } else {
	/**********************
	 * stream file normally
	 **********************/
	file_fd=r_open2(pmp3->path,O_RDONLY);
	if(file_fd == -1) {
	    pwsc->error=errno;
	    DPRINTF(E_WARN,L_WS,"Thread %d: Error opening %s: %s\n",
		    pwsc->threadno,pmp3->path,strerror(errno));
	    ws_returnerror(pwsc,404,"Not found");
	    config_set_status(pwsc,pqi->session_id,NULL);
	    db_dispose_item(pmp3);
	    free(pmp3);
	} else {
	    real_len=lseek(file_fd,0,SEEK_END);
	    lseek(file_fd,0,SEEK_SET);

	    /* Re-adjust content length for cover art */
	    if((config.artfilename) &&
	       ((img_fd=da_get_image_fd(pmp3->path)) != -1)) {
		fstat(img_fd, &sb);
		img_size = sb.st_size;
		
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
	    
	    config_set_status(pwsc,pqi->session_id,"Streaming file '%s'",pmp3->fname);
	    DPRINTF(E_LOG,L_WS,"Session %d: Streaming file '%s' to %s (offset %d)\n",
		    pqi->session_id,pmp3->fname, pwsc->hostname,(long)offset);
	    
	    if(!offset)
		config.stats.songs_served++; /* FIXME: remove stat races */
	    
	    if((config.artfilename) &&
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
	    
	    config_set_status(pwsc,pqi->session_id,NULL);
	    r_close(file_fd);
	    db_dispose_item(pmp3);
	    free(pmp3);
	}
    }

    free(pqi);
}

void dispatch_playlistitems(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char items_response[61];
    char *current=items_response;
    int song_count;
    int list_length;
    unsigned char *block;

    if(ws_getvar(pwsc,"meta")) {
	pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
	pqi->meta = ((1ll << metaItemId) |
		     (1ll << metaItemName) |
		     (1ll << metaItemKind) |
		     (1ll << metaContainerItemId) |
		     (1ll << metaParentContainerId));
    }

    /* should build the query string here, too */
    pqi->whereclause = NULL;

    pqi->query_type = queryTypePlaylistItems;
    pqi->index_type=indexTypeNone;
    if(db_enum_start(pqi)) {
	DPRINTF(E_LOG,L_DAAP,"Could not start enum\n");
	ws_returnerror(pwsc,500,"Internal server error: out of memory!");
	return;
    }
    
    list_length=db_enum_size(pqi,&song_count);

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d songs, dmap size: %d\n",song_count,list_length);

    current += db_dmap_add_container(current,"apso",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",song_count);  /* 12 */
    current += db_dmap_add_int(current,"mrco",song_count);  /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);  

    ws_addresponseheader(pwsc,"Content-Length","%d",61+list_length);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,items_response,61);

    while((list_length=db_enum_fetch(pqi,&block)) > 0) {
	DPRINTF(E_DBG,L_DAAP,"Got block of size %d\n",list_length);
	r_write(pwsc->fd,block,list_length);
	free(block);
    }

    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");

    db_enum_end();

    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_browse(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_playlists(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char playlist_response[61];
    char *current=playlist_response;
    int pl_count;
    int list_length;
    unsigned char *block;

    /* currently, this is ignored for playlist queries */
    if(ws_getvar(pwsc,"meta")) {
	pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
	pqi->meta = (MetaField_t) -1ll;
    }

    /* should build the query string here, too */
    pqi->whereclause = NULL;

    pqi->query_type = queryTypePlaylists;
    pqi->index_type=indexTypeNone;
    if(db_enum_start(pqi)) {
	DPRINTF(E_LOG,L_DAAP,"Could not start enum\n");
	ws_returnerror(pwsc,500,"Internal server error: out of memory!");
	return;
    }
    
    list_length=db_enum_size(pqi,&pl_count);

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d playlists, dmap size: %d\n",pl_count,list_length);

    current += db_dmap_add_container(current,"aply",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",pl_count);    /* 12 */
    current += db_dmap_add_int(current,"mrco",pl_count);    /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);  

    ws_addresponseheader(pwsc,"Content-Length","%d",61+list_length);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,playlist_response,61);

    while((list_length=db_enum_fetch(pqi,&block)) > 0) {
	DPRINTF(E_DBG,L_DAAP,"Got block of size %d\n",list_length);
	r_write(pwsc->fd,block,list_length);
	free(block);
    }

    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");

    db_enum_end();

    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_items(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char items_response[61];
    char *current=items_response;
    int song_count;
    int list_length;
    unsigned char *block;

    if(ws_getvar(pwsc,"meta")) {
	pqi->meta = db_encode_meta(ws_getvar(pwsc,"meta"));
    } else {
	pqi->meta = (MetaField_t) -1ll;
    }

    /* should build the query string here, too */
    pqi->whereclause = NULL;

    pqi->query_type = queryTypeItems;
    pqi->index_type=indexTypeNone;
    if(db_enum_start(pqi)) {
	DPRINTF(E_LOG,L_DAAP,"Could not start enum\n");
	ws_returnerror(pwsc,500,"Internal server error: out of memory!");
	return;
    }
    
    list_length=db_enum_size(pqi,&song_count);

    DPRINTF(E_DBG,L_DAAP,"Item enum:  got %d songs, dmap size: %d\n",song_count,list_length);

    current += db_dmap_add_container(current,"adbs",list_length + 53);
    current += db_dmap_add_int(current,"mstt",200);         /* 12 */
    current += db_dmap_add_char(current,"muty",0);          /*  9 */
    current += db_dmap_add_int(current,"mtco",song_count);  /* 12 */
    current += db_dmap_add_int(current,"mrco",song_count);  /* 12 */
    current += db_dmap_add_container(current,"mlcl",list_length);  

    ws_addresponseheader(pwsc,"Content-Length","%d",61+list_length);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,items_response,61);

    while((list_length=db_enum_fetch(pqi,&block)) > 0) {
	DPRINTF(E_DBG,L_DAAP,"Got block of size %d\n",list_length);
	r_write(pwsc->fd,block,list_length);
	free(block);
    }

    DPRINTF(E_DBG,L_DAAP,"Done enumerating.\n");

    db_enum_end();

    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_update(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char update_response[32];
    int clientver=1;
    fd_set rset;
    struct timeval tv;
    int result;
    char *current=update_response;

    DPRINTF(E_DBG,L_DAAP,"Preparing to send update response\n");

    if(ws_getvar(pwsc,"revision-number")) {
	clientver=atoi(ws_getvar(pwsc,"revision-number"));
    }

    while(clientver == db_revision()) {
	FD_ZERO(&rset);
	FD_SET(pwsc->fd,&rset);

	tv.tv_sec=30;
	tv.tv_usec=0;

	result=select(pwsc->fd+1,&rset,NULL,NULL,&tv);
	if(FD_ISSET(pwsc->fd,&rset)) {
	    /* can't be ready for read, must be error */
	    DPRINTF(E_DBG,L_DAAP,"Socket closed?\n");
	    
	    return;
	}
    }

    /* otherwise, send the info about this version */
    current += db_dmap_add_container(current,"mupd",24);
    current += db_dmap_add_int(current,"mstt",200);       /* 12 */
    current += db_dmap_add_int(current,"musr",db_revision());   /* 12 */

    ws_addresponseheader(pwsc,"Content-Length","32");
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,update_response,32);

    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
}

void dispatch_dbinfo(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char dbinfo_response[255];  /* FIXME */
    char *current = dbinfo_response;
    int namelen;

    namelen=strlen(config.servername);

    current += db_dmap_add_container(current,"avdb",105 + namelen);
    current += db_dmap_add_int(current,"mstt",200);                      /* 12 */
    current += db_dmap_add_char(current,"muty",0);                       /*  9 */
    current += db_dmap_add_int(current,"mtco",1);                        /* 12 */
    current += db_dmap_add_int(current,"mrco",1);                        /* 12 */
    current += db_dmap_add_container(current,"mlcl",52 + namelen);
    current += db_dmap_add_container(current,"mlit",44 + namelen); 
    current += db_dmap_add_int(current,"miid",1);                        /* 12 */
    current += db_dmap_add_string(current,"minm",config.servername);     /* 8 + namelen */
    current += db_dmap_add_int(current,"mimc",db_get_song_count());      /* 12 */
    current += db_dmap_add_int(current,"mctc",db_get_playlist_count());  /* 12 */

    ws_addresponseheader(pwsc,"Content-Length","%d",113 + namelen);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,dbinfo_response,113 + namelen);
    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_logout(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    ws_returnerror(pwsc,204,"Logout Successful");
}


void dispatch_login(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char login_response[32];
    char *current = login_response;
    int session;
    
    session = config_get_next_session();

    current += db_dmap_add_container(current,"mlog",24);
    current += db_dmap_add_int(current,"mstt",200);       /* 12 */
    current += db_dmap_add_int(current,"mlid",session);   /* 12 */

    ws_addresponseheader(pwsc,"Content-Length","32");
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,login_response,32);
    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
    return;
}

void dispatch_content_codes(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char content_codes[20];
    char mdcl[256];  /* FIXME: Don't make this static */
    int len;
    DAAP_ITEMS *dicurrent;

    char *current=content_codes;

    dicurrent=taglist;
    len=0;
    while(dicurrent->type) {
	len += (8 + 12 + 10 + 8 + strlen(dicurrent->description));
	dicurrent++;
    }

    current += db_dmap_add_container(current,"mccr",len + 12);
    current += db_dmap_add_int(current,"mstt",200);
    
    
    ws_addresponseheader(pwsc,"Content-Length","%d",len+20);
    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);
    r_write(pwsc->fd,content_codes,20);
    
    dicurrent=taglist;
    while(dicurrent->type) {
	current=mdcl;
	len = 12 + 10 + 8 + strlen(dicurrent->description);
	current += db_dmap_add_container(current,"mdcl",len);
	current += db_dmap_add_string(current,"mcnm",dicurrent->tag);         /* 12 */
	current += db_dmap_add_string(current,"mcna",dicurrent->description); /* 8 + descr */
	current += db_dmap_add_short(current,"mcty",dicurrent->type);         /* 10 */
	r_write(pwsc->fd,mdcl,len+8);
	dicurrent++;
    }

    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
}

void dispatch_server_info(WS_CONNINFO *pwsc, DBQUERYINFO *pqi) {
    char server_info[256]; /* FIXME: Don't make this static */
    char *current = server_info;
    char *client_version;
    int mpro = 2 << 16;
    int apro = 3 << 16;

    int actual_length=130 + strlen(config.servername);

    if(actual_length > sizeof(server_info)) {
	DPRINTF(E_FATAL,L_DAAP,"Server name too long.\n");
    }

    client_version=ws_getrequestheader(pwsc,"Client-DAAP-Version");
    
    current += db_dmap_add_container(current,"msrv",actual_length - 8);
    current += db_dmap_add_int(current,"mstt",200);        /* 12 */

    if((client_version) && (!strcmp(client_version,"1.0"))) {
	mpro = 1 << 16;
	apro = 1 << 16;
    }

    if((client_version) && (!strcmp(client_version,"2.0"))) {
	mpro = 1 << 16;
	apro = 2 << 16;
    }

    current += db_dmap_add_int(current,"mpro",mpro);       /* 12 */
    current += db_dmap_add_int(current,"apro",apro);       /* 12 */
    current += db_dmap_add_int(current,"mstm",1800);       /* 12 */
    current += db_dmap_add_string(current,"minm",config.servername); /* 8 + strlen(name) */

    current += db_dmap_add_char(current,"msau",            /* 9 */
				config.readpassword != NULL ? 2 : 0); 
    current += db_dmap_add_char(current,"msex",0);         /* 9 */
    current += db_dmap_add_char(current,"msix",0);         /* 9 */
    current += db_dmap_add_char(current,"msbr",0);         /* 9 */
    current += db_dmap_add_char(current,"msqy",0);         /* 9 */
    current += db_dmap_add_char(current,"msup",0);         /* 9 */
    current += db_dmap_add_int(current,"msdc",1);          /* 12 */

    ws_addresponseheader(pwsc,"Content-Length","%d",actual_length);

    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    r_write(pwsc->fd,server_info,actual_length);
    config_set_status(pwsc,pqi->session_id,NULL);
    free(pqi);
}

