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

    close=pwsc->close;
    pwsc->close=1;

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
	if(!ws_getvar(pwsc,"revision-number")) { /* first check */
	    clientrev=db_version() - 1;
	} else {
	    clientrev=atoi(ws_getvar(pwsc,"revision-number"));
	}
	root=daap_response_update(clientrev);
    } else if (!strcasecmp(pwsc->uri,"/databases")) {
	root=daap_response_databases(pwsc->uri);
    } else if (!strcasecmp(pwsc->uri,"/logout")) {
	ws_returnerror(pwsc,204,"Logout Successful");
	return;
    } else if (!strncasecmp(pwsc->uri,"/databases/",11)) {
	root=daap_response_databases(pwsc->uri);
    } else {
	DPRINTF(ERR_WARN,"Bad handler!  Can't find uri handler for %s\n",
		pwsc->uri);
	return;
    }

    if(!root) {
	ws_returnerror(pwsc,400,"Invalid Request");
	return;
    }

    pwsc->close=close;

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

    return;
}

/* 
 * config_handler
 *
 * Handle config web pages
 */

void config_handler(WS_CONNINFO *pwsc) {
    char path[PATH_MAX];
    char resolved_path[PATH_MAX];
    int file_fd;
    struct stat sb;
    char argbuffer[30];
    int in_arg;
    char *argptr;
    char next;
    
    pwsc->close=1;
    ws_addresponseheader(pwsc,"Connection","close");

    snprintf(path,PATH_MAX,"%s/%s",config.web_root,pwsc->uri);
    if(!realpath(path,resolved_path)) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Cannot resolve %s\n",path);
	ws_returnerror(pwsc,404,"Not found");
	return;
    }

    /* this should really return a 302:Found */
    stat(resolved_path,&sb);
    if(sb.st_mode & S_IFDIR)
	strcat(resolved_path,"/index.html");

    DPRINTF(ERR_DEBUG,"Thread %d: Preparing to serve %s\n",
	    pwsc->threadno, resolved_path);

    if(strncmp(resolved_path,config.web_root,
	       strlen(config.web_root))) {
	pwsc->error=EINVAL;
	DPRINTF(ERR_WARN,"Thread %d: Requested file %s out of root\n",
		pwsc->threadno,resolved_path);
	ws_returnerror(pwsc,403,"Forbidden");
	return;
    }

    file_fd=open(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
		pwsc->threadno,resolved_path,strerror(errno));
	ws_returnerror(pwsc,404,"Not found");
	return;
    }
    
    if(strcasecmp(pwsc->uri,"/config-update.html")==0) {
	/* we need to update stuff */
	argptr=ws_getvar(pwsc,"adminpw");
	if(argptr) {
	    if(config.adminpassword)
		free(config.adminpassword);
	    config.adminpassword=strdup(argptr);
	}
    }

    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    /* now throw out the file, with replacements */
    in_arg=0;
    argptr=argbuffer;

    while(1) {
	if(r_read(file_fd,&next,1) <= 0)
	    break;

	if(in_arg) {
	    if(next == '@') {
		in_arg=0;
		if(strcasecmp(argbuffer,"WEB_ROOT") == 0) {
		    ws_writefd(pwsc,"%s",config.web_root);
		} else if (strcasecmp(argbuffer,"PORT") == 0) {
		    ws_writefd(pwsc,"%d",config.port);
		} else if (strcasecmp(argbuffer,"ADMINPW") == 0) {
		    ws_writefd(pwsc,"%s",config.adminpassword);
		} else if (strcasecmp(argbuffer,"RELEASE") == 0) {
		    ws_writefd(pwsc,"mt-daapd %s\n",VERSION);
		} else if (strcasecmp(argbuffer,"MP3DIR") == 0) {
		    ws_writefd(pwsc,"%s",config.mp3dir);
		} else {
		    ws_writefd(pwsc,"@ERR@");
		}
	    } else {
		if((argptr - argbuffer) < (sizeof(argbuffer)-1))
		    *argptr++ = next;
	    }
	} else {
	    if(next == '@') {
		argptr=argbuffer;
		memset(argbuffer,0,sizeof(argbuffer));
		in_arg=1;
	    } else {
		if(r_write(pwsc->fd,&next,1) == -1)
		    break;
	    }
	}
    }

    close(file_fd);
    DPRINTF(ERR_DEBUG,"Thread %d: Served successfully\n",pwsc->threadno);
    return;
}

int config_auth(char *user, char *password) {
    return !strcmp(password,config.adminpassword);
}


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
	if(config_read("/etc/mt-daapd.conf",&config))
	    if(config_read("./mt-daapd.conf",&config)) {
		perror("configfile_read");
		exit(EXIT_FAILURE);
	    }
    } else {
	if(config_read(configfile,&config)) {
	    perror("config_read");
	    exit(EXIT_FAILURE);
	}
    }
	    
    /* Initialize the database before starting */
    if(db_init("none")) {
	perror("db_init");
	exit(EXIT_FAILURE);
    }

    if(scan_init(config.mp3dir)) {
	perror("scan_init");
	exit(EXIT_FAILURE);
    }

#ifdef DEBUG
    printf("Dump of database:\n");
    handle=db_enum_begin();
    while(pmp3=db_enum(&handle)) {
	printf("File: %s\n",pmp3->fname);
    }
    db_enum_end();

    
#endif    

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

