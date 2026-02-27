/*
MIT License

Copyright (c) 2026 OwnTone

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include <getopt.h>

#include <event2/event.h>
#include <event2/thread.h>

#ifdef HAVE_SIGNALFD
# include <sys/signalfd.h>
#else
# include <sys/time.h>
# include <sys/event.h>
#endif

#include "airptp.h"

struct event_base *evbase_main;

static struct event *sig_event;
static int main_exit;

// TODO In background mode, log via syslog or the likes of it

static void
version(void)
{
  fprintf(stdout, "%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static void
usage(char *program)
{
  version();
  printf("\n");
  printf("Usage: %s [options]\n\n", program);
  printf("Options:\n");
  printf("  -f              Run in foreground\n");
  printf("  -v              Display version information\n");
  printf("\n");
}

static int
daemonize(void)
{
  pid_t childpid;
  pid_t pid_ret;
  int fd = -1;

  fd = open("/dev/null", O_RDWR, 0);
  if (fd < 0) {
    printf("Error opening /dev/null: %s\n", strerror(errno));
    goto error;
  }

  signal(SIGTTOU, SIG_IGN);
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTSTP, SIG_IGN);

  childpid = fork();

  if (childpid > 0)
    exit(EXIT_SUCCESS);
  else if (childpid < 0) {
    printf("Fork failed: %s\n", strerror(errno));
    goto error;
  }

  pid_ret = setsid();
  if (pid_ret == (pid_t) -1) {
    printf("setsid() failed: %s\n", strerror(errno));
    goto error;
  }

  dup2(fd, STDIN_FILENO);
  dup2(fd, STDOUT_FILENO);
  dup2(fd, STDERR_FILENO);

  if (fd > 2) // Standard file descriptors <= 2 are opened by default
    close(fd);

  return 0;

 error:
  if (fd >= 0)
    close(fd);
  return -1;
}

#ifdef HAVE_SIGNALFD
static void
signal_signalfd_cb(int fd, short event, void *arg)
{
  struct signalfd_siginfo info;
  int status;

  while (read(fd, &info, sizeof(struct signalfd_siginfo)) == sizeof(struct signalfd_siginfo))
    {
      switch (info.ssi_signo)
	{
	  case SIGCHLD:
	    printf("Got SIGCHLD\n");
	    while (waitpid(-1, &status, WNOHANG) > 0)
	      /* Nothing. */ ;
	    break;

	  case SIGINT:
	  case SIGTERM:
	    printf("Got SIGTERM or SIGINT\n");
	    main_exit = 1;
	    break;

	  case SIGHUP:
	    printf("Got SIGHUP\n");
	    break;
	}
    }

  if (main_exit)
    event_base_loopbreak(evbase_main);
  else
    event_add(sig_event, NULL);
}

#else

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
	    printf("Got SIGCHLD\n");
	    while (waitpid(-1, &status, WNOHANG) > 0)
	      /* Nothing. */ ;
	    break;

	  case SIGINT:
	  case SIGTERM:
	    printf("Got SIGTERM or SIGINT\n");
	    main_exit = 1;
	    break;

	  case SIGHUP:
	    printf("Got SIGHUP\n");
	    break;
	}
    }

  if (main_exit)
    event_base_loopbreak(evbase_main);
  else
    event_add(sig_event, NULL);
}
#endif

int
main(int argc, char **argv)
{
  struct airptp_handle *ptpd_hdl = NULL;
  bool background = true;
  int option;
  sigset_t sigs;
  int sigfd;
#ifdef HAVE_KQUEUE
  struct kevent ke_sigs[4];
#endif
  int ret;

  struct option option_map[] = {
    { "foreground",    0, NULL, 'f' },
    { "version",       0, NULL, 'v' },

    { NULL,            0, NULL, 0   }
  };

  while ((option = getopt_long(argc, argv, "fv", option_map, NULL)) != -1)
    {
      switch (option)
	{
	  case 'f':
	    background = false;
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

  ptpd_hdl = airptp_daemon_bind();
  if (!ptpd_hdl) {
    printf("Error binding: %s\n", airptp_errmsg_get());
    goto error;
  }

  ret = airptp_daemon_start(ptpd_hdl, 0xdeadbeef, true);
  if (ret < 0) {
    printf("Error starting daemon: %s\n", airptp_errmsg_get());
    goto error;
  }

  /* Block signals for all threads except the main one */
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);
  sigaddset(&sigs, SIGHUP);
  sigaddset(&sigs, SIGCHLD);
  sigaddset(&sigs, SIGTERM);
  sigaddset(&sigs, SIGPIPE);
  ret = pthread_sigmask(SIG_BLOCK, &sigs, NULL);
  if (ret != 0) {
    printf("Error setting signal set\n");
    goto error;
  }

  ret = background ? daemonize() : 0;
  if (ret < 0) {
    printf("Could not daemonize server\n");
    goto error;
  }

  evbase_main = event_base_new();
  if (!evbase_main) {
    printf("Error creating event base\n");
    goto error;
  }

  ret = evthread_use_pthreads();
  if (ret < 0) {
    printf("libevent is missing support for pthreads\n");
    goto error;
  }

#ifdef HAVE_SIGNALFD
  /* Set up signal fd */
  sigfd = signalfd(-1, &sigs, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sigfd < 0) {
    printf("Could not setup signalfd: %s\n", strerror(errno));
    goto error;
  }

  sig_event = event_new(evbase_main, sigfd, EV_READ, signal_signalfd_cb, NULL);
#else
  sigfd = kqueue();
  if (sigfd < 0) {
    printf("Could not setup kqueue: %s\n", strerror(errno));
    goto error;
  }

  EV_SET(&ke_sigs[0], SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[1], SIGTERM, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[2], SIGHUP, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
  EV_SET(&ke_sigs[3], SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);

  ret = kevent(sigfd, ke_sigs, 4, NULL, 0, NULL);
  if (ret < 0) {
    printf("Could not register signal events: %s\n", strerror(errno));
    goto error;
  }

  sig_event = event_new(evbase_main, sigfd, EV_READ, signal_kqueue_cb, NULL);
#endif
  if (!sig_event) {
    printf("Could not create signal event\n");

    ret = EXIT_FAILURE;
    goto error;
  }

  event_add(sig_event, NULL);

  event_base_dispatch(evbase_main);

  event_free(sig_event);

  event_base_free(evbase_main);
  airptp_end(ptpd_hdl);
  return EXIT_SUCCESS;

 error:
  if (evbase_main)
    event_base_free(evbase_main);

  airptp_end(ptpd_hdl);
  return EXIT_FAILURE;
}
