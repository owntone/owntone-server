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
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>


#define __IN_ERR__
#include "err.h"


int err_debuglevel=0;
int err_logdestination=LOGDEST_STDERR;

#ifdef DEBUG_MEMORY

typedef struct tag_err_leak {
    void *ptr;
    char *file;
    int line;
    int size;
    struct tag_err_leak *next;
} ERR_LEAK;

pthread_mutex_t err_mutex=PTHREAD_MUTEX_INITIALIZER;
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
void log_err(int quit, char *fmt, ...)
{
    va_list ap;
    char errbuf[256];

    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof(errbuf), fmt, ap);
    va_end(ap);

    switch(err_logdestination) {
    case LOGDEST_STDERR:
        fprintf(stderr, "%s", errbuf);
        break;
    case LOGDEST_SYSLOG:
        syslog(LOG_INFO, "%s", errbuf);
        break;
    }

    if(quit) {
        exit(EXIT_FAILURE);
    }
}

/****************************************************
 * log_setdest
 ****************************************************/
void log_setdest(char *app, int destination) {
    switch(destination) {
    case LOGDEST_SYSLOG:
	if(err_logdestination != LOGDEST_SYSLOG) {
	    openlog(app,LOG_PID,LOG_DAEMON);
	}
	break;
    case LOGDEST_STDERR:
	if(err_logdestination == LOGDEST_SYSLOG) {
	    /* close the syslog */
	    closelog();
	}
	break;
    }

    err_logdestination=destination;
}

#ifdef DEBUG_MEMORY

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
