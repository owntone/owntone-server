/*
 * $Id$
 * Simple collection as linked lists
 *
 * Copyright (C) 2006 Ron Pedde (rpedde@users.sourceforge.net)
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

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "bag.h"
#include "err.h"

typedef struct _BAGITEM {
    int type;
    char *name;
    union {
        int as_int;
        char *as_string;
        struct _BAG *as_bag;
    } value;
    struct _BAGITEM *next;
} BAG_ITEM;

struct _BAG {
    unsigned int flags;
    struct _BAGITEM *tailptr;
    struct _BAGITEM itemlist;
};

/** Globals */
pthread_mutex_t bag_mutex=PTHREAD_MUTEX_INITIALIZER;

/** Internal functions  */
void _bag_lock(void);
void _bag_unlock(void);
int _bag_delete(BAG_HANDLE pb);
BAG_ITEM *_bag_fetch_item(BAG_HANDLE pb, char *key);
int _bag_add_item(BAG_HANDLE pb, void* vpval, int ival, int type);

/**
 * lock the mutex.  This could be done on a bag-by-bag basis,
 * and it could further be done as a reader-writer lock, but
 * I don't think performance is going to be an issue, so I'll just
 * use a global lock.
 */
void _bag_lock(void) {
    int err;

    if((err=pthread_mutex_lock(&bag_mutex))) {
        DPRINTF(E_FATAL,L_MISC,"Could not lock mutex: %s\n",strerror(err));
    }
}

/**
 * unlock the mutex
 */
void _bag_unlock(void) {
    int err;

    if((err = pthread_mutex_unlock(&bag_mutex))) {
        DPRINTF(E_FATAL,L_MISC,"Could not unlock mutex: %s\n",strerror(err));
    }
}

/**
 * create a bag
 *
 * @param pbp pointer to a BAG_HANDLE.  Returns valid handle on BAG_E_SUCCESS
 * @returns BAG_E_SUCCESS on success, error code otherwise
 */
int bag_create(BAG_HANDLE *ppb) {
    *ppb = (BAG_HANDLE)malloc(sizeof(struct _BAG));
    if(!*ppb)
        return BAG_E_MALLOC;

    memset(*ppb,0x0,sizeof(struct _BAG));
    return BAG_E_SUCCESS;
}

/**
 * destroy a bag, recusively, if neccesary
 *
 * @param pb bag to destroy
 * @returns BAG_E_SUCCESS
 */
int bag_destroy(BAG_HANDLE pb) {
    _bag_lock();
    _bag_delete(pb);
    _bag_unlock();
    return BAG_E_SUCCESS;
}

/**
 * internal function to implement the actual delete.
 * assumes that the semaphore is held
 *
 * @param pb bag to delete
 * @returns BAG_E_SUCCESS
 */
int _bag_delete(BAG_HANDLE pb) {
    BAG_ITEM *current,*last;

    last = &(pb->itemlist);
    current = pb->itemlist.next;

    while(current) {
        switch(current->type) {
        case BAG_TYPE_BAG:
            bag_delete(current->value.as_bag);
            break;
        case BAG_TYPE_STRING:
            free(current->value.as_string);
            break;
        default:
            break;
        }

        last = current;
        current = current->next;
        free(last);
    }

    free(pb);

    return BAG_E_SUCCESS;
}

/**
 * thin wrapper for _bag_add_item
 */
int bag_add_string(BAG_HANDLE pb, char *cval) {
    return _bag_add_item(pb,cval,0,BAG_TYPE_STRING);
}

/**
 * thin wrapper for _bag_add_item
 */
int bag_add_int(BAG_HANDLE pb, int ival) {
    return _bag_add_item(pb,NULL,ival,BAG_TYPE_INT);
}

/**
 * thin wrapper for _bag_add_item
 */
int bag_add_bag(BAG_HANDLE pb, BAG_HANDLE pnew) {
    return _bag_add_item(pb,(void*)pnew,0,BAG_TYPE_BAG);
}

/**
 * add an item to a bag
 */
int _bag_add_item(BAG_HANDLE pb, void* vpval, int ival, int type) {
    BAG_ITEM *pbi;

    pbi=(BAG_ITEM *)malloc(sizeof(BAG_ITEM));
    if(!pbi) {
        return BAG_E_MALLOC;
    }

    pbi->type = type;
    switch(type) {
    case BAG_TYPE_INT:
        pbi->value.as_int = ival;
        break;
    case BAG_TYPE_BAG:
        pbi->value.as_bag = (BAG_HANDLE)vpval;
        break;
    case BAG_TYPE_STRING:
        pbi->value.as_string = strdup((char*)vpval);
        break;
    default:
        break;
    }

    _bag_lock();

    if(pb->flags & BAG_FLAG_HEADINSERT) {
        pbi->next = pb->itemlist.next;
        pb->itemlist.next = pbi;
    } else {
        pbi->next = NULL;
        pb->tailptr->next = pbi;
        pb->tailptr = pbi;
    }

    _bag_unlock();
    return BAG_E_SUCCESS;
}

/**
 * internal function to get the bag item associated with
 * a specific key, using the case sensitivity specified
 * by the bag flags.  This assumes that the lock is held!
 *
 * @param pb bag to fetch item from
 * @param key key name to fetch
 * @returns pointer to bagitem, or null if not found
 */
BAG_ITEM *_bag_fetch_item(BAG_HANDLE pb, char *key) {
    BAG_ITEM *current;

    current = pb->itemlist.next;
    while(current) {
        if(pb->flags & BAG_FLAG_HONORCASE) {
            if(!strcasecmp(current->name,key))
                break;
            if(!strcmp(current->name,key))
                break;
        }
        current = current->next;
    }

    return current;
}

/**
 * set flags
 *
 * @param pb bag to set flags for
 * @returns BAG_E_SUCCESS
 */
int bag_set_flags(BAG_HANDLE pb, unsigned int flags) {
    pb->flags = flags;
    return BAG_E_SUCCESS;
}

/**
 * get flags
 *
 * @param pb bag to get flags from
 * @returns BAG_E_SUCCESS
 */
int bag_get_flags(BAG_HANDLE pb, unsigned int *flags) {
    *flags = pb->flags;
    return BAG_E_SUCCESS;
}


/**
 * get the type for a particular key in a bag.
 *
 * @param pb bag to search in
 * @param key key to search for
 * @param type return value for type of bag (BAG_TYPE_*)
 * @returns BAG_E_SUCCESS or BAG_E_NOKEY on failure
 */
int bag_get_type(BAG_HANDLE pb, char *key, int *type) {
    BAG_ITEM *pitem;

    _bag_lock();

    pitem = _bag_fetch_item(pb, key);
    if(!pitem) {
        _bag_unlock();
        return BAG_E_NOKEY;
    }

    *type = pitem->type;
    _bag_unlock;

    return BAG_E_SUCCESS;
}

