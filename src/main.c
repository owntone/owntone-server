/*
 * $Id$
 * Driver for multi-threaded daap server
 *
 * Copyright (C) 2003 Ron Pedde (ron@corbey.com)
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
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "configfile.h"
#include "db-memory.h"
#include "daap.h"
#include "daap-proto.h"
#include "err.h"
#include "mp3-scanner.h"
#include "rend.h"
#include "webserver.h"

// 3689


/*
 * Globals
 */
CONFIG config;

/* 
 * daap_handler
 *
 * Handle daap-related web pages
 */
void daap_handler(WS_CONNINFO *pwsc) {
    int len;
    int close;
    DAAP_BLOCK *root,*error;
    int compress=0;
    int clientrev;

    /* for the /databases URI */
    char *uri;
    int db_index;
    int playlist_index;
    int item;
    char *first, *last;
    int streaming=0;

    MP3FILE *pmp3;
    int file_fd;

    close=pwsc->close;
    pwsc->close=1;  /* in case we have any errors */
    root=NULL;

    ws_addresponseheader(pwsc,"Accept-Ranges","bytes");
    ws_addresponseheader(pwsc,"DAAP-Server","iTunes/4.1 (Mac OS X)");
    ws_addresponseheader(pwsc,"Content-Type","application/x-dmap-tagged");


    if(!strcasecmp(pwsc->uri,"/server-info")) {
	root=daap_response_server_info();
    } else if (!strcasecmp(pwsc->uri,"/content-codes")) {
	root=daap_response_content_codes();
    } else if (!strcasecmp(pwsc->uri,"/login")) {
	root=daap_response_login();
    } else if (!strcasecmp(pwsc->uri,"/update")) {
	if(!ws_getvar(pwsc,"delta")) { /* first check */
	    clientrev=db_version() - 1;
	} else {
	    clientrev=atoi(ws_getvar(pwsc,"delta"));
	}
	root=daap_response_update(clientrev);
    } else if (!strcasecmp(pwsc->uri,"/logout")) {
	ws_returnerror(pwsc,204,"Logout Successful");
	return;
    } else if(strcmp(pwsc->uri,"/databases")==0) {
	root=daap_response_dbinfo();
    } else if(strncmp(pwsc->uri,"/databases/",11) == 0) {

	/* the /databases/ uri will either be:
	 *
	 * /databases/id/items, which returns items in a db
	 * /databases/id/containers, which returns a container
	 * /databases/id/containers/id/items, which returns playlist elements
	 * /databases/id/items/id.mp3, to spool an mp3
	 */

	uri = strdup(pwsc->uri);
	first=(char*)&uri[11];
	last=first;
	while((*last) && (*last != '/')) {
	    last++;
	}
	
	if(*last) {
	    *last='\0';
	    db_index=atoi(first);
	    
	    last++;

	    if(strncasecmp(last,"items/",6)==0) {
		/* streaming */
		first=last+6;
		while((*last) && (*last != '.')) 
		    last++;

		if(*last == '.') {
		    *last='\0';
		    item=atoi(first);
		    streaming=1;
		}
		free(uri);
	    } else if (strncasecmp(last,"items",5)==0) {
		/* songlist */
		free(uri);
		root=daap_response_songlist();
	    } else if (strncasecmp(last,"containers/",11)==0) {
		/* playlist elements */
		first=last + 11;
		last=first;
		while((*last) && (*last != '/')) {
		    last++;
		}
	
		if(*last) {
		    *last='\0';
		    playlist_index=atoi(first);
		    root=daap_response_playlist_items(playlist_index);
		}
		free(uri);
	    } else if (strncasecmp(last,"containers",10)==0) {
		/* list of playlists */
		free(uri);
		root=daap_response_playlists();
	    }
	}
    }

    if((!root)&&(!streaming)) {
	ws_returnerror(pwsc,400,"Invalid Request");
	return;
    }

    pwsc->close=close;

    if(!streaming) {
	ws_addresponseheader(pwsc,"Content-Length","%d",root->reported_size + 8);
	ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
	ws_emitheaders(pwsc);

	/*
	  if(ws_testrequestheader(pwsc,"Accept-Encoding","gzip")) {
	  ws_addresponseheader(pwsc,"Content-Encoding","gzip");
	  compress=1;
	  }
	*/

	daap_serialize(root,pwsc->fd,0);
	daap_free(root);
    } else {
	/* stream out the song */
	pwsc->close=1;

	pmp3=db_find(item);
	if(!pmp3) {
	    ws_returnerror(pwsc,404,"File Not Found");
	} else {
	    /* got the file, let's open and serve it */
	    file_fd=r_open2(pmp3->path,O_RDONLY);
	    if(file_fd == -1) {
		pwsc->error=errno;
		DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
			pwsc->threadno,pmp3->path,strerror(errno));
		ws_returnerror(pwsc,404,"Not found");
	    } else {
		ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
		ws_addresponseheader(pwsc,"Connection","Close");
		ws_emitheaders(pwsc);

		copyfile(file_fd,pwsc->fd);
		r_close(file_fd);
	    }
	}
    }

    return;
}

/* 
 * config_handler
 *
 * Handle config web pages
 */

void usage(char *program) {
    printf("Usage: %s [options]\n\n",program);
    printf("Options:\n");
#ifdef DEBUG    
    printf("  -d <number>    Debuglevel (0-9)\n");
#endif
    printf("  -m             Use mDNS\n");
    printf("  -c <file>      Use configfile specified");
    printf("\n\n");
}

int main(int argc, char *argv[]) {
    int option;
    char *configfile=NULL;
    WSCONFIG ws_config;
    WSHANDLE server;
    pid_t rendezvous_pid;
    int use_mdns=0;
    ENUMHANDLE handle;
    MP3FILE *pmp3;

#ifdef DEBUG
    char *optval="d:c:m";
#else
    char *optval="c:m";
#endif /* DEBUG */

    printf("mt-daapd: version $Revision$\n");
    printf("Copyright (c) 2003 Ron Pedde.  All rights reserved\n");
    printf("Portions Copyright (c) 1999-2001 Apple Computer, Inc.  All rights Reserved.\n\n");

    while((option=getopt(argc,argv,optval)) != -1) {
	switch(option) {
#ifdef DEBUG
	case 'd':
	    err_debuglevel=atoi(optarg);
	    break;
#endif
	case 'c':
	    configfile=optarg;
	    break;

	case 'm':
	    use_mdns=1;
	    break;

	default:
	    usage(argv[0]);
	    exit(EXIT_FAILURE);
	    break;
	}
    }

    /* read the configfile, if specified, otherwise
     * try defaults */

    if(!configfile) {
	if(config_read("/etc/mt-daapd.conf"))
	    if(config_read("./mt-daapd.conf")) {
		perror("configfile_read");
		exit(EXIT_FAILURE);
	    }
    } else {
	if(config_read(configfile)) {
	    perror("config_read");
	    exit(EXIT_FAILURE);
	}
    }

    DPRINTF(ERR_DEBUG,"Initializing database\n");

    /* Initialize the database before starting */
    if(db_init("none")) {
	perror("db_init");
	exit(EXIT_FAILURE);
    }

    printf("Scanning MP3s\n");

    if(scan_init(config.mp3dir)) {
	perror("scan_init");
	exit(EXIT_FAILURE);
    }

    printf("Done... starting server\n");

    /* start up the web server */
    ws_config.web_root=config.web_root;
    ws_config.port=config.port;

    server=ws_start(&ws_config);
    if(!server) {
	perror("ws_start");
	return EXIT_FAILURE;
    }

    ws_registerhandler(server, "^.*$",config_handler,config_auth,1);
    ws_registerhandler(server, "^/server-info$",daap_handler,NULL,0);
    ws_registerhandler(server, "^/content-codes$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/login$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/update$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/databases$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/logout$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/databases/.*",daap_handler,NULL,0);

    if(use_mdns)
	rend_init(&rendezvous_pid);

    while(1) {
	sleep(20);
    }

    return EXIT_SUCCESS;
}

