/*
 * $Id$
 * Driver for multi-threaded daap server
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
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pthread.h>
#include <pwd.h>
#include <restart.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "configfile.h"
#include "db-memory.h"
#include "daap.h"
#include "daap-proto.h"
#include "err.h"
#include "mp3-scanner.h"
#include "rend.h"
#include "webserver.h"
#include "playlist.h"

#define DEFAULT_CONFIGFILE "/etc/mt-daapd.conf"

#ifndef SIGCLD
# define SIGCLD SIGCHLD
#endif

/*
 * Globals
 */
CONFIG config;

/* 
 * Forwards
 */
RETSIGTYPE sig_child(int signal);
int daemon_start(int reap_children);

/*
 * daap_auth
 *
 * Auth handler for the daap server
 */
int daap_auth(char *username, char *password) {
    if((password == NULL) && 
       ((config.readpassword == NULL) || (strlen(config.readpassword)==0)))
	return 1;

    if(password == NULL)
	return 0;

    return !strcasecmp(password,config.readpassword);
}

/* 
 * daap_handler
 *
 * Handle daap-related web pages
 */
void daap_handler(WS_CONNINFO *pwsc) {
    int close;
    DAAP_BLOCK *root;
    int clientrev;

    /* for the /databases URI */
    char *uri;
    int db_index;
    int playlist_index;
    int item=0;
    char *first, *last;
    int streaming=0;

    MP3FILE *pmp3;
    int file_fd;
    int session_id=0;

    off_t offset=0;
    off_t file_len;

    close=pwsc->close;
    pwsc->close=1;  /* in case we have any errors */
    root=NULL;

    ws_addresponseheader(pwsc,"Accept-Ranges","bytes");
    ws_addresponseheader(pwsc,"DAAP-Server","mt-daapd/%s",VERSION);
    ws_addresponseheader(pwsc,"Content-Type","application/x-dmap-tagged");

    if(ws_getvar(pwsc,"session-id")) {
	session_id=atoi(ws_getvar(pwsc,"session-id"));
    }

    if(!strcasecmp(pwsc->uri,"/server-info")) {
	config_set_status(pwsc,session_id,"Sending server info");
	root=daap_response_server_info(config.servername);
    } else if (!strcasecmp(pwsc->uri,"/content-codes")) {
	config_set_status(pwsc,session_id,"Sending content codes");
	root=daap_response_content_codes();
    } else if (!strcasecmp(pwsc->uri,"/login")) {
	config_set_status(pwsc,session_id,"Logging in");
	root=daap_response_login();
    } else if (!strcasecmp(pwsc->uri,"/update")) {
	if(!ws_getvar(pwsc,"delta")) { /* first check */
	    clientrev=db_version() - 1;
	    config_set_status(pwsc,session_id,"Sending database");
	} else {
	    clientrev=atoi(ws_getvar(pwsc,"delta"));
	    config_set_status(pwsc,session_id,"Waiting for DB updates");
	}
	root=daap_response_update(pwsc->fd,clientrev);
    } else if (!strcasecmp(pwsc->uri,"/logout")) {
	config_set_status(pwsc,session_id,NULL);
	ws_returnerror(pwsc,204,"Logout Successful");
	return;
    } else if(strcmp(pwsc->uri,"/databases")==0) {
	config_set_status(pwsc,session_id,"Sending database info");
	root=daap_response_dbinfo(config.servername);
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
		config_set_status(pwsc,session_id,"Sending songlist");
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
		config_set_status(pwsc,session_id,"Sending playlist info");
	    } else if (strncasecmp(last,"containers",10)==0) {
		/* list of playlists */
		free(uri);
		root=daap_response_playlists(config.servername);
		config_set_status(pwsc,session_id,"Sending playlist info");
	    }
	}
    }

    if((!root)&&(!streaming)) {
	DPRINTF(ERR_DEBUG,"Bad request -- root=%x, streaming=%d\n",root,streaming);
	ws_returnerror(pwsc,400,"Invalid Request");
	config_set_status(pwsc,session_id,NULL);
	return;
    }

    pwsc->close=close;

    if(!streaming) {
	DPRINTF(ERR_DEBUG,"Satisfying request\n");
	ws_addresponseheader(pwsc,"Content-Length","%d",root->reported_size + 8);
	ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");

	DPRINTF(ERR_DEBUG,"Emitting headers\n");
	ws_emitheaders(pwsc);

	/*
	  if(ws_testrequestheader(pwsc,"Accept-Encoding","gzip")) {
	  ws_addresponseheader(pwsc,"Content-Encoding","gzip");
	  compress=1;
	  }
	*/

	DPRINTF(ERR_DEBUG,"Serializing\n");
	daap_serialize(root,pwsc->fd,0);
	DPRINTF(ERR_DEBUG,"Done, freeing\n");
	daap_free(root);
    } else {
	/* stream out the song */
	pwsc->close=1;

	if(ws_getrequestheader(pwsc,"range")) { 
	    offset=atol(ws_getrequestheader(pwsc,"range") + 6);
	    DPRINTF(ERR_DEBUG,"Thread %d: Skipping to byte %ld\n",pwsc->threadno,offset);
	}

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
		config_set_status(pwsc,session_id,NULL);

	    } else {
		file_len=lseek(file_fd,0,SEEK_END);
		lseek(file_fd,0,SEEK_SET);
		file_len -= offset;

		DPRINTF(ERR_DEBUG,"Thread %d: Length of file (remaining) is %ld\n",
			pwsc->threadno,(long)file_len);
		ws_addresponseheader(pwsc,"Content-Length","%ld",(long)file_len);
		ws_addresponseheader(pwsc,"Connection","Close");

		ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
		ws_emitheaders(pwsc);

		config_set_status(pwsc,session_id,"Streaming file '%s'",pmp3->fname);
		DPRINTF(ERR_INFO,"Streaming %s\n",pmp3->fname);

		if(offset) {
		    DPRINTF(ERR_INFO,"Seeking to offset %d\n",offset);
		    lseek(file_fd,offset,SEEK_SET);
		}
		if(copyfile(file_fd,pwsc->fd)) {
		    DPRINTF(ERR_INFO,"Error copying file to remote... %s\n",
			    strerror(errno));
		}
		config_set_status(pwsc,session_id,NULL);
		r_close(file_fd);
	    }
	}
    }

    DPRINTF(ERR_DEBUG,"Finished serving DAAP response\n");

    return;
}

/*
 * sig_child
 *
 * reap children
 */
RETSIGTYPE sig_child(int signal)
{
    int status;

    while (wait(&status)) {
    };
}

/*
 * daemon_start
 *
 * This is pretty much stolen straight from Stevens
 */

int daemon_start(int reap_children)
{
    int childpid, fd;

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    // Fork and exit
    if ((childpid = fork()) < 0) {
	fprintf(stderr, "Can't fork!\n");
	return -1;
    } else if (childpid > 0)
	exit(0);

#ifdef SETPGRP_VOID
    setpgrp();
#else
    setpgrp(0,0);
#endif

#ifdef TIOCNOTTY
    if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
	ioctl(fd, TIOCNOTTY, (char *) NULL);
	close(fd);
    }
#endif

    if((fd = open("/dev/null", O_RDWR, 0)) != -1) {
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
	if (fd > 2)
	    close(fd);
    }

    /*
    for (fd = 0; fd < FOPEN_MAX; fd++)
	close(fd);
    */

    errno = 0;

    chdir("/");
    umask(0);

    if (reap_children) {
	signal(SIGCLD, sig_child);
    }
    return 0;
}

/*
 * usage
 *
 * print usage message
 */

void usage(char *program) {
    printf("Usage: %s [options]\n\n",program);
    printf("Options:\n");
    printf("  -d <number>    Debuglevel (0-9)\n");
    printf("  -m             Disable mDNS\n");
    printf("  -c <file>      Use configfile specified");
    printf("  -p             Parse playlist file\n");
    printf("  -f             Run in foreground\n");
    printf("\n\n");
}

/*
 * drop_privs
 *
 * drop privs to a specific user
 */
int drop_privs(char *user) {
    int err;
    struct passwd *pw=NULL;

    /* drop privs */
    if(getuid() == (uid_t)0) {
	pw=getpwnam(config.runas);
	if(pw) {
	    if(initgroups(user,pw->pw_gid) != 0 || 
	       setgid(pw->pw_gid) != 0 ||
	       setuid(pw->pw_uid) != 0) {
		err=errno;
		fprintf(stderr,"Couldn't change to %s, gid=%d, uid=%d\n",
			user,pw->pw_gid, pw->pw_uid);
		errno=err;
		return -1;
	    }
	} else {
	    err=errno;
	    fprintf(stderr,"Couldn't lookup user %s\n",user);
	    errno=err;
	    return -1;
	}
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int option;
    char *configfile=DEFAULT_CONFIGFILE;
    WSCONFIG ws_config;
    WSHANDLE server;
    int parseonly=0;
    int foreground=0;
    int start_time;
    int end_time;

    config.use_mdns=1;

    fprintf(stderr,"mt-daapd: version %s\n",VERSION);
    fprintf(stderr,"Copyright (c) 2003 Ron Pedde.  All rights reserved\n");
    fprintf(stderr,"Portions Copyright (c) 1999-2001 Apple Computer, Inc.  All rights Reserved.\n\n");

    while((option=getopt(argc,argv,"d:c:mpf")) != -1) {
	switch(option) {
	case 'd':
	    err_debuglevel=atoi(optarg);
	    break;
	case 'f':
	    foreground=1;
	    break;

	case 'c':
	    configfile=optarg;
	    break;

	case 'm':
	    config.use_mdns=0;
	    break;

	case 'p':
	    parseonly=1;
	    break;

	default:
	    usage(argv[0]);
	    exit(EXIT_FAILURE);
	    break;
	}
    }

    /* read the configfile, if specified, otherwise
     * try defaults */

    start_time=time(NULL);

    if(config_read(configfile)) {
	perror("config_read");
	exit(EXIT_FAILURE);
    }

    if((config.use_mdns) && (!parseonly)) {
	fprintf(stderr,"Starting rendezvous daemon\n");
	if(rend_init(config.runas)) {
	    perror("rend_init");
	    exit(EXIT_FAILURE);
	}
    }

    // Drop privs here
    if(drop_privs(config.runas)) {
	perror("drop_privs");
	exit(EXIT_FAILURE);
    }

    DPRINTF(ERR_DEBUG,"Loading playlists...\n");

    if(config.playlist)
	pl_load(config.playlist);

    DPRINTF(ERR_DEBUG,"Initializing database\n");

    if(parseonly) {
	if(!pl_error) {
	    fprintf(stderr,"Parsed successfully.\n");
	    pl_dump();
	}
	exit(EXIT_SUCCESS);
    }

    /* Initialize the database before starting */
    if(db_init(config.dbdir)) {
	perror("db_init");
	exit(EXIT_FAILURE);
    }

    /* will want to detach before we start scanning mp3 files */
    if(!foreground) {
	log_setdest("mt-daapd",LOGDEST_SYSLOG);
	daemon_start(1);
    }

    if(scan_init(config.mp3dir)) {
	log_err(1,"Error scanning MP3 files: %s\n",strerror(errno));
	exit(EXIT_FAILURE);
    }

    /* start up the web server */
    ws_config.web_root=config.web_root;
    ws_config.port=config.port;

    server=ws_start(&ws_config);
    if(!server) {
	log_err(1,"Error starting web server: %s\n",strerror(errno));
	return EXIT_FAILURE;
    }

    ws_registerhandler(server, "^.*$",config_handler,config_auth,1);
    ws_registerhandler(server, "^/server-info$",daap_handler,NULL,0);
    ws_registerhandler(server, "^/content-codes$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/login$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/update$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/databases$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/logout$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/databases/.*",daap_handler,NULL,0);

    if(config.use_mdns) { /* register services */
	DPRINTF(ERR_DEBUG,"Registering rendezvous names\n");
	rend_register(config.servername,"_daap._tcp",config.port);
	rend_register(config.servername,"_http._tcp",config.port);
    }

    end_time=time(NULL);

    log_err(0,"Scanned %d songs in  %d seconds\n",db_get_song_count(),
	    end_time-start_time);

    config.stop=0;

    while(!config.stop)
	sleep(10);

    if(config.use_mdns) {
	if(foreground) fprintf(stderr,"Killing rendezvous daemon\n");
	rend_stop();
    }

    if(foreground) fprintf(stderr,"Stopping webserver\n");
    ws_stop(server);

    config_close();

    if(foreground) fprintf(stderr,"Closing database\n");
    db_deinit();

#ifdef DEBUG_MEMORY
    fprintf(stderr,"Leaked memory:\n");
    err_leakcheck();
#endif

    if(foreground) fprintf(stderr,"\nDone\n");

    log_err(0,"Exiting");
    return EXIT_SUCCESS;
}

