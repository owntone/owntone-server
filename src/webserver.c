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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/param.h>
#include <sys/types.h>

#include "err.h"
#include "webserver.h"

/*
 * Defines
 */

#define MAX_HOSTNAME 256
#define MAX_LINEBUFFER 1024

/*
 * Local (private) typedefs
 */

typedef struct tag_ws_handler {
    regex_t regex;
    void (*req_handler)(WS_CONNINFO*);
    int(*auth_handler)(char *, char *);
    int addheaders;
    struct tag_ws_handler *next;
} WS_HANDLER;

typedef struct tag_ws_private {
    WSCONFIG wsconfig;
    WS_HANDLER handlers;
    int server_fd;
    int stop;
    int running;
    int threadno;
    int dispatch_threads;
    pthread_t server_tid;
    pthread_cond_t exit_cond;
    pthread_mutex_t exit_mutex;
} WS_PRIVATE;

/*
 * Forwards
 */
void *ws_mainthread(void*);
void *ws_dispatcher(void*);
int ws_lock_unsafe(void);
int ws_unlock_unsafe(void);
void ws_defaulthandler(WS_PRIVATE *pwsp, WS_CONNINFO *pwsc);
int ws_addarg(ARGLIST *root, char *key, char *fmt, ...);
void ws_freearglist(ARGLIST *root);
char *ws_urldecode(char *string);
int ws_getheaders(WS_CONNINFO *pwsc);
int ws_getpostvars(WS_CONNINFO *pwsc);
int ws_getgetvars(WS_CONNINFO *pwsc, char *string);
char *ws_getarg(ARGLIST *root, char *key);
int ws_testarg(ARGLIST *root, char *key, char *value);
void ws_emitheaders(WS_CONNINFO *pwsc);
int ws_findhandler(WS_PRIVATE *pwsp, WS_CONNINFO *pwsc, 
		   void(**preq)(WS_CONNINFO*),
		   int(**pauth)(char *, char *),
		   int *addheaders);
int ws_registerhandler(WSHANDLE ws, char *regex, 
		       void(*handler)(WS_CONNINFO*),
		       int(*auth)(char *, char *),
		       int addheaders);
int ws_decodepassword(char *header, char **username, char **password);
int ws_testrequestheader(WS_CONNINFO *pwsc, char *header, char *value);
char *ws_getrequestheader(WS_CONNINFO *pwsc, char *header);

/*
 * Globals
 */
pthread_mutex_t ws_unsafe=PTHREAD_MUTEX_INITIALIZER;

char *ws_dow[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
char *ws_moy[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul",
		   "Aug", "Sep", "Oct", "Nov", "Dec" };

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

    if(err=pthread_mutex_lock(&ws_unsafe)) {
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

    if(err=pthread_mutex_unlock(&ws_unsafe)) {
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
    pwsp->threadno=0;
    pwsp->stop=0;
    pwsp->dispatch_threads=0;
    pwsp->handlers.next=NULL;

    if(err=pthread_cond_init(&pwsp->exit_cond, NULL)) {
	errno=err;
	return NULL;
    }

    if(err=pthread_mutex_init(&pwsp->exit_mutex,NULL)) {
	errno=err;
	return NULL;
    }

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
	r_close(pwsp->server_fd);
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
extern int ws_stop(WSHANDLE ws) {
    WS_PRIVATE *pwsp = (WS_PRIVATE*)ws;
    WS_HANDLER *current;

    /* free the ws_handlers */
    while(pwsp->handlers.next) {
	current=pwsp->handlers.next;
	pwsp->handlers.next=current->next;
	free(current);
    }

    pwsp->stop=1;
    pwsp->running=0;

    r_close(pwsp->server_fd); /* this should tick off the listener */

    /* Wait for all the threads to die */
    if(pthread_mutex_lock(&pwsp->exit_mutex))
	log_err(1,"Cannot lock condition mutex\n");

    /* wait for condition */
    while(pwsp->dispatch_threads) {
	pthread_cond_wait(&pwsp->exit_cond, &pwsp->exit_mutex);
    }

    pthread_mutex_unlock(&pwsp->exit_mutex);

    free(pwsp);
    
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
    WS_CONNINFO *pwsc;
    pthread_t tid;
    char hostname[MAX_HOSTNAME];

    if(pthread_mutex_lock(&pwsp->exit_mutex))
	log_err(1,"Cannot lock condition mutex\n");
    
    pwsp->dispatch_threads++;

    pthread_mutex_unlock(&pwsp->exit_mutex);
    
    while(1) {
	pwsc=(WS_CONNINFO*)malloc(sizeof(WS_CONNINFO));
	if(!pwsc) {
	    /* can't very well service any more threads! */
	    DPRINTF(ERR_FATAL,"Error: %s\n",strerror(errno));
	    pwsp->running=0;


	    /* decrement the number of dispatch threads */
	    if(pthread_mutex_lock(&pwsp->exit_mutex))
		log_err(1,"Cannot lock condition mutex\n");
    
	    pwsp->dispatch_threads--;
	    pthread_cond_signal(&pwsp->exit_cond);
	    pthread_mutex_unlock(&pwsp->exit_mutex);
	    return NULL;
	}

	memset(pwsc,0,sizeof(WS_CONNINFO));

	if((fd=u_accept(pwsp->server_fd,hostname,MAX_HOSTNAME)) == -1) {
	    r_close(pwsp->server_fd);
	    pwsp->running=0;
	    free(pwsc);

	    /* decrement dispatch threads */
	    if(pthread_mutex_lock(&pwsp->exit_mutex))
		log_err(1,"Cannot lock condition mutex\n");
    
	    pwsp->dispatch_threads--;
	    pthread_cond_signal(&pwsp->exit_cond);
	    pthread_mutex_unlock(&pwsp->exit_mutex);
	    return NULL;
	}

	pwsc->hostname=strdup(hostname);
	pwsc->fd=fd;
	pwsc->pwsp = pwsp;

	/* Spawn off a dispatcher to decide what to do with
	 * the request
	 */

	/* don't really care if it locks or not */
	ws_lock_unsafe();
	pwsc->threadno=pwsp->threadno;
	pwsp->threadno++;
	ws_unlock_unsafe();

	/* now, throw off a dispatch thread */
	if(pthread_mutex_lock(&pwsp->exit_mutex))
	    log_err(1,"Cannot lock condition mutex\n");

	if(err=pthread_create(&tid,NULL,ws_dispatcher,(void*)pwsc)) {
	    pwsc->error=err;
	    DPRINTF(ERR_WARN,"Could not spawn thread: %s\n",strerror(err));
	    pthread_mutex_unlock(&pwsp->exit_mutex);
	    ws_close(pwsc);
	} else {
	    pwsp->dispatch_threads++;
	    pthread_mutex_unlock(&pwsp->exit_mutex);
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
    WS_PRIVATE *pwsp = (WS_PRIVATE *)pwsc->pwsp;

    DPRINTF(ERR_DEBUG,"Thread %d: Terminating\n",pwsc->threadno);
    DPRINTF(ERR_DEBUG,"Thread %d: Freeing request headers\n",pwsc->threadno);
    ws_freearglist(&pwsc->request_headers);
    DPRINTF(ERR_DEBUG,"Thread %d: Freeing response headers\n",pwsc->threadno);
    ws_freearglist(&pwsc->response_headers);
    DPRINTF(ERR_DEBUG,"Thread %d: Freeing request vars\n",pwsc->threadno);
    ws_freearglist(&pwsc->request_vars);
    if(pwsc->uri) 
	free(pwsc->uri);
    
    if((pwsc->close)||(pwsc->error)) {
	DPRINTF(ERR_DEBUG,"Thread %d: Closing fd\n",pwsc->threadno);
	r_close(pwsc->fd);
	free(pwsc->hostname);
	
	/* this thread is done */
	if(pthread_mutex_lock(&pwsp->exit_mutex)) 
	    log_err(1,"Error: cannot lock condition mutex\n");

	if(!pwsp->dispatch_threads) {
	    log_err(1,"Error: Bad dispatch thread count!\n");
	} else {
	    pwsp->dispatch_threads--;
	    if(pthread_cond_signal(&pwsp->exit_cond))
		log_err(1,"Error: cannot signal condition\n");
	}
	pthread_mutex_unlock(&pwsp->exit_mutex);

	free(pwsc);
    }
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


void ws_emitheaders(WS_CONNINFO *pwsc) {
    ARGLIST *pcurrent=pwsc->response_headers.next;

    while(pcurrent) {
	DPRINTF(ERR_DEBUG,"Emitting reponse header %s: %s\n",pcurrent->key,
		pcurrent->value);
	ws_writefd(pwsc,"%s: %s\r\n",pcurrent->key,pcurrent->value);
	pcurrent=pcurrent->next;
    }

    ws_writefd(pwsc,"\r\n");
}


/*
 * ws_getpostvars
 *
 * Receive and parse headers.  These will trump
 * get headers
 */
int ws_getpostvars(WS_CONNINFO *pwsc) {
    char *first, *last;
    char *content_length;
    int length;
    char *buffer;

    content_length = ws_getarg(&pwsc->request_headers,"Content-Length");
    if(!content_length) {
	pwsc->error = EINVAL;
	return -1;
    }

    length=atoi(content_length);
    DPRINTF(ERR_DEBUG,"Thread %d: Post var length: %d\n",
	    pwsc->threadno,length);

    buffer=(char*)malloc(length+1);

    if(!buffer) {
	pwsc->error = errno;
	DPRINTF(ERR_INFO,"Thread %d: Could not malloc %d bytes\n",
		pwsc->threadno, length);
	return -1;
    }

    if((readtimed(pwsc->fd, buffer, length, 30.0)) == -1) {
	DPRINTF(ERR_INFO,"Thread %d: Timeout reading post vars\n",
		pwsc->threadno);
	pwsc->error=errno;
	return -1;
    }

    DPRINTF(ERR_DEBUG,"Thread %d: Read post vars: %s\n",pwsc->threadno,buffer);

    pwsc->error=ws_getgetvars(pwsc,buffer);
    
    free(buffer);
    return pwsc->error;
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
	    pwsc->error=errno;
	    DPRINTF(ERR_INFO,"Thread %d: Unexpected close\n",pwsc->threadno);
	    return -1;
	}
	
	DPRINTF(ERR_DEBUG,"Thread %d: Read: %s",pwsc->threadno,buffer);

	first=buffer;
	if(buffer[0] == '\r')
	    first=&buffer[1];

	/* trim the trailing \n */
	if(first[strlen(first)-1] == '\n')
	    first[strlen(first)-1] = '\0';

	if(strlen(first) == 0) {
	    DPRINTF(ERR_DEBUG,"Thread %d: Headers parsed!\n",pwsc->threadno);
	    done=1;
	} else {
	    /* we have a header! */
	    last=first;
	    strsep(&last,":");

	    if(last==first) {
		DPRINTF(ERR_WARN,"Thread %d: Invalid header: %s\n",
			pwsc->threadno,first);
	    } else {
		while(*last==' ')
		    last++;

		while(last[strlen(last)-1] == '\r')
		    last[strlen(last)-1] = '\0';

		DPRINTF(ERR_DEBUG,"Thread %d: Adding header *%s=%s*\n",
			pwsc->threadno,first,last);

		if(ws_addarg(&pwsc->request_headers,first,last)) {
		    DPRINTF(ERR_FATAL,"Thread %d: Out of memory\n",
			    pwsc->threadno);
		    pwsc->error=ENOMEM;
		    return -1;
		}
	    }
	}
    }

    return 0;
}

/*
 * ws_getgetvars
 *
 * parse a GET string of variables (or POST)
 */
int ws_getgetvars(WS_CONNINFO *pwsc, char *string) {
    char *new_string;
    char *first, *last, *middle;
    int done;

    DPRINTF(ERR_DEBUG,"Thread %d: Original string: %s\n",
	    pwsc->threadno,string);

    new_string=ws_urldecode(string);

    DPRINTF(ERR_DEBUG,"Thread %d: Processing GET/POSTs from %s\n",
	    pwsc->threadno,string);

    done=0;

    first=new_string;
    
    while((!done) && (first)) {
	last=middle=first;
	strsep(&last,"&");
	strsep(&middle,"=");
	
	if(!middle) {
	    DPRINTF(ERR_WARN,"Thread %d: Bad arg: %s\n",
		    pwsc->threadno,first);
	} else {
	    DPRINTF(ERR_DEBUG,"Thread %d: Adding arg %s = %s\n",
		    pwsc->threadno,first,middle);
	    ws_addarg(&pwsc->request_vars,first,middle);
	}
	
	if(!last) {
	    DPRINTF(ERR_DEBUG,"Thread %d: Done parsing GET/POST args!\n",
		    pwsc->threadno);
	    done=1;
	} else {
	    first=last;
	}
    }

    free(new_string);
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
    WS_CONNINFO *pwsc=(WS_CONNINFO*)arg;
    WS_PRIVATE *pwsp=pwsc->pwsp;
    char buffer[MAX_LINEBUFFER];
    char *buffp;
    char *first,*last;
    int done;
    int connection_done=0;
    int can_dispatch;
    char *auth, *username, *password;
    int hdrs,handler;
    time_t now;
    struct tm now_tm;
    void (*req_handler)(WS_CONNINFO*);
    int(*auth_handler)(char *, char *);

    DPRINTF(ERR_DEBUG,"Thread %d: Connection from %s\n",pwsc->threadno,
	    pwsc->hostname);
	
    while(!connection_done) {
	/* Now, get the request from the other end
	 * and decide where to dispatch it
	 */

	if((readlinetimed(pwsc->fd,buffer,sizeof(buffer),30.0)) < 1) {
	    pwsc->error=errno;
	    pwsc->close=1;
	    DPRINTF(ERR_WARN,"Thread %d:  could not read: %s\n",
		    pwsc->threadno,strerror(errno));
	    ws_close(pwsc);
	    return NULL;
	}

	DPRINTF(ERR_DEBUG,"Thread %d: got request\n",pwsc->threadno);

	first=last=buffer;
	strsep(&last," ");
	if(!last) {
	    ws_returnerror(pwsc,400,"Bad request\n");
	    ws_close(pwsc);
	    return NULL;
	}
	
	if(!strcasecmp(first,"get")) {
	    pwsc->request_type = RT_GET;
	} else if(!strcasecmp(first,"post")) {
	    pwsc->request_type = RT_POST;
	} else {
	    /* return a 501 not implemented */
	    pwsc->error=EINVAL;
	    ws_returnerror(pwsc,501,"Not implemented");
	    ws_close(pwsc);
	    return NULL;
	}

	first=last;
	strsep(&last," ");
	pwsc->uri=strdup(first);
	
	/* Get headers */
	if(ws_getheaders(pwsc)) {
	    /* error already set */
	    DPRINTF(ERR_FATAL,"Thread %d: Couldn't parse headers - aborting\n",
		    pwsc->threadno);
	    ws_close(pwsc);
	    return NULL;
	}


	/* Now that we have the headers, we can
	 * decide whether or not this is a persistant
	 * connection */

	if(!pwsc->uri) {
	    /* We have memory allocation errors... might just
	     * as well bail */
	    pwsc->error=ENOMEM;
	    DPRINTF(ERR_FATAL,"Thread %d: Error allocation URI\n",
		    pwsc->threadno);
	    ws_returnerror(pwsc,500,"Internal server error");
	    ws_close(pwsc);
	    return NULL;
	}
	
	/* trim the URI */
	first=pwsc->uri;
	strsep(&first,"?");
	
	if(first) { /* got some GET args */
	    DPRINTF(ERR_DEBUG,"Thread %d: parsing GET args\n",pwsc->threadno);
	    ws_getgetvars(pwsc,first);
	}

	/* fix the URI by un urldecoding it */
	
	DPRINTF(ERR_DEBUG,"Thread %d: Original URI: %s\n",
		pwsc->threadno,pwsc->uri);
	
	first=ws_urldecode(pwsc->uri);
	free(pwsc->uri);
	pwsc->uri=first;
	
	DPRINTF(ERR_DEBUG,"Thread %d: Translated URI: %s\n",pwsc->threadno,
		pwsc->uri);

	/* now, parse POST args */
	if(pwsc->request_type == RT_POST)
	    ws_getpostvars(pwsc);

	hdrs=1;

	handler=ws_findhandler(pwsp,pwsc,&req_handler,&auth_handler,&hdrs);

	time(&now);
	DPRINTF(ERR_DEBUG,"Thread %d: Time is %d seconds after epoch\n",
		pwsc->threadno,now);
	gmtime_r(&now,&now_tm);
	DPRINTF(ERR_DEBUG,"Thread %d: Setting time header\n",pwsc->threadno);
	ws_addarg(&pwsc->response_headers,"Date", 
		  "%s, %d %s %d %02d:%02d:%02d GMT",
		  ws_dow[now_tm.tm_wday],now_tm.tm_mday,
		  ws_moy[now_tm.tm_mon],now_tm.tm_year + 1900,
		  now_tm.tm_hour,now_tm.tm_min,now_tm.tm_sec);

	pwsc->close=ws_testarg(&pwsc->request_headers,"connection",
			       "close");

	if(hdrs) {
	    ws_addarg(&pwsc->response_headers,"Connection",
		      pwsc->close ? "close" : "keep-alive");
	    
	    ws_addarg(&pwsc->response_headers,"Server",
		      "mt-daapd/%s",VERSION);
	    
	    ws_addarg(&pwsc->response_headers,"Content-Type","text/html");
	    ws_addarg(&pwsc->response_headers,"Content-Language","en_us");
	}

	/* Find the appropriate handler and dispatch it */
	if(handler == -1) {
	    DPRINTF(ERR_DEBUG,"Thread %d: Using default handler.\n",
		    pwsc->threadno);
	    ws_defaulthandler(pwsp,pwsc);
	} else {
	    DPRINTF(ERR_DEBUG,"Thread %d: Using non-default handler\n",
		    pwsc->threadno);

	    can_dispatch=0;
	    if(auth_handler) {
		/* do the auth thing */
		auth=ws_getarg(&pwsc->request_headers,"Authorization");
		if(auth) {
		    ws_decodepassword(auth,&username,&password);
		    if(auth_handler(username,password))
			can_dispatch=1;
		    ws_addarg(&pwsc->request_vars,"HTTP_USER",username);
		    free(username);
		} 

		if(!can_dispatch) { /* auth failed, or need auth */
		    ws_addarg(&pwsc->response_headers,"Connection","close");
		    ws_addarg(&pwsc->response_headers,"WWW-Authenticate",
			      "Basic realm=\"webserver\"");
		    pwsc->close=1;
		    ws_returnerror(pwsc,401,"Unauthorized");
		    ws_close(pwsc);
		    return NULL;
		}
	    }
	    if(req_handler)
		req_handler(pwsc);
	    else
		ws_defaulthandler(pwsp,pwsc);		
	}

	if((pwsc->close) || (pwsc->error) || (pwsp->stop)) {
	    pwsc->close=1;
	    connection_done=1;
	}
	ws_close(pwsc);
    }
    return NULL;
}


/*
 * ws_writefd
 *
 * Write a printf-style output to a connfd
 */
int ws_writefd(WS_CONNINFO *pwsc, char *fmt, ...) {
    char buffer[1024];

    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, 1024, fmt, ap);
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
    DPRINTF(ERR_WARN,"Thread %d: Pushing a %d: %s\n",
	    pwsc->threadno,error,description);
    ws_writefd(pwsc,"HTTP/1.1 %d %s\r\n",error,description);

    ws_emitheaders(pwsc);

    ws_writefd(pwsc,"<HTML>\r\n<TITLE>");
    ws_writefd(pwsc,"%d %s</TITLE>\r\n<BODY>",error,description);
    ws_writefd(pwsc,"\r\n<H1>%s</H1>\r\n",description);
    ws_writefd(pwsc,"Error %d\r\n<hr>\r\n",error);
    ws_writefd(pwsc,"<i>mt-daapd: %s\r\n<br>",VERSION);
    if(errno) 
	ws_writefd(pwsc,"Error: %s\r\n",strerror(errno));

    ws_writefd(pwsc,"</i></BODY>\r\n</HTML>\r\n");

    return 0;
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
    off_t len;
    
    snprintf(path,MAXPATHLEN,"%s/%s",pwsp->wsconfig.web_root,pwsc->uri);
    if(!realpath(path,resolved_path)) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Cannot resolve %s\n",path);
	ws_returnerror(pwsc,404,"Not found");
	ws_close(pwsc);
	return;
    }

    DPRINTF(ERR_DEBUG,"Thread %d: Preparing to serve %s\n",
	    pwsc->threadno, resolved_path);

    if(strncmp(resolved_path,pwsp->wsconfig.web_root,
	       strlen(pwsp->wsconfig.web_root))) {
	pwsc->error=EINVAL;
	DPRINTF(ERR_WARN,"Thread %d: Requested file %s out of root\n",
		pwsc->threadno,resolved_path);
	ws_returnerror(pwsc,403,"Forbidden");
	ws_close(pwsc);
	return;
    }

    file_fd=open(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
		pwsc->threadno,resolved_path,strerror(errno));
	ws_returnerror(pwsc,404,"Not found");
	ws_close(pwsc);
	return;
    }
    
    /* set the Content-Length response header */
    len=lseek(file_fd,0,SEEK_END);

    /* FIXME: assumes off_t == long */
    if(len != -1) {
	/* we have a real length */
	DPRINTF(ERR_DEBUG,"Length of file is %ld\n",(long)len);
	ws_addarg(&pwsc->response_headers,"Content-Length","%ld",(long)len);
	lseek(file_fd,0,SEEK_SET);
    }


    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    /* now throw out the file */
    copyfile(file_fd,pwsc->fd);

    r_close(file_fd);
    DPRINTF(ERR_DEBUG,"Thread %d: Served successfully\n",pwsc->threadno);
    return;
}


/*
 * ws_testrequestheader
 *
 * Check to see if a request header is a particular value
 *
 * Example:
 *
 */
int ws_testrequestheader(WS_CONNINFO *pwsc, char *header, char *value) {
    return ws_testarg(&pwsc->request_headers,header,value);
}

/*
 * ws_testarg
 *
 * Check an arg for a particular value
 *
 * Example:
 *
 * pwsc->close=ws_queryarg(&pwsc->request_headers,"Connection","close");
 *
 */
int ws_testarg(ARGLIST *root, char *key, char *value) {
    DPRINTF(ERR_DEBUG,"Checking to see if %s matches %s\n",key,value);

    char *retval=ws_getarg(root,key);
    if(!retval)
	return 0;

    return !strcasecmp(value,retval);
}

/*
 * ws_getarg
 *
 * Find an argument in an argument list
 *
 * returns a pointer to the value if successful,
 * NULL otherwise.
 *
 * This should be passed the pointer to the
 * stub in the pwsc.
 *
 * Ex: ws_getarg(pwsc->request_headers,"Connection");
 */
char *ws_getarg(ARGLIST *root, char *key) {
    ARGLIST *pcurrent=root->next;

    while((pcurrent)&&(strcasecmp(pcurrent->key,key))) 
	pcurrent=pcurrent->next;

    if(pcurrent)
	return pcurrent->value;
    return NULL;
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
int ws_addarg(ARGLIST *root, char *key, char *fmt, ...) {
    char *newkey;
    char *newvalue;
    ARGLIST *pnew;
    ARGLIST *current;
    va_list ap;
    char value[MAX_LINEBUFFER];

    va_start(ap,fmt);
    vsnprintf(value,sizeof(value),fmt,ap);
    va_end(ap);

    newkey=strdup(key);
    newvalue=strdup(value);
    pnew=(ARGLIST*)malloc(sizeof(ARGLIST));

    if((!pnew)||(!newkey)||(!newvalue))
	return -1;

    pnew->key=newkey;
    pnew->value=newvalue;

    /* first, see if the key exists... if it does, simply
     * replace it rather than adding a duplicate key
     */

    current=root->next;
    while(current) {
	if(!strcmp(current->key,key)) {
	    /* got a match! */
	    DPRINTF(ERR_DEBUG,"Updating %s from %s to %s\n",
		    key,current->value,value);
	    free(current->value);
	    current->value = newvalue;
	    free(newkey);
	    free(pnew);
	    return 0;
	}
	current=current->next;
    }


    pnew->next=root->next;
    DPRINTF(ERR_DEBUG,"Added *%s=%s*\n",newkey,newvalue);
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

    pnew=(char*)malloc(strlen(string)+1);
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


/*
 * ws_registerhandler
 *
 * Register a page and auth handler.  Returns 0 on success,
 * returns -1 on failure.
 *
 * If the regex is not well-formed, it returns -1 iwth
 * errno set to EINVAL.  It is up to the caller to use
 * regerror to display a more interesting error message,
 * if appropriate.
 */
int ws_registerhandler(WSHANDLE ws, char *regex, 
		       void(*handler)(WS_CONNINFO*),
		       int(*auth)(char *, char *),
		       int addheaders) {
    WS_HANDLER *phandler;
    WS_PRIVATE *pwsp = (WS_PRIVATE *)ws;

    phandler=(WS_HANDLER *)malloc(sizeof(WS_HANDLER));
    if(!phandler)
	return -1;

    if(regcomp(&phandler->regex,regex,REG_EXTENDED | REG_NOSUB)) {
	free(phandler);
	errno=EINVAL;
	return -1;
    }

    phandler->req_handler=handler;
    phandler->auth_handler=auth;
    phandler->addheaders=addheaders;

    ws_lock_unsafe();
    phandler->next=pwsp->handlers.next;
    pwsp->handlers.next=phandler;
    ws_unlock_unsafe();

    return 0;
}

/*
 * ws_findhandler
 *
 * Given a URI, determine the appropriate handler.
 *
 * If a handler is found, it returns 0, otherwise, returns
 * -1
 */
int ws_findhandler(WS_PRIVATE *pwsp, WS_CONNINFO *pwsc, 
		   void(**preq)(WS_CONNINFO*),
		   int(**pauth)(char *, char *),
		   int *addheaders) {
    WS_HANDLER *phandler=pwsp->handlers.next;

    ws_lock_unsafe();

    DPRINTF(ERR_DEBUG,"Thread %d: Preparing to find handler\n",
	    pwsc->threadno);

    while(phandler) {
	if(!regexec(&phandler->regex,pwsc->uri,0,NULL,0)) {
	    /* that's a match */
	    DPRINTF(ERR_DEBUG,"Thread %d: URI Match!\n",pwsc->threadno);
	    *preq=phandler->req_handler;
	    *pauth=phandler->auth_handler;
	    *addheaders=phandler->addheaders;
	    ws_unlock_unsafe();
	    return 0;
	}
	phandler=phandler->next;
    }

    ws_unlock_unsafe();
    return -1;
}

/*
 * ws_decodepassword
 *
 * Given a base64 encoded Authentication request header,
 * decode it into the username and password
 */
int ws_decodepassword(char *header, char **username, char **password) {
    static char ws_xlat[256];
    static int ws_xlat_init=0;
    int index;
    int len;
    int rack=0;
    int pads=0;
    unsigned char *decodebuffer;
    unsigned char *pin, *pout;
    int lookup;

    *username=NULL;
    *password=NULL;

    if(ws_lock_unsafe() == -1)
	return -1;

    if(!ws_xlat_init) {
	ws_xlat_init=1;

	memset((char*)&ws_xlat,0xFF,sizeof(ws_xlat));
	for(index=0; index < 26; index++) {
	    ws_xlat['A' + index] = index;
	    ws_xlat['a' + index] = index + 26;
	}

	for(index=0; index < 10; index++) {
	    ws_xlat['0' + index] = index + 52;
	}

	ws_xlat['+'] = 62;
	ws_xlat['/'] = 63;
    }
    if(ws_unlock_unsafe() == -1)
	return -1;

    /* xlat table is initialized */
    while(*header != ' ')
	header++;

    header++;

    decodebuffer=(unsigned char *)malloc(strlen(header));
    if(!decodebuffer)
	return;

    DPRINTF(ERR_DEBUG,"Preparing to decode %s\n",header);

    memset(decodebuffer,0,strlen(header));
    len=0;
    pout=decodebuffer;
    pin=header;

    /* this is more than a little sloppy */
    while(pin[rack]) {
	if(pin[rack] != '=') {
	    lookup=ws_xlat[pin[rack]];
	    if(lookup == 0xFF) {
		DPRINTF(ERR_WARN,"Got garbage Authenticate header\n");
		return -1;
	    }

	    /* valid character */
	    switch(rack) {
	    case 0:
		pout[0]=(lookup << 2);
		break;
	    case 1:
		pout[0] |= (lookup >> 4);
		pout[1] = (lookup << 4);
		break;
	    case 2:
		pout[1] |= (lookup >> 2);
		pout[2] = (lookup << 6);
		break;
	    case 3:
		pout[2] |= lookup;
		break;
	    }
	    rack++;
	} else {
	    /* padding char */
	    pads++;
	    rack++;
	}

	if(rack == 4) {
	    pin += 4;
	    pout += 3;

	    len += (3-pads);
	    rack=0;
	}
    }

    /* we now have the decoded string */
    DPRINTF(ERR_DEBUG,"Decoded %s\n",decodebuffer);

    *username = decodebuffer;
    *password = *username;

    strsep(password,":");

    DPRINTF(ERR_DEBUG,"Decoded user=%s, pw=%s\n",*username,*password);
    return 0;
}

/*
 * ws_addreponseheader
 *
 * Simple wrapper around the CONNINFO response headers
 */
int ws_addresponseheader(WS_CONNINFO *pwsc, char *header, char *fmt, ...) {
    va_list ap;
    char value[MAX_LINEBUFFER];

    va_start(ap,fmt);
    vsnprintf(value,sizeof(value),fmt,ap);
    va_end(ap);

    return ws_addarg(&pwsc->response_headers,header,value);
}

/*
 * ws_getvar
 *
 * Simple wrapper around the CONNINFO request vars
 */
char *ws_getvar(WS_CONNINFO *pwsc, char *var) {
    return ws_getarg(&pwsc->request_vars,var);
}

char *ws_getrequestheader(WS_CONNINFO *pwsc, char *header) {
    return ws_getarg(&pwsc->request_headers,header);
}
