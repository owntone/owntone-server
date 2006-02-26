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

/**
 * @file main.c
 *
 * Driver for mt-daapd, including the main() function.  This
 * is responsible for kicking off the initial mp3 scan, starting
 * up the signal handler, starting up the webserver, and waiting
 * around for external events to happen (like a request to rescan,
 * or a background rescan to take place.)
 *
 * It also contains the daap handling callback for the webserver.
 * This should almost certainly be somewhere else, and is in
 * desparate need of refactoring, but somehow continues to be in
 * this files.
 */

/** @mainpage mt-daapd
 * @section about_section About
 *
 * This is mt-daapd, an attempt to create an iTunes server for
 * linux and other POSIXish systems.  Maybe even Windows with cygwin,
 * eventually.
 *
 * You might check these locations for more info:
 * - <a href="http://www.mt-daapd.org">Home page</a>
 * - <a href="http://sf.net/projects/mt-daapd">Project page on SourceForge</a>
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include "configfile.h"
#include "dispatch.h"
#include "err.h"
#include "mp3-scanner.h"
#include "webserver.h"
#include "restart.h"
#include "ssc.h"
#include "dynamic-art.h"
#include "db-generic.h"
#include "os.h"

#ifdef HAVE_GETOPT_H
# include "getopt.h"
#endif

#ifndef WITHOUT_MDNS
# include "rend.h"
#endif

/**
 * Where the default configfile is.  On the NSLU2 running unslung,
 * thats in /opt, not /etc. */
#ifndef DEFAULT_CONFIGFILE
#ifdef NSLU2
#define DEFAULT_CONFIGFILE "/opt/etc/mt-daapd/mt-daapd.conf"
#else
#define DEFAULT_CONFIGFILE "/etc/mt-daapd.conf"
#endif
#endif

/** Seconds to sleep before checking for a shutdown or reload */
#define MAIN_SLEEP_INTERVAL  2

/** Let's hope if you have no atoll, you only have 32 bit inodes... */
#if !HAVE_ATOLL
#  define atoll(a) atol(a)
#endif

/*
 * Globals
 */
CONFIG config; /**< Main configuration structure, as read from configfile */

/*
 * Forwards
 */
static void usage(char *program);

/**
 * Print usage information to stdout
 *
 * \param program name of program (argv[0])
 */
void usage(char *program) {
    printf("Usage: %s [options]\n\n",program);
    printf("Options:\n");
    printf("  -d <number>    Debuglevel (0-9)\n");
    printf("  -D <mod,mod..> Debug modules\n");
    printf("  -m             Disable mDNS\n");
    printf("  -c <file>      Use configfile specified\n");
    printf("  -P <file>      Write the PID ot specified file\n");
    printf("  -f             Run in foreground\n");
    printf("  -y             Yes, go ahead and run as non-root user\n");
    printf("\n\n");
    printf("Valid debug modules:\n");
    printf(" config,webserver,database,scan,query,index,browse\n");
    printf(" playlist,art,daap,main,rend,misc\n");
    printf("\n\n");
}

/**
 * Kick off the daap server and wait for events.
 *
 * This starts the initial db scan, sets up the signal
 * handling, starts the webserver, then sits back and waits
 * for events, as notified by the signal handler and the
 * web interface.  These events are communicated via flags
 * in the config structure.
 *
 * \param argc count of command line arguments
 * \param argv command line argument pointers
 * \returns 0 on success, -1 otherwise
 *
 * \todo split out a ws_init and ws_start, so that the
 * web space handlers can be registered before the webserver
 * starts.
 *
 */
int main(int argc, char *argv[]) {
    int option;
    char *configfile=DEFAULT_CONFIGFILE;
    WSCONFIG ws_config;
    int foreground=0;
    int reload=0;
    int start_time;
    int end_time;
    int rescan_counter=0;
    int old_song_count, song_count;
    int force_non_root=0;
    int skip_initial=0;

    int err;
    char *perr;

    config.use_mdns=1;
    err_setlevel(1);

    while((option=getopt(argc,argv,"D:d:c:P:mfrysiu")) != -1) {
        switch(option) {
        case 'd':
            err_setlevel(atoi(optarg));
            break;
        case 'D':
            if(err_setdebugmask(optarg)) {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
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

#ifndef WIN32
        case 'P':
            os_set_pidfile(optarg);
            break;
#endif
        case 'r':
            reload=1;
            break;

        case 's':
            skip_initial=1;
            break;

        case 'y':
            force_non_root=1;
            break;

#ifdef WIN32
        case 'i':
            os_register();
            exit(EXIT_SUCCESS);
            break;

        case 'u':
            os_unregister();
            exit(EXIT_SUCCESS);
            break;
#endif

        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    if((getuid()) && (!force_non_root)) {
        fprintf(stderr,"You are not root.  This is almost certainly wrong.  If you are\n"
                "sure you want to do this, use the -y command-line switch\n");
        exit(EXIT_FAILURE);
    }

    /* read the configfile, if specified, otherwise
     * try defaults */
    config.stats.start_time=start_time=(int)time(NULL);
    config.stop=0;

    if(config_read(configfile)) {
        fprintf(stderr,"Error reading config file (%s)\n",configfile);
        exit(EXIT_FAILURE);
    }

    DPRINTF(E_LOG,L_MAIN,"Starting with debuglevel %d\n",err_getlevel());

    if(!foreground) {
        if(config.logfile) {
            err_setdest(config.logfile,LOGDEST_LOGFILE);
        } else {
            err_setdest("mt-daapd",LOGDEST_SYSLOG);
        }
    }

#ifndef WITHOUT_MDNS
    if(config.use_mdns) {
        DPRINTF(E_LOG,L_MAIN,"Starting rendezvous daemon\n");
        if(rend_init(config.runas)) {
            DPRINTF(E_FATAL,L_MAIN|L_REND,"Error in rend_init: %s\n",strerror(errno));
        }
    }
#endif

    if(!os_init(foreground)) {
        DPRINTF(E_LOG,L_MAIN,"Could not initialize server\n");
        os_deinit();
        exit(EXIT_FAILURE);
    }

    /* this will require that the db be readable by the runas user */
    err=db_open(&perr,config.dbtype,config.dbparms);

    if(err)
        DPRINTF(E_FATAL,L_MAIN|L_DB,"Error in db_open: %s\n",perr);

    /* Initialize the database before starting */
    DPRINTF(E_LOG,L_MAIN|L_DB,"Initializing database\n");
    if(db_init(reload)) {
        DPRINTF(E_FATAL,L_MAIN|L_DB,"Error in db_init: %s\n",strerror(errno));
    }

    if(!skip_initial) {
        DPRINTF(E_LOG,L_MAIN|L_SCAN,"Starting mp3 scan of %s\n",config.mp3dir);
        if(scan_init(config.mp3dir)) {
            DPRINTF(E_FATAL,L_MAIN|L_SCAN,"Error scanning MP3 files: %s\n",strerror(errno));
        }
    }

    /* start up the web server */
    ws_config.web_root=config.web_root;
    ws_config.port=config.port;

    DPRINTF(E_LOG,L_MAIN|L_WS,"Starting web server from %s on port %d\n",
            config.web_root, config.port);

    config.server=ws_start(&ws_config);
    if(!config.server) {
        DPRINTF(E_FATAL,L_MAIN|L_WS,"Error staring web server: %s\n",strerror(errno));
    }

    ws_registerhandler(config.server, "^.*$",config_handler,config_auth,1);
    ws_registerhandler(config.server, "^/server-info$",daap_handler,NULL,0);
    ws_registerhandler(config.server, "^/content-codes$",daap_handler,NULL,0);
    ws_registerhandler(config.server,"^/login$",daap_handler,daap_auth,0);
    ws_registerhandler(config.server,"^/update$",daap_handler,daap_auth,0);
    ws_registerhandler(config.server,"^/databases$",daap_handler,daap_auth,0);
    ws_registerhandler(config.server,"^/logout$",daap_handler,NULL,0);
    ws_registerhandler(config.server,"^/databases/.*",daap_handler,NULL,0);

#ifndef WITHOUT_MDNS
    if(config.use_mdns) { /* register services */
        DPRINTF(E_LOG,L_MAIN|L_REND,"Registering rendezvous names\n");
        rend_register(config.servername,"_daap._tcp",config.port,config.iface);
        rend_register(config.servername,"_http._tcp",config.port,config.iface);
    }
#endif

    end_time=(int) time(NULL);

    db_get_song_count(NULL,&song_count);
    DPRINTF(E_LOG,L_MAIN,"Scanned %d songs in  %d seconds\n",song_count,
            end_time-start_time);

    while(!config.stop) {
        if((config.rescan_interval) && (rescan_counter > config.rescan_interval)) {
            if((config.always_scan) || (config_get_session_count())) {
                config.reload=1;
            } else {
                DPRINTF(E_DBG,L_MAIN|L_SCAN|L_DB,"Skipped bground scan... no users\n");
            }
            rescan_counter=0;
        }

        if(config.reload) {
            old_song_count = song_count;
            start_time=(int) time(NULL);

            DPRINTF(E_LOG,L_MAIN|L_DB|L_SCAN,"Rescanning database\n");
            if(scan_init(config.mp3dir)) {
                DPRINTF(E_LOG,L_MAIN|L_DB|L_SCAN,"Error rescanning... exiting\n");
                config.stop=1;
            }
            config.reload=0;
            db_get_song_count(NULL,&song_count);
            DPRINTF(E_INF,L_MAIN|L_DB|L_SCAN,"Scanned %d songs (was %d) in "
                    "%d seconds\n",song_count,old_song_count,
                    time(NULL)-start_time);
        }

        sleep(MAIN_SLEEP_INTERVAL);
        rescan_counter += MAIN_SLEEP_INTERVAL;
    }

    DPRINTF(E_LOG,L_MAIN,"Stopping gracefully\n");

#ifndef WITHOUT_MDNS
    if(config.use_mdns) {
        DPRINTF(E_LOG,L_MAIN|L_REND,"Stopping rendezvous daemon\n");
        rend_stop();
    }
#endif


    /* Got to find a cleaner way to stop the web server.
     * Closing the fd of the socking accepting doesn't necessarily
     * cause the accept to fail on some libcs.
     *
    DPRINTF(E_LOG,L_MAIN|L_WS,"Stopping web server\n");
    ws_stop(config.server);
    */

    config_close();

    DPRINTF(E_LOG,L_MAIN|L_DB,"Closing database\n");
    db_deinit();

    DPRINTF(E_LOG,L_MAIN,"Done!\n");

    err_setdest(NULL,LOGDEST_STDERR);

    os_deinit();
    return EXIT_SUCCESS;
}

