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

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>

#include "conf.h"
#include "err.h"
#include "ll.h"
#include "daapd.h"
#include "os.h"
#include "webserver.h"
#include "xml-rpc.h"

/** Globals */
//static int ecode;
static LL_HANDLE conf_main=NULL;
static LL_HANDLE conf_comments=NULL;

static char *conf_main_file = NULL;
static pthread_mutex_t conf_mutex = PTHREAD_MUTEX_INITIALIZER;

#define CONF_LINEBUFFER 128

#define CONF_T_INT          0
#define CONF_T_STRING       1
#define CONF_T_EXISTPATH    2  /** a path that must exist */
#define CONF_T_MULTICOMMA   3  /** multiple entries separated by commas */

typedef struct _CONF_ELEMENTS {
    int required;
    int deprecated;
    int type;
    char *section;
    char *term;
} CONF_ELEMENTS;

/** Forwards */
static int _conf_verify(LL_HANDLE pll);
static LL_ITEM *_conf_fetch_item(LL_HANDLE pll, char *section, char *key);
static int _conf_exists(LL_HANDLE pll, char *section, char *key);
static void _conf_lock(void);
static void _conf_unlock(void);
static int _conf_write(FILE *fp, LL *pll, int sublevel, char *parent);
static CONF_ELEMENTS *_conf_get_keyinfo(char *section, char *key);
static int _conf_makedir(char *path, char *user);
static int _conf_existdir(char *path);
static int _conf_split(char *s, char *delimiters, char ***argvp);
static void _conf_dispose_split(char **argv);
static int _conf_xml_dump(XMLSTRUCT *pxml,LL *pll,int sublevel,char *parent);

static CONF_ELEMENTS conf_elements[] = {
    { 1, 0, CONF_T_STRING,"general","runas" },
    { 1, 0, CONF_T_EXISTPATH,"general","web_root" },
    { 1, 0, CONF_T_INT,"general","port" },
    { 1, 0, CONF_T_STRING,"general","admin_pw" },
    { 1, 0, CONF_T_MULTICOMMA,"general","mp3_dir" },
    { 0, 1, CONF_T_EXISTPATH,"general","db_dir" },
    { 0, 0, CONF_T_STRING,"general","db_type" },
    { 0, 0, CONF_T_EXISTPATH,"general","db_parms" }, /* this isn't right */
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
    { 0, 0, CONF_T_MULTICOMMA,"general","compdirs" },
    { 0, 0, CONF_T_STRING,"general","logfile" },
    { 0, 0, CONF_T_EXISTPATH,"plugins","plugin_dir" },
    { 0, 0, CONF_T_MULTICOMMA,"plugins","plugins" },
    { 0, 0, CONF_T_INT, NULL, NULL }
};


/**
 * Try and create a directory, including parents (probably
 * in response to a config file entry that does not exist).
 *
 * @param path path to make
 * @returns TRUE on success, FALSE otherwise
 */

int _conf_makedir(char *path,char *user) {
    char *token, *next_token;
    char *pathdup;
    char path_buffer[PATH_MAX];
    int retval = FALSE;

    DPRINTF(E_DBG,L_CONF,"Creating %s\n",path);

    pathdup=strdup(path);
    if(!pathdup) {
        DPRINTF(E_FATAL,L_CONF,"Malloc error\n");
    }

    next_token=pathdup;
    while(*next_token && (*next_token != PATHSEP))
        next_token++;
    if(*next_token)
        next_token++;

    memset(path_buffer,0,sizeof(path_buffer));

    while((token=strsep(&next_token,PATHSEP_STR))) {
        if((strlen(path_buffer) + strlen(token)) < PATH_MAX) {
            strcat(path_buffer,PATHSEP_STR);
            strcat(path_buffer,token);

            if(!_conf_existdir(path_buffer)) {
                DPRINTF(E_DBG,L_CONF,"Making %s\n",path_buffer);
                if((mkdir(path_buffer,0700)) && (errno != EEXIST)) {
                    free(pathdup);
                    DPRINTF(E_LOG,L_CONF,"Could not make dirctory %s: %s\n",
                            path_buffer,strerror(errno));
                    return FALSE;
                }
                os_chown(path_buffer,user);
            }
            retval = TRUE;
        }
    }

    free(pathdup);
    return retval;
}

/**
 * Determine if a particular directory exists or not
 *
 * @param path directory to test for existence
 * @returns true if path exists, false otherwise
 */
int _conf_existdir(char *path) {
    struct stat sb;

    DPRINTF(E_DBG,L_CONF,"Checking existence of %s\n",path);

    if(stat(path,&sb)) {
        return FALSE;
    }

    if(sb.st_mode & S_IFDIR)
        return TRUE;

    return FALSE;
}

/**
 * given a section and key, get the conf_element for it.
 * right now this is simple, but eventually there might
 * be more difficult mateches to be made
 *
 * @param section section the key was found ind
 * @param key key we are searching for info on
 * @returns the CONF_ELEMENT that is the closest match, or
 *          NULL if no match was found.
 */
CONF_ELEMENTS *_conf_get_keyinfo(char *section, char *key) {
    CONF_ELEMENTS *pcurrent;
    int found=0;

    pcurrent = &conf_elements[0];
    while(pcurrent->section && pcurrent->term) {
        if((strcasecmp(section,pcurrent->section) != 0) ||
           (strcasecmp(key,pcurrent->term) != 0)) {
            pcurrent++;
        } else {
            found = 1;
            break;
        }
    }

    return found ? pcurrent : NULL;
}

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
 * @param key key to look for
 * @returns LL_ITEM of the key, or NULL
 */
LL_ITEM *_conf_fetch_item(LL_HANDLE pll, char *section, char *key) {
    LL_ITEM *psection;
    LL_ITEM *pitem;

    if(!(psection = ll_fetch_item(pll,section)))
        return NULL;

    if(psection->type != LL_TYPE_LL)
        return NULL;

    if(!(pitem = ll_fetch_item(psection->value.as_ll,key)))
        return NULL;

    return pitem;
}

/**
 * simple test to see if a particular section/key value exists
 *
 * @param pll config tree to test
 * @param section section to find the term under
 * @param key key to search for under the specified section
 * @returns TRUE if key exists, FALSE otherwise
 */
int _conf_exists(LL_HANDLE pll, char *section, char *key) {
    if(!_conf_fetch_item(pll,section,key))
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
    LL_ITEM *ptemp = NULL;
    CONF_ELEMENTS *pce;
    int is_valid=TRUE;
    char resolved_path[PATH_MAX];
    char *user;

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
            if(_conf_exists(pll,pce->section,pce->term)) {
                DPRINTF(E_LOG,L_CONF,"Config entry %s/%s is deprecated.  Please "
                        "review the sample config\n",
                        pce->section, pce->term);
            }
        }
        if(pce->type == CONF_T_EXISTPATH) {
            /* first, need to resolve */
            pi = _conf_fetch_item(pll,pce->section, pce->term);
            if(pi) {
                memset(resolved_path,0,sizeof(resolved_path));
                if(pi->value.as_string) {
                    DPRINTF(E_SPAM,L_CONF,"Found %s/%s as %s... checking\n",
                            pce->section, pce->term, pi->value.as_string);

                    /* verify it exists, creating it if necessary */
                    if(!_conf_existdir(pi->value.as_string)) {
                        user = "nobody";
                        ptemp = _conf_fetch_item(pll, "general", "runas");
                        if(ptemp) {
                            user = ptemp->value.as_string;
                        }

                        if(!_conf_makedir(pi->value.as_string,user)) {
                            is_valid=0;
                            DPRINTF(E_LOG,L_CONF,"Can't make path %s, invalid config.\n",
                                    resolved_path);
                        }
                    }

                    if(_conf_existdir(pi->value.as_string)) {
                        realpath(pi->value.as_string,resolved_path);
                        free(pi->value.as_string);
                        pi->value.as_string = strdup(resolved_path);

                        DPRINTF(E_SPAM,L_CONF,"Resolved to %s\n",resolved_path);
                    }
                }
            }
        }
        pce++;
    }

    /* here we would walk through derived sections, if there
     * were any */

    return is_valid;
}

/**
 * reload the existing config file.
 * 
 * @returns CONF_E_SUCCESS on success
 */
int conf_reload(void) {
    return conf_read(conf_main_file);
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
    LL_HANDLE pllnew, plltemp, pllcurrent, pllcomment;
    LL_ITEM *pli;
    char linebuffer[CONF_LINEBUFFER+1];
    char keybuffer[256];
    char *comment, *term, *value, *delim;
    char *section_name=NULL;
    char *prev_comments=NULL;
    int total_comment_length=CONF_LINEBUFFER;
    int current_comment_length=0;
    int compat_mode=1;
    int warned_truncate=0;
    int line=0;
    int ws=0;
    CONF_ELEMENTS *pce;
    int key_type;
    char **valuearray;
    int index;

    if(conf_main_file) {
        conf_close();
    }

    conf_main_file = strdup(file);

    fin=fopen(file,"r");
    if(!fin) {
        return CONF_E_FOPEN;
    }

    prev_comments = (char*)malloc(total_comment_length);
    if(!prev_comments)
        DPRINTF(E_FATAL,L_CONF,"Malloc error\n");
    prev_comments[0] = '\0';

    if((err=ll_create(&pllnew)) != LL_E_SUCCESS) {
        DPRINTF(E_LOG,L_CONF,"Error creating linked list: %d\n",err);
        fclose(fin);
        return CONF_E_UNKNOWN;
    }

    ll_create(&pllcomment);  /* don't care if we lose comments */

    comment = NULL;
    pllcurrent=NULL;

    /* got what will be the root of the config tree, now start walking through
     * the input file, populating the tree
     */
    while(fgets(linebuffer,CONF_LINEBUFFER,fin)) {
        line++;
        linebuffer[CONF_LINEBUFFER] = '\0';
        ws=0;

        comment=strchr(linebuffer,'#');
        if(comment) {
            *comment = '\0';
            comment++;
        }

        while(strlen(linebuffer) &&
              (strchr("\n\r ",linebuffer[strlen(linebuffer)-1])))
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

            /* set current section and name */
            pllcurrent = plltemp;
            if(section_name)
                free(section_name);
            section_name = strdup(term);

            /* set precomments */
            if(prev_comments[0] != '\0') {
                /* we had some preceding comments */
                snprintf(keybuffer,sizeof(keybuffer),"pre_%s",section_name);
                ll_add_string(pllcomment,keybuffer,prev_comments);
                prev_comments[0] = '\0';
                current_comment_length=0;
            }
            if(comment) {
                /* we had some preceding comments */
                snprintf(keybuffer,sizeof(keybuffer),"in_%s",section_name);
                ll_add_string(pllcomment,keybuffer,comment);
                prev_comments[0] = '\0';
                current_comment_length=0;
                comment = NULL;
            }
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
                while((strlen(term) && (strchr("\t ",term[strlen(term)-1]))))
                      term[strlen(term)-1] = '\0';
                while(strlen(value) && (strchr("\t ",*value)))
                    value++;
                while((strlen(value) && (strchr("\t ",value[strlen(value)-1]))))
                      value[strlen(value)-1] = '\0';

                if(!pllcurrent) {
                    /* in compat mode -- add a general section */
                    if((err=ll_create(&plltemp)) != LL_E_SUCCESS) {
                        DPRINTF(E_LOG,L_CONF,"Error creating list: %d\n",err);
                        ll_destroy(pllnew);
                        fclose(fin);
                        return CONF_E_UNKNOWN;
                    }
                    ll_add_ll(pllnew,"general",plltemp);
                    pllcurrent = plltemp;

                    if(section_name)
                        free(section_name); /* shouldn't ahppen */
                    section_name = strdup("general");

                    /* no inline comments, just precomments */
                    if(prev_comments[0] != '\0') {
                        /* we had some preceding comments */
                        ll_add_string(pllcomment,"pre_general",prev_comments);
                        prev_comments[0] = '\0';
                        current_comment_length=0;
                    }

                }

                /* see what kind this is, and act accordingly */
                pce = _conf_get_keyinfo(section_name,term);
                key_type = CONF_T_STRING;
                if(pce)
                    key_type = pce->type;

                switch(key_type) {
                case CONF_T_MULTICOMMA:
                    /* first, see if we already have a tree... */
                    pli = ll_fetch_item(pllcurrent,term);
                    if(!pli) {
                        if((ll_create(&plltemp) != LL_E_SUCCESS)) {
                            DPRINTF(E_FATAL,L_CONF,"Could not create "
                                    "linked list.\n");
                        }
                        ll_add_ll(pllcurrent,term,plltemp);
                        ll_set_flags(plltemp,0); /* allow dups */
                    } else {
                        plltemp = pli->value.as_ll;
                    }

                    /* got list, break comma sep and add */
                    if(_conf_split(value,",",&valuearray) >= 0) {
                        index = 0;
                        while(valuearray[index]) {
                            ll_add_string(plltemp,term,valuearray[index]);
                            index++;
                        }
                        _conf_dispose_split(valuearray);
                    } else {
                        ll_add_string(plltemp,term,value);
                    }
                    break;
                case CONF_T_INT:
                case CONF_T_STRING:
                case CONF_T_EXISTPATH:
                default:
                    ll_add_string(pllcurrent,term,value);
                    break;
                }


                if(comment) {
                    /* this is an inline comment */
                    snprintf(keybuffer,sizeof(keybuffer),"in_%s_%s",
                             section_name,term);
                    ll_add_string(pllcomment,keybuffer,comment);
                    comment = NULL;
                }

                if(prev_comments[0] != '\0') {
                    /* we had some preceding comments */
                    snprintf(keybuffer,sizeof(keybuffer),"pre_%s_%s",
                             section_name, term);
                    ll_add_string(pllcomment,keybuffer,prev_comments);
                    prev_comments[0] = '\0';
                    current_comment_length=0;
                }
            } else {
                ws=1;
            }

            if(((term) && (strlen(term))) && (!value)) {
                DPRINTF(E_LOG,L_CONF,"Error in config file on line %d\n",line);
                ll_destroy(pllnew);
                return CONF_E_PARSE;
            }
        }

        if((comment)||(ws)) {
            if(!comment)
                comment = "";


            /* add to prev comments */
            while((current_comment_length + (int)strlen(comment) + 2 >=
                   total_comment_length) && (total_comment_length < 32768)) {
                total_comment_length *= 2;
                DPRINTF(E_DBG,L_CONF,"Expanding precomments to %d\n",
                        total_comment_length);
                prev_comments=realloc(prev_comments,total_comment_length);
                if(!prev_comments)
                    DPRINTF(E_FATAL,L_CONF,"Malloc error\n");
            }

            if(current_comment_length + (int)strlen(comment)+2 >=
               total_comment_length) {
                if(!warned_truncate)
                    DPRINTF(E_LOG,L_CONF,"Truncating comments in config\n");
                warned_truncate=1;
            } else {
                if(strlen(comment)) {
                    strcat(prev_comments,"#");
                    strcat(prev_comments,comment);
                    current_comment_length += ((int) strlen(comment) + 1);
                } else {
                    strcat(prev_comments,"\n");
                    current_comment_length += 2; /* windows, worst case */
                }
            }
        }
    }

    if(section_name)
        free(section_name);

    if(prev_comments) {
        if(prev_comments[0] != '\0') {
            ll_add_string(pllcomment,"end",prev_comments);
        }
        free(prev_comments);
    }

    fclose(fin);

    /*  Sanity check */
    if(_conf_verify(pllnew)) {
        DPRINTF(E_INF,L_CONF,"Loading new config file.\n");
        _conf_lock();
        if(conf_main) {
            ll_destroy(conf_main);
        }

        if(conf_comments) {
            ll_destroy(conf_comments);
        }

        conf_main = pllnew;
        conf_comments = pllcomment;
        _conf_unlock();
    } else {
        ll_destroy(pllnew);
        ll_destroy(pllcomment);
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

    if(conf_comments) {
        ll_destroy(conf_comments);
        conf_comments = NULL;
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

    if(!result) {
        _conf_unlock();
        return CONF_E_NOTFOUND;
    }

    len = (int) strlen(result) + 1;

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
 * return the value from the CURRENT config tree as an allocated string
 *
 * @param section section name to search in
 * @param key key to search for
 * @param dflt default value to return if key not found
 * @returns a pointer to an allocated string containing the required
 *          configuration key
 */
char *conf_alloc_string(char *section, char *key, char *dflt) {
    LL_ITEM *pitem;
    char *result;
    char *retval;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if((!pitem) || (pitem->type != LL_TYPE_STRING)) {
        result = dflt;
    } else {
        result = pitem->value.as_string;
    }

    if(result == NULL) {
        _conf_unlock();
        return NULL;
    }

    retval = strdup(result);

    if(!retval) {
        DPRINTF(E_FATAL,L_CONF,"Malloc error in conf_alloc_string\n");
    }
    _conf_unlock();
    return retval;
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
    LL *temp_ll;
    CONF_ELEMENTS *pce;
    int key_type = CONF_T_STRING;
    char **valuearray;
    int index;
    int err;

    _conf_lock();
    pce = _conf_get_keyinfo(section,key);
    if(pce)
        key_type = pce->type;

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
        if(key_type == CONF_T_MULTICOMMA) {
            if((err = ll_create(&temp_ll)) != LL_E_SUCCESS) {
                DPRINTF(E_FATAL,L_CONF,"conf_set_string: could not create ll\n");
            }
            ll_add_ll(section_ll,key,temp_ll);
            ll_set_flags(temp_ll,0); /* allow dups */
            if(_conf_split(value,",",&valuearray) >= 0) {
                index = 0;
                while(valuearray[index]) {
                    ll_add_string(temp_ll,key,valuearray[index]);
                    index++;
                }
                _conf_dispose_split(valuearray);
            }
        } else {
            if((err = ll_add_string(section_ll,key,value)) != LL_E_SUCCESS) {
                DPRINTF(E_LOG,L_CONF,"Error in conf_set_string: "
                        "(%s/%s)\n",section,key);
                _conf_unlock();
                return CONF_E_UNKNOWN;
            }
        }
    } else {
        /* we have the item, let's update it */
        if(key_type == CONF_T_MULTICOMMA) {
            /* delete whatever is there, then add from commas */
            ll_destroy(pitem->value.as_ll);
            if(ll_create(&pitem->value.as_ll) != LL_E_SUCCESS) {
                DPRINTF(E_FATAL,L_CONF,
                        "conf_set_string: could not create ll\n");
            }
            ll_set_flags(pitem->value.as_ll,0); /* allow dups */
            if(_conf_split(value,",",&valuearray) >= 0) {
                index = 0;
                while(valuearray[index]) {
                    ll_add_string(pitem->value.as_ll,key,valuearray[index]);
                    index++;
                }
                _conf_dispose_split(valuearray);
            }
        } else {
            ll_update_string(pitem,value);
        }
    }

    _conf_unlock();
    return conf_write();
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
    int retval = CONF_E_NOTWRITABLE;
    FILE *fp;

    if(!conf_main_file) {
        return CONF_E_NOCONF;
    }

    _conf_lock();
    if((fp = fopen(conf_main_file,"w+")) != NULL) {
        retval = _conf_write(fp,conf_main,0,NULL);
        fclose(fp);
    }
    _conf_unlock();

    return retval ? CONF_E_SUCCESS : CONF_E_NOTWRITABLE;
}

/**
 * do the actual work of writing the config file
 *
 * @param fp file we are writing the config file to
 * @param pll list we are dumping k/v pairs for
 * @param sublevel whether this is the root, or a subkey
 * @returns TRUE on success, FALSE otherwise
 */
int _conf_write(FILE *fp, LL *pll, int sublevel, char *parent) {
    LL_ITEM *pli;
    LL_ITEM *ppre, *pin;
    LL_ITEM *plitemp;

    char keybuffer[256];

    if(!pll)
        return TRUE;

    /* write all the solo keys, first! */
    pli = pll->itemlist.next;
    while(pli) {
        /* if there is a PRE there, then let's emit that*/
        if(sublevel) {
            snprintf(keybuffer,sizeof(keybuffer),"pre_%s_%s",parent,pli->key);
            ppre=ll_fetch_item(conf_comments,keybuffer);
            snprintf(keybuffer,sizeof(keybuffer),"in_%s_%s",parent,pli->key);
            pin = ll_fetch_item(conf_comments,keybuffer);
        } else {
            snprintf(keybuffer,sizeof(keybuffer),"pre_%s",pli->key);
            ppre=ll_fetch_item(conf_comments,keybuffer);
            snprintf(keybuffer,sizeof(keybuffer),"in_%s",pli->key);
            pin = ll_fetch_item(conf_comments,keybuffer);
        }

        if(ppre) {
            fprintf(fp,"%s",ppre->value.as_string);
        }

        switch(pli->type) {
        case LL_TYPE_LL:
            if(sublevel) {
                /* must be multivalued */
                plitemp = NULL;
                while((plitemp = ll_get_next(pli->value.as_ll,plitemp))) {
                    fprintf(fp,"%s = %s\n",pli->key,plitemp->value.as_string);
                }
            } else {
                fprintf(fp,"[%s]",pli->key);
                if(pin) {
                    fprintf(fp," #%s",pin->value.as_string);
                }
                fprintf(fp,"\n");

                if(!_conf_write(fp, pli->value.as_ll, 1, pli->key))
                   return FALSE;
            }
            break;

        case LL_TYPE_INT:
            fprintf(fp,"%s = %d",pli->key,pli->value.as_int);
            if(pin) {
                fprintf(fp," #%s",pin->value.as_string);
            }
            fprintf(fp,"\n");
            break;

        case LL_TYPE_STRING:
            fprintf(fp,"%s = %s",pli->key,pli->value.as_string);
            if(pin) {
                fprintf(fp," #%s",pin->value.as_string);
            }
            fprintf(fp,"\n");
            break;
        }

        pli = pli->next;
    }

    if(!sublevel) {
        pin = ll_fetch_item(conf_comments,"end");
        if(pin) {
            fprintf(fp,"%s",pin->value.as_string);
        }
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

/**
 * split a string on delimiter boundaries, filling
 * a string-pointer array.
 *
 * The user must free both the first element in the array,
 * and the array itself.
 *
 * @param s string to split
 * @param delimiters boundaries to split on
 * @param argvp an argv array to be filled
 * @returns number of tokens
 */
int _conf_split(char *s, char *delimiters, char ***argvp) {
    int i;
    int numtokens;
    const char *snew;
    char *t;
    char *tokptr;
    char *tmp;

    if ((s == NULL) || (delimiters == NULL) || (argvp == NULL))
        return -1;
    *argvp = NULL;
    snew = s + strspn(s, delimiters);
    if ((t = malloc(strlen(snew) + 1)) == NULL)
        return -1;

    strcpy(t, snew);
    numtokens = 0;
    tokptr = NULL;
    tmp = t;
    while(strtok_r(tmp,delimiters,&tokptr) != NULL) {
        tmp=NULL;
        numtokens++;
    }

    DPRINTF(E_DBG,L_CONF,"Found %d tokens in %s\n",numtokens,s);

    if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
        free(t);
        return -1;
    }

    if (numtokens == 0)
        free(t);
    else {
        strcpy(t, snew);
        tokptr = NULL;
        tmp = t;
        for (i = 0; i < numtokens; i++) {
            *((*argvp) + i) = strtok_r(tmp, delimiters, &tokptr);
            tmp=NULL;
            DPRINTF(E_DBG,L_CONF,"Token %d: %s\n",i+1,(*argvp)[i]);
        }
    }

    *((*argvp) + numtokens) = NULL;
    return numtokens;
}

/**
 * implode a multivalued term in a perl sense.
 *
 * @param section section of term to implode
 * @param key key of term to implode
 * @pararm delimiter what to "glue" them with
 * @returns imploded string (preallocated), or NULL
 */
char *conf_implode(char *section, char *key, char *delimiter) {
    LL_ITEM *pitem;
    LL_ITEM *penum;
    int count;
    int len;
    char *retval;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if((!pitem) || (pitem->type != LL_TYPE_LL)) {
        _conf_unlock();
        return NULL;
    }

    /* otherwise, alloc a string and go */
    count = len = 0;
    penum = NULL;
    while((penum = ll_get_next(pitem->value.as_ll,penum))) {
        if(penum->type != LL_TYPE_STRING) {
            DPRINTF(E_FATAL,L_CONF,"multivalued property not a string?\n");
        }
        len += (int)strlen(penum->value.as_string);
        count++;
    }

    if(!count) {
        _conf_unlock();
        return NULL;
    }

    len += ((int)strlen(delimiter) * (count-1));
    retval = (char*)malloc(len + 1);
    if(!retval) {
        DPRINTF(E_FATAL,L_CONF,"conf_implode: malloc\n");
    }

    memset(retval,0,len+1);
    penum = NULL;
    while((penum = ll_get_next(pitem->value.as_ll,penum))) {
        strcat(retval,penum->value.as_string);
        if(--count) {
            strcat(retval,delimiter);
        }
    }

    _conf_unlock();
    return retval;
}

/**
 * dispose of the argv set that was created in _conf_split
 *
 * @param argv string array to delete
 */
void _conf_dispose_split(char **argv) {
    if(!argv)
        return;

    if(argv[0])
        free(argv[0]);

    free(argv);
}


/**
 * return a multi-valued item as an array (values)
 *
 * @param section section to fetch
 * @param key multivalued key to get from array
 * @returns TRUE on success, FALSE on failure
 */
int conf_get_array(char *section, char *key, char ***argvp) {
    LL_ITEM *pitem, *penum;
    int count;
    int len;

    _conf_lock();
    pitem = _conf_fetch_item(conf_main,section,key);
    if((!pitem) || (pitem->type != LL_TYPE_LL)) {
        _conf_unlock();
        return FALSE;
    }

    /* otherwise, alloc a string and go */
    count = 0;
    penum = NULL;
    while((penum = ll_get_next(pitem->value.as_ll,penum))) {
        if(penum->type != LL_TYPE_STRING) {
            DPRINTF(E_FATAL,L_CONF,"multivalued property not a string?\n");
        }
        count++;
    }

    /* now we have a count, alloc an argv */
    len = (count+1) * sizeof(char*);
    *(argvp) = (char**)malloc(len);
    if(!*(argvp)) {
        DPRINTF(E_FATAL,L_CONF,"conf_get_array: malloc\n");
    }

    memset(*(argvp),0,len);

    count=0;
    penum=NULL;
    while((penum = ll_get_next(pitem->value.as_ll,penum))) {
        (*argvp)[count] = strdup(penum->value.as_string);
        if(!(*argvp)[count]) {
            DPRINTF(E_FATAL,L_CONF,"conf_get_array: malloc\n");
        }
        count++;
    }

    _conf_unlock();
    return TRUE;
}

/**
 * dispose of the array created above
 *
 * @param argv argv pointer created
 */
void conf_dispose_array(char **argv) {
    int index=0;

    if(!argv)
        return;


    while(argv[index]) {
        free(argv[index]);
        index++;
    }
}

/* FIXME: this belongs in xml-rpc, but need config enumerating fns */

/**
 * dump the config to xml
 *
 * @param pwsc web connection to dump to
 * @returns TRUE on success, FALSE otherwise
 */
int conf_xml_dump(WS_CONNINFO *pwsc) {
    XMLSTRUCT *pxml;
    int retval;

    if(!conf_main_file) {
        return FALSE; /* CONF_E_NOCONF */
    }

    pxml = xml_init(pwsc,1);
    xml_push(pxml,"config");

    _conf_lock();

    retval = _conf_xml_dump(pxml,conf_main,0,NULL);

    _conf_unlock();

    xml_pop(pxml);
    xml_deinit(pxml);

    return retval;
}

/**
 * do the actual work of dumping the config file
 *
 * @param pwsc web connection we are writing the config file to
 * @param pll list we are dumping k/v pairs for
 * @param sublevel whether this is the root, or a subkey
 * @returns TRUE on success, FALSE otherwise
 */
int _conf_xml_dump(XMLSTRUCT *pxml, LL *pll, int sublevel, char *parent) {
    LL_ITEM *pli;
    LL_ITEM *plitemp;

    if(!pll)
        return TRUE;

    /* write all the solo keys, first! */
    pli = pll->itemlist.next;
    while(pli) {
        switch(pli->type) {
        case LL_TYPE_LL:
            if(sublevel) {
                /* must be multivalued */
                plitemp = NULL;
                xml_push(pxml,pli->key);
                while((plitemp = ll_get_next(pli->value.as_ll,plitemp))) {
                    xml_output(pxml,"item","%s",plitemp->value.as_string);
                }
                xml_pop(pxml);
            } else {
                xml_push(pxml,pli->key);
                if(!_conf_xml_dump(pxml, pli->value.as_ll, 1, pli->key))
                   return FALSE;
                xml_pop(pxml);
            }
            break;

        case LL_TYPE_INT:
            xml_output(pxml,pli->key,"%d",pli->value.as_int);
            break;

        case LL_TYPE_STRING:
            xml_output(pxml,pli->key,"%s",pli->value.as_string);
            break;
        }

        pli = pli->next;
    }

    return TRUE;
}

/**
 * get the filename of the currently runnig config file
 *
 * @returns path if it exists, or NULL if no config file opened
 */
char *conf_get_filename(void) {
    return conf_main_file;
}
