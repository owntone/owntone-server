/*
 * $Id$
 * Implementation file for server side format conversion.
 *
 * Copyright (C) 2005 Timo J. Rinne (tri@iki.fi)
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

#define _POSIX_PTHREAD_SEMANTICS
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <id3tag.h>
#include <limits.h>
#include <restart.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>  /* htons and friends */
#include <sys/stat.h>
#include <dirent.h>      /* why here?  For osx 10.2, of course! */

#include "daapd.h"
#include "db-generic.h"
#include "err.h"
#include "mp3-scanner.h"
#include "ssc.h"

#ifndef HAVE_STRCASESTR
# include "strcasestr.h"
#endif

/**
 * Check if the file specified by fname should be converted in
 * server to wav.  Currently it does this by file extension, but
 * could in the future decided to transcode based on user agent.
 *
 * @param codectype codec type of the file we are checking for conversion
 * @returns 1 if should be converted.  0 if not
 */
int server_side_convert(char *codectype) {
    if ((!config.ssc_codectypes) ||
        (!config.ssc_codectypes[0]) ||
        (!config.ssc_prog) ||
        (!config.ssc_prog[0]) ||
        (!codectype)) {
        DPRINTF(E_DBG,L_SCAN,"Nope\n");
        return 0;
    }

    if(strcasestr(config.ssc_codectypes, codectype)) {
        return 1;
    }

    return 0;
}


/**
 * Open the source file with convert fiter.
 *
 * @param path char * to the real filename.
 * @param offset off_t to the point in file where the streaming starts.
 */
FILE *server_side_convert_open(char *path, off_t offset, unsigned long len_ms) {
    char *cmd;
    FILE *f;
    char *newpath;
    char *metachars = "\"$`\\"; /* More?? */
    char metacount = 0;
    char *src,*dst;
    
    src=path;
    while(*src) {
        if(strchr(metachars,*src))
            metacount++;
        src++;
    }
    
    if(metachars) {
        newpath = (char*)malloc(strlen(path) + metacount + 1);
        if(!newpath) {
            DPRINTF(E_FATAL,L_SCAN,"Malloc error.\n");
        }
        src=path;
        dst=newpath;
        
        while(*src) {
            if(strchr(metachars,*src))
                *dst++='\\';
             *dst++=*src++;
        }
        *dst='\0';
    } else {
        newpath = strdup(path); /* becuase it will be freed... */
    }

    /* FIXME: is 64 enough? is there a better way to determine this? */
    cmd=(char *)malloc(strlen(config.ssc_prog) +
                       strlen(path) +
                       64);
    sprintf(cmd, "%s \"%s\" %ld %lu.%03lu",
            config.ssc_prog, newpath, (long)offset, len_ms / 1000, len_ms % 1000);
    DPRINTF(E_INF,L_SCAN,"Executing %s\n",cmd);
    f = popen(cmd, "r");
    free(newpath);
    free(cmd);  /* should really have in-place expanded the path */
    return f;
}

/**
 * Open the source file with convert fiter.
 *
 * @param FILE * returned by server_side_convert_open be closed.
 */
void server_side_convert_close(FILE *f)
{
    if (f)
        pclose(f);
    return;
}

