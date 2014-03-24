/*
 * Avahi mDNS backend, with libevent polling
 *
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

#ifdef HAVE_LIBEVENT2
# include <event2/event.h>
#else
# include <event.h>
#endif

#include <avahi-common/watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>

#include "logger.h"
#include "mdns.h"


/* Main event base, from main.c */
extern struct event_base *evbase_main;

static AvahiClient *mdns_client = NULL;
static AvahiEntryGroup *mdns_group = NULL;


struct AvahiWatch
{
  struct event *ev;

  AvahiWatchCallback cb;
  void *userdata;

  AvahiWatch *next;
};

struct AvahiTimeout
{
  struct event *ev;

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

  event_add(w->ev, NULL);

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
    ev_events |= EV_WRITE;

#ifdef HAVE_LIBEVENT2
  if (w->ev)
    event_free(w->ev);

  w->ev = event_new(evbase_main, fd, ev_events, evcb_watch, w);
  if (!w->ev)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not make new event in _ev_watch_add\n");
      return -1;
    }
#else
  if (w->ev)
    free(w->ev);

  w->ev = (struct event *)malloc(sizeof(struct event));
  if (!w->ev)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory in _ev_watch_add\n");
      return -1;
    }

  event_set(w->ev, fd, ev_events, evcb_watch, w);
  event_base_set(evbase_main, w->ev);
#endif

  return event_add(w->ev, NULL);
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
  if (w->ev)
    event_del(w->ev);

#ifdef HAVE_LIBEVENT2
  _ev_watch_add(w, (int)event_get_fd(w->ev), a_events);
#else
  _ev_watch_add(w, EVENT_FD(w->ev), a_events);
#endif
}

static AvahiWatchEvent
ev_watch_get_events(AvahiWatch *w)
{
  AvahiWatchEvent a_events;

  a_events = 0;

  if (event_pending(w->ev, EV_READ, NULL))
    a_events |= AVAHI_WATCH_IN;
  if (event_pending(w->ev, EV_WRITE, NULL))
    a_events |= AVAHI_WATCH_OUT;

  return a_events;
}

static void
ev_watch_free(AvahiWatch *w)
{
  AvahiWatch *prev;
  AvahiWatch *cur;

  if (w->ev)
    {
      event_del(w->ev);
#ifdef HAVE_LIBEVENT2
      event_free(w->ev);
#else
      free(w->ev);
#endif
      w->ev = NULL;
    }

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

#ifdef HAVE_LIBEVENT2
  if (t->ev)
    event_free(t->ev);

  t->ev = evtimer_new(evbase_main, evcb_timeout, t);
  if (!t->ev)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not make event in _ev_timeout_add - out of memory?\n");
      return -1;
    }
#else
  if (t->ev)
    free(t->ev);

  t->ev = (struct event *)malloc(sizeof(struct event));
  if (!t->ev)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory in _ev_timeout_add\n");
      return -1;
    }

  evtimer_set(t->ev, evcb_timeout, t);
  event_base_set(evbase_main, t->ev);
#endif

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

  return evtimer_add(t->ev, &e_tv);
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
  if (t->ev)
    event_del(t->ev);

  if (tv)
    _ev_timeout_add(t, tv);
}

static void
ev_timeout_free(AvahiTimeout *t)
{
  AvahiTimeout *prev;
  AvahiTimeout *cur;

  if (t->ev)
    {
      event_del(t->ev);
#ifdef HAVE_LIBEVENT2
      event_free(t->ev);
#else
      free(t->ev);
#endif
      t->ev = NULL;
    }

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


/* Avahi client callbacks & helpers */

struct mdns_browser
{
  char *type;
  mdns_browse_cb cb;

  int flags;

  struct mdns_browser *next;
};

struct mdns_record_browser {
  struct mdns_browser *mb;

  char *name;
  char *domain;
  struct keyval txt_kv;

  unsigned short port;
};

struct mdns_group_entry
{
  char *name;
  char *type;
  int port;
  AvahiStringList *txt;

  struct mdns_group_entry *next;
};

static struct mdns_browser *browser_list;
static struct mdns_group_entry *group_entries;


#define IPV4LL_NETWORK 0xA9FE0000
#define IPV4LL_NETMASK 0xFFFF0000
#define IPV6LL_NETWORK 0xFE80
#define IPV6LL_NETMASK 0xFFC0

static int
is_v4ll(struct in_addr *addr)
{
  return ((ntohl(addr->s_addr) & IPV4LL_NETMASK) == IPV4LL_NETWORK);
}

static int
is_v6ll(struct in6_addr *addr)
{
  return ((((addr->s6_addr[0] << 8) | addr->s6_addr[1]) & IPV6LL_NETMASK) == IPV6LL_NETWORK);
}

static void
browse_record_callback_v4(AvahiRecordBrowser *b, AvahiIfIndex intf, AvahiProtocol proto,
			  AvahiBrowserEvent event, const char *hostname, uint16_t clazz, uint16_t type,
			  const void *rdata, size_t size, AvahiLookupResultFlags flags, void *userdata)
{
  char address[INET_ADDRSTRLEN];
  struct in_addr addr;
  struct mdns_record_browser *rb_data;
  int ll;

  rb_data = (struct mdns_record_browser *)userdata;

  switch (event)
    {
      case AVAHI_BROWSER_NEW:
	if (size != sizeof(addr.s_addr))
	  {
	    DPRINTF(E_WARN, L_MDNS, "Got RR type A size %ld (should be %ld)\n", (long)size, (long)sizeof(addr.s_addr));

	    return;
	  }

	memcpy(&addr.s_addr, rdata, sizeof(addr.s_addr));

	ll = is_v4ll(&addr);
	if (ll && !(rb_data->mb->flags & MDNS_WANT_V4LL))
	  {
	    DPRINTF(E_DBG, L_MDNS, "Discarding IPv4 LL, not interested (service %s)\n", rb_data->name);
	    return;
	  }
	else if (!ll && !(rb_data->mb->flags & MDNS_WANT_V4))
	  {
	    DPRINTF(E_DBG, L_MDNS, "Discarding IPv4, not interested (service %s)\n", rb_data->name);
	    return;
	  }

	if (!inet_ntop(AF_INET, &addr.s_addr, address, sizeof(address)))
	  {
	    DPRINTF(E_LOG, L_MDNS, "Could not print IPv4 address: %s\n", strerror(errno));

	    return;
	  }

	DPRINTF(E_DBG, L_MDNS, "Service %s, hostname %s resolved to %s\n", rb_data->name, hostname, address);

	/* Execute callback (mb->cb) with all the data */
	rb_data->mb->cb(rb_data->name, rb_data->mb->type, rb_data->domain, hostname, AF_INET, address, rb_data->port, &rb_data->txt_kv);
	/* Got a suitable address, stop record browser */
	break;

      case AVAHI_BROWSER_REMOVE:
	/* Not handled - record browser lifetime too short for this to happen */
	return;

      case AVAHI_BROWSER_CACHE_EXHAUSTED:
      case AVAHI_BROWSER_ALL_FOR_NOW:
	DPRINTF(E_DBG, L_MDNS, "Avahi Record Browser (%s v4): no more results (%s)\n", hostname,
		(event == AVAHI_BROWSER_CACHE_EXHAUSTED) ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");	

	break;

      case AVAHI_BROWSER_FAILURE:
	DPRINTF(E_LOG, L_MDNS, "Avahi Record Browser (%s v4) failure: %s\n", hostname,
		avahi_strerror(avahi_client_errno(avahi_record_browser_get_client(b))));

	break;
    }

  keyval_clear(&rb_data->txt_kv);      
  free(rb_data->name);
  free(rb_data->domain);
  free(rb_data);

  avahi_record_browser_free(b);
}

static void
browse_record_callback_v6(AvahiRecordBrowser *b, AvahiIfIndex intf, AvahiProtocol proto,
			  AvahiBrowserEvent event, const char *hostname, uint16_t clazz, uint16_t type,
			  const void *rdata, size_t size, AvahiLookupResultFlags flags, void *userdata)
{
  char address[INET6_ADDRSTRLEN + IF_NAMESIZE + 1];
  char ifname[IF_NAMESIZE];
  struct in6_addr addr;
  struct mdns_record_browser *rb_data;
  int ll;
  int len;
  int ret;

  rb_data = (struct mdns_record_browser *)userdata;

  switch (event)
    {
      case AVAHI_BROWSER_NEW:
	if (size != sizeof(addr.s6_addr))
	  {
	    DPRINTF(E_WARN, L_MDNS, "Got RR type AAAA size %ld (should be %ld)\n", (long)size, (long)sizeof(addr.s6_addr));

	    return;
	  }

	memcpy(&addr.s6_addr, rdata, sizeof(addr.s6_addr));

	ll = is_v6ll(&addr);
	if (ll && !(rb_data->mb->flags & MDNS_WANT_V6LL))
	  {
	    DPRINTF(E_DBG, L_MDNS, "Discarding IPv6 LL, not interested (service %s)\n", rb_data->name);
	    return;
	  }
	else if (!ll && !(rb_data->mb->flags & MDNS_WANT_V6))
	  {
	    DPRINTF(E_DBG, L_MDNS, "Discarding IPv6, not interested (service %s)\n", rb_data->name);
	    return;
	  }

	if (!inet_ntop(AF_INET6, &addr.s6_addr, address, sizeof(address)))
	  {
	    DPRINTF(E_LOG, L_MDNS, "Could not print IPv6 address: %s\n", strerror(errno));

	    return;
	  }

	if (ll)
	  {
	    if (!if_indextoname(intf, ifname))
	      {
		DPRINTF(E_LOG, L_MDNS, "Could not map interface index %d to a name\n", intf);

		return;
	      }

	    len = strlen(address);
	    ret = snprintf(address + len, sizeof(address) - len, "%%%s", ifname);
	    if ((ret < 0) || (ret > sizeof(address) - len))
	      {
		DPRINTF(E_LOG, L_MDNS, "Buffer too short for scoped IPv6 LL\n");

		return;
	      }
	  }

	DPRINTF(E_DBG, L_MDNS, "Service %s, hostname %s resolved to %s\n", rb_data->name, hostname, address);

	/* Execute callback (mb->cb) with all the data */
	rb_data->mb->cb(rb_data->name, rb_data->mb->type, rb_data->domain, hostname, AF_INET6, address, rb_data->port, &rb_data->txt_kv);
	/* Got a suitable address, stop record browser */
	break;

      case AVAHI_BROWSER_REMOVE:
	/* Not handled - record browser lifetime too short for this to happen */
	return;

      case AVAHI_BROWSER_CACHE_EXHAUSTED:
      case AVAHI_BROWSER_ALL_FOR_NOW:
	DPRINTF(E_DBG, L_MDNS, "Avahi Record Browser (%s v6): no more results (%s)\n", hostname,
		(event == AVAHI_BROWSER_CACHE_EXHAUSTED) ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");	

	break;

      case AVAHI_BROWSER_FAILURE:
	DPRINTF(E_LOG, L_MDNS, "Avahi Record Browser (%s v6) failure: %s\n", hostname,
		avahi_strerror(avahi_client_errno(avahi_record_browser_get_client(b))));

	break;
    }

  /* Cleanup when done/error */
  keyval_clear(&rb_data->txt_kv);      
  free(rb_data->name);
  free(rb_data->domain);
  free(rb_data);

  avahi_record_browser_free(b);
}

static int
spawn_record_browser(AvahiClient *c, AvahiIfIndex intf, AvahiProtocol proto, const char *hostname, const char *domain,
		     uint16_t type, struct mdns_browser *mb, const char *name, uint16_t port, AvahiStringList *txt)
{
  AvahiRecordBrowser *rb;
  struct mdns_record_browser *rb_data;
  char *key;
  char *value;
  char *ptr;
  size_t len;
  int ret;

  rb_data = (struct mdns_record_browser *)malloc(sizeof(struct mdns_record_browser));
  if (!rb_data)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory for record browser data\n");

      return -1;
    }

  memset(rb_data, 0, sizeof(struct mdns_record_browser));

  rb_data->mb = mb;
  rb_data->port = port;

  rb_data->name = strdup(name);
  if (!rb_data->name)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory for service name\n");

      goto out_free_rb;
    }

  rb_data->domain = strdup(domain);
  if (!rb_data->domain)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory for service domain\n");

      goto out_free_name;
    }

  while (txt)
    {
      len = avahi_string_list_get_size(txt);
      key = (char *)avahi_string_list_get_text(txt);

      ptr = memchr(key, '=', len);
      if (!ptr)
	{
	  value = "";
	  len = 0;
	}
      else
	{
	  *ptr = '\0';
	  value = ptr + 1;

	  len -= strlen(key) + 1;
	}

      ret = keyval_add_size(&rb_data->txt_kv, key, value, len);

      if (ptr)
	*ptr = '=';

      if (ret < 0)
	{
	  DPRINTF(E_LOG, L_MDNS, "Could not build TXT record keyval\n");

	  goto out_free_keyval;
	}

      txt = avahi_string_list_get_next(txt);
    }

  rb = NULL;
  switch (type)
    {
      case AVAHI_DNS_TYPE_A:
	rb = avahi_record_browser_new(c, intf, proto, hostname,
				      AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_A, 0,
				      browse_record_callback_v4, rb_data);
	if (!rb)
	  DPRINTF(E_LOG, L_MDNS, "Could not create v4 record browser for host %s: %s\n",
		  hostname, avahi_strerror(avahi_client_errno(c)));
	break;

      case AVAHI_DNS_TYPE_AAAA:
	rb = avahi_record_browser_new(c, intf, proto, hostname,
				      AVAHI_DNS_CLASS_IN, AVAHI_DNS_TYPE_AAAA, 0,
				      browse_record_callback_v6, rb_data);
	if (!rb)
	  DPRINTF(E_LOG, L_MDNS, "Could not create v4 record browser for host %s: %s\n",
		  hostname, avahi_strerror(avahi_client_errno(c)));
	break;
    }

  if (!rb)
    goto out_free_keyval;

  return 0;

 out_free_keyval:
  keyval_clear(&rb_data->txt_kv);
  free(rb_data->domain);
 out_free_name:
  free(rb_data->name);
 out_free_rb:
  free(rb_data);

  return -1;
}

static void
browse_resolve_callback(AvahiServiceResolver *r, AvahiIfIndex intf, AvahiProtocol proto, AvahiResolverEvent event,
			const char *name, const char *type, const char *domain, const char *hostname, const AvahiAddress *addr,
			uint16_t port, AvahiStringList *txt, AvahiLookupResultFlags flags, void *userdata)
{
  AvahiClient *c;
  struct mdns_browser *mb;
  int ret;

  mb = (struct mdns_browser *)userdata;

  switch (event)
    {
      case AVAHI_RESOLVER_FAILURE:
	DPRINTF(E_LOG, L_MDNS, "Avahi Resolver failure: service '%s' type '%s': %s\n", name, type,
		avahi_strerror(avahi_client_errno(mdns_client)));
	break;

      case AVAHI_RESOLVER_FOUND:
	DPRINTF(E_DBG, L_MDNS, "Avahi Resolver: resolved service '%s' type '%s' proto %d\n", name, type, proto);

	c = avahi_service_resolver_get_client(r);

	if (mb->flags & (MDNS_WANT_V4 | MDNS_WANT_V4LL))
	  {
	    ret = spawn_record_browser(c, intf, proto, hostname, domain,
				       AVAHI_DNS_TYPE_A, mb, name, port, txt);
	    if (ret < 0)
	      DPRINTF(E_LOG, L_MDNS, "Failed to create record browser for type A\n");
	  }

	if (mb->flags & (MDNS_WANT_V6 | MDNS_WANT_V6LL))
	  {
	    ret = spawn_record_browser(c, intf, proto, hostname, domain,
				       AVAHI_DNS_TYPE_AAAA, mb, name, port, txt);
	    if (ret < 0)
	      DPRINTF(E_LOG, L_MDNS, "Failed to create record browser for type A\n");
	  }


	break;
    }

  avahi_service_resolver_free(r);

  return;
}

static void
browse_callback(AvahiServiceBrowser *b, AvahiIfIndex intf, AvahiProtocol proto, AvahiBrowserEvent event,
		const char *name, const char *type, const char *domain, AvahiLookupResultFlags flags, void *userdata)
{
  struct mdns_browser *mb;
  AvahiServiceResolver *res;
  int family;

  mb = (struct mdns_browser *)userdata;

  switch (event)
    {
      case AVAHI_BROWSER_FAILURE:
	DPRINTF(E_LOG, L_MDNS, "Avahi Browser failure: %s\n",
		avahi_strerror(avahi_client_errno(mdns_client)));

	avahi_service_browser_free(b);

	b = avahi_service_browser_new(mdns_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, mb->type, NULL, 0, browse_callback, mb);
	if (!b)
	  {
	    DPRINTF(E_LOG, L_MDNS, "Failed to recreate service browser (service type %s): %s\n", mb->type,
		    avahi_strerror(avahi_client_errno(mdns_client)));
	  }
	return;

      case AVAHI_BROWSER_NEW:
	DPRINTF(E_DBG, L_MDNS, "Avahi Browser: NEW service '%s' type '%s' proto %d\n", name, type, proto);

	res = avahi_service_resolver_new(mdns_client, intf, proto, name, type, domain, proto, 0, browse_resolve_callback, mb);
	if (!res)
	  DPRINTF(E_LOG, L_MDNS, "Failed to create service resolver: %s\n",
		  avahi_strerror(avahi_client_errno(mdns_client)));
	break;

      case AVAHI_BROWSER_REMOVE:
	DPRINTF(E_DBG, L_MDNS, "Avahi Browser: REMOVE service '%s' type '%s' proto %d\n", name, type, proto);

	switch (proto)
	  {
	    case AVAHI_PROTO_INET:
	      family = AF_INET;
	      break;

	    case AVAHI_PROTO_INET6:
	      family = AF_INET6;
	      break;

	    default:
	      DPRINTF(E_INFO, L_MDNS, "Avahi Browser: unknown protocol %d\n", proto);

	      family = AF_UNSPEC;
	      break;
	  }

	if (family != AF_UNSPEC)
	  mb->cb(name, type, domain, NULL, family, NULL, -1, NULL);
	break;

      case AVAHI_BROWSER_ALL_FOR_NOW:
      case AVAHI_BROWSER_CACHE_EXHAUSTED:
	DPRINTF(E_DBG, L_MDNS, "Avahi Browser (%s): no more results (%s)\n", mb->type,
		(event == AVAHI_BROWSER_CACHE_EXHAUSTED) ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
	break;
    }
}


static void
entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata)
{
  if (!g || (g != mdns_group))
    return;

  switch (state)
    {
      case AVAHI_ENTRY_GROUP_ESTABLISHED:
        DPRINTF(E_DBG, L_MDNS, "Successfully added mDNS services\n");
        break;

      case AVAHI_ENTRY_GROUP_COLLISION:
        DPRINTF(E_DBG, L_MDNS, "Group collision\n");
        break;

      case AVAHI_ENTRY_GROUP_FAILURE:
        DPRINTF(E_DBG, L_MDNS, "Group failure\n");
        break;

      case AVAHI_ENTRY_GROUP_UNCOMMITED:
        DPRINTF(E_DBG, L_MDNS, "Group uncommitted\n");
	break;

      case AVAHI_ENTRY_GROUP_REGISTERING:
        DPRINTF(E_DBG, L_MDNS, "Group registering\n");
        break;
    }
}

static void
_create_services(void)
{
  struct mdns_group_entry *pentry;
  int ret;

  DPRINTF(E_DBG, L_MDNS, "Creating service group\n");

  if (!group_entries)
    {
      DPRINTF(E_DBG, L_MDNS, "No entries yet... skipping service create\n");

      return;
    }

    if (mdns_group == NULL)
      {
        mdns_group = avahi_entry_group_new(mdns_client, entry_group_callback, NULL);
	if (!mdns_group)
	  {
            DPRINTF(E_WARN, L_MDNS, "Could not create Avahi EntryGroup: %s\n",
                    avahi_strerror(avahi_client_errno(mdns_client)));

            return;
	  }
      }

    pentry = group_entries;
    while (pentry)
      {
        DPRINTF(E_DBG, L_MDNS, "Re-registering %s/%s\n", pentry->name, pentry->type);

        ret = avahi_entry_group_add_service_strlst(mdns_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0,
						   pentry->name, pentry->type,
						   NULL, NULL, pentry->port, pentry->txt);
	if (ret < 0)
	  {
	    DPRINTF(E_WARN, L_MDNS, "Could not add mDNS services: %s\n", avahi_strerror(ret));

	    return;
	  }

	pentry = pentry->next;
      }

    ret = avahi_entry_group_commit(mdns_group);
    if (ret < 0)
      {
	DPRINTF(E_WARN, L_MDNS, "Could not commit mDNS services: %s\n",
		avahi_strerror(avahi_client_errno(mdns_client)));
      }
}

static void
client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata)
{
  struct mdns_browser *mb;
  AvahiServiceBrowser *b;
  int error;

  switch (state)
    {
      case AVAHI_CLIENT_S_RUNNING:
        DPRINTF(E_LOG, L_MDNS, "Avahi state change: Client running\n");
        if (!mdns_group)
	  _create_services();

	for (mb = browser_list; mb; mb = mb->next)
	  {
	    b = avahi_service_browser_new(mdns_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, mb->type, NULL, 0, browse_callback, mb);
	    if (!b)
	      {
		DPRINTF(E_LOG, L_MDNS, "Failed to recreate service browser (service type %s): %s\n", mb->type,
			avahi_strerror(avahi_client_errno(mdns_client)));
	      }
	  }
        break;

      case AVAHI_CLIENT_S_COLLISION:
        DPRINTF(E_LOG, L_MDNS, "Avahi state change: Client collision\n");
        if(mdns_group)
	  avahi_entry_group_reset(mdns_group);
        break;

      case AVAHI_CLIENT_FAILURE:
        DPRINTF(E_LOG, L_MDNS, "Avahi state change: Client failure\n");

	error = avahi_client_errno(c);
	if (error == AVAHI_ERR_DISCONNECTED)
	  {
	    DPRINTF(E_LOG, L_MDNS, "Avahi Server disconnected, reconnecting\n");

	    avahi_client_free(mdns_client);
	    mdns_group = NULL;

	    mdns_client = avahi_client_new(&ev_poll_api, AVAHI_CLIENT_NO_FAIL,
					   client_callback, NULL, &error);
	    if (!mdns_client)
	      {
		DPRINTF(E_LOG, L_MDNS, "Failed to create new Avahi client: %s\n",
			avahi_strerror(error));
	      }
	  }
	else
	  {
	    DPRINTF(E_LOG, L_MDNS, "Avahi client failure: %s\n", avahi_strerror(error));
	  }
        break;

      case AVAHI_CLIENT_S_REGISTERING:
        DPRINTF(E_LOG, L_MDNS, "Avahi state change: Client registering\n");
        if (mdns_group)
	  avahi_entry_group_reset(mdns_group);
        break;

      case AVAHI_CLIENT_CONNECTING:
        DPRINTF(E_LOG, L_MDNS, "Avahi state change: Client connecting\n");
        break;
    }
}


/* mDNS interface - to be called only from the main thread */

int
mdns_init(void)
{
  int error;

  DPRINTF(E_DBG, L_MDNS, "Initializing Avahi mDNS\n");

  all_w = NULL;
  all_t = NULL;
  group_entries = NULL;
  browser_list = NULL;

  mdns_client = avahi_client_new(&ev_poll_api, AVAHI_CLIENT_NO_FAIL,
				 client_callback, NULL, &error);
  if (!mdns_client)
    {
      DPRINTF(E_WARN, L_MDNS, "mdns_init: Could not create Avahi client: %s\n",
	      avahi_strerror(avahi_client_errno(mdns_client)));

      return -1;
    }

  return 0;
}

void
mdns_deinit(void)
{
  struct mdns_group_entry *ge;
  struct mdns_browser *mb;
  AvahiWatch *w;
  AvahiTimeout *t;

  for (t = all_t; t; t = t->next)
    if (t->ev)
      {
	event_del(t->ev);
#ifdef HAVE_LIBEVENT2
	event_free(t->ev);
#else
	free(t->ev);
#endif
	t->ev = NULL;
      }

  for (w = all_w; w; w = w->next)
    if (w->ev)
      {
	event_del(w->ev);
#ifdef HAVE_LIBEVENT2
	event_free(w->ev);
#else
	free(w->ev);
#endif
	w->ev = NULL;
      }

  for (ge = group_entries; group_entries; ge = group_entries)
    {
      group_entries = ge->next;

      free(ge->name);
      free(ge->type);
      avahi_string_list_free(ge->txt);

      free(ge);
    }

  for (mb = browser_list; browser_list; mb = browser_list)
    {
      browser_list = mb->next;

      free(mb->type);
      free(mb);
    }

  if (mdns_client)
    avahi_client_free(mdns_client);
}

int
mdns_register(char *name, char *type, int port, char **txt)
{
  struct mdns_group_entry *ge;
  AvahiStringList *txt_sl;
  int i;

  DPRINTF(E_DBG, L_MDNS, "Adding mDNS service %s/%s\n", name, type);

  ge = (struct mdns_group_entry *)malloc(sizeof(struct mdns_group_entry));
  if (!ge)
    return -1;

  ge->name = strdup(name);
  ge->type = strdup(type);
  ge->port = port;

  txt_sl = NULL;
  for (i = 0; txt[i]; i++)
    {
      txt_sl = avahi_string_list_add(txt_sl, txt[i]);

      DPRINTF(E_DBG, L_MDNS, "Added key %s\n", txt[i]);
    }

  ge->txt = txt_sl;

  ge->next = group_entries;
  group_entries = ge;

  if (mdns_group)
    {
      DPRINTF(E_DBG, L_MDNS, "Resetting mDNS group\n");
      avahi_entry_group_reset(mdns_group);
    }

  DPRINTF(E_DBG, L_MDNS, "Creating service group\n");
  _create_services();

  return 0;
}

int
mdns_browse(char *type, int flags, mdns_browse_cb cb)
{
  struct mdns_browser *mb;
  AvahiServiceBrowser *b;

  DPRINTF(E_DBG, L_MDNS, "Adding service browser for type %s\n", type);

  mb = (struct mdns_browser *)malloc(sizeof(struct mdns_browser));
  if (!mb)
    return -1;

  mb->type = strdup(type);
  mb->cb = cb;

  mb->flags = (flags) ? flags : MDNS_WANT_DEFAULT;

  mb->next = browser_list;
  browser_list = mb;

  b = avahi_service_browser_new(mdns_client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, type, NULL, 0, browse_callback, mb);
  if (!b)
    {
      DPRINTF(E_LOG, L_MDNS, "Failed to create service browser: %s\n",
	      avahi_strerror(avahi_client_errno(mdns_client)));

      browser_list = mb->next;
      free(mb->type);
      free(mb);

      return -1;
    }

  return 0;
}
