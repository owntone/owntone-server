/*
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
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
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>

#include <event.h>

#include <libavutil/log.h>

#include "conffile.h"
#include "logger.h"


static pthread_mutex_t logger_lck = PTHREAD_MUTEX_INITIALIZER;
static int logdomains;
static int threshold;
static int console;
static char *logfilename;
static FILE *logfile;
static char *labels[] = { "config", "daap", "db", "httpd", "http", "main", "mdns", "misc", "rsp", "scan", "xcode", "event", "remote", "dacp", "ffmpeg", "artwork", "player", "raop", "laudio", "dmap", "dbperf", "spotify", "lastfm", "cache", "mpd" };
static char *severities[] = { "FATAL", "LOG", "WARN", "INFO", "DEBUG", "SPAM" };


static int
set_logdomains(char *domains)
{
  char *ptr;
  char *d;
  int i;

  logdomains = 0;

  while ((d = strtok_r(domains, " ,", &ptr)))
    {
      domains = NULL;

      for (i = 0; i < N_LOGDOMAINS; i++)
	{
	  if (strcmp(d, labels[i]) == 0)
	    {
	      logdomains |= (1 << i);
	      break;
	    }
	}

      if (i == N_LOGDOMAINS)
	{
	  fprintf(stderr, "Error: unknown log domain '%s'\n", d);
	  return -1;
	}
    }

  return 0;
}

static void
vlogger(int severity, int domain, const char *fmt, va_list args)
{
  va_list ap;
  char stamp[32];
  time_t t;
  int ret;

  if (!((1 << domain) & logdomains) || (severity > threshold))
    return;

  pthread_mutex_lock(&logger_lck);

  if (!logfile && !console)
    {
      pthread_mutex_unlock(&logger_lck);
      return;
    }

  if (logfile)
    {
      t = time(NULL);
      ret = strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
      if (ret == 0)
	stamp[0] = '\0';

      fprintf(logfile, "[%s] [%5s] %8s: ", stamp, severities[severity], labels[domain]);

      va_copy(ap, args);
      vfprintf(logfile, fmt, ap);
      va_end(ap);

      fflush(logfile);
    }

  if (console)
    {
      fprintf(stderr, "[%5s] %8s: ", severities[severity], labels[domain]);

      va_copy(ap, args);
      vfprintf(stderr, fmt, ap);
      va_end(ap);
    }

  pthread_mutex_unlock(&logger_lck);
}

void
DPRINTF(int severity, int domain, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vlogger(severity, domain, fmt, ap);
  va_end(ap);
}

void
logger_ffmpeg(void *ptr, int level, const char *fmt, va_list ap)
{
  int severity;

  if (level <= AV_LOG_FATAL)
    severity = E_LOG;
  else if (level <= AV_LOG_WARNING)
    severity = E_WARN;
  else if (level <= AV_LOG_VERBOSE)
    severity = E_INFO;
  else if (level <= AV_LOG_DEBUG)
    severity = E_DBG;
  else
    severity = E_SPAM;

  vlogger(severity, L_FFMPEG, fmt, ap);
}

void
logger_libevent(int severity, const char *msg)
{
  switch (severity)
    {
      case _EVENT_LOG_DEBUG:
	severity = E_DBG;
	break;

      case _EVENT_LOG_ERR:
	severity = E_LOG;
	break;

      case _EVENT_LOG_WARN:
	severity = E_WARN;
	break;

      case _EVENT_LOG_MSG:
	severity = E_INFO;
	break;

      default:
	severity = E_LOG;
	break;
    }

  DPRINTF(severity, L_EVENT, "%s\n", msg);
}

#ifdef LAUDIO_USE_ALSA
void
logger_alsa(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vlogger(E_LOG, L_LAUDIO, fmt, ap);
  va_end(ap);
}
#endif /* LAUDIO_USE_ALSA */

void
logger_reinit(void)
{
  FILE *fp;

  if (!logfile)
    return;

  pthread_mutex_lock(&logger_lck);

  fp = fopen(logfilename, "a");
  if (!fp)
    {
      fprintf(logfile, "Could not reopen logfile: %s\n", strerror(errno));

      goto out;
    }

  fclose(logfile);
  logfile = fp;

 out:
  pthread_mutex_unlock(&logger_lck);
}


/* The functions below are used at init time with a single thread running */
void
logger_domains(void)
{
  int i;

  fprintf(stdout, "%s", labels[0]);

  for (i = 1; i < N_LOGDOMAINS; i++)
    fprintf(stdout, ", %s", labels[i]);

  fprintf(stdout, "\n");
}

void
logger_detach(void)
{
  console = 0;
}

int
logger_init(char *file, char *domains, int severity)
{
  int ret;

  if ((sizeof(labels) / sizeof(labels[0])) != N_LOGDOMAINS)
    {
      fprintf(stderr, "WARNING: log domains do not match\n");

      return -1;
    }

  console = 1;
  threshold = severity;

  if (domains)
    {
      ret = set_logdomains(domains);
      if (ret < 0)
	return ret;
    }
  else
    logdomains = ~0;

  if (!file)
    return 0;

  logfile = fopen(file, "a");
  if (!logfile)
    {
      fprintf(stderr, "Could not open logfile %s: %s\n", file, strerror(errno));

      return -1;
    }

  ret = fchown(fileno(logfile), runas_uid, 0);
  if (ret < 0)
    fprintf(stderr, "Failed to set ownership on logfile: %s\n", strerror(errno));

  ret = fchmod(fileno(logfile), 0644);
  if (ret < 0)
    fprintf(stderr, "Failed to set permissions on logfile: %s\n", strerror(errno));

  logfilename = file;

  return 0;
}

void
logger_deinit(void)
{
  if (logfile)
    fclose(logfile);
}
