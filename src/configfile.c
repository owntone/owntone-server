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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configfile.h"
#include "err.h"

/*
 * Defines
 */

#define MAX_LINE 1024


/*
 * config_read
 *
 * Read the specified config file, padding the config structure
 * appropriately.
 *
 * This function returns 0 on success, errorcode on failure
 */
int config_read(char *file, CONFIG *pconfig) {
    FILE *fin;
    char *buffer;
    int err;
    char *value;
    char *comment;
    err=0;

    buffer=(char*)malloc(MAX_LINE);
    if(!buffer)
	return -1;

    if((fin=fopen(file,"r")) == NULL) {
	err=errno;
	free(buffer);
	errno=err;
	return -1;
    }

    memset(pconfig,0,sizeof(CONFIG));

    pconfig->configfile=strdup(file);

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

		if(!strcasecmp(buffer,"web_root")) {
		    pconfig->web_root=strdup(value);
		    DPRINTF(ERR_DEBUG,"Web root: %s\n",value);
		} else if(!strcasecmp(buffer,"port")) {
		    pconfig->port=atoi(value);
		    DPRINTF(ERR_DEBUG,"Port: %d\n",pconfig->port);
		} else if(!strcasecmp(buffer,"admin_password")) {
		    pconfig->adminpassword=strdup(value);
		    DPRINTF(ERR_DEBUG,"Admin pw: %s\n",value);
		} else if(!strcasecmp(buffer,"mp3_dir")) {
		    pconfig->mp3dir=strdup(value);
		    DPRINTF(ERR_DEBUG,"MP3 Dir: %s\n",value);
		} else {
		    DPRINTF(ERR_INFO,"Bad config directive: %s\n",buffer);
		}
	    }
	}
    }

    fclose(fin);

    if(!pconfig->web_root) {
	fprintf(stderr,"Config: missing web_root entry\n");
	errno=EINVAL;
	err=-1;
    }

    if(!pconfig->adminpassword) {
	fprintf(stderr,"Config: missing admin_password entry\n");
	errno=EINVAL;
	err=-1;
    }

    if(!pconfig->port) {
	fprintf(stderr,"Config: missing port entry\n");
	errno=EINVAL;
	err=-1;
    }

    if(!pconfig->mp3dir) {
	fprintf(stderr,"Config: missing mp3_dir entry\n");
	errno=EINVAL;
	err=-1;
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

