/*
 * $idx: conf.c,v 1.4 2006/02/21 03:08:14 rpedde Exp $
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

#ifdef HAVE_conf_H
#  include "config.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "conf.h"
#include "err.h"
#include "ll.h"
#include "daapd.h"

/** Globals */
static int ecode;
static LL_HANDLE conf_main=NULL;
static char *conf_main_file = NULL;
static pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;

#define conf_LINEBUFFER 128

#define CONF_T_INT          0
#define CONF_T_STRING       1

/** Forwards */
static int _conf_verify(LL_HANDLE pll);
static LL_ITEM *_conf_fetch_item(LL_HANDLE pll, char *section, char *term);
static int _conf_exists(LL_HANDLE pll, char *section, char *term);
static void _conf_lock(void);
static void _conf_unlock(void);
static int _conf_write(FILE *fp, LL *pll, int sublevel);

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
 * lock the conf mutex
 */
void _conf_lock() {
    int err;

    if((err=pthread_mutex_lock(&conf_mutex))) {
        DPRINTF(E_FATAL,L_CONF,"Cannot lock configuration mutex: %s\n",
            strerror(err));
    }
}

/**
 * unlock the conf mutex
 */
void _conf_unlock() {
    int err;

    if((err = pthread_mutex_unlock(&conf_mutex))) {
        DPRINTF(E_FATAL,L_CONF,"Cannot unlock configuration mutex %s\n",
            strerror(err));
    }
}

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
            if(!_conf_exists(pll,pce->section, pce->term)) {
                DPRINTF(E_LOG,L_CONF,"Missing configuration entry "
                    " %s/%s.  Please review the sample config\n",
                    pce->section, pce->term);
                is_valid=FALSE;
            }
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
int conf_read(char *file) {
    FILE *fin;
    int err;
    LL_HANDLE pllnew, plltemp, pllcurrent;
    char linebuffer[conf_LINEBUFFER+1];
    char *comment, *term, *value, *delim;
    int compat_mode=1;
    int line=0;

    if(conf_main_file) {
        conf_close();
    }

    conf_main_file = strdup(file);

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
    while(fgets(linebuffer,conf_LINEBUFFER,fin)) {
        line++;
        linebuffer[conf_LINEBUFFER] = '\0';

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
    if(_conf_verify(pllnew)) {
        DPRINTF(E_INF,L_CONF,"Loading new config file.\n");
        _conf_lock();
        if(conf_main) {
            ll_destroy(conf_main);
        }
        conf_main = pllnew;
        _conf_unlock();
    } else {
        ll_destroy(pllnew);
        DPRINTF(E_LOG,L_CONF,"Could not validate config file.  Ignoring\n");
    }

    return CONF_E_SUCCESS;
}

/**
 * do final config file shutdown
 */
int conf_close(void) {
    if(conf_main) {
        ll_destroy(conf_main);
        conf_main = NULL;
    }

    if(conf_main_file) {
        free(conf_main_file);
        conf_main_file = NULL;
    }

    return CONF_E_SUCCESS;
}


/**
 * read a value from the CURRENT config tree as an integer
 *
 * @param section section name to search in
 * @param key key to search for
 * @param dflt default value to return if key not found
 * @returns value as integer if found, dflt value otherwise
 */
int conf_get_int(char *section, char *key, int dflt) {
    LL_ITEM *pitem;
    int retval;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if((!pitem) || (pitem->type != LL_TYPE_STRING)) {
        retval = dflt;
    } else {
        retval = atoi(pitem->value.as_string);
    }
    _conf_unlock();

    return retval;
}

/**
 * read a value from the CURRENT config tree as a string
 *
 * @param section section name to search in
 * @param key key to search for
 * @param dflt default value to return if key not found
 * @param out buffer to put resulting string in
 * @param size pointer to size of buffer
 * @returns CONF_E_SUCCESS with out filled on success,
 *          or CONF_E_OVERFLOW, with size set to required buffer size
 */
int conf_get_string(char *section, char *key, char *dflt, char *out, int *size) {
    LL_ITEM *pitem;
    char *result;
    int len;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if((!pitem) || (pitem->type != LL_TYPE_STRING)) {
        result = dflt;
    } else {
        result = pitem->value.as_string;
    }

    len = strlen(result) + 1;

    if(len <= *size) {
        *size = len;
        strcpy(out,result);
    } else {
        _conf_unlock();
        *size = len;
        return CONF_E_OVERFLOW;
    }

    _conf_unlock();
    return CONF_E_SUCCESS;
}

/**
 * set (update) the config tree with a particular value.
 * this accepts an int, but it actually adds it as a string.
 * in that sense, it's really just a wrapper for conf_set_string
 *
 * @param section section that the key is in
 * @param key key to update
 * @param value value to set it to
 * @returns E_CONF_SUCCESS on success, error code otherwise
 */
int conf_set_int(char *section, char *key, int value) {
    char buffer[40]; /* ?? */
    snprintf(buffer,sizeof(buffer),"%d",value);

    return conf_set_string(section, key, buffer);
}

/**
 * set (update) the config tree with a particular string value
 *
 * @param section section that the key is in
 * @param key key to update
 * @param value value to set it to
 * @returns E_CONF_SUCCESS on success, error code otherwise
 */
int conf_set_string(char *section, char *key, char *value) {
    LL_ITEM *pitem;
    LL_ITEM *psection;
    LL *section_ll;
    int err;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if(!pitem) {
        /* fetch the section and add it to that list */
        if(!(psection = ll_fetch_item(conf_main,section))) {
            /* that subkey doesn't exist yet... */
            if((err = ll_create(&section_ll)) != LL_E_SUCCESS) {
                DPRINTF(E_LOG,L_CONF,"Could not create linked list: %d\n",err);
                _conf_unlock();
                return CONF_E_UNKNOWN;
            }
            if((err=ll_add_ll(conf_main,section,section_ll)) != LL_E_SUCCESS) {
                DPRINTF(E_LOG,L_CONF,"Error inserting new subkey: %d\n",err);
                _conf_unlock();
                return CONF_E_UNKNOWN;
            }
        } else {
            section_ll = psection->value.as_ll;
        }
        /* have the section, now add it */
        if((err = ll_add_string(section_ll,key,value)) != LL_E_SUCCESS) {
            DPRINTF(E_LOG,L_CONF,"Error in conf_set_string: "
                    "(%s/%s)\n",section,key);
            _conf_unlock();
            return CONF_E_UNKNOWN;
        }
    } else {
        /* we have the item, let's update it */
        ll_update_string(pitem,value);
    }

    _conf_unlock();
    return CONF_E_SUCCESS;
}

/**
 * determine if the configuration file is writable
 *
 * @returns TRUE if writable, FALSE otherwise
 */
int conf_iswritable(void) {
    FILE *fp;
    int retval = FALSE;

    /* don't want configfile reopened under us */
    _conf_lock();

    if(!conf_main_file)
        return FALSE;

    if((fp = fopen(conf_main_file,"r+")) != NULL) {
        fclose(fp);
        retval = TRUE;
    }

    _conf_unlock();
    return retval;
}

/**
 * write the current config tree back to the config file
 *
 */
int conf_write(void) {
    int retval = FALSE;
    FILE *fp;

    if(!conf_main_file) {
        return CONF_E_NOCONF;
    }

    _conf_lock();
    if((fp = fopen(conf_main_file,"w+")) != NULL) {
        retval = _conf_write(fp,conf_main,0);
        fclose(fp);
    }
    _conf_unlock();

    return retval;
}

/**
 * do the actual work of writing the config file
 *
 * @param fp file we are writing the config file to
 * @param pll list we are dumping k/v pairs for
 * @param sublevel whether this is the root, or a subkey
 * @returns TRUE on success, FALSE otherwise
 */
int _conf_write(FILE *fp, LL *pll, int sublevel) {
    LL_ITEM *pli;
    int retval;

    if(!pll)
        return TRUE;

    /* write all the solo keys, first! */
    pli = pll->itemlist.next;
    while(pli) {
        switch(pli->type) {
        case LL_TYPE_LL:
            if(sublevel) {
                /* something wrong! */
                DPRINTF(E_LOG,L_CONF,"LL in sublevel: %s\n",pli->key);
            } else {
                fprintf(fp,"[%s]\n",pli->key);
                if(!_conf_write(fp, pli->value.as_ll, 1))
                   return FALSE;
            }
            break;
        case LL_TYPE_INT:
            fprintf(fp,"%s=%d\n",pli->key,pli->value.as_int);
            break;

        case LL_TYPE_STRING:
            fprintf(fp,"%s=%s\n",pli->key,pli->value.as_string);
            break;
        }

        pli = pli->next;
    }

    return TRUE;
}


/**
 * determine if a configuration entry is actually set
 *
 * @param section section to test
 * @key key to check
 * @return TRUE if set, FALSE otherwise
 */
int conf_isset(char *section, char *key) {
    int retval = FALSE;

    _conf_lock();
    if(_conf_fetch_item(conf_main,section,key)) {
        retval = TRUE;
    }
    _conf_unlock();

    return retval;
}


