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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "configfile.h"
#include "err.h"


/*
 * Forwards
 */
void config_emit_string(WS_CONNINFO *pwsc, void *value);
void config_emit_literal(WS_CONNINFO *pwsc, void *value);
void config_emit_int(WS_CONNINFO *pwsc, void *value);

/*
 * Defines
 */
#define CONFIG_TYPE_INT       0
#define CONFIG_TYPE_STRING    1

typedef struct tag_configelement {
    int config_element;
    int required;
    int changed;
    int type;
    char *name;
    void *var;
    void (*emit)(WS_CONNINFO *, void *);
} CONFIGELEMENT;

CONFIGELEMENT config_elements[] = {
    { 1,1,0,CONFIG_TYPE_STRING,"web_root",(void*)&config.web_root,config_emit_string },
    { 1,1,0,CONFIG_TYPE_INT,"port",(void*)&config.port,config_emit_int },
    { 1,1,0,CONFIG_TYPE_STRING,"admin_pw",(void*)&config.adminpassword,config_emit_string },
    { 1,1,0,CONFIG_TYPE_STRING,"mp3_dir",(void*)&config.mp3dir,config_emit_string },
    { 0,0,0,CONFIG_TYPE_STRING,"release",(void*)VERSION,config_emit_literal },
    { 0,0,0,CONFIG_TYPE_STRING,"package",(void*)PACKAGE,config_emit_literal },
    { -1,1,0,CONFIG_TYPE_STRING,NULL,NULL,NULL }
};


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
 * config_write
 *
 */
int config_write(CONFIG *pconfig) {
    return 0;
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
    char argbuffer[30];
    int in_arg;
    char *argptr;
    char next;
    CONFIGELEMENT *pce;

    DPRINTF(ERR_DEBUG,"Entereing config_handler\n");
    
    pwsc->close=1;
    ws_addresponseheader(pwsc,"Connection","close");

    snprintf(path,PATH_MAX,"%s/%s",config.web_root,pwsc->uri);
    if(!realpath(path,resolved_path)) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Cannot resolve %s\n",path);
	ws_returnerror(pwsc,404,"Not found");
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
	return;
    }

    file_fd=r_open2(resolved_path,O_RDONLY);
    if(file_fd == -1) {
	pwsc->error=errno;
	DPRINTF(ERR_WARN,"Thread %d: Error opening %s: %s\n",
		pwsc->threadno,resolved_path,strerror(errno));
	ws_returnerror(pwsc,404,"Not found");
	return;
    }
    
    if(strcasecmp(pwsc->uri,"/config-update.html")==0) {
	/* we need to update stuff */
	argptr=ws_getvar(pwsc,"adminpw");
	if(argptr) {
	    if(config.adminpassword)
		free(config.adminpassword);
	    config.adminpassword=strdup(argptr);
	}
    }

    ws_writefd(pwsc,"HTTP/1.1 200 OK\r\n");
    ws_emitheaders(pwsc);

    /* now throw out the file, with replacements */
    in_arg=0;
    argptr=argbuffer;

    while(1) {
	if(r_read(file_fd,&next,1) <= 0)
	    break;

	if(in_arg) {
	    if(next == '@') {
		in_arg=0;

		DPRINTF(ERR_DEBUG,"Got directive %s\n",argbuffer);

		pce=config_elements;
		while(pce->config_element != -1) {
		    if(strcasecmp(argbuffer,pce->name) == 0) {
			pce->emit(pwsc, pce->var);
			break;
		    }
		    pce++;
		}

		if(pce->config_element == -1) { /* bad subst */
		    ws_writefd(pwsc,"@%s@",argbuffer);
		}
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

    r_close(file_fd);
    DPRINTF(ERR_DEBUG,"Thread %d: Served successfully\n",pwsc->threadno);
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
void config_emit_string(WS_CONNINFO *pwsc, void *value) {
    ws_writefd(pwsc,"%s",*((char**)value));
}

/*
 * config_emit_literal
 *
 * Emit a regular char *
 */
void config_emit_literal(WS_CONNINFO *pwsc, void *value) {
    ws_writefd(pwsc,"%s",(char*)value);
}


/*
 * config_emit_int
 *
 * write a simple int value to the connection
 */
void config_emit_int(WS_CONNINFO *pwsc, void *value) {
    ws_writefd(pwsc,"%d",*((int*)value));
}

