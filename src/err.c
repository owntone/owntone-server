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
#include <syslog.h>


#define __IN_ERR__
#include "err.h"


int err_debuglevel=0;
int err_logdestination=LOGDEST_STDERR;
FILE *err_file=NULL;
pthread_mutex_t err_mutex=PTHREAD_MUTEX_INITIALIZER;

#ifdef DEBUG_MEMORY

typedef struct tag_err_leak {
    void *ptr;
    char *file;
    int line;
    int size;
    struct tag_err_leak *next;
} ERR_LEAK;


ERR_LEAK err_leak = { NULL, NULL, 0, 0, NULL };
#endif

/*
 * Forwards
 */

int err_lock_mutex(void);
int err_unlock_mutex(void);

/****************************************************
 * log_err
 ****************************************************/
void log_err(int level, char *fmt, ...)
{
    va_list ap;
    char timebuf[256];
    char errbuf[1024];
    struct tm tm_now;
    time_t tt_now;

    if(level > err_debuglevel)
	return;

    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);
 
    err_lock_mutex(); /* atomic file writes */

    switch(err_logdestination) {
    case LOGDEST_LOGFILE:
	tt_now=time(NULL);
	gmtime_r(&tt_now,&tm_now);
	strftime(timebuf,sizeof(timebuf),"%F %T",&tm_now);
	fprintf(err_file,"%s %s: ",timebuf,errbuf);
	if(!level) fprintf(err_file,"%s %s: Aborting\n");
	fflush(err_file);
	break;
    case LOGDEST_STDERR:
        fprintf(stderr, "%s", errbuf);
	if(!level) fprintf(stderr,"Aborting\n");
        break;
    case LOGDEST_SYSLOG:
        syslog(LOG_INFO, "%s", errbuf);
	if(!level) syslog(LOG_INFO, "Aborting\n");
        break;
    }

    err_unlock_mutex();

    if(!level) {
        exit(EXIT_FAILURE);
    }
}

/****************************************************
 * log_setdest
 ****************************************************/
void log_setdest(char *app, int destination) {
    if(err_logdestination == destination)
	return;

    switch(err_logdestination) {
    case LOGDEST_SYSLOG:
	closelog();
	break;
    case LOGDEST_LOGFILE:
	fclose(err_file);
	break;
    }

    switch(destination) {
    case LOGDEST_LOGFILE:
	err_file=fopen(app,"w+");
	if(err_file==NULL) {
	    fprintf(stderr,"Error opening %s: %s\n",app,strerror(errno));
	    exit(EXIT_FAILURE);
	}
	break;
    case LOGDEST_SYSLOG:
	openlog(app,LOG_PID,LOG_DAEMON);
	break;
    }

    err_logdestination=destination;
}


/*
 * err_lock
 *
 * Lock the error mutex
 */
int err_lock_mutex(void) {
    int err;

    if((err=pthread_mutex_lock(&err_mutex))) {
	errno=err;
	return -1;
    }

    return 0;
}

/*
 * err_unlock
 *
 * Unlock the error mutex
 *
 * returns 0 on success,
 * returns -1 on failure, with errno set
 */
int err_unlock_mutex(void) {
    int err;

    if((err=pthread_mutex_unlock(&err_mutex))) {
	errno=err;
	return -1;
    }

    return 0;
}

#ifdef DEBUG_MEMORY

/*
 * Let the leak detector know about a chunk of memory
 * that needs to be freed, but came from an external library
 */
void err_notify(char *file, int line, void *ptr) {
    ERR_LEAK *pnew;

    if(!ptr)
	return;

    pnew=(ERR_LEAK*)malloc(sizeof(ERR_LEAK));
    if(!pnew) 
	log_err(1,"Error: cannot allocate leak struct\n");

    if(err_lock_mutex()) 
	log_err(1,"Error: cannot lock error mutex\n");
	
    pnew->file=file;
    pnew->line=line;
    pnew->size=0;
    pnew->ptr=ptr;

    pnew->next=err_leak.next;
    err_leak.next=pnew;

    err_unlock_mutex();
}

/*
 * err_malloc
 *
 * safe malloc
 */
void *err_malloc(char *file, int line, size_t size) {
    ERR_LEAK *pnew;

    pnew=(ERR_LEAK*)malloc(sizeof(ERR_LEAK));
    if(!pnew) 
	log_err(1,"Error: cannot allocate leak struct\n");

    if(err_lock_mutex()) 
	log_err(1,"Error: cannot lock error mutex\n");
	
    pnew->file=file;
    pnew->line=line;
    pnew->size=size;
    pnew->ptr=malloc(size);

    pnew->next=err_leak.next;
    err_leak.next=pnew;

    err_unlock_mutex();

    return pnew->ptr;
}


/*
 * err_strdup
 *
 * safe strdup
 */
char *err_strdup(char *file, int line, const char *str) {
    void *pnew;

    pnew=err_malloc(file,line,strlen(str) + 1);
    if(!pnew) 
	log_err(1,"Cannot malloc enough space for strdup\n");

    memcpy(pnew,str,strlen(str)+1);
    return pnew;
}

/*
 * err_free
 *
 * safe free
 */
void err_free(char *file, int line, void *ptr) {
    ERR_LEAK *current,*last;

    if(err_lock_mutex()) 
	log_err(1,"Error: cannot lock error mutex\n");

    last=&err_leak;
    current=last->next;

    while((current) && (current->ptr != ptr)) {
	last=current;
	current=current->next;
    }

    if(!current) {
	log_err(1,"Attempt to free unallocated memory: %s, %d\n",file,line);
    } else {
	free(current->ptr);
	last->next=current->next;
	free(current);
    }

    err_unlock_mutex();
}

/*
 * void err_leakcheck
 *
 * Walk through the list of memory
 */
void err_leakcheck(void) {
    ERR_LEAK *current;

    if(err_lock_mutex()) 
	log_err(1,"Error: cannot lock error mutex\n");

    current=err_leak.next;
    while(current) {
	printf("%s: %d - %d bytes at %p\n",current->file, current->line, current->size,
	       current->ptr);
	current=current->next;
    }

    err_unlock_mutex();
}
#endif
