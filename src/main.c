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
#include "webserver.h"
#include "playlist.h"
#include "dynamic-art.h"

#ifndef WITHOUT_MDNS
# include "rend.h"
#endif


#ifndef DEFAULT_CONFIGFILE
#ifdef NSLU2
#define DEFAULT_CONFIGFILE "/opt/etc/mt-daapd.conf"
#else
#define DEFAULT_CONFIGFILE "/etc/mt-daapd.conf"
#endif
#endif

#ifndef PIDFILE
#define PIDFILE	"/var/run/mt-daapd.pid"
#endif

#ifndef SIGCLD
# define SIGCLD SIGCHLD
#endif

#define MAIN_SLEEP_INTERVAL  2   /* seconds to sleep before checking for shutdown/reload */
/*
 * Globals
 */
CONFIG config;

/* 
 * Forwards
 */
int daemon_start(void);
void write_pid_file(void);

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
    char* index = 0;
    int streaming=0;

    MP3FILE *pmp3;
    int file_fd;
    int session_id=0;

    int img_fd;
    struct stat sb;
    long img_size;

    off_t offset=0;
    off_t real_len;
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
	root=daap_response_server_info(config.servername,
				       ws_getrequestheader(pwsc,"Client-DAAP-Version"));
    } else if (!strcasecmp(pwsc->uri,"/content-codes")) {
	config_set_status(pwsc,session_id,"Sending content codes");
	root=daap_response_content_codes();
    } else if (!strcasecmp(pwsc->uri,"/login")) {
	config_set_status(pwsc,session_id,"Logging in");
	root=daap_response_login(pwsc->hostname);
    } else if (!strcasecmp(pwsc->uri,"/update")) {
	if(!ws_getvar(pwsc,"delta")) { /* first check */
	    clientrev=db_version() - 1;
	    config_set_status(pwsc,session_id,"Sending database");
	} else {
	    clientrev=atoi(ws_getvar(pwsc,"delta"));
	    config_set_status(pwsc,session_id,"Waiting for DB updates");
	}
	root=daap_response_update(pwsc->fd,clientrev);
	if((ws_getvar(pwsc,"delta")) && (root==NULL)) {
	    DPRINTF(ERR_LOG,"Client %s disconnected\n",pwsc->hostname);
	    config_set_status(pwsc,session_id,NULL);
	    pwsc->close=1;
	    return;
	}
    } else if (!strcasecmp(pwsc->uri,"/logout")) {
	config_set_status(pwsc,session_id,NULL);
	ws_returnerror(pwsc,204,"Logout Successful");
	return;
    } else if(strcmp(pwsc->uri,"/databases")==0) {
	config_set_status(pwsc,session_id,"Sending database info");
	root=daap_response_dbinfo(config.servername);
	if(0 != (index = ws_getvar(pwsc, "index")))
	    daap_handle_index(root, index);
    } else if(strncmp(pwsc->uri,"/databases/",11) == 0) {

	/* the /databases/ uri will either be:
	 *
	 * /databases/id/items, which returns items in a db
	 * /databases/id/containers, which returns a container
	 * /databases/id/containers/id/items, which returns playlist elements
	 * /databases/id/items/id.mp3, to spool an mp3
	 * /databases/id/browse/category
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
		// pass the meta field request for processing
		// pass the query request for processing
		root=daap_response_songlist(ws_getvar(pwsc,"meta"),
					    ws_getvar(pwsc,"query"));
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
		    // pass the meta list info for processing
		    root=daap_response_playlist_items(playlist_index,
						      ws_getvar(pwsc,"meta"),
						      ws_getvar(pwsc,"query"));
		}
		free(uri);
		config_set_status(pwsc,session_id,"Sending playlist info");
	    } else if (strncasecmp(last,"containers",10)==0) {
		/* list of playlists */
		free(uri);
		root=daap_response_playlists(config.servername);
		config_set_status(pwsc,session_id,"Sending playlist info");
	    } else if (strncasecmp(last,"browse/",7)==0) {
		config_set_status(pwsc,session_id,"Compiling browse info");
		root = daap_response_browse(last + 7, 
					    ws_getvar(pwsc, "filter"));
		config_set_status(pwsc,session_id,"Sending browse info");
		free(uri);
	    }
	}

	// prune the full list if an index range was specified
	if(0 != (index = ws_getvar(pwsc, "index")))
	    daap_handle_index(root, index);
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
	    offset=(off_t)atol(ws_getrequestheader(pwsc,"range") + 6);
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

		DPRINTF(ERR_DEBUG,"Thread %d: Length of file (remaining) is %ld\n",
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

		config_set_status(pwsc,session_id,"Streaming file '%s'",pmp3->fname);
		DPRINTF(ERR_LOG,"Session %d: Streaming file '%s' to %s (offset %d)\n",
			session_id,pmp3->fname, pwsc->hostname,(long)offset);
		
		if(!offset)
		    config.stats.songs_served++; /* FIXME: remove stat races */

		if((config.artfilename) &&
		   (!offset) &&
		   ((img_fd=da_get_image_fd(pmp3->path)) != -1)) {
                  if (strncasecmp(pmp3->type,"mp3",4) ==0) {
		    DPRINTF(ERR_INFO,"Dynamically attaching artwork to %s (fd %d)\n",
			    pmp3->fname, img_fd);
		    da_attach_image(img_fd, pwsc->fd, file_fd, offset);
                  } else if (strncasecmp(pmp3->type, "m4a", 4) == 0) {
		    DPRINTF(ERR_INFO,"Dynamically attaching artwork to %s (fd %d)\n", pmp3->fname, img_fd);
                    da_aac_attach_image(img_fd, pwsc->fd, file_fd, offset);
                  }
		} else if(offset) {
		    DPRINTF(ERR_INFO,"Seeking to offset %ld\n",(long)offset);
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

int daemon_start(void) {
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
	if(atoi(user)) {
	    pw=getpwuid((uid_t)atoi(user)); /* doh! */
	} else {
	    pw=getpwnam(config.runas);
	}

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

/*
 * signal_handler
 *
 * This thread merely spins waiting for signals
 */
void *signal_handler(void *arg) {
    sigset_t intmask;
    int sig;
    int status;

    config.stop=0;
    config.reload=0;

    DPRINTF(ERR_WARN,"Signal handler started\n");

    while(!config.stop) {
	if((sigemptyset(&intmask) == -1) ||
	   (sigaddset(&intmask, SIGCLD) == -1) ||
	   (sigaddset(&intmask, SIGINT) == -1) ||
	   (sigaddset(&intmask, SIGHUP) == -1) ||
	   (sigwait(&intmask, &sig) == -1)) {
	    DPRINTF(ERR_FATAL,"Error waiting for signals.  Aborting\n");
	} else {
	    /* process the signal */
	    switch(sig) {
	    case SIGCLD:
		DPRINTF(ERR_LOG,"Got CLD signal.  Reaping\n");
		while (wait(&status)) {
		};
		break;
	    case SIGINT:
		DPRINTF(ERR_LOG,"Got INT signal. Notifying daap server.\n");
		config.stop=1;
		return NULL;
		break;
	    case SIGHUP:
		DPRINTF(ERR_LOG,"Got HUP signal. Notifying daap server.\n");
		config.reload=1;
		break;
	    default:
		DPRINTF(ERR_LOG,"What am I doing here?\n");
		break;
	    }
	}
    }    

    return NULL;
}

/*
 * start_signal_handler
 *
 * Block signals and set up the signal handler
 */
int start_signal_handler(void) {
    int error;
    sigset_t set;
    pthread_t handler_tid;

    if((sigemptyset(&set) == -1) ||
       (sigaddset(&set,SIGINT) == -1) ||
       (sigaddset(&set,SIGHUP) == -1) ||
       (sigprocmask(SIG_BLOCK, &set, NULL) == -1)) {
	DPRINTF(ERR_LOG,"Error setting signal set\n");
	return -1;
    }

    if(error=pthread_create(&handler_tid, NULL, signal_handler, NULL)) {
	errno=error;
	DPRINTF(ERR_LOG,"Error creating signal_handler thread\n");
	return -1;
    }

    pthread_detach(handler_tid);
    return 0;
}

int main(int argc, char *argv[]) {
    int option;
    char *configfile=DEFAULT_CONFIGFILE;
    WSCONFIG ws_config;
    WSHANDLE server;
    int parseonly=0;
    int foreground=0;
    int reload=0;
    int start_time;
    int end_time;
    int rescan_counter=0;
    int old_song_count;

    config.use_mdns=1;
    err_debuglevel=1;

    while((option=getopt(argc,argv,"d:c:mpfr")) != -1) {
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
	    foreground=1;
	    break;

	case 'r':
	    reload=1;
	    break;

	default:
	    usage(argv[0]);
	    exit(EXIT_FAILURE);
	    break;
	}
    }

    /* read the configfile, if specified, otherwise
     * try defaults */
    config.stats.start_time=start_time=time(NULL);

    if(config_read(configfile)) {
	if(errno) perror("config_read");
	else fprintf(stderr,"Error reading config file (%s)\n",configfile);
	exit(EXIT_FAILURE);
    }

    if((config.logfile) && (!parseonly) && (!foreground)) {
	log_setdest(config.logfile,LOGDEST_LOGFILE);	
    } else {
	if(!foreground) {
	    log_setdest("mt-daapd",LOGDEST_SYSLOG);
	}
    }


#ifndef WITHOUT_MDNS
    if((config.use_mdns) && (!parseonly)) {
	DPRINTF(ERR_LOG,"Starting rendezvous daemon\n");
	if(rend_init(config.runas)) {
	    DPRINTF(ERR_FATAL,"Error in rend_init: %s\n",strerror(errno));
	}
    }
#endif

    /* DWB: we want to detach before we drop privs so the pid file can
       be created with the original permissions.  This has the
       drawback that there's a bit less error checking done while
       we're attached, but if is much better when being automatically
       started as a system service. */
    if(!foreground) {
	daemon_start();
	write_pid_file();
    }

    /* DWB: shouldn't this be done after dropping privs? */
    if(db_open(config.dbdir, reload)) {
	DPRINTF(ERR_FATAL,"Error in db_open: %s\n",strerror(errno));
    }

    // Drop privs here
    if(drop_privs(config.runas)) {
	DPRINTF(ERR_FATAL,"Error in drop_privs: %s\n",strerror(errno));
    }

    /* block signals and set up the signal handling thread */
    DPRINTF(ERR_LOG,"Starting signal handler\n");
    if(start_signal_handler()) {
	DPRINTF(ERR_FATAL,"Error starting signal handler %s\n",strerror(errno));
    }

    DPRINTF(ERR_LOG,"Loading playlists\n");

    if(config.playlist)
	pl_load(config.playlist);

    if(parseonly) {
	if(!pl_error) {
	    fprintf(stderr,"Parsed successfully.\n");
	    pl_dump();
	}
	exit(EXIT_SUCCESS);
    }

    /* Initialize the database before starting */
    DPRINTF(ERR_LOG,"Initializing database\n");
    if(db_init()) {
	DPRINTF(ERR_FATAL,"Error in db_init: %s\n",strerror(errno));
    }

    DPRINTF(ERR_LOG,"Starting mp3 scan\n");
    if(scan_init(config.mp3dir)) {
	DPRINTF(ERR_FATAL,"Error scanning MP3 files: %s\n",strerror(errno));
    }

    /* start up the web server */
    ws_config.web_root=config.web_root;
    ws_config.port=config.port;

    DPRINTF(ERR_LOG,"Starting web server from %s on port %d\n",
	    config.web_root, config.port);

    server=ws_start(&ws_config);
    if(!server) {
	DPRINTF(ERR_FATAL,"Error staring web server: %s\n",strerror(errno));
    }

    ws_registerhandler(server, "^.*$",config_handler,config_auth,1);
    ws_registerhandler(server, "^/server-info$",daap_handler,NULL,0);
    ws_registerhandler(server, "^/content-codes$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/login$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/update$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/databases$",daap_handler,daap_auth,0);
    ws_registerhandler(server,"^/logout$",daap_handler,NULL,0);
    ws_registerhandler(server,"^/databases/.*",daap_handler,NULL,0);

#ifndef WITHOUT_MDNS
    if(config.use_mdns) { /* register services */
	DPRINTF(ERR_LOG,"Registering rendezvous names\n");
	rend_register(config.servername,"_daap._tcp",config.port);
	rend_register(config.servername,"_http._tcp",config.port);
    }
#endif

    end_time=time(NULL);

    DPRINTF(ERR_LOG,"Scanned %d songs in  %d seconds\n",db_get_song_count(),
	    end_time-start_time);

    config.stop=0;

    while(!config.stop) {
	if((config.rescan_interval) && (rescan_counter > config.rescan_interval)) {
	    if((config.always_scan) || (config_get_session_count())) {
		config.reload=1;
	    } else {
		DPRINTF(ERR_DEBUG,"Skipping background scan... no connected users\n");
	    }
	    rescan_counter=0;
	}

	if(config.reload) {
	    old_song_count = db_get_song_count();
	    start_time=time(NULL);

	    DPRINTF(ERR_LOG,"Rescanning database\n");
	    if(scan_init(config.mp3dir)) {
		DPRINTF(ERR_LOG,"Error rescanning... exiting\n");
		config.stop=1;
	    }
	    config.reload=0;
	    DPRINTF(ERR_INFO,"Background scanned %d songs (previously %d) in %d seconds\n",db_get_song_count(),old_song_count,time(NULL)-start_time);
	}

	sleep(MAIN_SLEEP_INTERVAL);
	rescan_counter += MAIN_SLEEP_INTERVAL;
    }

    DPRINTF(ERR_LOG,"Stopping gracefully\n");

#ifndef WITHOUT_MDNS
    if(config.use_mdns) {
	DPRINTF(ERR_LOG,"Stopping rendezvous daemon\n");
	rend_stop();
    }
#endif

    DPRINTF(ERR_LOG,"Stopping web server\n");
    ws_stop(server);

    config_close();

    DPRINTF(ERR_LOG,"Closing database\n");
    db_deinit();

#ifdef DEBUG_MEMORY
    fprintf(stderr,"Leaked memory:\n");
    err_leakcheck();
#endif

    DPRINTF(ERR_LOG,"Done!\n");

    log_setdest(NULL,LOGDEST_STDERR);

    return EXIT_SUCCESS;
}

/*
 * write_pid_file
 *
 * Assumes we haven't dropped privs yet
 */
void write_pid_file(void)
{
    FILE*	fp;
    int		fd;

    /* use open/fdopen instead of fopen for more control over the file
       permissions */
    if(-1 == (fd = open(PIDFILE, O_CREAT | O_WRONLY | O_TRUNC, 0644)))
	return;

    if(0 == (fp = fdopen(fd, "w")))
    {
	close(fd);
	return;
    }

    fprintf(fp, "%d\n", getpid());
    fclose(fp);
}
