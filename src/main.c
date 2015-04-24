/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
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
#include <grp.h>
#include <stdint.h>

#if defined(__linux__)
# include <sys/signalfd.h>
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
# include <sys/time.h>
# include <sys/event.h>
#endif

#include <pthread.h>

#include <getopt.h>
#include <event.h>
#include <libavutil/log.h>
#include <libavformat/avformat.h>

#include <gcrypt.h>
GCRY_THREAD_OPTION_PTHREAD_IMPL;

#include "conffile.h"
#include "db.h"
#include "logger.h"
#include "misc.h"
#include "cache.h"
#include "filescanner.h"
#include "httpd.h"
#include "mpd.h"
#include "mdns.h"
#include "remote_pairing.h"
#include "player.h"
#include "worker.h"

#ifdef LASTFM
# include "lastfm.h"
#endif
#ifdef HAVE_SPOTIFY_H
# include "spotify.h"
#endif

#define PIDFILE   STATEDIR "/run/" PACKAGE ".pid"

struct event_base *evbase_main;

static struct event sig_event;
static int main_exit;

static void
version(void)
{
  fprintf(stdout, "Forked Media Server: Version %s\n", VERSION);
  fprintf(stdout, "Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>\n");
  fprintf(stdout, "Based on mt-daapd, Copyright (C) 2003-2007 Ron Pedde <ron@pedde.com>\n");
  fprintf(stdout, "Released under the GNU General Public License version 2 or later\n");
}

static void
usage(char *program)
{
  version();
  printf("\n");
  printf("Usage: %s [options]\n\n", program);
  printf("Options:\n");
  printf("  -d <number>    Log level (0-5)\n");
  printf("  -D <dom,dom..> Log domains\n");
  printf("  -c <file>      Use <file> as the configfile\n");
  printf("  -P <file>      Write PID to specified file\n");
  printf("  -f             Run in foreground\n");
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
  pid_t pid_ret;
  int fd;
  int ret;
  char *runas;

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

      pid_ret = setsid();
      if (pid_ret == (pid_t) -1)
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

      ret = chdir("/");
      if (ret < 0)
        DPRINTF(E_WARN, L_MAIN, "chdir() failed: %s\n", strerror(errno));

      umask(0);

      fprintf(fp, "%d\n", getpid());
      fclose(fp);

      DPRINTF(E_DBG, L_MAIN, "PID: %d\n", getpid());
    }

  if (geteuid() == (uid_t) 0)
    {
      runas = cfg_getstr(cfg_getsec(cfg, "general"), "uid");

      ret = initgroups(runas, runas_gid);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "initgroups() failed: %s\n", strerror(errno));

	  return -1;
	}

      ret = setegid(runas_gid);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "setegid() failed: %s\n", strerror(errno));

	  return -1;
	}

      ret = seteuid(runas_uid);
      if (ret != 0)
	{
	  DPRINTF(E_FATAL, L_MAIN, "seteuid() failed: %s\n", strerror(errno));

	  return -1;
	}
    }

  return 0;
}

static int
register_services(char *ffid, int no_rsp, int no_daap)
{
  cfg_t *lib;
  char *libname;
  char *password;
  char *txtrecord[10];
  char records[9][128];
  int port;
  uint32_t hash;
  int i;
  int ret;

  srand((unsigned int)time(NULL));

  lib = cfg_getsec(cfg, "library");

  libname = cfg_getstr(lib, "name");
  hash = djb_hash(libname, strlen(libname));

  for (i = 0; i < (sizeof(records) / sizeof(records[0])); i++)
    {
      memset(records[i], 0, 128);
      txtrecord[i] = records[i];
    }

  txtrecord[9] = NULL;

  snprintf(txtrecord[0], 128, "txtvers=1");
  snprintf(txtrecord[1], 128, "Database ID=%0X", hash);
  snprintf(txtrecord[2], 128, "Machine ID=%0X", hash);
  snprintf(txtrecord[3], 128, "Machine Name=%s", libname);
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

  /* Register web server service - disabled since we have no web interface */
/*  ret = mdns_register(libname, "_http._tcp", port, txtrecord);
  if (ret < 0)
    return ret;
*/
  /* Register RSP service */
  if (!no_rsp)
    {
      ret = mdns_register(libname, "_rsp._tcp", port, txtrecord);
      if (ret < 0)
	return ret;
    }

  /* Register DAAP service */
  if (!no_daap)
    {
      ret = mdns_register(libname, "_daap._tcp", port, txtrecord);
      if (ret < 0)
	return ret;
    }

  for (i = 0; i < (sizeof(records) / sizeof(records[0])); i++)
    {
      memset(records[i], 0, 128);
    }

  snprintf(txtrecord[0], 128, "txtvers=1");
  snprintf(txtrecord[1], 128, "DbId=%016" PRIX64, libhash);
  snprintf(txtrecord[2], 128, "DvTy=iTunes");
  snprintf(txtrecord[3], 128, "DvSv=2306"); /* Magic number! Yay! */
  snprintf(txtrecord[4], 128, "Ver=131073"); /* iTunes 6.0.4 */
  snprintf(txtrecord[5], 128, "OSsi=0x1F5"); /* Magic number! Yay! */
  snprintf(txtrecord[6], 128, "CtlN=%s", libname);

  /* Terminator */
  txtrecord[7] = NULL;

  /* The group name for the touch-able service advertising is a 64bit hash
   * but is different from the DbId in iTunes. For now we'll use a hash of
   * the library name for both, and we'll change that if needed.
   */

  /* Use as scratch space for the hash */
  snprintf(records[7], 128, "%016" PRIX64, libhash);

  /* Register touch-able service, for Remote.app */
  ret = mdns_register(records[7], "_touch-able._tcp", port, txtrecord);
  if (ret < 0)
    return ret;

  return 0;
}


#if defined(__linux__)
static void
signal_signalfd_cb(int fd, short event, void *arg)
{
  struct signalfd_siginfo info;
  int status;

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
  else
    event_add(&sig_event, NULL);
}

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)

static void
signal_kqueue_cb(int fd, short event, void *arg)
{
  struct timespec ts;
  struct kevent ke;
  int status;

  ts.tv_sec = 0;
  ts.tv_nsec = 0;

  while (kevent(fd, NULL, 0, &ke, 1, &ts) > 0)
    {
      switch (ke.ident)
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
  else
    event_add(&sig_event, NULL);
}
#endif


static int
ffmpeg_lockmgr(void **mutex, enum AVLockOp op)
{
  switch (op)
    {
      case AV_LOCK_CREATE:
	*mutex = malloc(sizeof(pthread_mutex_t));
	if (!*mutex)
	  return 1;

	return !!pthread_mutex_init(*mutex, NULL);

      case AV_LOCK_OBTAIN:
	return !!pthread_mutex_lock(*mutex);

      case AV_LOCK_RELEASE:
	return !!pthread_mutex_unlock(*mutex);

      case AV_LOCK_DESTROY:
	pthread_mutex_destroy(*mutex);
	free(*mutex);

	return 0;
    }

  return 1;
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
  char *pidfile;
  const char *gcry_version;
  sigset_t sigs;
  int sigfd;
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  struct kevent ke_sigs[4];
#endif
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
	    ret = safe_atoi32(optarg, &option);
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
	    version();
            return EXIT_SUCCESS;
            break;

          default:
            usage(argv[0]);
            return EXIT_FAILURE;
            break;
        }
    }

  ret = logger_init(NULL, NULL, (loglevel < 0) ? E_LOG : loglevel);
  if (ret != 0)
    {
      fprintf(stderr, "Could not initialize log facility\n");

      return EXIT_FAILURE;
    }

  ret = conffile_load(configfile);
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Config file errors; please fix your config\n");

      logger_deinit();
      return EXIT_FAILURE;
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
      return EXIT_FAILURE;
    }

  /* Set up libevent logging callback */
  event_set_log_callback(logger_libevent);

  DPRINTF(E_LOG, L_MAIN, "Forked Media Server Version %s taking off\n", VERSION);

  ret = av_lockmgr_register(ffmpeg_lockmgr);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not register ffmpeg lock manager callback\n");

      ret = EXIT_FAILURE;
      goto ffmpeg_init_fail;
    }

  av_register_all();
#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 13)
  avformat_network_init();
#endif
  av_log_set_callback(logger_ffmpeg);

  /* Initialize libgcrypt */
  gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);

  gcry_version = gcry_check_version(GCRYPT_VERSION);
  if (!gcry_version)
    {
      DPRINTF(E_FATAL, L_MAIN, "libgcrypt version mismatch\n");

      ret = EXIT_FAILURE;
      goto gcrypt_init_fail;
    }

  /* We aren't handling anything sensitive, so give up on secure
   * memory, which is a scarce system resource.
   */
  gcry_control(GCRYCTL_DISABLE_SECMEM, 0);

  gcry_control(GCRYCTL_INITIALIZATION_FINISHED, 0);

  DPRINTF(E_DBG, L_MAIN, "Initialized with gcrypt %s\n", gcry_version);

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

      ret = EXIT_FAILURE;
      goto signal_block_fail;
    }

  /* Daemonize and drop privileges */
  ret = daemonize(background, pidfile);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MAIN, "Could not initialize server\n");

      ret = EXIT_FAILURE;
      goto daemon_fail;
    }

  /* Initialize libevent (after forking) */
  evbase_main = event_init();

  DPRINTF(E_LOG, L_MAIN, "mDNS init\n");
  ret = mdns_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "mDNS init failed\n");

      ret = EXIT_FAILURE;
      goto mdns_fail;
    }

  /* Initialize the database before starting */
  DPRINTF(E_INFO, L_MAIN, "Initializing database\n");
  ret = db_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Database init failed\n");

      ret = EXIT_FAILURE;
      goto db_fail;
    }

  /* Open a DB connection for the main thread */
  ret = db_perthread_init();
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not perform perthread DB init for main\n");

      ret = EXIT_FAILURE;
      goto db_fail;
    }

  /* Spawn worker thread */
  ret = worker_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Worker thread failed to start\n");

      ret = EXIT_FAILURE;
      goto worker_fail;
    }

  /* Spawn cache thread */
  ret = cache_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Cache thread failed to start\n");

      ret = EXIT_FAILURE;
      goto cache_fail;
    }

  /* Spawn file scanner thread */
  ret = filescanner_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "File scanner thread failed to start\n");

      ret = EXIT_FAILURE;
      goto filescanner_fail;
    }

#ifdef HAVE_SPOTIFY_H
  /* Spawn Spotify thread */
  ret = spotify_init();
  if (ret < 0)
    {
      DPRINTF(E_INFO, L_MAIN, "Spotify thread not started\n");;
    }
#endif

  /* Spawn player thread */
  ret = player_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Player thread failed to start\n");

      ret = EXIT_FAILURE;
      goto player_fail;
    }

  /* Spawn HTTPd thread */
  ret = httpd_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "HTTPd thread failed to start\n");

      ret = EXIT_FAILURE;
      goto httpd_fail;
    }

#ifdef MPD
  /* Spawn MPD thread */
  ret = mpd_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "MPD thread failed to start\n");

      ret = EXIT_FAILURE;
      goto mpd_fail;
    }
#endif

  /* Start Remote pairing service */
  ret = remote_pairing_init();
  if (ret != 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Remote pairing service failed to start\n");

      ret = EXIT_FAILURE;
      goto remote_fail;
    }

  /* Register mDNS services */
  ret = register_services(ffid, mdns_no_rsp, mdns_no_daap);
  if (ret < 0)
    {
      ret = EXIT_FAILURE;
      goto mdns_reg_fail;
    }

#if defined(__linux__)
  /* Set up signal fd */
  sigfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sigfd < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not setup signalfd: %s\n", strerror(errno));

      ret = EXIT_FAILURE;
      goto signalfd_fail;
    }

  event_set(&sig_event, sigfd, EV_READ, signal_signalfd_cb, NULL);

#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
  sigfd = kqueue();
  if (sigfd < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not setup kqueue: %s\n", strerror(errno));

      ret = EXIT_FAILURE;
      goto signalfd_fail;
    }

  EV_SET(&ke_sigs[0], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[2], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[3], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);

  ret = kevent(sigfd, ke_sigs, 4, NULL, 0, NULL);
  if (ret < 0)
    {
      DPRINTF(E_FATAL, L_MAIN, "Could not register signal events: %s\n", strerror(errno));

      ret = EXIT_FAILURE;
      goto signalfd_fail;
    }

  event_set(&sig_event, sigfd, EV_READ, signal_kqueue_cb, NULL);
#endif

  event_base_set(evbase_main, &sig_event);
  event_add(&sig_event, NULL);

  /* Run the loop */
  event_base_dispatch(evbase_main);

  DPRINTF(E_LOG, L_MAIN, "Stopping gracefully\n");
  ret = EXIT_SUCCESS;

  /*
   * On a clean shutdown, bring mDNS down first to give a chance
   * to the clients to perform a clean shutdown on their end
   */
  DPRINTF(E_LOG, L_MAIN, "mDNS deinit\n");
  mdns_deinit();

 signalfd_fail:
 mdns_reg_fail:
  DPRINTF(E_LOG, L_MAIN, "Remote pairing deinit\n");
  remote_pairing_deinit();

 remote_fail:
  DPRINTF(E_LOG, L_MAIN, "HTTPd deinit\n");
  httpd_deinit();

 httpd_fail:
  DPRINTF(E_LOG, L_MAIN, "TCPd deinit\n");
#ifdef MPD
  DPRINTF(E_LOG, L_MAIN, "MPD deinit\n");
  mpd_deinit();
 mpd_fail:
#endif

  DPRINTF(E_LOG, L_MAIN, "Player deinit\n");
  player_deinit();

 player_fail:
#ifdef LASTFM
  DPRINTF(E_LOG, L_MAIN, "LastFM deinit\n");
  lastfm_deinit();
#endif
#ifdef HAVE_SPOTIFY_H
  DPRINTF(E_LOG, L_MAIN, "Spotify deinit\n");
  spotify_deinit();
#endif
  DPRINTF(E_LOG, L_MAIN, "File scanner deinit\n");
  filescanner_deinit();

 filescanner_fail:
  DPRINTF(E_LOG, L_MAIN, "Cache deinit\n");
  cache_deinit();

 cache_fail:
  DPRINTF(E_LOG, L_MAIN, "Worker deinit\n");
  worker_deinit();

 worker_fail:
  DPRINTF(E_LOG, L_MAIN, "Database deinit\n");
  db_perthread_deinit();
  db_deinit();

 db_fail:
  if (ret == EXIT_FAILURE)
    {
      DPRINTF(E_LOG, L_MAIN, "mDNS deinit\n");
      mdns_deinit();
    }

 mdns_fail:
 daemon_fail:
  if (background)
    {
      ret = seteuid(0);
      if (ret < 0)
	DPRINTF(E_LOG, L_MAIN, "seteuid() failed: %s\n", strerror(errno));
      else
	{
	  ret = unlink(pidfile);
	  if (ret < 0)
	    DPRINTF(E_LOG, L_MAIN, "Could not unlink PID file %s: %s\n", pidfile, strerror(errno));
	}
    }

 signal_block_fail:
 gcrypt_init_fail:
#if LIBAVFORMAT_VERSION_MAJOR >= 54 || (LIBAVFORMAT_VERSION_MAJOR == 53 && LIBAVFORMAT_VERSION_MINOR >= 13)
  avformat_network_deinit();
#endif
  av_lockmgr_register(NULL);

 ffmpeg_init_fail:
  DPRINTF(E_LOG, L_MAIN, "Exiting.\n");
  conffile_unload();
  logger_deinit();

  return ret;
}
