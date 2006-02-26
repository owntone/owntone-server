/*
 * $Id$
 * Generic error handling
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
 * \file err.c
 * Error handling, logging, and memory leak checking.
 *
 * Most of these functions should not be used directly.  For the most
 * part, they are hidden in macros like #DPRINTF and #MEMNOTIFY.  The
 * only function here that is really directly useable is log_setdest()
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"
#include "os.h"

static int err_debuglevel=0; /**< current debuglevel, set from command line with -d */
static int err_logdestination=LOGDEST_STDERR; /**< current log destination */
static FILE *err_file=NULL; /**< if logging to file, the handle of that file */
static pthread_mutex_t err_mutex=PTHREAD_MUTEX_INITIALIZER; /**< for serializing log messages */
static unsigned int err_debugmask=0xFFFFFFFF; /**< modules to debug, see \ref log_categories */

/** text list of modules to match for setting debug mask */
static char *err_categorylist[] = {
    "config","webserver","database","scan","query","index","browse",
    "playlist","art","daap","main","rend","xml","parse",NULL
};

/*
 * Forwards
 */

static int _err_lock(void);
static int _err_unlock(void);

/**
 * Write a printf-style formatted message to the log destination.
 * This can be stderr, syslog/eventviewer, or a logfile, as determined by 
 * err_setdest().  Note that this function should not be directly
 * used, rather it should be used via the #DPRINTF macro.
 *
 * \param level Level at which to log \ref log_levels
 * \param cat the category to log \ref log_categories
 * \param fmt printf-style 
 */
void err_log(int level, unsigned int cat, char *fmt, ...)
{
    va_list ap;
    char timebuf[256];
    char errbuf[4096];
    struct tm tm_now;
    time_t tt_now;

    if(level) {
        if(level > err_debuglevel)
            return;

        if(!(cat & err_debugmask))
            return;
    } /* we'll *always* process a log level 0 */

    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);
 
    _err_lock(); /* atomic file writes */

    if((!level) && (err_logdestination != LOGDEST_STDERR)) {
        fprintf(stderr,"%s",errbuf);
        fprintf(stderr,"Aborting\n");
        fflush(stderr); /* shouldn't have to do this? */
    }

    switch(err_logdestination) {
    case LOGDEST_LOGFILE:
        tt_now=time(NULL);
        localtime_r(&tt_now,&tm_now);
        strftime(timebuf,sizeof(timebuf),"%Y-%m-%d %T",&tm_now);
        fprintf(err_file,"%s: %s",timebuf,errbuf);
        if(!level) fprintf(err_file,"%s: Aborting\n",timebuf);
        fflush(err_file);
        break;
    case LOGDEST_STDERR:
        fprintf(stderr, "%s",errbuf);
        if(!level) fprintf(stderr,"Aborting\n");
        break;
    case LOGDEST_SYSLOG:
        os_syslog(level,errbuf);
        if(!level) os_syslog(0, "Fatal error... Aborting\n");
        break;
    }

    _err_unlock();

    if(!level) {
        exit(EXIT_FAILURE);      /* this should go to an OS-specific exit routine */
    }
}

/*
 * simple get/set interface to debuglevel to avoid global 
 */
void err_setlevel(int level) {
    err_debuglevel = level;
}

int err_getlevel(void) {
    return err_debuglevel;
}

/**
 * Sets the log destination.  (stderr, syslog, or logfile)
 *
 * \param app appname (used only for syslog destination)
 * \param destination where to log to \ref log_dests "as defined in err.h"
 */
void err_setdest(char *cvalue, int destination) {
    if(err_logdestination == destination)
        return;

    switch(err_logdestination) {
    case LOGDEST_SYSLOG:
        os_closesyslog();
        break;
    case LOGDEST_LOGFILE:
        fclose(err_file);
        break;
    }

    switch(destination) {
    case LOGDEST_LOGFILE:
        err_file=fopen(cvalue,"a");
        if(err_file==NULL) {
            fprintf(stderr,"Error opening %s: %s\n",cvalue,strerror(errno));
            exit(EXIT_FAILURE);
        }
        break;
    case LOGDEST_SYSLOG:
        os_opensyslog(); // (app,LOG_PID,LOG_DAEMON);
        break;
    }

    err_logdestination=destination;
}
/**
 * Set the debug mask.  Given a comma separated list, this walks
 * through the err_categorylist and sets the bitfields for the
 * requested log modules.
 *
 * \param list comma separated list of modules to debug.  
 */
extern int err_setdebugmask(char *list) {
    unsigned int rack;
    char *token, *str, *last;
    int index;

    err_debugmask=0x80000000; /* always log L_MISC! */
    str=list;

    while(1) {
        token=strtok_r(str,",",&last);
        str=NULL;

        if(token) {
            rack=1;
            index=0;
            while((err_categorylist[index]) && 
                  (strcasecmp(err_categorylist[index],token))) {
                rack <<= 1;
                index++;
            }

            if(!err_categorylist[index]) {
                DPRINTF(E_LOG,L_MISC,"Unknown module: %s\n",token);
                return 1;
            } else {
                DPRINTF(E_DBG,L_MISC,"Adding module %s to debug list (0x%08x)\n",token,rack);
                err_debugmask |= rack;
            }
        } else break; /* !token */
    }

    DPRINTF(E_INF,L_MISC,"Debug mask is 0x%08x\n",err_debugmask);

    return 0;
}

/**
 * Lock the error mutex.  This is used to serialize
 * log messages, as well as protect access to the memory
 * list, when memory debugging is enabled.
 *
 * \returns 0 on success, otherwise -1 with errno set
 */
int _err_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&err_mutex))) {
        errno=err;
        return -1;
    }

    return 0;
}

/**
 * Unlock the error mutex
 *
 * \returns 0 on success, otherwise -1 with errno set
 */
int _err_unlock(void) {
    int err;

    if((err=pthread_mutex_unlock(&err_mutex))) {
        errno=err;
        return -1;
    }

    return 0;
}

