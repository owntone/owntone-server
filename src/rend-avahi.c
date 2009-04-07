/*
 * Rendezvous support with avahi
 *
 * Copyright (C) 2005 Sebastian Dröge <slomo@ubuntu.com>
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
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
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <net/if.h>


#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include <avahi-client/client.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/timeval.h>
#include <avahi-common/malloc.h>

#include "daapd.h"
#include "err.h"

static AvahiClient *mdns_client = NULL;
static AvahiEntryGroup *mdns_group = NULL;
static AvahiThreadedPoll *threaded_poll = NULL;

typedef struct tag_rend_avahi_group_entry {
    char *name;
    char *type;
    int port;
    char *iface;
    char *txt;
    struct tag_rend_avahi_group_entry *next;
} REND_AVAHI_GROUP_ENTRY;

static REND_AVAHI_GROUP_ENTRY rend_avahi_entries = { NULL, NULL, 0, NULL };

static int _rend_avahi_create_services(void);

/* add a new group entry node */
static int _rend_avahi_add_group_entry(char *name, char *type, int port, char *iface, char *txt) {
    REND_AVAHI_GROUP_ENTRY *pge;

    pge = (REND_AVAHI_GROUP_ENTRY *)malloc(sizeof(REND_AVAHI_GROUP_ENTRY));
    if(!pge)
        return 0;

    pge->name = strdup(name);
    pge->type = strdup(type);
    pge->iface = strdup(iface);
    pge->txt = strdup(txt);
    pge->port = port;

    pge->next = rend_avahi_entries.next;
    rend_avahi_entries.next = pge;

    return 1;
}

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
    //    assert(g == mdns_group);

    if (!g || (g != mdns_group))
        return;

    switch (state) {
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
        DPRINTF(E_DBG, L_REND, "Successfully added mdns services\n");
        break;
    case AVAHI_ENTRY_GROUP_COLLISION:
        DPRINTF(E_DBG, L_REND, "Group collision\n");
        /*
          new_name = avahi_alternative_service_name(mdns_name);
          DPRINTF(E_WARN, L_REND, "mdns service name collision. Renamed service %s -> %s\n", mdns_name, new_name);
          free(mdns_name);
          mdns_name = new_name;
          add_services(avahi_entry_group_get_client(g));
        */
        break;
    case AVAHI_ENTRY_GROUP_FAILURE :
        avahi_threaded_poll_quit(threaded_poll);
        break;
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
    case AVAHI_ENTRY_GROUP_REGISTERING:
        break;
    }
}

int rend_register(char *name, char *type, int port, char *iface, char *txt) {
    avahi_threaded_poll_lock(threaded_poll);

    DPRINTF(E_DBG,L_REND,"Adding %s/%s\n",name,type);
    _rend_avahi_add_group_entry(name,type,port,iface,txt);
    if(mdns_group) {
        DPRINTF(E_DBG,L_MISC,"Resetting mdns group\n");
        avahi_entry_group_reset(mdns_group);
    }
    DPRINTF(E_DBG,L_REND,"Creating service group (again?)\n");
    _rend_avahi_create_services();

    avahi_threaded_poll_unlock(threaded_poll);

    return 0;
}


/**
 * register the block of services
 *
 * @returns true if successful, false otherwise
 */
int _rend_avahi_create_services(void) {
    int ret = 0;
    REND_AVAHI_GROUP_ENTRY *pentry;
    AvahiStringList *psl;
    unsigned char count=0;
    unsigned char *key,*nextkey;
    unsigned char *newtxt;

    DPRINTF(E_DBG,L_REND,"Creating service group\n");

    if(!rend_avahi_entries.next) {
        DPRINTF(E_DBG,L_REND,"No entries yet... skipping service create\n");
        return 1;
    }

    if (mdns_group == NULL) {
        if (!(mdns_group = avahi_entry_group_new(mdns_client,
                                                 entry_group_callback,
                                                 NULL))) {
            DPRINTF(E_WARN, L_REND, "Could not create AvahiEntryGroup: %s\n",
                    avahi_strerror(avahi_client_errno(mdns_client)));
            return 0;
        }
    }

    pentry = rend_avahi_entries.next;
    while(pentry) {
        /* TODO: honor iface parameter */
        DPRINTF(E_DBG,L_REND,"Re-registering %s/%s\n",pentry->name,pentry->type);

        /* build a string list */
        psl = NULL;
        newtxt = (unsigned char *)strdup(pentry->txt);
        if(!newtxt)
            DPRINTF(E_FATAL,L_REND,"malloc\n");

        key=nextkey=newtxt;
        if(*nextkey)
            count = *nextkey;

        DPRINTF(E_DBG,L_REND,"Found key of size %d\n",count);
        while((*nextkey)&&(nextkey < (newtxt + strlen(pentry->txt)))) {
            key = nextkey + 1;
            nextkey += (count+1);
            count = *nextkey;
            *nextkey = '\0';
            psl=avahi_string_list_add(psl,(char*)key);
            DPRINTF(E_DBG,L_REND,"Added key %s\n",key);
            *nextkey=count;
        }

        free(newtxt);

        if ((ret = avahi_entry_group_add_service_strlst(mdns_group,
                                                        AVAHI_IF_UNSPEC,
                                                 AVAHI_PROTO_UNSPEC, 0,
                                                 avahi_strdup(pentry->name),
                                                 avahi_strdup(pentry->type),
                                                 NULL, NULL,pentry->port,
                                                 psl)) < 0) {
            DPRINTF(E_WARN, L_REND, "Could not add mdns services: %s\n", avahi_strerror(ret));
            avahi_string_list_free(psl);

            return 0;
        }
        pentry = pentry->next;
    }

    if ((ret = avahi_entry_group_commit(mdns_group)) < 0) {
        DPRINTF(E_WARN, L_REND, "Could not commit mdns services: %s\n",
                avahi_strerror(avahi_client_errno(mdns_client)));
        return 0;
    }

    return 1;
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    int error;

    assert(c);
    switch(state) {
    case AVAHI_CLIENT_S_RUNNING:
        DPRINTF(E_LOG,L_REND,"Client running\n");
        if(!mdns_group)
            _rend_avahi_create_services();
        break;
    case AVAHI_CLIENT_S_COLLISION:
        DPRINTF(E_LOG,L_REND,"Client collision\n");
        if(mdns_group)
            avahi_entry_group_reset(mdns_group);
        break;
    case AVAHI_CLIENT_FAILURE:
        DPRINTF(E_LOG,L_REND,"Client failure\n");

	error = avahi_client_errno(c);
	if (error == AVAHI_ERR_DISCONNECTED)
	  {
	    DPRINTF(E_LOG,L_REND,"Server disconnected, reconnecting\n");

	    avahi_client_free(mdns_client);
	    mdns_group = NULL;

	    mdns_client = avahi_client_new(avahi_threaded_poll_get(threaded_poll),
					   AVAHI_CLIENT_NO_FAIL,
					   client_callback,NULL,&error);
	    if (mdns_client == NULL)
	      {
		DPRINTF(E_LOG,L_REND,"Failed to create new Avahi client: %s\n", avahi_strerror(error));
		avahi_threaded_poll_quit(threaded_poll);
	      }
	  }
	else
	  {
	    DPRINTF(E_LOG,L_REND,"Client failure: %s\n", avahi_strerror(error));
	    avahi_threaded_poll_quit(threaded_poll);
	  }
        break;
    case AVAHI_CLIENT_S_REGISTERING:
        DPRINTF(E_LOG,L_REND,"Client registering\n");
        if(mdns_group)
            avahi_entry_group_reset(mdns_group);
        break;
    case AVAHI_CLIENT_CONNECTING:
        DPRINTF(E_LOG,L_REND,"Client connecting\n");
        break;
    }
}

int rend_init(char *user) {
    int error;

    DPRINTF(E_DBG, L_REND, "Initializing avahi\n");
    if(!(threaded_poll = avahi_threaded_poll_new())) {
        DPRINTF(E_LOG,L_REND,"Error starting poll thread\n");
        return -1;
    }

    /*
        mdns_name = strdup(name);
        mdns_port = port;
        //      if ((interface != NULL) && (if_nametoindex(interface) != 0))
        //              mdns_interface = if_nametoindex(interface);
        //      else
        mdns_interface = AVAHI_IF_UNSPEC;
    */

    if (!(mdns_client = avahi_client_new(avahi_threaded_poll_get(threaded_poll),
                                         AVAHI_CLIENT_NO_FAIL,
					 client_callback,NULL,&error))) {
        DPRINTF(E_WARN, L_REND, "avahi_client_new: Error in avahi: %s\n",
                avahi_strerror(avahi_client_errno(mdns_client)));
        avahi_threaded_poll_free(threaded_poll);
        return -1;
    }

    DPRINTF(E_DBG, L_REND, "Starting Avahi ThreadedPoll\n");
    if (avahi_threaded_poll_start(threaded_poll) < 0)
      {
	DPRINTF(E_WARN, L_REND, "avahi_threaded_poll_start: error: %s\n",
		avahi_strerror(avahi_client_errno(mdns_client)));

	avahi_threaded_poll_free(threaded_poll);

	return -1;
      }

    return 0;
}

int rend_stop() {
    avahi_threaded_poll_stop(threaded_poll);

    if (mdns_client != NULL)
        avahi_client_free(mdns_client);

    avahi_threaded_poll_free(threaded_poll);

    return 0;
}

int rend_running(void) {
    return 1;
}

int rend_unregister(char *name, char *type, int port) {
    return 0;
}

