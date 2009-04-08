/*
 * Avahi mDNS backend, with libevent polling
 *
 * Copyright (C) 2009 Julien BLACHE <jb@jblache.org>
 *
 * Pieces coming from mt-daapd:
 * Copyright (C) 2005 Sebastian Dröge <slomo@ubuntu.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <event.h>

#include <avahi-common/watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>

#include "daapd.h"
#include "err.h"
#include "mdns_avahi.h"

static AvahiClient *mdns_client = NULL;
static AvahiEntryGroup *mdns_group = NULL;


struct AvahiWatch
{
  struct event ev;

  AvahiWatchCallback cb;
  void *userdata;

  AvahiWatch *next;
};

struct AvahiTimeout
{
  struct event ev;

  AvahiTimeoutCallback cb;
  void *userdata;

  AvahiTimeout *next;
};

static AvahiWatch *all_w;
static AvahiTimeout *all_t;

/* libevent callbacks */

static void
evcb_watch(int fd, short ev_events, void *arg)
{
  AvahiWatch *w;
  AvahiWatchEvent a_events;

  w = (AvahiWatch *)arg;

  a_events = 0;
  if (ev_events & EV_READ)
    a_events |= AVAHI_WATCH_IN;
  if (ev_events & EV_WRITE)
    a_events |= AVAHI_WATCH_OUT;

  event_add(&w->ev, NULL);

  w->cb(w, fd, a_events, w->userdata);
}

static void
evcb_timeout(int fd, short ev_events, void *arg)
{
  AvahiTimeout *t;

  t = (AvahiTimeout *)arg;

  t->cb(t, t->userdata);
}

/* AvahiPoll implementation for libevent */

static int
_ev_watch_add(AvahiWatch *w, int fd, AvahiWatchEvent a_events)
{
  short ev_events;

  ev_events = 0;
  if (a_events & AVAHI_WATCH_IN)
    ev_events |= EV_READ;
  if (a_events & AVAHI_WATCH_OUT)
    ev_events | EV_WRITE;

  event_set(&w->ev, fd, ev_events, evcb_watch, w);
  event_base_set(evbase_main, &w->ev);

  return event_add(&w->ev, NULL);
}

static AvahiWatch *
ev_watch_new(const AvahiPoll *api, int fd, AvahiWatchEvent a_events, AvahiWatchCallback cb, void *userdata)
{
  AvahiWatch *w;
  int ret;

  w = (AvahiWatch *)malloc(sizeof(AvahiWatch));
  if (!w)
    return NULL;

  memset(w, 0, sizeof(AvahiWatch));

  w->cb = cb;
  w->userdata = userdata;

  ret = _ev_watch_add(w, fd, a_events);
  if (ret != 0)
    {
      free(w);
      return NULL;
    }

  w->next = all_w;
  all_w = w;

  return w;
}

static void
ev_watch_update(AvahiWatch *w, AvahiWatchEvent a_events)
{
  event_del(&w->ev);

  _ev_watch_add(w, EVENT_FD(&w->ev), a_events);
}

static AvahiWatchEvent
ev_watch_get_events(AvahiWatch *w)
{
  AvahiWatchEvent a_events;

  a_events = 0;

  if (event_pending(&w->ev, EV_READ, NULL))
    a_events |= AVAHI_WATCH_IN;
  if (event_pending(&w->ev, EV_WRITE, NULL))
    a_events |= AVAHI_WATCH_OUT;

  return a_events;
}

static void
ev_watch_free(AvahiWatch *w)
{
  AvahiWatch *prev;
  AvahiWatch *cur;

  event_del(&w->ev);

  prev = NULL;
  for (cur = all_w; cur; prev = cur, cur = cur->next)
    {
      if (cur != w)
	continue;

      if (prev == NULL)
	all_w = w->next;
      else
	prev->next = w->next;

      break;
    }

  free(w);
}


static int
_ev_timeout_add(AvahiTimeout *t, const struct timeval *tv)
{
  struct timeval e_tv;
  struct timeval now;
  int ret;

  evtimer_set(&t->ev, evcb_timeout, t);
  event_base_set(evbase_main, &t->ev);

  if ((tv->tv_sec == 0) && (tv->tv_usec == 0))
    {
      evutil_timerclear(&e_tv);
    }
  else
    {
      ret = gettimeofday(&now, NULL);
      if (ret != 0)
	return -1;

      evutil_timersub(tv, &now, &e_tv);
    }

  return evtimer_add(&t->ev, &e_tv);
}

static AvahiTimeout *
ev_timeout_new(const AvahiPoll *api, const struct timeval *tv, AvahiTimeoutCallback cb, void *userdata)
{
  AvahiTimeout *t;
  int ret;

  t = (AvahiTimeout *)malloc(sizeof(AvahiTimeout));
  if (!t)
    return NULL;

  memset(t, 0, sizeof(AvahiTimeout));

  t->cb = cb;
  t->userdata = userdata;

  if (tv != NULL)
    {
      ret = _ev_timeout_add(t, tv);
      if (ret != 0)
	{
	  free(t);

	  return NULL;
	}
    }

  t->next = all_t;
  all_t = t;

  return t;
}

static void
ev_timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
  event_del(&t->ev);

  if (tv)
    _ev_timeout_add(t, tv);
}

static void
ev_timeout_free(AvahiTimeout *t)
{
  AvahiTimeout *prev;
  AvahiTimeout *cur;

  event_del(&t->ev);

  prev = NULL;
  for (cur = all_t; cur; prev = cur, cur = cur->next)
    {
      if (cur != t)
	continue;

      if (prev == NULL)
	all_t = t->next;
      else
	prev->next = t->next;

      break;
    }

  free(t);
}

static struct AvahiPoll ev_poll_api =
  {
    .userdata = NULL,
    .watch_new = ev_watch_new,
    .watch_update = ev_watch_update,
    .watch_get_events = ev_watch_get_events,
    .watch_free = ev_watch_free,
    .timeout_new = ev_timeout_new,
    .timeout_update = ev_timeout_update,
    .timeout_free = ev_timeout_free
  };


/* Avahi client callbacks & helpers (imported from mt-daapd) */

struct mdns_group_entry
{
  char *name;
  char *type;
  int port;
  AvahiStringList *txt;

  struct mdns_group_entry *next;
};

static struct mdns_group_entry *group_entries;

static void
entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata)
{
  if (!g || (g != mdns_group))
    return;

  switch (state)
    {
      case AVAHI_ENTRY_GROUP_ESTABLISHED:
        DPRINTF(E_DBG, L_REND, "Successfully added mDNS services\n");
        break;

      case AVAHI_ENTRY_GROUP_COLLISION:
        DPRINTF(E_DBG, L_REND, "Group collision\n");
        break;

      case AVAHI_ENTRY_GROUP_FAILURE:
        DPRINTF(E_DBG, L_REND, "Group failure\n");
        break;

      case AVAHI_ENTRY_GROUP_UNCOMMITED:
        DPRINTF(E_DBG, L_REND, "Group uncommitted\n");
	break;

      case AVAHI_ENTRY_GROUP_REGISTERING:
        DPRINTF(E_DBG, L_REND, "Group registering\n");
        break;
    }
}

static void
_create_services(void)
{
  struct mdns_group_entry *pentry;
  int ret;

  DPRINTF(E_DBG, L_REND, "Creating service group\n");

  if (!group_entries)
    {
      DPRINTF(E_DBG, L_REND, "No entries yet... skipping service create\n");

      return;
    }

    if (mdns_group == NULL)
      {
        mdns_group = avahi_entry_group_new(mdns_client, entry_group_callback, NULL);
	if (!mdns_group)
	  {
            DPRINTF(E_WARN, L_REND, "Could not create Avahi EntryGroup: %s\n",
                    avahi_strerror(avahi_client_errno(mdns_client)));

            return;
	  }
      }

    pentry = group_entries;
    while (pentry)
      {
        DPRINTF(E_DBG, L_REND, "Re-registering %s/%s\n", pentry->name, pentry->type);

        ret = avahi_entry_group_add_service_strlst(mdns_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0,
						   avahi_strdup(pentry->name), avahi_strdup(pentry->type),
						   NULL, NULL, pentry->port, pentry->txt);
	if (ret < 0)
	  {
	    DPRINTF(E_WARN, L_REND, "Could not add mDNS services: %s\n", avahi_strerror(ret));

	    return;
	  }

	pentry = pentry->next;
      }

    ret = avahi_entry_group_commit(mdns_group);
    if (ret < 0)
      {
	DPRINTF(E_WARN, L_REND, "Could not commit mDNS services: %s\n",
		avahi_strerror(avahi_client_errno(mdns_client)));
      }
}

static void
client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata)
{
  int error;

  switch (state)
    {
      case AVAHI_CLIENT_S_RUNNING:
        DPRINTF(E_LOG, L_REND, "Avahi state change: Client running\n");
        if (!mdns_group)
	  _create_services();
        break;

      case AVAHI_CLIENT_S_COLLISION:
        DPRINTF(E_LOG, L_REND, "Avahi state change: Client collision\n");
        if(mdns_group)
	  avahi_entry_group_reset(mdns_group);
        break;

      case AVAHI_CLIENT_FAILURE:
        DPRINTF(E_LOG, L_REND, "Avahi state change: Client failure\n");

	error = avahi_client_errno(c);
	if (error == AVAHI_ERR_DISCONNECTED)
	  {
	    DPRINTF(E_LOG, L_REND, "Avahi Server disconnected, reconnecting\n");

	    avahi_client_free(mdns_client);
	    mdns_group = NULL;

	    mdns_client = avahi_client_new(&ev_poll_api, AVAHI_CLIENT_NO_FAIL,
					   client_callback, NULL, &error);
	    if (!mdns_client)
	      {
		DPRINTF(E_LOG, L_REND, "Failed to create new Avahi client: %s\n",
			avahi_strerror(error));
	      }
	  }
	else
	  {
	    DPRINTF(E_LOG, L_REND, "Avahi client failure: %s\n", avahi_strerror(error));
	  }
        break;

      case AVAHI_CLIENT_S_REGISTERING:
        DPRINTF(E_LOG, L_REND, "Avahi state change: Client registering\n");
        if (mdns_group)
	  avahi_entry_group_reset(mdns_group);
        break;

      case AVAHI_CLIENT_CONNECTING:
        DPRINTF(E_LOG, L_REND, "Avahi state change: Client connecting\n");
        break;
    }
}


/* mDNS interface - to be called only from the main thread */

int
mdns_init(void)
{
  int error;

  DPRINTF(E_DBG, L_REND, "Initializing Avahi mDNS\n");

  all_w = NULL;
  all_t = NULL;
  group_entries = NULL;

  mdns_client = avahi_client_new(&ev_poll_api, AVAHI_CLIENT_NO_FAIL,
				 client_callback, NULL, &error);
  if (!mdns_client)
    {
      DPRINTF(E_WARN, L_REND, "mdns_init: Could not create Avahi client: %s\n",
	      avahi_strerror(avahi_client_errno(mdns_client)));

      return -1;
    }

  return 0;
}

void
mdns_deinit(void)
{
  struct mdns_group_entry *ge;
  AvahiWatch *w;
  AvahiTimeout *t;

  for (t = all_t; t; t = t->next)
    event_del(&t->ev);

  for (w = all_w; w; w = w->next)
    event_del(&w->ev);

  for (ge = group_entries; ge; ge = ge->next)
    {
      group_entries = ge->next;

      free(ge->name);
      free(ge->type);
      avahi_string_list_free(ge->txt);

      free(ge);
    }

  if (mdns_client != NULL)
    avahi_client_free(mdns_client);
}

int
mdns_register(char *name, char *type, int port, char *txt)
{
  struct mdns_group_entry *ge;
  AvahiStringList *txt_sl;
  unsigned char count;
  unsigned char *key;
  unsigned char *nextkey;
  unsigned char *newtxt;

  DPRINTF(E_DBG, L_REND, "Adding mDNS service %s/%s\n", name, type);

  ge = (struct mdns_group_entry *)malloc(sizeof(struct mdns_group_entry));
  if (!ge)
    return -1;

  ge->name = strdup(name);
  ge->type = strdup(type);
  ge->port = port;

  /* Build a string list from the "encoded" txt record
   * "<len1><record1><len2><record2>...<recordN>\0"
   * Length is 1 byte
   */
  count = 0;
  txt_sl = NULL;
  newtxt = (unsigned char *)strdup(txt);
  if (!newtxt)
    {
      DPRINTF(E_FATAL, L_REND, "Out of memory\n");

      return -1;
    }

  key = nextkey = newtxt;
  if (*nextkey)
    count = *nextkey;

  DPRINTF(E_DBG, L_REND, "Found key of size %d\n", count);
  while ((*nextkey) && (nextkey < (newtxt + strlen(txt))))
    {
      key = nextkey + 1;
      nextkey += (count + 1);
      count = *nextkey;
      *nextkey = '\0';

      txt_sl = avahi_string_list_add(txt_sl, (char *)key);

      DPRINTF(E_DBG, L_REND, "Added key %s\n", key);

      *nextkey = count;
    }

  free(newtxt);

  ge->txt = txt_sl;

  ge->next = group_entries;
  group_entries = ge;

  if (mdns_group)
    {
      DPRINTF(E_DBG, L_MISC, "Resetting mDNS group\n");
      avahi_entry_group_reset(mdns_group);
    }

  DPRINTF(E_DBG, L_REND, "Creating service group\n");
  _create_services();

  return 0;
}
