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

#ifndef WIN32
#include <netinet/in.h>  /* htons and friends */
#include <dirent.h>      /* why here?  For osx 10.2, of course! */
#endif

#include <sys/stat.h>

#include "conf.h"
#include "daapd.h"
#include "db-generic.h"
#include "err.h"
#include "mp3-scanner.h"
#include "ssc.h"

/**
 * Check if the file specified by fname should be converted in
 * server to wav.  Currently it does this by file extension, but
 * could in the future decided to transcode based on user agent.
 *
 * @param codectype codec type of the file we are checking for conversion
 * @returns 1 if should be converted.  0 if not
 */
int server_side_convert(char *codectype) {
    char *ssc_codectypes;

    ssc_codectypes = conf_alloc_string("general","ssc_codectypes",
                                       "ogg,flac,wma,alac");

    if ((!conf_isset("general","ssc_codectypes")) ||
        (!conf_isset("general","ssc_prog")) ||
        (!codectype)) {
        DPRINTF(E_DBG,L_SCAN,"Nope\n");
        free(ssc_codectypes);
        return 0;
    }

    if(strcasestr(ssc_codectypes, codectype)) {
        free(ssc_codectypes);
        return 1;
    }

    free(ssc_codectypes);
    return 0;
}


/**
 * Open the source file with convert fiter.
 *
 * @param path char * to the real filename.
 * @param offset off_t to the point in file where the streaming starts.
 */
FILE *server_side_convert_open(char *path, off_t offset, unsigned long len_ms, char *codectype) {
    char *cmd;
    FILE *f;
    char *newpath;
    char *metachars = "\"\\!(){}#*?$&<>`"; /* More?? */
    char metacount = 0;
    char *src,*dst,*ssc_prog;

    ssc_prog = conf_alloc_string("general","ssc_prog","");

    if(ssc_prog[0] == '\0') { /* can't happen */
        free(ssc_prog);
        return NULL;
    }

    src=path;
    while(*src) {
        if(strchr(metachars,*src))
            metacount+=5;
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
            if(strchr(metachars,*src)) {
                *dst++='"';
                *dst++='\'';
                *dst++=*src++;
                *dst++='\'';
                *dst++='"';
            } else {
                *dst++=*src++;
            }
        }
        *dst='\0';
    } else {
        newpath = strdup(path); /* becuase it will be freed... */
    }

    /* FIXME: is 64 enough? is there a better way to determine this? */
    cmd=(char *)malloc(strlen(ssc_prog) +
                       strlen(path) +
                       64);
    sprintf(cmd, "%s \"%s\" %ld %lu.%03lu \"%s\"",
            ssc_prog, newpath, (long)offset, len_ms / 1000,
            len_ms % 1000, (codectype && *codectype) ? codectype : "*");
    DPRINTF(E_INF,L_SCAN,"Executing %s\n",cmd);
    f = popen(cmd, "r");
    free(newpath);
    free(cmd);  /* should really have in-place expanded the path */
    free(ssc_prog);
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

