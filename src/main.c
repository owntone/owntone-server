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
 * \file main.c
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
 * this file.
 *
 * \todo Refactor daap_handler()
 */

/** \mainpage mt-daapd
 * \section about_section About
 *
 * This is mt-daapd, an attempt to create an iTunes server for
 * linux and other POSIXish systems.  Maybe even Windows with cygwin,
 * eventually.
 *
 * You might check these locations for more info:
 * - <a href="http://sf.net/projects/mt-daapd">Project page on SourceForge</a>
 * - <a href="http://mt-daapd.sf.net">Home page</a>
 *
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
#include "dispatch.h"
#include "err.h"
#include "mp3-scanner.h"
#include "webserver.h"
#include "ssc.h"
#include "dynamic-art.h"
#include "db-generic.h"

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

/** Where to dump the pidfile */
#ifndef PIDFILE
#define PIDFILE "/var/run/mt-daapd.pid"
#endif

/** You say po-tay-to, I say po-tat-o */
#ifndef SIGCLD
# define SIGCLD SIGCHLD
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
static int daemon_start(void);
static void usage(char *program);
static void *signal_handler(void *arg);
static int start_signal_handler(pthread_t *handler_tid);

/**
 * Fork and exit.  Stolen pretty much straight from Stevens.
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
 * Drop privs.  This allows mt-daapd to run as a non-privileged user.
 * Hopefully this will limit the damage it could do if exploited
 * remotely.  Note that only the user need be specified.  GID
 * is set to the primary group of the user.
 *
 * \param user user to run as (or UID)
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

/**
 * Wait for signals and flag the main process.  This is
 * a thread handler for the signal processing thread.  It
 * does absolutely nothing except wait for signals.  The rest
 * of the threads are running with signals blocked, so this thread
 * is guaranteed to catch all the signals.  It sets flags in
 * the config structure that the main thread looks for.  Specifically,
 * the stop flag (from an INT signal), and the reload flag (from HUP).
 * \param arg NULL, but required of a thread procedure
 */
void *signal_handler(void *arg) {
    sigset_t intmask;
    int sig;
    int status;

    config.stop=0;
    config.reload=0;
    config.pid=getpid();

    DPRINTF(E_WARN,L_MAIN,"Signal handler started\n");

    while(!config.stop) {
        if((sigemptyset(&intmask) == -1) ||
           (sigaddset(&intmask, SIGCLD) == -1) ||
           (sigaddset(&intmask, SIGINT) == -1) ||
           (sigaddset(&intmask, SIGHUP) == -1) ||
           (sigwait(&intmask, &sig) == -1)) {
            DPRINTF(E_FATAL,L_MAIN,"Error waiting for signals.  Aborting\n");
        } else {
            /* process the signal */
            switch(sig) {
            case SIGCLD:
                DPRINTF(E_LOG,L_MAIN,"Got CLD signal.  Reaping\n");
                while (wait3(&status, WNOHANG, NULL) > 0) {
                }
                break;
            case SIGINT:
                DPRINTF(E_LOG,L_MAIN,"Got INT signal. Notifying daap server.\n");
                config.stop=1;
                return NULL;
                break;
            case SIGHUP:
                DPRINTF(E_LOG,L_MAIN,"Got HUP signal. Notifying daap server.\n");
                config.reload=1;
                break;
            default:
                DPRINTF(E_LOG,L_MAIN,"What am I doing here?\n");
                break;
            }
        }
    }

    return NULL;
}

/**
 * Block signals, then start the signal handler.  The
 * signal handler started by spawning a new thread on
 * signal_handler().
 *
 * \returns 0 on success, -1 on failure with errno set
 */
int start_signal_handler(pthread_t *handler_tid) {
    int error;
    sigset_t set;

    if((sigemptyset(&set) == -1) ||
       (sigaddset(&set,SIGINT) == -1) ||
       (sigaddset(&set,SIGHUP) == -1) ||
       (sigaddset(&set,SIGCLD) == -1) ||
       (sigprocmask(SIG_BLOCK, &set, NULL) == -1)) {
        DPRINTF(E_LOG,L_MAIN,"Error setting signal set\n");
        return -1;
    }

    if((error=pthread_create(handler_tid, NULL, signal_handler, NULL))) {
        errno=error;
        DPRINTF(E_LOG,L_MAIN,"Error creating signal_handler thread\n");
        return -1;
    }

    /* we'll not detach this... let's join it */
    //pthread_detach(handler_tid);
    return 0;
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
    char *pidfile=PIDFILE;
    WSCONFIG ws_config;
    int foreground=0;
    int reload=0;
    int start_time;
    int end_time;
    int rescan_counter=0;
    int old_song_count;
    int force_non_root=0;
    int skip_initial=0;
    pthread_t signal_tid;

    int pid_fd;
    FILE *pid_fp=NULL;

    config.use_mdns=1;
    err_debuglevel=1;

    while((option=getopt(argc,argv,"D:d:c:P:mfrys")) != -1) {
        switch(option) {
        case 'd':
            err_debuglevel=atoi(optarg);
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

        case 'P':
            pidfile=optarg;
            break;

        case 'r':
            reload=1;
            break;

        case 's':
            skip_initial=1;
            break;

        case 'y':
            force_non_root=1;
            break;

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
    config.stats.start_time=start_time=time(NULL);
    config.stop=0;

    if(config_read(configfile)) {
        fprintf(stderr,"Error reading config file (%s)\n",configfile);
        exit(EXIT_FAILURE);
    }

    DPRINTF(E_LOG,L_MAIN,"Starting with debuglevel %d\n",err_debuglevel);

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

    /* open the pidfile, so it can be written once we detach */
    if((!foreground) && (!force_non_root)) {
        if(-1 == (pid_fd = open(pidfile,O_CREAT | O_WRONLY | O_TRUNC, 0644)))
            DPRINTF(E_FATAL,L_MAIN,"Error opening pidfile (%s): %s\n",pidfile,strerror(errno));

        if(0 == (pid_fp = fdopen(pid_fd, "w")))
            DPRINTF(E_FATAL,L_MAIN,"fdopen: %s\n",strerror(errno));

        /* just to be on the safe side... */
        config.pid=0;

        daemon_start();
    }

    // Drop privs here
    if(drop_privs(config.runas)) {
        DPRINTF(E_FATAL,L_MAIN,"Error in drop_privs: %s\n",strerror(errno));
    }

    /* block signals and set up the signal handling thread */
    DPRINTF(E_LOG,L_MAIN,"Starting signal handler\n");
    if(start_signal_handler(&signal_tid)) {
        DPRINTF(E_FATAL,L_MAIN,"Error starting signal handler %s\n",strerror(errno));
    }


    if(pid_fp) {
        /* wait to for config.pid to be set by the signal handler */
        while(!config.pid) {
            sleep(1);
        }

        fprintf(pid_fp,"%d\n",config.pid);
        fclose(pid_fp);
    }

    /* this will require that the db be readable by the runas user */
    if(db_open(config.dbdir))
        DPRINTF(E_FATAL,L_MAIN|L_DB,"Error in db_open: %s\n",strerror(errno));

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

    end_time=time(NULL);

    DPRINTF(E_LOG,L_MAIN,"Scanned %d songs in  %d seconds\n",db_get_song_count(),
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
            old_song_count = db_get_song_count();
            start_time=time(NULL);

            DPRINTF(E_LOG,L_MAIN|L_DB|L_SCAN,"Rescanning database\n");
            if(scan_init(config.mp3dir)) {
                DPRINTF(E_LOG,L_MAIN|L_DB|L_SCAN,"Error rescanning... exiting\n");
                config.stop=1;
            }
            config.reload=0;
            DPRINTF(E_INF,L_MAIN|L_DB|L_SCAN,"Scanned %d songs (was %d) in %d seconds\n",
                    db_get_song_count(),old_song_count,time(NULL)-start_time);
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


    DPRINTF(E_LOG,L_MAIN,"Stopping signal handler\n");
    if(!pthread_kill(signal_tid,SIGINT)) {
        pthread_join(signal_tid,NULL);
    }

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

#ifdef DEBUG_MEMORY
    fprintf(stderr,"Leaked memory:\n");
    err_leakcheck();
#endif

    DPRINTF(E_LOG,L_MAIN,"Done!\n");

    err_setdest(NULL,LOGDEST_STDERR);

    return EXIT_SUCCESS;
}

