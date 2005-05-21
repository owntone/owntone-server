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
 * If path is server side convertable, return the path to real file.
 *
 * @param path char * to the path in the database.
 */
char *server_side_convert_path(char *path)
{
    char *r = NULL;

    if (path &&
	(strlen(path) > strlen(SERVER_SIDE_CONVERT_SUFFIX)) &&
	(strcmp(path + strlen(path) - strlen(SERVER_SIDE_CONVERT_SUFFIX),
		SERVER_SIDE_CONVERT_SUFFIX) == 0)) {
	/* Lose the artificial suffix.  Could use strndup here.*/
	r = strdup(path);
	r[strlen(path) - strlen(SERVER_SIDE_CONVERT_SUFFIX)] = '\0';
    }

    return r;
}


/**
 * Check if the file specified by fname should be converted in
 * server to wav.  Currently it does this by file extension, but
 * could in the future decided to transcode based on user agent.
 *
 * @param fname file name of file to check for conversion
 * @returns 1 if should be converted.  0 if not
 */
int server_side_convert(char *fname) {
    char *ext;

    DPRINTF(E_DBG,L_SCAN,"Checking for ssc: %s\n",fname);

    if ((!config.ssc_extensions) ||
	(!config.ssc_extensions[0]) ||
	(!config.ssc_prog) ||
	(!config.ssc_prog[0]) ||
	(!fname)) {
	DPRINTF(E_DBG,L_SCAN,"Nope\n");
	return 0;
    }

    if(((ext = strrchr(fname, '.')) != NULL) && 
       (strcasestr(config.ssc_extensions, ext))) {
	DPRINTF(E_DBG,L_SCAN,"Yup\n");
	return 1;
    }

    DPRINTF(E_DBG,L_SCAN,"Nope\n");
    return 0;
}

/**
 * Check if the file entry (otherwise complete) is such that
 * file should be converted in server end to wav-format.
 * If so, the info is modified accordingly and non-zero return
 * value is returned.
 *
 * @param song MP3FILE of the file to possibly set to server side conversion
 */
int server_side_convert_set(MP3FILE *pmp3)
{
    char *fname, *path, *description, *ext;
    DPRINTF(E_SPAM,L_SCAN,"Checking for ssc: %s\n",pmp3->fname);
    if ((!config.ssc_extensions) ||
	(!config.ssc_extensions[0]) ||
	(!config.ssc_prog) ||
	(!config.ssc_prog[0]) ||
	(!pmp3->fname) ||
	(!pmp3->path) ||
	(!pmp3->type) ||
	((strlen(pmp3->fname) > strlen(SERVER_SIDE_CONVERT_SUFFIX)) &&
	 (strcmp(pmp3->fname +
		 strlen(pmp3->fname) -
		 strlen(SERVER_SIDE_CONVERT_SUFFIX),
		 SERVER_SIDE_CONVERT_SUFFIX) == 0))) {
	DPRINTF(E_SPAM,L_SCAN,"Nope\n");
	return 0;
    }

    DPRINTF(E_SPAM,L_SCAN,"Yup\n");
    if (((ext = strrchr(pmp3->path, '.')) != NULL) &&
	(strcasestr(config.ssc_extensions, ext))) {
	fname = (char *)malloc(strlen(pmp3->fname) +
			       strlen(SERVER_SIDE_CONVERT_SUFFIX) + 1);
	path = (char *)malloc(strlen(pmp3->path) +
			      strlen(SERVER_SIDE_CONVERT_SUFFIX) + 1);
	description = (char *)malloc(strlen(pmp3->description) +
				     strlen(SERVER_SIDE_CONVERT_DESCR) + 1);
	strcpy(fname, pmp3->fname);
	strcat(fname, SERVER_SIDE_CONVERT_SUFFIX);
	free(pmp3->fname);
	pmp3->fname = fname;
	strcpy(path, pmp3->path);
	strcat(path, SERVER_SIDE_CONVERT_SUFFIX);
	free(pmp3->path);
	pmp3->path = path;
	strcpy(description, pmp3->description);
	strcat(description, SERVER_SIDE_CONVERT_DESCR);
	free(pmp3->description);
	pmp3->description = description;
	free(pmp3->type);
	pmp3->type = strdup("wav");
	if (pmp3->samplerate > 0) {
	    // Here we guess that it's always 16 bit stereo samples,
	    // which is accurate enough for now.
	    pmp3->bitrate = (pmp3->samplerate * 4 * 8) / 1000;
	}
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
FILE *server_side_convert_open(char *path, off_t offset, unsigned long len_ms)
{
    char *cmd;
    FILE *f;

    cmd=(char *)malloc(strlen(config.ssc_prog) +
		       strlen(path) +
		       64);
    sprintf(cmd, "%s \"%s\" %ld %lu.%03lu",
	    config.ssc_prog, path, (long)offset, len_ms / 1000, len_ms % 1000);
    f = popen(cmd, "r");
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

