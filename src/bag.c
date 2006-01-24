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

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "bag.h"
#include "err.h"

struct _BAGITEM {
    int type;
    char *name;
    union {
        int as_int;
        char *as_string;
        struct _BAG *as_bag;
    } value;
    struct _BAGITEM *next;
    struct _BAGITEM *prev;
};

struct _BAG {
    struct _BAGITEM itemlist;
};


/** Globals */
pthread_mutex_t bag_mutex=PTHREAD_MUTEX_INITIALIZER;

/** Forwards */
void bag_lock(void);
void bag_unlock(void);
int bag_delete(BAG_HANDLE bp);

/**
 * lock the mutex.  This could be done on a bag-by-bag basis,
 * and it could further be done as a reader-writer lock, but
 * I don't think performance is going to be an issue, so I'll just
 * use a global lock.
 */
void bag_lock(void) {
    int err;

    DPRINTF(E_SPAM,L_WS,"Entering ws_lock_unsafe\n");

    if((err=pthread_mutex_lock(&bag_mutex))) {
        DRPINTF(E_FATAL,L_MISC,"Could not lock mutex: %s\n",strerror(err));
    }
}

/**
 * unlock the mutex
 */
void bag_unlock(void) {
}

/**
 * create a bag
 *
 * @param bpp pointer to a BAG_HANDLE.  Returns valid handle on BAG_E_SUCCESS
 * @returns BAG_E_SUCCESS on success, error code otherwise
 */
int bag_create(BAG_HANDLE *bpp) {
    *bpp = (BAG_HANDLE)malloc(sizeof(struct _BAG));
    if(!*bpp)
        return BAG_E_MALLOC;

    memset(*bpp,0x0,sizeof(struct _BAG));
    return BAG_E_SUCCESS;
}

/**
 * destroy a bag, recusively, if neccesary
 *
 * @param bp bag to destroy
 * @returns BAG_E_SUCCESS
 */
int bag_destroy(BAG_HANDLE bp) {
    bag_lock();
    bag_delete(bp);
    bag_unlock();
    return BAG_E_SUCCESS;
}

/**
 * do the actual recursive delete
 */
int bag_delete(BAG_HANDLE bp) {
    BAG_ITEMHANDLE current;

    current = bp->itemlist.next;
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

        current = current->next;
        free(current->prev);
    }

    free(bp);

    return BAG_E_SUCCESS;
}

/**
 * add an item to a bag
 */
int bag_add_item(BAG_HANDLE bp, void* vpval, int ival, int type) {
    BAG_ITEMHANDLE pbi;

    pbi=(BAG_ITEMHANDLE)malloc(sizeof(struct _BAGITEM));
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

    bag_lock();

    pbi->next = bp->itemlist.next;
    bp->itemlist.next = pbi;
    pbi->prev=pbi->next->prev;
    pbi->next->prev = pbi;

    bag_unlock();
    return BAG_E_SUCCESS;
}



