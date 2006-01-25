/*
 * $Id$
 * Functions for reading and writing the config file
 *
 * Copyright (C) 2006 Ron Pedde (ron@pedde.com)
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


/**
 * \file config.c
 *
 * Config file reading and writing
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>


#include "bag.h"
#include "daap.h"

/** Globals */
int ecode;
BAG_HANDLE config_main;

/**
 * read a configfile into a bag
 *
 * @param file file to read
 * @returns TRUE if successful, FALSE otherwise
 */
int config_read(char *file) {
    FILE *fin;
    int err;

    fin=fopen(file,"r");
    if(!fin) {
        ecode = errno;
        return CONFIG_E_FOPEN;
    }

    if((err=bag_create(&config_main)) != BAG_E_SUCCESS) {
        DPRINTF(E_LOG,L_CONF,"Error creating bag: %d\n",err);
        return CONFIG_E_UNKNOWN;
    }



    fclose(fin);
    return CONFIG_E_SUCCESS;
}

int config_close(void) {

}

