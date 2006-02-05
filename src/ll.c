/*
 * $Id$
 * Rock stupid char* indexed linked lists
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

#include "ll.h"
#include "err.h"


/** Internal functions  */

int _ll_add_item(LL *pl, char *key, void *vpval, int ival, int type);

/**
 * create a ll
 *
 * @param ppl pointer to a LL *.  Returns valid handle on LL_E_SUCCESS
 * @returns LL_E_SUCCESS on success, error code otherwise
 */
int ll_create(LL **ppl) {
    *ppl = (LL *)malloc(sizeof(struct _LL));
    if(!*ppl)
        return LL_E_MALLOC;

    memset(*ppl,0x0,sizeof(struct _LL));
    return LL_E_SUCCESS;
}

/**
 * destroy a ll, recusively, if neccesary
 *
 * @param pl ll to destroy
 * @returns LL_E_SUCCESS
 */
int ll_destroy(LL *pl) {
    LL_ITEM *current,*last;

    last = &(pl->itemlist);
    current = pl->itemlist.next;

    while(current) {
        switch(current->type) {
        case LL_TYPE_LL:
            ll_destroy(current->value.as_ll);
            break;
        case LL_TYPE_STRING:
            free(current->value.as_string);
            break;
        default:
            break;
        }
        free(current->key);
        last = current;
        current = current->next;
        free(last);
    }

    free(pl);

    return LL_E_SUCCESS;
}

/**
 * thin wrapper for _ll_add_item
 */
int ll_add_string(LL *pl, char *key, char *cval) {
    return _ll_add_item(pl,cval,key,0,LL_TYPE_STRING);
}

/**
 * thin wrapper for _ll_add_item
 */
int ll_add_int(LL *pl, char *key, int ival) {
    return _ll_add_item(pl,key,NULL,ival,LL_TYPE_INT);
}

/**
 * thin wrapper for _ll_add_item
 */
int ll_add_ll(LL *pl, char *key, LL *pnew) {
    return _ll_add_item(pl,key,(void*)pnew,0,LL_TYPE_LL);
}

/**
 * add an item to a ll
 */
int _ll_add_item(LL *pl, char *key, void* vpval, int ival, int type) {
    LL_ITEM *pli;

    pli=(LL_ITEM *)malloc(sizeof(LL_ITEM));
    if(!pli) {
        return LL_E_MALLOC;
    }

    pli->type = type;
    pli->key = strdup(key);

    switch(type) {
    case LL_TYPE_INT:
        pli->value.as_int = ival;
        break;
    case LL_TYPE_LL:
        pli->value.as_ll = (LL *)vpval;
        break;
    case LL_TYPE_STRING:
        pli->value.as_string = strdup((char*)vpval);
        break;
    default:
        break;
    }

    if(pl->flags & LL_FLAG_HEADINSERT) {
        pli->next = pl->itemlist.next;
        pl->itemlist.next = pli;
    } else {
        pli->next = NULL;
        pl->tailptr->next = pli;
        pl->tailptr = pli;
    }

    return LL_E_SUCCESS;
}

/**
 * internal function to get the ll item associated with
 * a specific key, using the case sensitivity specified
 * by the ll flags.  This assumes that the lock is held!
 *
 * @param pl ll to fetch item from
 * @param key key name to fetch
 * @returns pointer to llitem, or null if not found
 */
LL_ITEM *ll_fetch_item(LL *pl, char *key) {
    LL_ITEM *current;

    current = pl->itemlist.next;
    while(current) {
        if(pl->flags & LL_FLAG_HONORCASE) {
            if(!strcasecmp(current->key,key))
                break;
            if(!strcmp(current->key,key))
                break;
        }
        current = current->next;
    }

    return current;
}

/**
 * set flags
 *
 * @param pl ll to set flags for
 * @returns LL_E_SUCCESS
 */
int ll_set_flags(LL *pl, unsigned int flags) {
    pl->flags = flags;
    return LL_E_SUCCESS;
}

/**
 * get flags
 *
 * @param pl ll to get flags from
 * @returns LL_E_SUCCESS
 */
int ll_get_flags(LL *pl, unsigned int *flags) {
    *flags = pl->flags;
    return LL_E_SUCCESS;
}

/**
 * Dump a linked list
 *
 * @parm pl linked list to dump
 */
extern void ll_dump(LL *pl) {

}

