/*
 * $Id: $
 * simple gdbm database implementation
 *
 * Copyright (C) 2003-2006 Ron Pedde (rpedde@sourceforge.net)
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
#include <gdbm.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "err.h"
#include "mp3-scanner.h"
#include "redblack.h"
#include "db-gdbm.h"

#ifndef GDBM_SYNC
# define GDBM_SYNC 0
#endif

#ifndef GDBM_NOLOCK
# define GDBM_NOLOCK 0
#endif


/* Typedefs */
/* Globals */
static pthread_mutex_t _gdbm_mutex = PTHREAD_MUTEX_INITIALIZER; /**< gdbm not reentrant */
static GDBM_FILE _gdbm_songs;

/* Forwards */
void _gdbm_lock(void);
void _gdbm_unlock(void);

/**
 * lock the db_mutex
 */
void _gdbm_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&_gdbm_mutex))) {
        DPRINTF(E_FATAL,L_DB,"cannot lock gdbm lock: %s\n",strerror(err));
    }
}

/**
 * unlock the db_mutex
 */
void _gdbm_unlock(void) {
    int err;

    if((err=pthread_mutex_unlock(&_gdbm_mutex))) {
        DPRINTF(E_FATAL,L_DB,"cannot unlock gdbm lock: %s\n",strerror(err));
    }
}


/**
 * open the gdbm database
 *
 * @param pe error buffer
 * @param parameters db-specific parameter.  In this case,
 *                   the path to use
 * @returns DB_E_SUCCESS on success, DB_E_* otherwise.
 */
int db_gdbm_open(char **pe, char *parameters) {
    char db_path[PATH_MAX + 1];

    snprintf(db_path,sizeof(db_path),"%s/%s",parameters,"songs.gdb");

    //    reload = reload ? GDBM_NEWDB : GDBM_WRCREAT;
    _gdbm_lock();
    _gdbm_songs = gdbm_open(db_path, 0, GDBM_WRCREAT | GDBM_SYNC | GDBM_NOLOCK,
                           0600,NULL);

    if(!_gdbm_songs) {
        /* let's try creating it! */
        _gdbm_songs = gdbm_open(db_path,0,GDBM_NEWDB | GDBM_SYNC | GDBM_NOLOCK,
                                0600,NULL);
    }

    _gdbm_unlock();
    if(!_gdbm_songs) {
        DPRINTF(E_FATAL,L_DB,"Could not open songs database (%s): %s\n",
                db_path,strerror(errno));
        return FALSE;
    }

    return TRUE;


}

/**
 * Don't really have a db initialization separate from opening.
 */
int db_gdbm_init(int reload) {
    return TRUE;
}

/**
 * Close the database
 */
int db_gdbm_deinit(void) {
    _gdbm_lock();
    gdbm_close(_gdbm_songs);
    _gdbm_unlock();
    return TRUE;
}

