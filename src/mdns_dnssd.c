/*
 * Bonjour mDNS backend, with libevent polling
 *
 * Copyright (c) Scott Shambarger <devel@shambarger.net>
 * Copyright (C) 2009-2011 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2005 Sebastian Dr�ge <slomo@ubuntu.com>
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

#include "mdns.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>

#include <event2/event.h>

#include <dns_sd.h>

/* timeout for service resolves */
#define MDNS_RESOLVE_TIMEOUT_SECS 5

// Hack for FreeBSD, don't want to bother with sysconf()
#ifndef HOST_NAME_MAX
# include <limits.h>
# define HOST_NAME_MAX _POSIX_HOST_NAME_MAX
#endif

#include "logger.h"

/* Main event base, from main.c */
extern struct event_base *evbase_main;

static DNSServiceRef mdns_sdref_main;
static struct event *mdns_ev_main;

/* registered services last the life of the program */
struct mdns_service {
  struct mdns_service *next;
  /* allocated */
  DNSServiceRef sdref;
  TXTRecordRef txtRecord;
};

static struct mdns_service *mdns_services = NULL;

/* we keep records forever to display names in logs when
   registered or renamed */
struct mdns_record {
  struct mdns_record *next;
  /* allocated */
  char *name;
  /* references */
  DNSRecordRef recRef;
  uint16_t rrtype;
};

static struct mdns_record *mdns_records = NULL;

struct mdns_addr_lookup {
  struct mdns_addr_lookup *next;
  /* allocated */
  DNSServiceRef sdref;
  struct keyval txt_kv;
  /* references */
  u_int16_t port;
  struct mdns_resolver *rs;
};

/* resolvers and address lookups clean themselves up */
struct mdns_resolver
{
  struct mdns_resolver *next;
  /* allocated */
  DNSServiceRef sdref;
  char *service;
  char *regtype;
  char *domain;
  struct event *timer;
  struct mdns_addr_lookup *lookups;
  /* references */
  void *uuid;
  uint32_t interface;
  struct mdns_browser *mb;
};

/* browsers keep running for the life of the program */
struct mdns_browser
{
  struct mdns_browser *next;
  /* allocated */
  DNSServiceRef sdref;
  struct mdns_resolver *resolvers;
  char *regtype;
  /* references */
  enum mdns_options flags;
  mdns_browse_cb cb;
  DNSServiceProtocol protocol;
  void *res_uuid;
};

static struct mdns_browser *mdns_browsers = NULL;

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
  return ((((addr->s6_addr[0] << 8) | addr->s6_addr[1]) & IPV6LL_NETMASK)
          == IPV6LL_NETWORK);
}

/* mDNS interface - to be called only from the main thread */

static int
mdns_service_free(struct mdns_service *s)
{
  if(! s)
    return -1;

  /* free sdref, then everything else */
  if(s->sdref)
    DNSServiceRefDeallocate(s->sdref);
  TXTRecordDeallocate(&(s->txtRecord));
  free(s);

  return -1;
}

static int
mdns_addr_lookup_free(struct mdns_addr_lookup *lu)
{
  if (! lu)
    return -1;

  if(lu->sdref)
    DNSServiceRefDeallocate(lu->sdref);
  keyval_clear(&lu->txt_kv);
  free(lu);

  return -1;
}

static int
mdns_resolver_free(struct mdns_resolver *rs) {

  struct mdns_addr_lookup *lu;

  if (! rs)
    return -1;

  /* free/cancel all lookups */
  for(lu = rs->lookups; lu; lu = rs->lookups)
    {
      rs->lookups = lu->next;
      mdns_addr_lookup_free(lu);
    }

  if(rs->timer)
    event_free(rs->timer);
  if(rs->sdref)
    DNSServiceRefDeallocate(rs->sdref);
  free(rs->service);
  free(rs->regtype);
  free(rs->domain);
  free(rs);

  return -1;
}

static int
mdns_browser_free(struct mdns_browser *mb)
{
  struct mdns_resolver *rs;

  if(! mb)
    return -1;

  /* free all resolvers */
  for(rs = mb->resolvers; rs; rs = mb->resolvers)
    {
      mb->resolvers = rs->next;
      mdns_resolver_free(rs);
    }
  /* free sdref, then everything else */
  if(mb->sdref)
    DNSServiceRefDeallocate(mb->sdref);
  free(mb->regtype);
  free(mb);

  return -1;
}

static int
mdns_record_free(struct mdns_record *r)
{
  if (! r)
    return -1;

  free(r->name);
  free(r);

  return -1;
}

static int
mdns_main_free(void)
{
  struct mdns_service *s;
  struct mdns_browser *mb;
  struct mdns_record *r;

  for(s = mdns_services; mdns_services; s = mdns_services)
    {
      mdns_services = s->next;
      mdns_service_free(s);
    }

  for (mb = mdns_browsers; mdns_browsers; mb = mdns_browsers)
    {
      mdns_browsers = mb->next;
      mdns_browser_free(mb);
    }

  for (r = mdns_records; mdns_records; r = mdns_records)
    {
      mdns_records = r->next;
      mdns_record_free(r);
    }

  if(mdns_ev_main)
    event_free(mdns_ev_main);
  mdns_ev_main = NULL;
  if(mdns_sdref_main)
    DNSServiceRefDeallocate(mdns_sdref_main);
  mdns_sdref_main = NULL;

  return -1;
}

void
mdns_deinit(void)
{
  mdns_main_free();
}

static void
mdns_event_cb(evutil_socket_t fd, short flags, void *data) {

  DNSServiceErrorType err;

  err = DNSServiceProcessResult(mdns_sdref_main);

  if (err != kDNSServiceErr_NoError)
    DPRINTF(E_LOG, L_MDNS, "DNSServiceProcessResult error %d\n", err);
}

int
mdns_init(void)
{
  DNSServiceErrorType err;
  int fd;
  int ret;

  DPRINTF(E_DBG, L_MDNS, "Initializing DNS_SD mDNS\n");

  mdns_services = NULL;
  mdns_browsers = NULL;
  mdns_records = NULL;
  mdns_sdref_main = NULL;
  mdns_ev_main = NULL;

  err = DNSServiceCreateConnection(&mdns_sdref_main);
  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not create mDNS connection\n");
      return -1;
    }

  fd = DNSServiceRefSockFD(mdns_sdref_main);
  if (fd == -1) {
      DPRINTF(E_LOG, L_MDNS, "DNSServiceRefSockFD failed\n");
      return mdns_main_free();
  }
  mdns_ev_main = event_new(evbase_main, fd, EV_PERSIST | EV_READ,
                           mdns_event_cb, NULL);
  if (! mdns_ev_main)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not make new event in mdns\n");
      return mdns_main_free();
    }

  ret = event_add(mdns_ev_main, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not add new event in mdns\n");
      return mdns_main_free();
    }

  return 0;
}

static void
mdns_register_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                       DNSServiceErrorType errorCode, const char *name,
                       const char *regtype, const char *domain,
                       void *context) {

  switch (errorCode) {
    case kDNSServiceErr_NoError:
      DPRINTF(E_DBG, L_MDNS, "Successfully added mDNS service '%s.%s'\n",
              name, regtype);
      break;

    case kDNSServiceErr_NameConflict:
      DPRINTF(E_DBG, L_MDNS,
              "Name collision for service '%s.%s' - automatically assigning new name\n",
              name, regtype);
      break;

    case kDNSServiceErr_NoMemory:
      DPRINTF(E_DBG, L_MDNS, "Out of memory registering service %s\n", name);
      break;

    default:
      DPRINTF(E_DBG, L_MDNS, "Unspecified error registering service %s, error %d\n",
              name, errorCode);
  }
}

int
mdns_register(char *name, char *regtype, int port, char **txt)
{
  struct mdns_service *s;
  DNSServiceErrorType err;
  int i;
  char *eq;

  DPRINTF(E_DBG, L_MDNS, "Adding mDNS service '%s.%s'\n", name, regtype);

  s = calloc(1, sizeof(*s));
  if (!s)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory registering service.\n");
      return -1;
    }
  TXTRecordCreate(&(s->txtRecord), 0, NULL);

  for (i = 0; txt && txt[i]; i++)
    {
      if ((eq = strchr(txt[i], '=')))
        {
          *eq = '\0';
          eq++;
          err = TXTRecordSetValue(&(s->txtRecord), txt[i], strlen(eq) * sizeof(char), eq);
          *(--eq) = '=';
          if (err != kDNSServiceErr_NoError)
            {
              DPRINTF(E_LOG, L_MDNS, "Could not set TXT record value\n");
              return mdns_service_free(s);
            }
        }
    }

  s->sdref = mdns_sdref_main;
  err = DNSServiceRegister(&(s->sdref), kDNSServiceFlagsShareConnection, 0,
                           name, regtype, NULL, NULL, htons(port),
                           TXTRecordGetLength(&(s->txtRecord)),
                           TXTRecordGetBytesPtr(&(s->txtRecord)),
                           mdns_register_callback, NULL);

  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Error registering service '%s.%s'\n",
              name, regtype);
      s->sdref = NULL;
      return mdns_service_free(s);
    }

  s->next = mdns_services;
  mdns_services = s;

  return 0;
}

static void
mdns_record_callback(DNSServiceRef sdRef, DNSRecordRef RecordRef,
                     DNSServiceFlags flags, DNSServiceErrorType errorCode,
                     void *context)
{
  struct mdns_record *r;

  r = context;

  switch (errorCode) {
    case kDNSServiceErr_NoError:
      DPRINTF(E_DBG, L_MDNS, "Successfully added mDNS record %s\n", r->name);
      break;

    case kDNSServiceErr_NameConflict:
      DPRINTF(E_DBG, L_MDNS, "Record ame collision - automatically assigning new name\n");
      break;

    case kDNSServiceErr_NoMemory:
      DPRINTF(E_DBG, L_MDNS, "Out of memory registering record %s\n", r->name);
      break;

    default:
      DPRINTF(E_DBG, L_MDNS, "Unspecified error registering record %s, error %d\n",
              r->name, errorCode);
  }

}

static int
mdns_register_record(uint16_t rrtype, const char *name, uint16_t rdlen,
                     const void *rdata)
{
  struct mdns_record *r;
  DNSServiceErrorType err;

  DPRINTF(E_DBG, L_MDNS, "Adding mDNS record %s/%u\n", name, rrtype);

  r = calloc(1, sizeof(*r));
  if (!r)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory adding record.\n");
      return -1;
    }
  r->name = strdup(name);
  if (!(r->name))
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory adding record.\n");
      return mdns_record_free(r);
    }

  r->rrtype = rrtype;

  err = DNSServiceRegisterRecord(mdns_sdref_main, &r->recRef,
                                 kDNSServiceFlagsShared, 0, r->name,
                                 r->rrtype, kDNSServiceClass_IN, rdlen, rdata,
                                 0, mdns_record_callback, r);

  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Error registering record %s, error %d\n", name,
              err);
      return mdns_record_free(r);
    }

  /* keep these around so we can display r->name in the callback */
  r->next = mdns_records;
  mdns_records = r;

  return 0;
}

int
mdns_cname(char *name)
{
  char hostname[HOST_NAME_MAX + 1];
  // Includes room for "..local" and 0-terminator
  char rdata[HOST_NAME_MAX + 8];
  int count;
  int i;
  int ret;

  ret = gethostname(hostname, HOST_NAME_MAX);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not add CNAME %s, gethostname failed\n",
              name);
      return -1;
    }
  // Note, gethostname does not guarantee 0-termination
  hostname[HOST_NAME_MAX] = 0;

  ret = snprintf(rdata, sizeof(rdata), ".%s.local", hostname);
  if (!(ret > 0 && ret < sizeof(rdata)))
    {
      DPRINTF(E_LOG, L_MDNS, "Could not add CNAME %s, hostname is invalid\n",
              name);
      return -1;
    }

  // Convert to dns string: .forked-daapd.local -> \12forked-daapd\6local
  count = 0;
  for (i = ret - 1; i >= 0; i--)
    {
      if (rdata[i] == '.')
        {
          rdata[i] = count;
          count = 0;
        }
      else
        count++;
    }

  return mdns_register_record(kDNSServiceType_CNAME, name, (uint16_t)ret,
                              rdata);
}

static void
mdns_browse_call_cb(struct mdns_addr_lookup *lu, const char *hostname,
                    const struct sockaddr *address)
{
  char addr_str[INET6_ADDRSTRLEN];

  if (address->sa_family == AF_INET)
    {
      struct sockaddr_in *addr = (struct sockaddr_in *)address;

      if (!inet_ntop(AF_INET, &addr->sin_addr, addr_str, sizeof(addr_str)))
        {
          DPRINTF(E_LOG, L_MDNS, "Could not print IPv4 address: %s\n",
                  strerror(errno));
          return;
        }

      if (!(lu->rs->mb->protocol & kDNSServiceProtocol_IPv4))
        {
          DPRINTF(E_DBG, L_MDNS,
                  "Discarding IPv4, not interested (service %s)\n",
                  lu->rs->service);
          return;
        }
      else if (is_v4ll(&addr->sin_addr))
        {
          DPRINTF(E_WARN, L_MDNS,
                  "Ignoring announcement from %s, address %s is link-local\n",
                  hostname, addr_str);
          return;
        }

    }
  else if (address->sa_family == AF_INET6)
    {
      struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)address;

      if (!inet_ntop(AF_INET6, &addr6->sin6_addr, addr_str, sizeof(addr_str)))
        {
          DPRINTF(E_LOG, L_MDNS, "Could not print IPv6 address: %s\n",
                  strerror(errno));
          return;
        }

      if (!(lu->rs->mb->protocol & kDNSServiceProtocol_IPv6))
        {
          DPRINTF(E_DBG, L_MDNS,
                  "Discarding IPv6, not interested (service %s)\n",
                  lu->rs->service);
          return;
        }
      else if (is_v6ll(&addr6->sin6_addr))
        {
          DPRINTF(E_WARN, L_MDNS,
                  "Ignoring announcement from %s, address %s is link-local\n",
                  hostname, addr_str);
          return;
        }
    }

  DPRINTF(E_DBG, L_MDNS, "Service %s, hostname %s resolved to %s\n",
          lu->rs->service, hostname, addr_str);

  /* Execute callback (mb->cb) with all the data */
  lu->rs->mb->cb(lu->rs->service, lu->rs->regtype, lu->rs->domain, hostname,
                 address->sa_family, addr_str, lu->port, &lu->txt_kv);
}

static void
mdns_lookup_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                     uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                     const char *hostname, const struct sockaddr *address,
                     uint32_t ttl, void *context)
{
  struct mdns_addr_lookup *lu;

  lu = context;

  if (errorCode != kDNSServiceErr_NoError )
    {
      DPRINTF(E_LOG, L_MDNS, "Error resolving hostname '%s', error %d\n",
              hostname, errorCode);
      return;
    }

  if (flags & kDNSServiceFlagsAdd)
        mdns_browse_call_cb(lu, hostname, address);
}

static int
mdns_addr_lookup_start(struct mdns_resolver *rs, uint32_t interfaceIndex,
                       const char *hosttarget, uint16_t port, uint16_t txtLen,
                       const unsigned char *txtRecord)
{
  struct mdns_addr_lookup *lu;
  DNSServiceErrorType err;
  char key[256];
  int i;
  uint8_t valueLen;
  const char *value;
  int ret;

  lu = calloc(1, sizeof(*lu));
  if (!lu)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating address lookup.\n");
      return -1;
    }
  lu->port = port;
  lu->rs = rs;

  for (i=0; TXTRecordGetItemAtIndex(txtLen, txtRecord, i, sizeof(key),
                                    key, &valueLen, (const void **)&value)
         != kDNSServiceErr_Invalid; i++)
    {
      ret = keyval_add_size(&lu->txt_kv, key, value, valueLen);
      if (ret < 0)
        {
          DPRINTF(E_LOG, L_MDNS, "Could not build TXT record keyval\n");
          return mdns_addr_lookup_free(lu);
        }
    }

  lu->sdref = mdns_sdref_main;
  err = DNSServiceGetAddrInfo(&lu->sdref, kDNSServiceFlagsShareConnection,
                              interfaceIndex, rs->mb->protocol, hosttarget,
                              mdns_lookup_callback, lu);
  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Failed to create service resolver.\n");
      lu->sdref = NULL;
      return mdns_addr_lookup_free(lu);
    }

  /* resolver now owns the lookup */
  lu->next = rs->lookups;
  rs->lookups = lu;

  return 0;
}

static void
mdns_resolver_remove(struct mdns_resolver *rs)
{
  struct mdns_resolver *cur;

  /* remove from browser's resolver list */
  if(rs->mb->resolvers == rs)
    rs->mb->resolvers = rs->next;
  else
    {
      for(cur = rs->mb->resolvers; cur; cur = cur->next)
        if (cur->next == rs)
          {
            cur->next = rs->next;
            break;
          }
    }

  /* free resolver (which cancels resolve) */
  mdns_resolver_free(rs);
}

static void
mdns_resolve_timeout_cb(evutil_socket_t fd, short flags, void *uuid) {

  struct mdns_browser *mb;
  struct mdns_resolver *rs = NULL;

  for(mb = mdns_browsers; mb && !rs; mb = mb->next)
    for(rs = mb->resolvers; rs; rs = rs->next)
      if(rs->uuid == uuid)
        {
          DPRINTF(E_DBG, L_MDNS,
                  "Resolve finished for '%s' type '%s' interface %d\n",
                  rs->service, rs->regtype, rs->interface);
          mdns_resolver_remove(rs);
          break;
        }
}

static void
mdns_resolve_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                      uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                      const char *fullname, const char *hosttarget,
                      uint16_t port, uint16_t txtLen,
                      const unsigned char *txtRecord, void *context)
{
  struct mdns_resolver *rs;

  rs = context;

  /* convert port to host order */
  port = ntohs(port);

  if (errorCode != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Error resolving service '%s', error %d\n",
              rs->service, errorCode);
    }
  else
    {
      DPRINTF(E_DBG, L_MDNS, "Bonjour resolved '%s' as '%s:%u' on interface %d\n",
              fullname, hosttarget, port, interfaceIndex);

      mdns_addr_lookup_start(rs, interfaceIndex, hosttarget, port,
                             txtLen, txtRecord);
    }
}

static int
mdns_resolve_start(struct mdns_browser *mb, uint32_t interfaceIndex,
                   const char *serviceName, const char *regtype,
                   const char *replyDomain)
{
  DNSServiceErrorType err;
  struct mdns_resolver *rs;
  struct timeval tv;

  rs = calloc(1, sizeof(*rs));
  if (!rs)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return -1;
    }

  rs->service = strdup(serviceName);
  if (!rs->service)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return mdns_resolver_free(rs);
    }
  rs->regtype = strdup(regtype);
  if (!rs->regtype)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return mdns_resolver_free(rs);
    }
  rs->domain = strdup(replyDomain);
  if (!rs->domain)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return mdns_resolver_free(rs);
    }
  rs->mb = mb;
  rs->interface = interfaceIndex;
  /* create a timer with a uuid, so we can search for resolver without
     leaking */
  rs->uuid = ++(mb->res_uuid);
  rs->timer = evtimer_new(evbase_main, mdns_resolve_timeout_cb, rs->uuid);
  if(! rs->timer)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver timer.\n");
      return mdns_resolver_free(rs);
    }

  rs->sdref = mdns_sdref_main;
  err = DNSServiceResolve(&(rs->sdref), kDNSServiceFlagsShareConnection,
                          interfaceIndex, serviceName, regtype, replyDomain,
                          mdns_resolve_callback, rs);
  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Failed to create service resolver.\n");
      rs->sdref = NULL;
      return mdns_resolver_free(rs);
    }

  /* add to browser's resolvers */
  rs->next = mb->resolvers;
  mb->resolvers = rs;

  /* setup a timeout to cancel the resolve */
  tv.tv_sec = MDNS_RESOLVE_TIMEOUT_SECS;
  tv.tv_usec = 0;
  evtimer_add(rs->timer, &tv);

  return 0;
}

static void
mdns_resolve_cancel(const struct mdns_browser *mb, uint32_t interfaceIndex,
                    const char *serviceName, const char *regtype,
                    const char *replyDomain) {

  struct mdns_resolver *rs;

  for(rs = mb->resolvers; rs; rs = rs->next)
    {
      if ((rs->interface == interfaceIndex)
          && (! strcasecmp(rs->service, serviceName))
          && (! strcmp(rs->regtype, regtype))
          && (! strcasecmp(rs->domain, replyDomain)))
        {
          /* remove from resolvers, and free (which cancels resolve) */
          DPRINTF(E_DBG, L_MDNS, "Cancelling resolve for '%s'\n", rs->service);
          mdns_resolver_remove(rs);
          break;
        }
    }

  return;
}

static void
mdns_browse_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                     uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                     const char *serviceName, const char *regtype,
                     const char *replyDomain, void *context)
{
  struct mdns_browser *mb;

  if (errorCode != kDNSServiceErr_NoError)
    {
      // FIXME: if d/c, we sould recreate the browser?
      DPRINTF(E_LOG, L_MDNS, "Bonjour browsing error %d\n", errorCode);
      return;
    }

  mb = context;

  if (flags & kDNSServiceFlagsAdd)
    {
      DPRINTF(E_DBG, L_MDNS,
              "Bonjour Browser: NEW service '%s' type '%s' interface %d\n",
              serviceName, regtype, interfaceIndex);
      mdns_resolve_start(mb, interfaceIndex, serviceName, regtype,
                         replyDomain);
    }
  else
    {
      DPRINTF(E_DBG, L_MDNS,
              "Bonjour Browser: REMOVE service '%s' type '%s' interface %d\n",
              serviceName, regtype, interfaceIndex);
      mdns_resolve_cancel(mb, interfaceIndex, serviceName, regtype,
                          replyDomain);
      mb->cb(serviceName, regtype, replyDomain, NULL, 0, NULL, -1, NULL);
    }
}

int
mdns_browse(char *regtype, int family, mdns_browse_cb cb, enum mdns_options flags)
{
  struct mdns_browser *mb;
  DNSServiceErrorType err;

  DPRINTF(E_DBG, L_MDNS, "Adding service browser for type %s\n", regtype);

  CHECK_NULL(L_MDNS, mb = calloc(1, sizeof(*mb)));

  mb->flags = flags;
  mb->cb = cb;

  /* flags are ignored in DNS-SD implementation */
  switch(family) {
  case AF_UNSPEC:
    mb->protocol = kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6;
    break;
  case AF_INET:
    mb->protocol = kDNSServiceProtocol_IPv4;
    break;
  case AF_INET6:
    mb->protocol = kDNSServiceProtocol_IPv6;
    break;
  default:
    DPRINTF(E_LOG, L_MDNS, "Unrecognized protocol family %d.\n", family);
    return mdns_browser_free(mb);
  }

  mb->regtype = strdup(regtype);
  if (!mb->regtype)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service browser.\n");
      return mdns_browser_free(mb);
    }
  mb->sdref = mdns_sdref_main;
  err = DNSServiceBrowse(&(mb->sdref), kDNSServiceFlagsShareConnection, 0,
                         regtype, NULL, mdns_browse_callback, mb);
  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Failed to create service browser.\n");
      mb->sdref = NULL;
      return mdns_browser_free(mb);
    }

  mb->next = mdns_browsers;
  mdns_browsers = mb;

  return 0;
}
