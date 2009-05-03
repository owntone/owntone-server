/*
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
 *
 * Pieces from mt-daapd:
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
#include <stdarg.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <sys/signalfd.h>

#include <pwd.h>
#include <grp.h>

#include "daapd.h"

#include "conffile.h"
#include "conf.h"
#include "configfile.h"
#include "err.h"
#include "misc.h"
#include "filescanner.h"
#include "httpd.h"
#include "db-generic.h"
#include "plugin.h"
#include "util.h"
#include "io.h"

#include "mdns_avahi.h"

#ifdef HAVE_GETOPT_H
# include "getopt.h"
#endif

#include <event.h>
#include <libavformat/avformat.h>


/** Seconds to sleep before checking for a shutdown or reload */
#define MAIN_SLEEP_INTERVAL  2

/** Let's hope if you have no atoll, you only have 32 bit inodes... */
#if !HAVE_ATOLL
#  define atoll(a) atol(a)
#endif

#ifndef PIDFILE
#define PIDFILE "/var/run/mt-daapd.pid"
#endif

/*
 * Globals
 */
CONFIG config; /**< Main configuration structure, as read from configfile */

struct event_base *evbase_main;

/*
 * Forwards
 */
static void usage(char *program);
static void main_io_errhandler(int level, char *msg);

/**
 * Print usage information to stdout
 *
 * \param program name of program (argv[0])
 */
void usage(char *program) {
    printf("Usage: %s [options]\n\n",program);
    printf("Options:\n");
    printf("  -d <number>    Debug level (0-9)\n");
    printf("  -D <mod,mod..> Debug modules\n");
    printf("  -c <file>      Use configfile specified\n");
    printf("  -P <file>      Write the PID to specified file\n");
    printf("  -f             Run in foreground\n");
    printf("  -y             Yes, go ahead and run as non-root user\n");
    printf("  -b <id>        ffid to be broadcast\n");
    printf("  -v             Display version information\n");
    printf("\n\n");
    printf("Valid debug modules:\n");
    printf(" config,webserver,database,scan,query,index,browse\n");
    printf(" playlist,art,daap,main,rend,misc\n");
    printf("\n\n");
}

/**
 * process a directory for plugins
 *
 * @returns TRUE if at least one plugin loaded successfully
 */
int load_plugin_dir(char *plugindir) {
    DIR *d_plugin;
    char de[sizeof(struct dirent) + MAXNAMLEN + 1]; /* ?? solaris  */
    struct dirent *pde;
    char *pext;
    char *perr=NULL;
    int loaded=FALSE;
    char plugin[PATH_MAX];

    if((d_plugin=opendir(plugindir)) == NULL) {
        DPRINTF(E_LOG,L_MAIN,"Error opening plugin dir %s.  Ignoring\n",
                plugindir);
        return FALSE;

    } else {
        while((readdir_r(d_plugin,(struct dirent *)de,&pde) != 1) && pde) {
                pext = strrchr(pde->d_name,'.');
                if((pext) && ((strcasecmp(pext,".so") == 0) ||
                   (strcasecmp(pext,".dylib") == 0) ||
                   (strcasecmp(pext,".dll") == 0))) {
                    /* must be a plugin */
                    snprintf(plugin,PATH_MAX,"%s%c%s",plugindir,
                             PATHSEP,pde->d_name);
                    if(plugin_load(&perr,plugin) != PLUGIN_E_SUCCESS) {
                        DPRINTF(E_LOG,L_MAIN,"Error loading plugin %s: %s\n",
                                plugin,perr);
                        free(perr);
                        perr = NULL;
                    } else {
                        loaded = TRUE;
                    }
                }
        }
        closedir(d_plugin);
    }

    return loaded;
}

static int
daemonize(int foreground, char *runas, char *pidfile)
{
  FILE *fp;
  int fd;
  pid_t childpid;
  pid_t ret;
  int iret;
  struct passwd *pw;

  if (geteuid() == (uid_t) 0)
    {
      pw = getpwnam(runas);

      if (!pw)
	{
	  DPRINTF(E_FATAL, L_MAIN, "Could not lookup user %s: %s\n", runas, strerror(errno));

	  return -1;
	}
    }
  else
    pw = NULL;

  if (!foreground)
    {
      fp = fopen(pidfile, "w");
      if (!fp)
	{
	  DPRINTF(E_LOG, L_MAIN, "Error opening pidfile (%s): %s\n", pidfile, strerror(errno));

	  return -1;
	}

      fd = open("/dev/null", O_RDWR, 0);
      if (fd < 0)
	{
	  DPRINTF(E_LOG, L_MAIN, "Error opening /dev/null: %s\n", strerror(errno));

	  fclose(fp);
	  return -1;
	}

      signal(SIGTTOU, SIG_IGN);
      signal(SIGTTIN, SIG_IGN);
      signal(SIGTSTP, SIG_IGN);

      childpid = fork();

      if (childpid > 0)
	exit(EXIT_SUCCESS);
      else if (childpid < 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "Fork failed: %s\n", strerror(errno));

	  close(fd);
	  fclose(fp);
	  return -1;
	}

      ret = setsid();
      if (ret == (pid_t) -1)
	{
	  DPRINTF(E_FATAL, L_MAIN, "setsid() failed: %s\n", strerror(errno));

	  close(fd);
	  fclose(fp);
	  return -1;
	}

      dup2(fd, STDIN_FILENO);
      dup2(fd, STDOUT_FILENO);
      dup2(fd, STDERR_FILENO);

      if (fd > 2)
	close(fd);

      chdir("/");
      umask(0);

      fprintf(fp, "%d\n", getpid());
      fclose(fp);

      DPRINTF(E_DBG, L_MAIN, "PID: %d\n", getpid());
    }

  if (pw)
    {
      iret = initgroups(runas, pw->pw_gid);
      if (iret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "initgroups() failed: %s\n", strerror(errno));

	  return -1;
	}

      iret = setgid(pw->pw_gid);
      if (iret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "setgid() failed: %s\n", strerror(errno));

	  return -1;
	}

      iret = setuid(pw->pw_uid);
      if (iret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "setuid() failed: %s\n", strerror(errno));

	  return -1;
	}
    }

  return 0;
}


/**
 * set up an errorhandler for io errors
 *
 * @param int level of the error (0=fatal, 9=debug)
 * @param msg the text error
 */
void main_io_errhandler(int level, char *msg) {
    DPRINTF(level,L_MAIN,"%s",msg);
}


static void
signal_cb(int fd, short event, void *arg)
{
  struct sigaction sa_ign;
  struct sigaction sa_dfl;
  struct signalfd_siginfo info;
  int status;

  sa_ign.sa_handler=SIG_IGN;
  sa_ign.sa_flags=0;
  sigemptyset(&sa_ign.sa_mask);

  sa_dfl.sa_handler=SIG_DFL;
  sa_dfl.sa_flags=0;
  sigemptyset(&sa_dfl.sa_mask);

  while (read(fd, &info, sizeof(struct signalfd_siginfo)) > 0)
    {
      switch (info.ssi_signo)
	{
	  case SIGCHLD:
	    DPRINTF(E_LOG, L_MAIN, "Got SIGCHLD, reaping children\n");

	    while (wait3(&status, WNOHANG, NULL) > 0)
	      /* Nothing. */ ;
	    break;

	  case SIGINT:
	  case SIGTERM:
	    DPRINTF(E_LOG, L_MAIN, "Got SIGTERM or SIGINT\n");

	    config.stop = 1;
	    break;

	  case SIGHUP:
	    DPRINTF(E_LOG, L_MAIN, "Got SIGHUP\n");

	    if (!config.stop)
	      {
		conf_reload();
		err_reopen();

		config.reload = 1;
	      }
	    break;
	}
    }

  if (config.stop)
    event_base_loopbreak(evbase_main);
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
    char *configfile=CONFFILE;
    int reload=0;
    int start_time;
    int end_time;
    int song_count;
    int force_non_root=0;
    int skip_initial=1;
    char *db_type,*db_parms,*runas, *tmp;
    int port;
    char *servername;
    char *ffid = NULL;
    char *perr=NULL;
    char *txtrecord[10];
    void *phandle;
    char *plugindir;
    char *pidfile = PIDFILE;
    sigset_t sigs;
    int sigfd;
    struct event sig_event;
    int ret;
    int i;

    int err;

    int debuglevel=0;

    err_setlevel(2);

    config.foreground=0;
    while((option=getopt(argc,argv,"D:d:c:P:frysiub:v")) != -1) {
        switch(option) {
        case 'b':
            ffid=optarg;
            break;

        case 'd':
            debuglevel = atoi(optarg);
            err_setlevel(debuglevel);
            break;

        case 'D':
            if(err_setdebugmask(optarg)) {
                usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;

        case 'f':
            config.foreground=1;
            err_setdest(err_getdest() | LOGDEST_STDERR);
            break;

        case 'c':
            configfile=optarg;
            break;

        case 'P':
	    pidfile = optarg;
            break;

        case 'r':
            reload=1;
            break;

        case 's':
            skip_initial=0;
            break;

        case 'y':
            force_non_root=1;
            break;

        case 'v':
            fprintf(stderr,"Firefly Media Server: Version %s\n",VERSION);
            exit(EXIT_SUCCESS);
            break;

        default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

    if((getuid()) && (!force_non_root)) {
        fprintf(stderr,"You are not root.  This is almost certainly wrong.  "
                "If you are\nsure you want to do this, use the -y "
                "command-line switch\n");
        exit(EXIT_FAILURE);
    }

    io_init();
    io_set_errhandler(main_io_errhandler);

    /* read the configfile, if specified, otherwise
     * try defaults */
    config.stats.start_time=start_time=(int)time(NULL);
    config.stop=0;

    ret = conffile_load(configfile);
    if (ret != 0)
      {
	fprintf(stderr, "Config file errors; please fix your config\n");

	exit(EXIT_FAILURE);
      }

    if(debuglevel) /* was specified, should override the config file */
        err_setlevel(debuglevel);

    DPRINTF(E_LOG,L_MAIN,"Firefly Version %s: Starting with debuglevel %d\n",
            VERSION,err_getlevel());

    /* initialize ffmpeg */
    av_register_all();

    /* load plugins before we drop privs?  Maybe... let the
     * plugins do stuff they might need to */
    plugin_init();

    plugindir = conf_alloc_string("plugins", "plugin_dir", NULL);
    if (plugindir)
      {
        /* instead of specifying plugins, let's walk through the directory
         * and load each of them */
        if (!load_plugin_dir(plugindir))
	  DPRINTF(E_LOG, L_MAIN, "Warning: Could not load plugins\n");

        free(plugindir);
      }

    phandle = NULL;
    while ((phandle = plugin_enum(phandle)))
      DPRINTF(E_LOG, L_MAIN, "Plugin loaded: %s\n", plugin_get_description(phandle));

    /* Block signals for all threads except the main one */
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGHUP);
    sigaddset(&sigs, SIGCHLD);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGPIPE);
    ret = pthread_sigmask(SIG_BLOCK, &sigs, NULL);
    if (ret != 0)
      {
        DPRINTF(E_LOG, L_MAIN, "Error setting signal set\n");
	exit(EXIT_FAILURE);
      }

    /* Daemonize and drop privileges */
    runas = conf_alloc_string("general", "runas", "nobody");

    ret = daemonize(config.foreground, runas, pidfile);
    if (ret < 0)
      {
	DPRINTF(E_LOG, L_MAIN, "Could not initialize server\n");

	exit(EXIT_FAILURE);
      }

    free(runas);

    /* Initialize libevent (after forking) */
    evbase_main = event_init();

    DPRINTF(E_LOG, L_MAIN, "mDNS init\n");
    ret = mdns_init();
    if (ret != 0)
      {
	DPRINTF(E_FATAL, L_MAIN | L_REND, "mDNS init failed\n");

	exit(EXIT_FAILURE);
      }

    /* this will require that the db be readable by the runas user */
    db_type = conf_alloc_string("general","db_type","sqlite");
    db_parms = conf_alloc_string("general","db_parms","/var/cache/mt-daapd");
    err=db_open(&perr,db_type,db_parms);

    if(err) {
        DPRINTF(E_LOG,L_MAIN|L_DB,"Error opening db: %s\n",perr);

	mdns_deinit();
        exit(EXIT_FAILURE);
    }

    free(db_type);
    free(db_parms);

    /* Initialize the database before starting */
    DPRINTF(E_LOG,L_MAIN|L_DB,"Initializing database\n");
    if(db_init(reload)) {
        DPRINTF(E_FATAL,L_MAIN|L_DB,"Error in db_init: %s\n",strerror(errno));
    }

    err=db_get_song_count(&perr,&song_count);
    if(err != DB_E_SUCCESS) {
        DPRINTF(E_FATAL,L_MISC,"Error getting song count: %s\n",perr);
    }
    /* do a full reload if the db is empty */
    if(!song_count)
        reload = 1;

    /* Spawn file scanner thread */
    ret = filescanner_init();
    if (ret != 0)
      {
	DPRINTF(E_FATAL, L_MAIN, "File scanner thread failed to start\n");

	mdns_deinit();
	db_deinit();
	exit(EXIT_FAILURE);
      }

    /* Spawn HTTPd thread */
    ret = httpd_init();
    if (ret != 0)
      {
	DPRINTF(E_FATAL, L_MAIN, "HTTPd thread failed to start\n");

	mdns_deinit();
	db_deinit();
	exit(EXIT_FAILURE);
      }

    /* Register mDNS services */
    servername = conf_get_servername();

    for (i = 0; i < (sizeof(txtrecord) / sizeof(*txtrecord) - 1); i++)
      {
	txtrecord[i] = (char *)malloc(128);
	if (!txtrecord[i])
	  {
	    DPRINTF(E_FATAL, L_MAIN, "Out of memory for TXT record\n");

	    httpd_deinit();
	    filescanner_deinit();
	    mdns_deinit();
	    db_deinit();
	    exit(EXIT_FAILURE);
	  }

	memset(txtrecord[i], 0, 128);
      }

    snprintf(txtrecord[0], 128, "txtvers=1");
    snprintf(txtrecord[1], 128, "Database ID=%0X", djb_hash(servername, strlen(servername)));
    snprintf(txtrecord[2], 128, "Machine ID=%0X", djb_hash(servername, strlen(servername)));
    snprintf(txtrecord[3], 128, "Machine Name=%s", servername);
    snprintf(txtrecord[4], 128, "mtd-version=%s", VERSION);
    snprintf(txtrecord[5], 128, "iTSh Version=131073"); /* iTunes 6.0.4 */
    snprintf(txtrecord[6], 128, "Version=196610");      /* iTunes 6.0.4 */

    tmp = conf_alloc_string("general", "password", NULL);
    snprintf(txtrecord[7], 128, "Password=%s", (tmp && (strlen(tmp) > 0)) ? "true" : "false");
    if (tmp)
      free(tmp);

    srand((unsigned int)time(NULL));

    if (ffid)
      snprintf(txtrecord[8], 128, "ffid=%s", ffid);
    else
      snprintf(txtrecord[8], 128, "ffid=%08x", rand());

    txtrecord[9] = NULL;

    DPRINTF(E_LOG,L_MAIN|L_REND,"Registering rendezvous names\n");

    port = conf_get_int("general", "port", 0);

    /* Register web server service */
    mdns_register(servername, "_http._tcp", port, txtrecord);
    /* Register RSP service */
    mdns_register(servername, "_rsp._tcp", port, txtrecord);
    /* Register DAAP service */
    mdns_register(servername, "_daap._tcp", port, txtrecord);

    for (i = 0; i < (sizeof(txtrecord) / sizeof(*txtrecord) - 1); i++)
      free(txtrecord[i]);

    free(servername);

    end_time=(int) time(NULL);

    err=db_get_song_count(&perr,&song_count);
    if(err != DB_E_SUCCESS) {
        DPRINTF(E_FATAL,L_MISC,"Error getting song count: %s\n",perr);
    }

    DPRINTF(E_LOG,L_MAIN,"Serving %d songs.  Startup complete in %d seconds\n",
            song_count,end_time-start_time);

    if(conf_get_int("general","rescan_interval",0) && (!reload) &&
       (!conf_get_int("scanning","skip_first",0)))
        config.reload = 1; /* force a reload on start */

    /* Set up signal fd */
    sigfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigfd < 0)
      {
	DPRINTF(E_FATAL, L_MAIN, "Could not setup signalfd: %s\n", strerror(errno));

	httpd_deinit();
	filescanner_deinit();
	mdns_deinit();
	db_deinit();
	exit(EXIT_FAILURE);
      }

    event_set(&sig_event, sigfd, EV_READ, signal_cb, NULL);
    event_base_set(evbase_main, &sig_event);
    event_add(&sig_event, NULL);

    /* Run the loop */
    event_base_dispatch(evbase_main);

    DPRINTF(E_LOG,L_MAIN,"Stopping gracefully\n");

    DPRINTF(E_LOG, L_MAIN, "HTTPd deinit\n");
    httpd_deinit();

    DPRINTF(E_LOG, L_MAIN, "File scanner deinit\n");
    filescanner_deinit();

    DPRINTF(E_LOG, L_MAIN | L_REND, "mDNS deinit\n");
    mdns_deinit();

    conffile_unload();

    DPRINTF(E_LOG,L_MAIN|L_DB,"Closing database\n");
    db_deinit();

    DPRINTF(E_LOG,L_MAIN,"Done!\n");

    io_deinit();
    return EXIT_SUCCESS;
}

