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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>
#include <stdint.h>

#include <sys/signalfd.h>

#include <pthread.h>

#include <getopt.h>
#include <event.h>
#include <libavformat/avformat.h>

#include "conffile.h"
#include "logger.h"
#include "misc.h"
#include "filescanner.h"
#include "httpd.h"
#include "db-generic.h"
#include "mdns_avahi.h"


#define PIDFILE "/var/run/mt-daapd.pid"

struct event_base *evbase_main;
static int main_exit;


static void
usage(char *program)
{
  printf("Usage: %s [options]\n\n",program);
  printf("Options:\n");
  printf("  -d <number>    Log level (0-5)\n");
  printf("  -D <dom,dom..> Log domains\n");
  printf("  -c <file>      Use <file> as the configfile\n");
  printf("  -P <file>      Write PID to specified file\n");
  printf("  -f             Run in foreground\n");
  printf("  -y             Start even if user is not root\n");
  printf("  -b <id>        ffid to be broadcast\n");
  printf("  -v             Display version information\n");
  printf("\n\n");
  printf("Available log domains:\n");
  logger_domains();
  printf("\n\n");
}

static int
daemonize(int background, char *pidfile)
{
  FILE *fp;
  pid_t childpid;
  pid_t ret;
  int fd;
  int iret;
  char *runas;
  struct passwd *pw;

  if (geteuid() == (uid_t) 0)
    {
      runas = cfg_getstr(cfg_getsec(cfg, "general"), "uid");

      pw = getpwnam(runas);

      if (!pw)
	{
	  DPRINTF(E_FATAL, L_MAIN, "Could not lookup user %s: %s\n", runas, strerror(errno));

	  return -1;
	}
    }
  else
    pw = NULL;

  if (background)
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

      logger_detach();

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

static int
register_services(char *ffid, int no_rsp, int no_daap)
{
  cfg_t *lib;
  char *servername;
  char *password;
  char *txtrecord[10];
  char records[9][128];
  int port;
  uint32_t hash;
  int i;
  int ret;

  srand((unsigned int)time(NULL));

  lib = cfg_getnsec(cfg, "library", 0);

  servername = cfg_getstr(lib, "name");
  hash = djb_hash(servername, strlen(servername));

  for (i = 0; i < (sizeof(records) / sizeof(records[0])); i++)
    {
      memset(records[i], 0, 128);
      txtrecord[i] = records[i];
    }

  txtrecord[9] = NULL;

  snprintf(txtrecord[0], 128, "txtvers=1");
  snprintf(txtrecord[1], 128, "Database ID=%0X", hash);
  snprintf(txtrecord[2], 128, "Machine ID=%0X", hash);
  snprintf(txtrecord[3], 128, "Machine Name=%s", servername);
  snprintf(txtrecord[4], 128, "mtd-version=%s", VERSION);
  snprintf(txtrecord[5], 128, "iTSh Version=131073"); /* iTunes 6.0.4 */
  snprintf(txtrecord[6], 128, "Version=196610");      /* iTunes 6.0.4 */

  password = cfg_getstr(lib, "password");
  snprintf(txtrecord[7], 128, "Password=%s", (password) ? "true" : "false");

  if (ffid)
    snprintf(txtrecord[8], 128, "ffid=%s", ffid);
  else
    snprintf(txtrecord[8], 128, "ffid=%08x", rand());

  DPRINTF(E_INFO, L_MAIN, "Registering rendezvous names\n");

  port = cfg_getint(lib, "port");

  /* Register web server service */
  ret = mdns_register(servername, "_http._tcp", port, txtrecord);
  if (ret < 0)
    return ret;

  /* Register RSP service */
  if (!no_rsp)
    {
      ret = mdns_register(servername, "_rsp._tcp", port, txtrecord);
      if (ret < 0)
	return ret;
    }

  /* Register DAAP service */
  if (!no_daap)
    {
      ret = mdns_register(servername, "_daap._tcp", port, txtrecord);
      if (ret < 0)
	return ret;
    }

  return 0;
}


static void
signal_cb(int fd, short event, void *arg)
{
  struct sigaction sa_ign;
  struct sigaction sa_dfl;
  struct signalfd_siginfo info;
  int status;

  sa_ign.sa_handler = SIG_IGN;
  sa_ign.sa_flags = 0;
  sigemptyset(&sa_ign.sa_mask);

  sa_dfl.sa_handler = SIG_DFL;
  sa_dfl.sa_flags = 0;
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

	    main_exit = 1;
	    break;

	  case SIGHUP:
	    DPRINTF(E_LOG, L_MAIN, "Got SIGHUP\n");

	    if (!main_exit)
	      logger_reinit();
	    break;
	}
    }

  if (main_exit)
    event_base_loopbreak(evbase_main);
}


int
main(int argc, char **argv)
{
  int option;
  char *configfile;
  int background;
  int mdns_no_rsp;
  int mdns_no_daap;
  int loglevel;
  char *logdomains;
  char *logfile;
  char *ffid;
  char *perr;
  char *pidfile;
  sigset_t sigs;
  int sigfd;
  struct event sig_event;
  int ret;

  struct option option_map[] =
    {
      { "ffid",         1, NULL, 'b' },
      { "debug",        1, NULL, 'd' },
      { "logdomains",   1, NULL, 'D' },
      { "foreground",   0, NULL, 'f' },
      { "config",       1, NULL, 'c' },
      { "pidfile",      1, NULL, 'P' },
      { "version",      0, NULL, 'v' },

      { "mdns-no-rsp",  0, NULL, 512 },
      { "mdns-no-daap", 0, NULL, 513 },

      { NULL,           0, NULL, 0 }
    };

  configfile = CONFFILE;
  pidfile = PIDFILE;
  loglevel = -1;
  logdomains = NULL;
  logfile = NULL;
  background = 1;
  ffid = NULL;
  mdns_no_rsp = 0;
  mdns_no_daap = 0;

  while ((option = getopt_long(argc, argv, "D:d:c:P:fb:v", option_map, NULL)) != -1)
    {
      switch (option)
	{
	  case 512:
	    mdns_no_rsp = 1;
	    break;

	  case 513:
	    mdns_no_daap = 1;
	    break;

	  case 'b':
            ffid = optarg;
            break;

	  case 'd':
	    ret = safe_atoi(optarg, &option);
	    if (ret < 0)
	      fprintf(stderr, "Error: loglevel must be an integer in '-d %s'\n", optarg);
	    else
	      loglevel = option;
            break;

	  case 'D':
	    logdomains = optarg;
            break;

          case 'f':
            background = 0;
            break;

          case 'c':
            configfile = optarg;
            break;

          case 'P':
	    pidfile = optarg;
            break;

          case 'v':
            fprintf(stdout, "Firefly Media Server: Version %s\n",VERSION);
            exit(EXIT_SUCCESS);
            break;

          default:
            usage(argv[0]);
            exit(EXIT_FAILURE);
            break;
        }
    }

  ret = logger_init(NULL, NULL, (loglevel < 0) ? E_LOG : loglevel);
  if (ret != 0)
    {
      fprintf(stderr, "Could not initialize log facility\n");

      exit(EXIT_FAILURE);
    }

  ret = conffile_load(configfile);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Config file errors; please fix your config\n");

      logger_deinit();
      exit(EXIT_FAILURE);
    }

  logger_deinit();

  /* Reinit log facility with configfile values */
  if (loglevel < 0)
    loglevel = cfg_getint(cfg_getsec(cfg, "general"), "loglevel");

  logfile = cfg_getstr(cfg_getsec(cfg, "general"), "logfile");

  ret = logger_init(logfile, logdomains, loglevel);
  if (ret != 0)
    {
      fprintf(stderr, "Could not reinitialize log facility with config file settings\n");

      conffile_unload();
      exit(EXIT_FAILURE);
    }

  /* Set up libevent logging callback */
  event_set_log_callback(logger_libevent);

  DPRINTF(E_LOG, L_MAIN, "Firefly Version %s taking off\n", VERSION);

  /* initialize ffmpeg */
  av_register_all();

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

      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Daemonize and drop privileges */
  ret = daemonize(background, pidfile);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not initialize server\n");

      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Initialize libevent (after forking) */
  evbase_main = event_init();

  DPRINTF(E_LOG, L_MAIN, "mDNS init\n");
  ret = mdns_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "mDNS init failed\n");

      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* this will require that the db be readable by the runas user */
  ret = db_open(&perr, "sqlite3", "/var/cache/mt-daapd"); /* FIXME */

  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Error opening db: %s\n", perr);

      mdns_deinit();
      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Initialize the database before starting */
  DPRINTF(E_INFO, L_MAIN, "Initializing database\n");
  if (db_init(0))
    {
      DPRINTF(E_FATAL, L_MAIN, "Error in db_init: %s\n", strerror(errno));
    }

  /* Spawn file scanner thread */
  ret = filescanner_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "File scanner thread failed to start\n");

      mdns_deinit();
      db_deinit();
      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Spawn HTTPd thread */
  ret = httpd_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "HTTPd thread failed to start\n");

      filescanner_deinit();
      mdns_deinit();
      db_deinit();
      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Register mDNS services */
  ret = register_services(ffid, mdns_no_rsp, mdns_no_daap);
  if (ret < 0)
    {
      httpd_deinit();
      filescanner_deinit();
      mdns_deinit();
      db_deinit();
      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  /* Set up signal fd */
  sigfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sigfd < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not setup signalfd: %s\n", strerror(errno));

      httpd_deinit();
      filescanner_deinit();
      mdns_deinit();
      db_deinit();
      conffile_unload();
      logger_deinit();
      exit(EXIT_FAILURE);
    }

  event_set(&sig_event, sigfd, EV_READ, signal_cb, NULL);
  event_base_set(evbase_main, &sig_event);
  event_add(&sig_event, NULL);

  /* Run the loop */
  event_base_dispatch(evbase_main);

  DPRINTF(E_LOG, L_MAIN, "Stopping gracefully\n");

  DPRINTF(E_LOG, L_MAIN, "HTTPd deinit\n");
  httpd_deinit();

  DPRINTF(E_LOG, L_MAIN, "File scanner deinit\n");
  filescanner_deinit();

  DPRINTF(E_LOG, L_MAIN, "mDNS deinit\n");
  mdns_deinit();

  conffile_unload();

  DPRINTF(E_LOG, L_MAIN, "Closing database\n");
  db_deinit();

  if (background)
    {
      ret = unlink(pidfile);
      if (ret < 0)
	DPRINTF(E_WARN, L_MAIN, "Could not unlink PID file %s: %s\n", pidfile, strerror(errno));
    }

  DPRINTF(E_LOG, L_MAIN, "Exiting.\n");

  logger_deinit();

  return EXIT_SUCCESS;
}
