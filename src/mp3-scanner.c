/*
 * $Id$
 * Implementation file for mp3 scanner and monitor
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

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>

#include "db-memory.h"
#include "mp3-scanner.h"

/*
 * Forwards
 */
int scan_foreground(char *path);

/*
 * scan_init
 *
 * This assumes the database is already initialized.
 *
 * Ideally, this would check to see if the database is empty.
 * If it is, it sets the database into bulk-import mode, and scans
 * the MP3 directory.
 *
 * If not empty, it would start a background monitor thread
 * and update files on a file-by-file basis
 */

int scan_init(char *path) {
    if(db_is_empty()) {
	if(db_start_initial_update()) 
	    return -1;

	scan_foreground(path);

	if(db_end_initial_update())
	    return -1;
    } else {
	/* do deferred updating */
	return ENOTIMPL;
    }

    return 0;
}

/*
 * scan_foreground
 *
 * Do a brute force scan of a path, finding all the MP3 files there
 */
int scan_foreground(char *path) {
    MP3FILE mp3file;
    DIR *current;
    struct dirent de;
    struct dirent *pde;
    int err;
    char mp3_path[PATH_MAX];

    if((current_dir=opendir(path)) == NULL) {
	return -1;
    }

    while(1) {
	err=readdir_r(current_dir,&de,&pde);
	if(err == -1) {
	    err=errno;
	    closedir(current);
	    errno=err;
	    return -1;
	}
	
	if(!pde)
	    break;

	/* process the file */
	if(de.d_namelen > 4) {
	    if(strcasecmp(".mp3",de.d_name[de.d_namelen - 4]) == 0) {
		/* we found an mp3 file */
		sprintf(mp3_path,"%s/%s",path,de.d_name);
		memset(mp3file,0,sizeof(mp3file));
		mp3file->path=mp3_path;
		mp3file->fname=de.d_name;

		db_add(&mp3file);
	    }
	}
    }

    closedir(current);
}

