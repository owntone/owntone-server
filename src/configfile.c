/*
 * $Id$
 * Functions for reading and writing the config file
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "configfile.h"
#include "err.h"


/*
 * Forwards
 */
void config_emit_string(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_literal(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_int(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_include(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_threadstatus(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_ispage(WS_CONNINFO *pwsc, void *value, char *arg);
void config_emit_session_count(WS_CONNINFO *pwsc, void *value, char *arg);
void config_subst_stream(WS_CONNINFO *pwsc, int fd_src);
int config_mutex_lock(void);
int config_mutex_unlock(void);

/*
 * Defines
 */
#define CONFIG_TYPE_INT       0
#define CONFIG_TYPE_STRING    1
#define CONFIG_TYPE_SPECIAL   4

typedef struct tag_configelement {
    int config_element;
    int required;
    int changed;
    int type;
    char *name;
    void *var;
    void (*emit)(WS_CONNINFO *, void *, char *);
} CONFIGELEMENT;

CONFIGELEMENT config_elements[] = {
    { 1,1,0,CONFIG_TYPE_STRING,"web_root",(void*)&config.web_root,config_emit_string },
    { 1,1,0,CONFIG_TYPE_INT,"port",(void*)&config.port,config_emit_int },
    { 1,1,0,CONFIG_TYPE_STRING,"admin_pw",(void*)&config.adminpassword,config_emit_string },
    { 1,1,0,CONFIG_TYPE_STRING,"mp3_dir",(void*)&config.mp3dir,config_emit_string },
    { 1,1,0,CONFIG_TYPE_STRING,"servername",(void*)&config.servername,config_emit_string },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"release",(void*)VERSION,config_emit_literal },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"package",(void*)PACKAGE,config_emit_literal },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"include",(void*)NULL,config_emit_include },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"threadstat",(void*)NULL,config_emit_threadstatus },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"ispage",(void*)NULL,config_emit_ispage },
    { 0,0,0,CONFIG_TYPE_SPECIAL,"session-count",(void*)NULL,config_emit_session_count },
    { -1,1,0,CONFIG_TYPE_STRING,NULL,NULL,NULL }
};

typedef struct tag_scan_status {
    int session;
    int thread;
    char *what;
    char *host;
    struct tag_scan_status *next;
} SCAN_STATUS;

SCAN_STATUS scan_status = { 0,0,NULL,NULL };
pthread_mutex_t scan_mutex = PTHREAD_MUTEX_INITIALIZER;
int config_session=0;

#define MAX_LINE 1024


/*
 * config_read
 *
 * Read the specified config file, padding the config structure
 * appropriately.
 *
 * This function returns 0 on success, errorcode on failure
 */
int config_read(char *file) {
    FILE *fin;
    char *buffer;
    int err;
    char *value;
    char *comment;
    char path_buffer[PATH_MAX];
    err=0;
    CONFIGELEMENT *pce;
    int handled;

    buffer=(char*)malloc(MAX_LINE);
    if(!buffer)
	return -1;

    if((fin=fopen(file,"r")) == NULL) {
	err=errno;
	free(buffer);
	errno=err;
	return -1;
    }

    memset((void*)&config,0,sizeof(CONFIG));

    config.configfile=strdup(file);

    while(fgets(buffer,MAX_LINE,fin)) {
	if(*buffer != '#') {
	    value=buffer;
	    strsep(&value,"\t ");
	    if(value) {
		while((*value==' ')||(*value=='\t'))
		    value++;

		comment=value;
		strsep(&comment,"#");

		if(value[strlen(value)-1] == '\n')
		    value[strlen(value)-1] = '\0';

		pce=config_elements;
		handled=0;
		while((!handled) && (pce->config_element != -1)) {
		    if((strcasecmp(buffer,pce->name)==0) && (pce->config_element)) {
			/* valid config directive */
			handled=1;
			pce->changed=1;
			
			switch(pce->type) {
			case CONFIG_TYPE_STRING:
			    *((char **)(pce->var)) = (void*)strdup(value);
			    break;
			case CONFIG_TYPE_INT:
			    *((int*)(pce->var)) = atoi(value);
			    break;
			}
		    }
		    pce++;
		}

		if(!handled) {
		    fprintf(stderr,"Invalid config directive: %s\n",buffer);
		    fclose(fin);
		    return -1;
		}
	    }
	}
    }

    fclose(fin);
    free(buffer);

    /* fix the fullpath of the web root */
    realpath(config.web_root,path_buffer);
    free(config.web_root);
    config.web_root=strdup(path_buffer);

    /* check to see if all required elements are satisfied */
    pce=config_elements;
    err=0;
    while((pce->config_element != -1)) {
	if(pce->required && pce->config_element && !pce->changed) {
	    fprintf(stderr,"Required config entry '%s' not specified\n",pce->name);
	    errno=EINVAL;
	    err=-1;
	}
	if((pce->config_element) && (pce->changed)) {
	    switch(pce->type) {
	    case CONFIG_TYPE_STRING:
		DPRINTF(ERR_INFO,"%s: %s\n",pce->name,*((char**)pce->var));
		break;
	    case CONFIG_TYPE_INT:
		DPRINTF(ERR_INFO,"%s: %d\n",pce->name,*((int*)pce->var));
		break;
	    }
	}

	pce->changed=0;
	pce++;
    }
    

    return err;
}


/*
 * config_close
 *
 * free up any memory used
 */
void config_close(void) {
    CONFIGELEMENT *pce;
    int err;

    /* check to see if all required elements are satisfied */
    free(config.configfile);
    pce=config_elements;
    err=0;
    while((pce->config_element != -1)) {
	if((pce->config_element) && (pce->type == CONFIG_TYPE_STRING)) 
	    free(*((char**)pce->var));
	pce++;
    }
}

/*
 * config_write
 *
 */
int config_write(CONFIG *pconfig) {
    return 0;
}

/* 
 * config_subst_stream
 * 
 * walk through a stream doing substitution on the
 * meta commands
 */
void config_subst_stream(WS_CONNINFO *pwsc, int fd_src) {
    int in_arg;
    char *argptr;
    char argbuffer[80];
    char next;
    CONFIGELEMENT *pce;
    char *first, *last;

    /* now throw out the file, with replacements */
    in_arg=0;
    argptr=argbuffer;

    while(1) {
	if(r_read(fd_src,&next,1) <= 0)
	    break;

	if(in_arg) {
	    if((next == '@') && (strlen(argbuffer) > 0)) {
		in_arg=0;

		DPRINTF(ERR_DEBUG,"Got directive %s\n",argbuffer);

		/* see if there are args */
		first=last=argbuffer;
		strsep(&last," ");

		pce=config_elements;
		while(pce->config_element != -1) {
		    if(strcasecmp(first,pce->name) == 0) {
			pce->emit(pwsc, pce->var,last);
			break;
		    }
		    pce++;
		}

		if(pce->config_element == -1) { /* bad subst */
		    ws_writefd(pwsc,"@%s@",argbuffer);
		}
	    } else if(next == '@') {
		ws_writefd(pwsc,"@");
		in_arg=0;
	    } else {
		if((argptr - argbuffer) < (sizeof(argbuffer)-1))
		    *argptr++ = next;
	    }
	} else {
	    if(next == '@') {
		argptr=argbuffer;
		memset(argbuffer,0,sizeof(argbuffer));
		in_arg=1;
	    } else {
		if(r_write(pwsc->fd,&next,1) == -1)
		    break;
	    }
	}
    }
}

/*
 * config_handler
 *
 * Handle serving pages from the admin-root
 */
void config_handler(WS_CONNINFO *pwsc) {
    char path[PATH_MAX];
    char resolved_path[PATH_MAX];
    int file_fd;
    struct stat sb;
    char *pw;

    DPRINTF(ERR_DEBUG,"Entereing config_handler\n");

    config_set_status(pwsc,0,"Serving admin pages");
    
    pwsc->close=1;
    ws_addresponseheader(pwsc,"Connection","close");

    snprintf(path,PATH_MAX,"%s/%s",config.web_root,pwsc->uri);
    if(!realpath(path,resolved_path)) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Cannot resolve %s\n",path);
	ws_returnerror(pwsc,404,"Not found");
	config_set_status(pwsc,0,NULL);
	return;
    }

    /* this should really return a 302:Found */
    stat(resolved_path,&sb);
    if(sb.st_mode & S_IFDIR)
	strcat(resolved_path,"/index.html");

    DPRINTF(ERR_DEBUG,"Thread %d: Preparing to serve %s\n",
	    pwsc->threadno, resolved_path);

    if(strncmp(resolved_path,config.web_root,
	       strlen(config.web_root))) {
	pwsc->error=EINVAL;
	DPRINTF(ERR_WARN,"Thread %d: Requested file %s out of root\n",
		pwsc->threadno,resolved_path);
	ws_returnerror(pwsc,403,"Forbidden");
	config_set_status(pwsc,0,NULL);
	return;
    }

    file_fd=r_open2(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
		pwsc->threadno,resolved_path,strerror(errno));
	ws_returnerror(pwsc,404,"Not found");
	config_set_status(pwsc,0,NULL);
	return;
    }
    
    if(strcasecmp(pwsc->uri,"/config-update.html")==0) {
	/* we need to update stuff */
	pw=ws_getvar(pwsc,"adminpw");
	if(pw) {
	    if(config.adminpassword)
		free(config.adminpassword);
	    config.adminpassword=strdup(pw);
	}
    }

    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);
    
    config_subst_stream(pwsc, file_fd);

    r_close(file_fd);
    DPRINTF(ERR_DEBUG,"Thread %d: Served successfully\n",pwsc->threadno);
    config_set_status(pwsc,0,NULL);
    return;
}

int config_auth(char *user, char *password) {
    if((!password)||(!config.adminpassword))
	return 0;
    return !strcmp(password,config.adminpassword);
}


/*
 * config_emit_string
 *
 * write a simple string value to the connection
 */
void config_emit_string(WS_CONNINFO *pwsc, void *value, char *arg) {
    ws_writefd(pwsc,"%s",*((char**)value));
}

/*
 * config_emit_literal
 *
 * Emit a regular char *
 */
void config_emit_literal(WS_CONNINFO *pwsc, void *value, char *arg) {
    ws_writefd(pwsc,"%s",(char*)value);
}


/*
 * config_emit_int
 *
 * write a simple int value to the connection
 */
void config_emit_int(WS_CONNINFO *pwsc, void *value, char *arg) {
    ws_writefd(pwsc,"%d",*((int*)value));
}

/*
 * config_emit_session_count
 *
 * emit the number of unique hosts (with a session)
 */
void config_emit_session_count(WS_CONNINFO *pwsc, void *value, char *arg) {
    SCAN_STATUS *pcurrent, *pcheck;
    int count=0;

    if(config_mutex_lock())
	return;

    pcurrent=scan_status.next;

    while(pcurrent) {
	if(pcurrent->session != 0) {
	    /* check to see if there is another one before this one */
	    pcheck=scan_status.next;
	    while(pcheck != pcurrent) {
		if(pcheck->session == pcurrent->session) 
		    break;
		pcheck=pcheck->next;
	    }

	    if(pcheck == pcurrent)
		count++;
	}
	pcurrent=pcurrent->next;
    }

    ws_writefd(pwsc,"%d",count);
    config_mutex_unlock();
}

/*
 * config_emit_threadstatus
 *
 * dump thread status info into a html table
 */
void config_emit_threadstatus(WS_CONNINFO *pwsc, void *value, char *arg) {
    SCAN_STATUS *pss;
    
    if(config_mutex_lock())
	return;

    ws_writefd(pwsc,"<TABLE><TR><TH ALIGN=LEFT>Thread</TH>");
    ws_writefd(pwsc,"<TH ALIGN=LEFT>Session</TH><TH ALIGN=LEFT>Host</TH>");
    ws_writefd(pwsc,"<TH ALIGN=LEFT>Action</TH></TR>\n");


    pss=scan_status.next;
    while(pss) {
	ws_writefd(pwsc,"<TR><TD>%d</TD><TD>%d</TD><TD>%s</TD><TD>%s</TD></TR>\n",
		   pss->thread,pss->session,pss->host,pss->what);
	pss=pss->next;
    }

    ws_writefd(pwsc,"</TABLE>\n");
    config_mutex_unlock();
}


/*
 * config_emit_ispage
 *
 * This is a tacky function to make the headers look right.  :)
 */
void config_emit_ispage(WS_CONNINFO *pwsc, void *value, char *arg) {
    char *first;
    char *last;

    char *page, *true, *false;

    DPRINTF(ERR_DEBUG,"Splitting arg %s\n",arg);

    first=last=arg;
    strsep(&last,":");
    
    if(last) {
	page=strdup(first);
	if(!page)
	    return;
	first=last;
	strsep(&last,":");
	if(last) {
	    true=strdup(first);
	    false=strdup(last);
	    if((!true)||(!false))
		return;
	} else {
	    true=strdup(first);
	    if(!true)
		return;
	    false=NULL;
	}
    } else {
	return;
    }


    DPRINTF(ERR_DEBUG,"page: %s, uri: %s\n",page,pwsc->uri);

    if((strlen(page) > strlen(pwsc->uri)) ||
       (strcasecmp(page,(char*)&pwsc->uri[strlen(pwsc->uri) - strlen(page)]) != 0)) {
	ws_writefd(pwsc,"%s",false);
    } else {
	ws_writefd(pwsc,"%s",true);
    }


    if(page)
	free(page);

    if(true)
	free(true);

    if(false)
	free(false);
}

/*
 * config_emit_include
 *
 * Do a server-side include
 */
void config_emit_include(WS_CONNINFO *pwsc, void *value, char *arg) {
    char resolved_path[PATH_MAX];
    char path[PATH_MAX];
    int file_fd;
    struct stat sb;
    char argbuffer[30];
    int in_arg;
    char *argptr;
    char next;
    CONFIGELEMENT *pce;
    char *first, *last;

    DPRINTF(ERR_DEBUG,"Preparing to include %s\n",arg);
    
    snprintf(path,PATH_MAX,"%s/%s",config.web_root,arg);
    if(!realpath(path,resolved_path)) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Cannot resolve %s\n",path);
	ws_writefd(pwsc,"<hr><i>error: cannot find %s</i><hr>",arg);
	return;
    }

    /* this should really return a 302:Found */
    stat(resolved_path,&sb);
    if(sb.st_mode & S_IFDIR) {
	ws_writefd(pwsc,"<hr><i>error: cannot include director %s</i><hr>",arg);
	return;
    }


    DPRINTF(ERR_DEBUG,"Thread %d: Preparing to serve %s\n",
	    pwsc->threadno, resolved_path);

    if(strncmp(resolved_path,config.web_root,
	       strlen(config.web_root))) {
	pwsc->error=EINVAL;
	DPRINTF(ERR_WARN,"Thread %d: Requested file %s out of root\n",
		pwsc->threadno,resolved_path);
	ws_writefd(pwsc,"<hr><i>error: %s out of web root</i><hr>",arg);
	return;
    }

    file_fd=r_open2(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
		pwsc->threadno,resolved_path,strerror(errno));
	ws_writefd(pwsc,"<hr><i>error: cannot open %s: %s</i><hr>",arg,strerror(errno));
	return;
    }
    
    config_subst_stream(pwsc, file_fd);

    r_close(file_fd);
    DPRINTF(ERR_DEBUG,"Thread %d: included successfully\n",pwsc->threadno);
    return;
}

/*
 * config_set_status
 *
 * update the status info for a particular thread
 */
void config_set_status(WS_CONNINFO *pwsc, int session, char *fmt, ...) {
    char buffer[1024];
    va_list ap;
    SCAN_STATUS *pfirst, *plast;

    if(config_mutex_lock()) {
	/* we should really shutdown the app here... */
	exit(EXIT_FAILURE);
    }

    pfirst=plast=scan_status.next;
    while((pfirst) && (pfirst->thread != pwsc->threadno)) {
	plast=pfirst;
	pfirst=pfirst->next;
    }

    if(fmt) {
	va_start(ap, fmt);
	vsnprintf(buffer, 1024, fmt, ap);
	va_end(ap);

	if(pfirst) { /* already there */
	    free(pfirst->what);
	    pfirst->what = strdup(buffer);
	    pfirst->session = session; /* this might change! */
	} else {
	    pfirst=(SCAN_STATUS*)malloc(sizeof(SCAN_STATUS));
	    if(pfirst) {
		pfirst->what = strdup(buffer);
		pfirst->session = session;
		pfirst->thread = pwsc->threadno;
		pfirst->next = scan_status.next;
		pfirst->host = strdup(pwsc->hostname);
		scan_status.next=pfirst;
	    }
	}
    } else {
	if(!pfirst) {
	    config_mutex_unlock();
	    return;
	}

	if(pfirst==plast) { 
	    scan_status.next=pfirst->next;
	    free(pfirst->what);
	    free(pfirst->host);
	    free(pfirst);
	} else {
	    plast->next = pfirst->next;
	    free(pfirst->what);
	    free(pfirst->host);
	    free(pfirst);
	}
    }

    config_mutex_unlock();
}

/*
 * config_mutex_lock
 *
 * Lock the scan status mutex
 */
int config_mutex_lock(void) {
    int err;

    if(err=pthread_mutex_lock(&scan_mutex)) {
	errno=err;
	return - 1;
    }

    return 0;
}

/*
 * config_mutex_unlock
 *
 * Unlock the scan status mutex
 */
int config_mutex_unlock(void) {
    int err;

    if(err=pthread_mutex_unlock(&scan_mutex)) {
	errno=err;
	return -1;
    }

    return 0;
}

/*
 * return the next available session ID
 */
int config_get_next_session(void) {
    int session;
    config_mutex_lock();

    session=++config_session;
    config_mutex_unlock();

    return session;
}
