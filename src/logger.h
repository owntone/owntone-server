
#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <stdarg.h>

/* Log domains */
#define L_CONF    0
#define L_DAAP    1
#define L_DB      2
#define L_HTTPD   3
#define L_HTTP    4
#define L_MAIN    5
#define L_MDNS    6
#define L_MISC    7
#define L_RSP     8
#define L_SCAN    9
#define L_XCODE   10
/* libevent logging */
#define L_EVENT   11
#define L_REMOTE  12
#define L_DACP    13
#define L_FFMPEG  14
#define L_ART     15
#define L_PLAYER  16
#define L_RAOP    17
#define L_LAUDIO  18
#define L_DMAP    19
#define L_DBPERF  20
#define L_SPOTIFY 21
#define L_LASTFM  22
#define L_CACHE   23
#define L_MPD     24

#define N_LOGDOMAINS  25

/* Severities */
#define E_FATAL   0
#define E_LOG     1
#define E_WARN    2
#define E_INFO    3
#define E_DBG     4
#define E_SPAM    5


void
DPRINTF(int severity, int domain, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

void
logger_ffmpeg(void *ptr, int level, const char *fmt, va_list ap);

void
logger_libevent(int severity, const char *msg);

#ifdef LAUDIO_USE_ALSA
void
logger_alsa(const char *file, int line, const char *function, int err, const char *fmt, ...);
#endif

void
logger_reinit(void);

void
logger_domains(void);

void
logger_detach(void);

int
logger_init(char *file, char *domains, int severity);

void
logger_deinit(void);


#endif /* !__LOGGER_H__ */
