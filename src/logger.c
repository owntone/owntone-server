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

#include "logger.h"

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>

#include <event2/event.h>

#include <libavutil/log.h>

#include "conffile.h"
#include "misc.h"

#define LOGGER_REPEAT_MAX 15

/* We need our own check to avoid nested locking or recursive calls */
#define LOGGER_CHECK_ERR(f) \
  do { int lerr; lerr = f; if (lerr != 0) { \
      vlogger_fatal("%s failed at line %d, err %d (%s)\n", #f, __LINE__, \
                    lerr, strerror(lerr)); \
      abort(); \
    } } while(0)

static pthread_mutex_t logger_lck;
static int logger_initialized;
static int logdomains;
static int threshold;
static int console = 1;
static int logger_repeat_counter;
static uint32_t logger_last_hash;
static char *logfilename;
static FILE *logfile;
static char *labels[] = { "config", "daap", "db", "httpd", "http", "main", "mdns", "misc", "rsp", "scan", "xcode", "event", "remote", "dacp", "ffmpeg", "artwork", "player", "raop", "laudio", "dmap", "dbperf", "spotify", "lastfm", "cache", "mpd", "stream", "cast", "fifo", "lib", "web" };
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

static int
repeat_count(const char *fmt)
{
  uint32_t hash;

  hash = djb_hash(fmt, strlen(fmt));

  if (hash == logger_last_hash)
    logger_repeat_counter++;
  else
    logger_repeat_counter = 0;

  logger_last_hash = hash;

  return logger_repeat_counter;
}

static void
vlogger_writer(int severity, int domain, const char *fmt, va_list args)
{
  va_list ap;
  char content[2048];
  char stamp[32];
  time_t t;
  int ret;

  va_copy(ap, args);
  ret = vsnprintf(content, sizeof(content), fmt, ap);
  if (ret < 0 || ret >= sizeof(content))
    strcpy(content, "(LOGGING SKIPPED - invalid content)\n");
  va_end(ap);

  // On debug and spam levels we don't suppress repeating log messages
  if (severity < E_DBG)
    {
      ret = repeat_count(content);
      if (ret == LOGGER_REPEAT_MAX)
	strcpy(content, "(LOGGING SKIPPED - above log message is repeating)\n");
      else if (ret > LOGGER_REPEAT_MAX)
	return;
    }

  if (logfile)
    {
      t = time(NULL);
      ret = strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", localtime(&t));
      if (ret == 0)
	stamp[0] = '\0';

      fprintf(logfile, "[%s] [%5s] %8s: %s", stamp, severities[severity], labels[domain], content);

      fflush(logfile);
    }

  if (console)
    {
      fprintf(stderr, "[%5s] %8s: %s", severities[severity], labels[domain], content);
    }
}

static void
vlogger_fatal(const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vlogger_writer(E_FATAL, L_MISC, fmt, ap);
  va_end(ap);
}

static void
vlogger(int severity, int domain, const char *fmt, va_list args)
{

  if(! logger_initialized)
    {
      /* lock not initialized, use stderr */
      vlogger_writer(severity, domain, fmt, args);
      return;
    }

  if (!((1 << domain) & logdomains) || (severity > threshold))
    return;

  LOGGER_CHECK_ERR(pthread_mutex_lock(&logger_lck));

  if (!logfile && !console)
    {
      LOGGER_CHECK_ERR(pthread_mutex_unlock(&logger_lck));
      return;
    }

  vlogger_writer(severity, domain, fmt, args);

  LOGGER_CHECK_ERR(pthread_mutex_unlock(&logger_lck));
}

void
DPRINTF(int severity, int domain, const char *fmt, ...)
{
  va_list ap;

  // If domain and severity do not match the current log configuration, return early to
  // save some unnecessary code execution (tiny performance gain)
  if (logger_initialized && (!((1 << domain) & logdomains) || (severity > threshold)))
    return;

  va_start(ap, fmt);
  vlogger(severity, domain, fmt, ap);
  va_end(ap);
}

void
DVPRINTF(int severity, int domain, const char *fmt, va_list ap)
{
  // If domain and severity do not match the current log configuration, return early to
  // safe some unnecessary code execution (tiny performance gain)
  if (logger_initialized && (!((1 << domain) & logdomains) || (severity > threshold)))
    return;

  vlogger(severity, domain, fmt, ap);
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
    severity = E_DBG;
  else if (level <= AV_LOG_DEBUG)
    severity = E_SPAM;
  else
    severity = E_SPAM;

  vlogger(severity, L_FFMPEG, fmt, ap);
}

void
logger_libevent(int severity, const char *msg)
{
  switch (severity)
    {
      case EVENT_LOG_DEBUG:
	severity = E_DBG;
	break;

      case EVENT_LOG_ERR:
	severity = E_LOG;
	break;

      case EVENT_LOG_WARN:
	severity = E_WARN;
	break;

      case EVENT_LOG_MSG:
	severity = E_INFO;
	break;

      default:
	severity = E_LOG;
	break;
    }

  DPRINTF(severity, L_EVENT, "%s\n", msg);
}

#ifdef HAVE_ALSA
void
logger_alsa(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vlogger(E_LOG, L_LAUDIO, fmt, ap);
  va_end(ap);
}
#endif /* HAVE_ALSA */

void
logger_reinit(void)
{
  FILE *fp;

  if (!logfile)
    return;

  LOGGER_CHECK_ERR(pthread_mutex_lock(&logger_lck));

  fp = fopen(logfilename, "a");
  if (!fp)
    {
      fprintf(logfile, "Could not reopen logfile: %s\n", strerror(errno));

      goto out;
    }

  fclose(logfile);
  logfile = fp;

 out:
  LOGGER_CHECK_ERR(pthread_mutex_unlock(&logger_lck));
}


int
logger_severity(void)
{
  return threshold;
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

  /* logging w/o locks before initialized complete */
  CHECK_ERR(L_MISC, mutex_init(&logger_lck));

  logger_initialized = 1;

  return 0;
}

void
logger_deinit(void)
{
  if (logfile)
    {
      fclose(logfile);
      logfile = NULL;
    }

  if(logger_initialized)
    {
      /* logging w/o locks to stderr now */
      logger_initialized = 0;
      console = 1;
      CHECK_ERR(L_MISC, pthread_mutex_destroy(&logger_lck));
    }
}
