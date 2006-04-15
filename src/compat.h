#ifndef _COMPAT_H_
#define _COMPAT_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifndef HAVE_STRCASESTR
extern char * strcasestr(char* haystack, char* needle);
#endif

#ifndef HAVE_STRPTIME
char * strptime( char *buf, char *fmt, struct tm *tm );
#endif

#ifndef HAVE_STRTOK_R
extern char *strtok_r(char *s, const char *delim, char **last);
#endif

#ifndef HAVE_TIMEGM
extern time_t timegm(struct tm *tm);
#endif

#endif /* _COMPAT_H_ */
