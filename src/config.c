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

#include "err.h"
#include "ll.h"
#include "daapd.h"

/** Globals */
int ecode;
LL_HANDLE config_main=NULL;

#define CONFIG_LINEBUFFER 128

/**
 * read a configfile into a bag
 *
 * @param file file to read
 * @returns TRUE if successful, FALSE otherwise
 */
int config_read(char *file) {
    FILE *fin;
    int err;
    LL_HANDLE pllnew, plltemp;
    char linebuffer[CONFIG_LINEBUFFER+1];
    char *comment, *term, *value, *delim;
    int compat_mode=1;

    fin=fopen(file,"r");
    if(!fin) {
        return CONFIG_E_FOPEN;
    }

    if((err=ll_create(&pllnew)) != LL_E_SUCCESS) {
        DPRINTF(E_LOG,L_CONF,"Error creating linked list: %d\n",err);
        fclose(fin);
        return CONFIG_E_UNKNOWN;
    }

    /* got what will be the root of the config tree, now start walking through
     * the input file, populating the tree
     */
    while(fgets(linebuffer,CONFIG_LINEBUFFER,fin)) {
        linebuffer[CONFIG_LINEBUFFER] = '\0';

        comment=strchr(linebuffer,'#');
        if(comment) {
            /* we should really preserve these in another tree*/
            *comment = '\0';
        }

        while(strlen(linebuffer) && (strchr("\n\r ",linebuffer[strlen(linebuffer)-1])))
            linebuffer[strlen(linebuffer)-1] = '\0';

        if(linebuffer[0] == '[') {
            /* section header */
            compat_mode=0;
            term=&linebuffer[1];
            value = strchr(term,']');
            if(!value) {
                ll_destroy(pllnew);
                fclose(fin);
                return CONFIG_E_BADHEADER;
            }
            *value = '\0';

            if((err = ll_create(&plltemp)) != LL_E_SUCCESS) {
                ll_destroy(pllnew);
                fclose(fin);
                return CONFIG_E_UNKNOWN;
            }

            ll_add_ll(pllnew,term,plltemp);
        } else {
            /* k/v pair */
            term=&linebuffer[0];

            while((*term=='\t') || (*term==' '))
                term++;

            value=term;

            if(compat_mode) {
                delim="\t ";
            } else {
                delim="=";
            }

            strsep(&value,delim);
            if((value) && (term) && (strlen(term))) {
                while(strlen(value) && (strchr("\t ",*value)))
                    value++;

                ll_add_string(pllnew,term,value);
            }
        }
    }

    fclose(fin);

    /*  Sanity check */
    ll_dump(pllnew);
    ll_destroy(pllnew);

    return CONFIG_E_SUCCESS;
}

int config_close(void) {
    if(config_main)
        ll_destroy(config_main);

    return CONFIG_E_SUCCESS;
}

