/*
 * $Id$
 * Webserver library
 *
 * Copyright (C) 2003 Ron Pedde (ron@corbey.com)
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

#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <sys/param.h>

#include "err.h"
#include "webserver.h"

/*
 * Defines
 */

#define MAX_HOSTNAME 256
#define MAX_LINEBUFFER 256

#ifndef VERSION
#  define VERSION "Version 0.1 beta"
#endif

/*
 * Local (private) typedefs
 */
typedef struct tag_ws_private {
    WSCONFIG wsconfig;
    int server_fd;
    int stop;
    int running;
    pthread_t server_tid;
} WS_PRIVATE;

typedef struct tag_ws_dispatchinfo {
    WS_PRIVATE *pwsp;
    WS_CONNINFO *pwsc;
} WS_DISPATCHINFO;

/*
 * Forwards
 */
void *ws_mainthread(void*);
void *ws_dispatcher(void*);
int ws_makeargv(const char *s, const char *delimiters, char ***argvp);
int ws_lock_unsafe(void);
int ws_unlock_unsafe(void);
int ws_writefd(WS_CONNINFO *pwsc,char *buffer, int len, char *fmt, ...);
void ws_defaulthandler(WS_PRIVATE *pwsp, WS_CONNINFO *pwsc);
int ws_addarg(ARGLIST *root, char *key, char *value);
void ws_freearglist(ARGLIST *root);
char *ws_urldecode(char *string);
int ws_getheaders(WS_CONNINFO *pwsc);

/*
 * Globals
 */
pthread_mutex_t munsafe=PTHREAD_MUTEX_INITIALIZER;

/*
 * ws_lock_unsafe
 *
 * Lock non-thread-safe functions
 *
 * returns 0 on success,
 * returns -1 on failure, with errno set
 */
int ws_lock_unsafe(void) {
    int err;

    if(err=pthread_mutex_lock(&munsafe)) {
	errno=err;
	return -1;
    }

    return 0;
}

/*
 * ws_unlock_unsafe
 *
 * Lock non-thread-safe functions
 *
 * returns 0 on success,
 * returns -1 on failure, with errno set
 */
int ws_unlock_unsafe(void) {
    int err;

    if(err=pthread_mutex_unlock(&munsafe)) {
	errno=err;
	return -1;
    }

    return 0;
}

/*
 * ws_start
 *
 * Start the main webserver thread.  Should really
 * bind and listen to the port before spawning the thread,
 * since it will be hard to detect and return the error unless
 * we listen first
 *
 * RETURNS
 *   Success: WSHANDLE
 *   Failure: NULL, with errno set
 *
 */
WSHANDLE ws_start(WSCONFIG *config) {
    int err;
    
    WS_PRIVATE *pwsp;

    if((pwsp=(WS_PRIVATE*)malloc(sizeof(WS_PRIVATE))) == NULL)
	return NULL;

    memcpy(&pwsp->wsconfig,config,sizeof(WS_PRIVATE));
    pwsp->running=0;

    DPRINTF(ERR_INFO,"Preparing to listen on port %d\n",pwsp->wsconfig.port);

    if((pwsp->server_fd = u_open(pwsp->wsconfig.port)) == -1) {
	err=errno;
	DPRINTF(ERR_WARN,"Could not open port: %s\n",strerror(errno));
	errno=err;
	return NULL;
    }

    DPRINTF(ERR_INFO,"Starting server thread\n");
    if(err=pthread_create(&pwsp->server_tid,NULL,ws_mainthread,(void*)pwsp)) {
	DPRINTF(ERR_WARN,"Could not spawn thread: %s\n",strerror(err));
	close(pwsp->server_fd);
	errno=err;
	return NULL;
    }
    
    /* we're really running */
    pwsp->running=1;

    return (WSHANDLE)pwsp;
}

/*
 * ws_stop
 *
 * Stop the web server and all the child threads
 */
int ws_stop(WSHANDLE arg) {
    WS_PRIVATE *pwsp = (WS_PRIVATE*)arg;

    return 0;
}

/*
 * ws_mainthread
 *
 * Main thread for webserver - this accepts connections
 * and spawns a handler thread for each incoming connection.
 *
 * For a persistant connection, these threads will be 
 * long-lived, otherwise, they will terminate as soon as
 * the request has been honored.
 *
 * These client threads will, of course, be detached
 */
void *ws_mainthread(void *arg) {
    int fd;
    int err;
    WS_PRIVATE *pwsp = (WS_PRIVATE*)arg;
    WS_DISPATCHINFO *pwsd;
    WS_CONNINFO *pwsc;
    pthread_t tid;
    char hostname[MAX_HOSTNAME];

    while(1) {
	pwsc=(WS_CONNINFO*)malloc(sizeof(WS_CONNINFO));
	if(!pwsc) {
	    /* can't very well service any more threads! */
	    DPRINTF(ERR_FATAL,"Error: %s\n",strerror(errno));
	    pwsp->running=0;
	    return NULL;
	}

	memset(pwsc,0,sizeof(WS_CONNINFO));

	if((fd=u_accept(pwsp->server_fd,hostname,MAX_HOSTNAME)) == -1) {
	    close(pwsp->server_fd);
	    pwsp->running=0;
	    return NULL;
	}

	pwsc->hostname=strdup(hostname);
	pwsc->fd=fd;

	/* Spawn off a dispatcher to decide what to do with
	 * the request
	 */
	pwsd=(WS_DISPATCHINFO*)malloc(sizeof(WS_DISPATCHINFO));
	if(!pwsd) {
	    /* keep on trucking until we crash bigger */
	    DPRINTF(ERR_FATAL,"Error: %s\n",strerror(errno));
	    free(pwsd);
	    ws_close(pwsc);
	} else {
	    pwsd->pwsp=pwsp;
	    pwsd->pwsc=pwsc;

	    /* now, throw off a dispatch thread */
	    if(err=pthread_create(&tid,NULL,ws_dispatcher,(void*)pwsd)) {
		DPRINTF(ERR_WARN,"Could not spawn thread: %s\n",strerror(err));
		free(pwsd);
		ws_close(pwsc);
	    }

	    pthread_detach(tid);
	}
    }
}


/*
 * ws_close
 *
 * Close the connection.  This might be called when things
 * are already in bad shape, so we'll ignore errors and let
 * them be detected back in either the dispatch
 * thread or the main server thread
 *
 * Mainly, we just want to make sure that any
 * allocated memory has been freed
 */
void ws_close(WS_CONNINFO *pwsc) {
    ws_freearglist(&pwsc->request_headers);
    ws_freearglist(&pwsc->response_headers);
    ws_freearglist(&pwsc->request_vars);
    
    close(pwsc->fd);
    free(pwsc->hostname);
    free(pwsc);
}

/*
 * ws_freearglist
 *
 * Walks through an arg list freeing as it goes
 */
void ws_freearglist(ARGLIST *root) {
    ARGLIST *current;

    while(root->next) {
	free(root->next->key);
	free(root->next->value);
	current=root->next;
	root->next=current->next;
	free(current);
    }
}

/*
 * ws_getheaders
 *
 * Receive and parse headers.  This is called from
 * ws_dispatcher
 */
int ws_getheaders(WS_CONNINFO *pwsc) {
    char *first, *last;
    int done;
    char buffer[MAX_LINEBUFFER];

    /* Break down the headers into some kind of header list */
    done=0;
    while(!done) {
	if(readline(pwsc->fd,buffer,sizeof(buffer)) == -1) {
	    DPRINTF(ERR_INFO,"Unexpected close\n");
	    return -1;
	}
	
	DPRINTF(ERR_DEBUG,"Read: %s",buffer);

	first=buffer;
	if(buffer[0] == '\r')
	    first=&buffer[1];

	/* trim the trailing \n */
	if(first[strlen(first)-1] == '\n')
	    first[strlen(first)-1] = '\0';

	if(strlen(first) == 0) {
	    DPRINTF(ERR_DEBUG,"Headers parsed!\n");
	    done=1;
	} else {
	    /* we have a header! */
	    last=first;
	    strsep(&last,":");

	    if(last==first) {
		DPRINTF(ERR_WARN,"Invalid header: %s\n",first);
	    } else {
		while(*last==' ')
		    last++;

		DPRINTF(ERR_DEBUG,"Adding header %s=%s\n",first,last);
		if(ws_addarg(&pwsc->request_headers,first,last)) {
		    DPRINTF(ERR_FATAL,"Out of memory\n");
		    return -1;
		}
	    }
	}
    }

    return 0;
}


/*
 * ws_dispatcher
 *
 * Main dispatch thread.  This gets the request, reads the
 * headers, decodes the GET'd or POST'd variables,
 * then decides what function should service the request
 */
void *ws_dispatcher(void *arg) {
    WS_DISPATCHINFO *pwsd=(WS_DISPATCHINFO *)arg;
    WS_PRIVATE *pwsp=pwsd->pwsp;
    WS_CONNINFO *pwsc=pwsd->pwsc;
    char buffer[MAX_LINEBUFFER];
    char *buffp;
    char *last,*first,*middle;
    char **argvp;
    int tokens;
    int done;

    free(pwsd);

    DPRINTF(ERR_DEBUG,"Connection from %s\n",pwsc->hostname);

    /* Now, get the request from the other end
     * and decide where to dispatch it
     */

    if(readline(pwsc->fd,buffer,sizeof(buffer)) == -1) {
	DPRINTF(ERR_WARN,"Could not read from client: %s\n",strerror(errno));
	ws_close(pwsc);
	return NULL;
    }

    tokens=ws_makeargv(buffer," ",&argvp);
    if(tokens != 3) {
	free(argvp[0]);
	ws_returnerror(pwsc,400,"Bad request");
	return NULL;
    }

    if(!strcasecmp(argvp[0],"get")) {
	pwsc->request_type = RT_GET;
    } else if(!strcasecmp(argvp[0],"post")) {
	pwsc->request_type = RT_POST;
    } else {
	/* return a 501 not implemented */
	free(argvp[0]);
	ws_returnerror(pwsc,501,"Not implemented");
	return NULL;
    }

    /* Get headers */
    if(ws_getheaders(pwsc)) {
	DPRINTF(ERR_FATAL,"Could not parse headers - aborting\n");
	ws_close(pwsc);
	return NULL;
    }
	

    pwsc->uri=strdup(argvp[1]);
    free(argvp[0]);

    if(!pwsc->uri) {
	/* We have memory allocation errors... might just
	 * as well bail */
	DPRINTF(ERR_FATAL,"Error allocation URI\n");
	ws_returnerror(pwsc,500,"Internal server error");
	return NULL;
    }

    /* fix the URI by un urlencoding it */

    DPRINTF(ERR_DEBUG,"Original URI: %s\n",pwsc->uri);

    first=ws_urldecode(pwsc->uri);
    free(pwsc->uri);
    pwsc->uri=first;

    DPRINTF(ERR_DEBUG,"Translated URI: %s\n",pwsc->uri);

    /* now, trim the URI */
    first=last=pwsc->uri;
    strsep(&first,"?");
    done=0;
    if(first != last) {
	while((!done) && (first)) {
	    /* we've got GET args */
	    last=middle=first;
	    strsep(&last,"&");
	    strsep(&middle,"=");

	    if(!middle) {
		DPRINTF(ERR_WARN,"Bad arg: %s\n",first);
	    } else {
		DPRINTF(ERR_DEBUG,"Adding arg %s = %s\n",first,middle);
		ws_addarg(&pwsc->request_vars,first,middle);
	    }

	    if(!last) {
		DPRINTF(ERR_DEBUG,"Done parsing GET args!\n");
		done=1;
	    } else {
		first=last;
	    }
	}
    }
	
    /* now, parse POST args */

    /* Find the appropriate handler and dispatch it */
    ws_defaulthandler(pwsp, pwsc);
    return NULL;
}


/*
 * ws_writefd
 *
 * Write a printf-style output to a connfd
 */
int ws_writefd(WS_CONNINFO *pwsc,char *buffer, int len, char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, len, fmt, ap);
    va_end(ap);

    return r_write(pwsc->fd,buffer,strlen(buffer));
}


/*
 * ws_returnerror
 *
 * return a particular error code to the requesting
 * agent
 *
 * This will always succeed.  If it cannot, it will
 * just close the connection with prejudice.
 */
int ws_returnerror(WS_CONNINFO *pwsc,int error, char *description) {
    char buffer[MAX_LINEBUFFER];   /* so we don't have to allocate */

    DPRINTF(ERR_WARN,"Pushing a %d: %s\n",error,description);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"HTTP/1.1 %d %s",error,description);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"\n\r\n\r<HTML>\n\r<TITLE>");
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"ERROR %d</TITLE>\n\r<BODY>",error);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"\n\r<H1>%d Error</H1>\n\r",error);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"%s\n\r<hr>\n\r",description);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"mt-daapd: %s\n\r",VERSION);
    ws_writefd(pwsc,buffer,MAX_LINEBUFFER,"</BODY>\n\r</HTML>\n\r");

    ws_close(pwsc);
    return 0;
}

/*
 * ws_makeargv
 *
 * Turn a string into an argv-like array of char *.
 * the array must be destroyed by freeing the first
 * element.
 *
 * This code was stolen from Dr. Robbins.
 * (srobbins@cs.utsa.edu).  Any errors in it
 * were introduced by me, though.
 */
int ws_makeargv(const char *s, const char *delimiters, char ***argvp) {
    int i;
    int numtokens;
    const char *snew;
    char *t;
    int err;

    /* this whole function is sickeningly unsafe */
    DPRINTF(ERR_DEBUG,"Parsing input: %s",s);

    if(ws_lock_unsafe())
	return -1;

    if ((s == NULL) || (delimiters == NULL) || (argvp == NULL)) {
	ws_unlock_unsafe();
	return -1;
    }
    *argvp = NULL;                           
    snew = s + strspn(s, delimiters);  /* snew is real start of string */
    if ((t = malloc(strlen(snew) + 1)) == NULL) {
	ws_unlock_unsafe();
	return -1; 
    }
    
    /* count the number of tokens in s */
    strcpy(t, snew);
    numtokens = 0;
    if (strtok(t, delimiters) != NULL) 
	for (numtokens = 1; strtok(NULL, delimiters) != NULL; numtokens++) ; 
    
    /* create argument array for ptrs to the tokens */
    if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
	free(t);
	ws_unlock_unsafe();
	return -1; 
    } 

    /* insert pointers to tokens into the argument array */
    if (numtokens == 0) {
      free(t);
    } else {
	strcpy(t, snew);
	**argvp = strtok(t, delimiters);
	for (i = 1; i < numtokens; i++)
	    *((*argvp) + i) = strtok(NULL, delimiters);
    } 

    /* put in the final NULL pointer and return */
    *((*argvp) + numtokens) = NULL;
    return ws_unlock_unsafe() ? -1 : numtokens;
}     

/*
 * ws_defaulthandler
 *
 * default URI handler.  This simply finds the file
 * and serves it up
 */
void ws_defaulthandler(WS_PRIVATE *pwsp, WS_CONNINFO *pwsc) {
    char path[MAXPATHLEN];
    char resolved_path[MAXPATHLEN];
    int file_fd;
    
    snprintf(path,MAXPATHLEN,"%s/%s",pwsp->wsconfig.web_root,pwsc->uri);
    if(!realpath(path,resolved_path)) {
	ws_returnerror(pwsc,404,"Not found");
	return;
    }

    DPRINTF(ERR_DEBUG,"Preparing to serve %s\n",resolved_path);

    if(strncmp(resolved_path,pwsp->wsconfig.web_root,
	       strlen(pwsp->wsconfig.web_root))) {
	DPRINTF(ERR_WARN,"Requested file %s out of root\n",resolved_path);
	ws_returnerror(pwsc,403,"Forbidden");
	return;
    }

    file_fd=open(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	DPRINTF(ERR_WARN,"Error opening %s: %s\n",resolved_path,strerror(errno));
	ws_returnerror(pwsc,404,"Not found");
	return;
    }

    ws_writefd(pwsc,path,MAXPATHLEN,"HTTP/1.1 200 OK\n\r\n\r");

    /* now throw out the file */
    copyfile(file_fd,pwsc->fd);

    close(file_fd);
    DPRINTF(ERR_DEBUG,"Served successfully\n");
    ws_close(pwsc);
    return;
}

/*
 * ws_addarg
 *
 * Add an argument to an arg list
 *
 * This will strdup the passed key and value.
 * The arglist will then have to be freed.  This should
 * be done in ws_close, and handler functions will
 * have to remember to ws_close them.
 *
 * RETURNS
 *   -1 on failure, with errno set (ENOMEM)
 *    0 on success
 */
int ws_addarg(ARGLIST *root, char *key, char *value) {
    char *newkey;
    char *newvalue;
    ARGLIST *pnew;

    newkey=strdup(key);
    newvalue=strdup(value);
    pnew=(ARGLIST*)malloc(sizeof(ARGLIST));

    if((!pnew)||(!newkey)||(!newvalue))
	return -1;

    pnew->key=newkey;
    pnew->value=newvalue;
    pnew->next=root->next;
    root->next=pnew;
    return 0;
}

/*
 * ws_urldecode
 *
 * decode a urlencoded string
 *
 * the returned char will be malloced -- it must be
 * freed by the caller
 *
 * returns NULL on error (ENOMEM)
 */
char *ws_urldecode(char *string) {
    char *pnew;
    char *src,*dst;
    int val;

    pnew=(char*)malloc(strlen(string));
    if(!pnew)
	return NULL;

    src=string;
    dst=pnew;

    while(*src) {
	switch(*src) {
	case '+':
	    *dst++=' ';
	    src++;
	    break;
	case '%':
	    /* this is hideous */
	    src++;
	    if(*src) {
		if((*src <= '9') && (*src >='0'))
		    val=(*src - '0');
		else if((tolower(*src) <= 'f')&&(tolower(*src) >= 'a'))
		    val=10+(tolower(*src) - 'a');
		src++;
	    }
	    if(*src) {
		val *= 16;
		if((*src <= '9') && (*src >='0'))
		    val+=(*src - '0');
		else if((tolower(*src) <= 'f')&&(tolower(*src) >= 'a'))
		    val+=(10+(tolower(*src) - 'a'));
		src++;
	    }
	    *dst++=val;
	    break;
	default:
	    *dst++=*src++;
	    break;
	}
    }

    *dst='\0';
    return pnew;
}
