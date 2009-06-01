
#ifndef __LOGGER_H__
#define __LOGGER_H__

#include <stdarg.h>

/* Log domains */
#define L_CONF    0
#define L_DAAP    1
#define L_DB      2
#define L_HTTPD   3
#define L_MAIN    4
#define L_MDNS    5
#define L_MISC    6
#define L_PARSE   7
#define L_RSP     8
#define L_SCAN    9
#define L_XCODE   10
/* libevent logging */
#define L_EVENT   11

/* Will go away */
#define L_LOCK    12
#define N_LOGDOMAINS  13

/* Severities */
#define E_FATAL   0
#define E_LOG     1
#define E_WARN    2
#define E_INFO    3
#define E_DBG     4
#define E_SPAM    5


void
vlogger(int severity, int domain, char *fmt, va_list args);

void
DPRINTF(int severity, int domain, char *fmt, ...);

void
logger_libevent(int severity, const char *msg);

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
