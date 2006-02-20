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
#include <string.h>

#include "conf.h"
#include "err.h"
#include "ll.h"
#include "daapd.h"

/** Globals */
static int ecode;
static LL_HANDLE config_main=NULL;

#define CONFIG_LINEBUFFER 128

#define CONF_T_INT          0
#define CONF_T_STRING       1

/** Forwards */
static int _conf_verify(LL_HANDLE pll);
static LL_ITEM *_conf_fetch_item(LL_HANDLE pll, char *section, char *term);
static int _conf_exists(LL_HANDLE pll, char *section, char *term);



typedef struct _CONF_ELEMENTS {
    int required;
    int deprecated;
    int type;
    char *section;
    char *term;
} CONF_ELEMENTS;

static CONF_ELEMENTS conf_elements[] = {
    { 1, 0, CONF_T_STRING,"general","runas" },
    { 1, 0, CONF_T_STRING,"general","web_root" },
    { 1, 0, CONF_T_INT,"general","port" },
    { 1, 0, CONF_T_STRING,"general","admin_pw" },
    { 1, 0, CONF_T_STRING,"general","mp3_dir" },
    { 0, 1, CONF_T_STRING,"general","db_dir" },
    { 0, 0, CONF_T_STRING,"general","db_type" },
    { 0, 0, CONF_T_STRING,"general","db_parms" },
    { 0, 0, CONF_T_INT,"general","debuglevel" },
    { 1, 0, CONF_T_STRING,"general","servername" },
    { 0, 0, CONF_T_INT,"general","rescan_interval" },
    { 0, 0, CONF_T_INT,"general","always_scan" },
    { 0, 1, CONF_T_INT,"general","latin1_tags" },
    { 0, 0, CONF_T_INT,"general","process_m3u" },
    { 0, 0, CONF_T_INT,"general","scan_type" },
    { 0, 1, CONF_T_INT,"general","compress" },
    { 0, 0, CONF_T_STRING,"general","playlist" },
    { 0, 0, CONF_T_STRING,"general","extensions" },
    { 0, 0, CONF_T_STRING,"general","interface" },
    { 0, 0, CONF_T_STRING,"general","ssc_codectypes" },
    { 0, 0, CONF_T_STRING,"general","ssc_prog" },
    { 0, 0, CONF_T_STRING,"general","password" },
    { 0, 0, CONF_T_STRING,"general","compdirs" },
    { 0, 0, CONF_T_STRING,"general","logfile" },
    { 0, 0, CONF_T_INT, NULL, NULL }
};

/**
 * fetch item based on section/term basis, rather than just a single
 * level deep, like ll_fetch_item does
 * 
 * @param pll top level linked list to test (config tree)
 * @param section section to term (key) is in
 * @param term term/key to look for
 * @returns LL_ITEM of the key, or NULL
 */
LL_ITEM *_conf_fetch_item(LL_HANDLE pll, char *section, char *term) {
    LL_ITEM *psection;
    LL_ITEM *pitem;

    if(!(psection = ll_fetch_item(pll,section)))
	return NULL;

    if(psection->type != LL_TYPE_LL)
	return NULL;

    if(!(pitem = ll_fetch_item(psection->value.as_ll,term)))
	return NULL;

    return pitem;
}

/**
 * simple test to see if a particular section/key value exists
 *
 * @param pll config tree to test
 * @param section section to find the term under
 * @param term key to search for under the specified section
 * @returns TRUE if key exists, FALSE otherwise
 */
int _conf_exists(LL_HANDLE pll, char *section, char *term) {
    if(!_conf_fetch_item(pll,section,term))
	return FALSE;

    return TRUE;
}


/**
 * Verify that the configuration isn't obviously wrong.
 * Type checking has already been done, this just checks
 * required stuff isn't missing.
 *
 * @param pll tree to check
 * @returns TRUE if configuration appears valid, FALSE otherwise
 */
int _conf_verify(LL_HANDLE pll) {
    LL_ITEM *pi = NULL;
    CONF_ELEMENTS *pce;
    int is_valid=TRUE;
    
    /* first, walk through the elements and make sure
     * all required elements are there */
    pce = &conf_elements[0];
    while(pce->section) {
        if(pce->required) {
            if(!_conf_exists(pll,pce->section, pce->term))
                DPRINTF(E_LOG,L_CONF,"Missing configuration entry "
                    " %s/%s.  Please review the sample config\n",
                    pce->section, pce->term);
            is_valid=FALSE;
        }
        if(pce->deprecated) {
            DPRINTF(E_LOG,L_CONF,"Config entry %s/%s is deprecated.  Please "
                "review the sample config\n",
                pce->section, pce->term);
        }
        pce++;
    }
    
    /* here we would walk through derived sections, if there
     * were any */

    return is_valid;
}


/**
 * read a configfile into a tree
 *
 * @param file file to read
 * @returns TRUE if successful, FALSE otherwise
 */
int config_read(char *file) {
    FILE *fin;
    int err;
    LL_HANDLE pllnew, plltemp, pllcurrent;
    char linebuffer[CONFIG_LINEBUFFER+1];
    char *comment, *term, *value, *delim;
    int compat_mode=1;
    int line=0;

    fin=fopen(file,"r");
    if(!fin) {
        return CONF_E_FOPEN;
    }

    if((err=ll_create(&pllnew)) != LL_E_SUCCESS) {
        DPRINTF(E_LOG,L_CONF,"Error creating linked list: %d\n",err);
        fclose(fin);
        return CONF_E_UNKNOWN;
    }

    pllcurrent=NULL;

    /* got what will be the root of the config tree, now start walking through
     * the input file, populating the tree
     */
    while(fgets(linebuffer,CONFIG_LINEBUFFER,fin)) {
        line++;
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
                return CONF_E_BADHEADER;
            }
            *value = '\0';

            if((err = ll_create(&plltemp)) != LL_E_SUCCESS) {
                ll_destroy(pllnew);
                fclose(fin);
                return CONF_E_UNKNOWN;
            }

            ll_add_ll(pllnew,term,plltemp);
            pllcurrent = plltemp;
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

                if(!pllcurrent) {
                    /* we are definately in compat mode -- add a general section */
                    if((err=ll_create(&plltemp)) != LL_E_SUCCESS) {
                        DPRINTF(E_LOG,L_CONF,"Error creating linked list: %d\n",err);
                        ll_destroy(pllnew);
                        fclose(fin);
                        return CONF_E_UNKNOWN;
                    }
                    ll_add_ll(pllnew,"general",plltemp);
                    pllcurrent = plltemp;
                }
                ll_add_string(pllcurrent,term,value);
            }
            if(((term) && (strlen(term))) && (!value)) {
                DPRINTF(E_LOG,L_CONF,"Error in config file on line %d\n",line);
                ll_destroy(pllnew);
                return CONF_E_PARSE;
            }
        }
    }

    fclose(fin);

    /*  Sanity check */
    _conf_verify(pllnew);

    ll_dump(pllnew);
    ll_destroy(pllnew);

    return CONF_E_SUCCESS;
}

/**
 * do final config file shutdown
 */
int config_close(void) {
    if(config_main)
        ll_destroy(config_main);

    return CONF_E_SUCCESS;
}


/**
 * read a value from the CURRENT config tree as an integer
 *
 * @param section section name to search in
 * @param key key to search for
 * @param default default value to return if key not found
 * @returns value as integer if found, default value otherwise
 */
int config_get_int(char *section, char *key, int default) {
    LL_ITEM *pitem;


}
