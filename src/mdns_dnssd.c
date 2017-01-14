/*
 * Avahi mDNS backend, with libevent polling
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

/*
 * NOTE: dns_sd.h indicates that MoreComing is unified on shared
 * connecdtions; this means that the triggers to free structs when
 * MoreComing is not set may not be called if another operation has
 * pending data... we probably need a way to prevent leaks (how do you
 * know if it's safe to free a context, after timeout with cancel??)
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

struct mdns_service {
  DNSServiceRef sdref;
  char *name;
  char *regtype;
  TXTRecordRef txtRecord;
  struct event *ev;
  struct mdns_service *next;
};

static struct mdns_service *mdns_services = NULL;

struct mdns_record {
  DNSRecordRef recRef;
  uint16_t rrtype;
  char *name;
  struct mdns_record *next;
};

static struct mdns_record *mdns_records = NULL;

/* Avahi client callbacks & helpers */

struct mdns_browser
{
  DNSServiceRef sdref;
  char *regtype;
  mdns_browse_cb cb;
  DNSServiceProtocol protocol;
  struct event *ev;
  struct mdns_browser *next;
};

static struct mdns_browser *mdns_browsers = NULL;

struct mdns_resolver
{
  DNSServiceRef sdref;
  char *name;
  char *regtype;
  char *domain;
  struct mdns_browser *mb;
  struct event *ev;
  struct mdns_resolver *next;
};

struct mdns_addr_lookup {
  DNSServiceRef sdref;
  char *name;
  char *regtype;
  char *domain;
  u_int16_t port;
  struct keyval txt_kv;
  struct mdns_browser *mb;
  struct event *ev;
};

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

  /* free event, then sdref, then everything else */
  event_free(s->ev);
  DNSServiceRefDeallocate(s->sdref);
  TXTRecordDeallocate(&(s->txtRecord));
  free(s->name);
  free(s->regtype);
  free(s);

  return -1;
}

static int
mdns_browser_free(struct mdns_browser *mb)
{
  if(! mb)
    return -1;

  /* free event, then sdref, then everything else */
  event_free(mb->ev);
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

  event_free(mdns_ev_main);
  mdns_ev_main = NULL;
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
  DNSServiceProcessResult(*(DNSServiceRef *)data);
}

static int
mdns_event_add(DNSServiceRef *psdref, struct event **pev)
{
  int fd;

  fd = DNSServiceRefSockFD(*psdref);
  *pev = event_new(evbase_main, fd, EV_PERSIST | EV_READ, mdns_event_cb,
                   psdref);
  if (!*pev)
    {
      DPRINTF(E_LOG, L_MDNS, "Could not make new event in mdns\n");
      return -1;
    }

  return event_add(*pev, NULL);
}

int
mdns_init(void)
{
  DNSServiceErrorType err;

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

  if (mdns_event_add(&mdns_sdref_main, &mdns_ev_main) < 0)
    return mdns_main_free();

  return 0;
}

static void
mdns_register_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                       DNSServiceErrorType errorCode, const char *name,
                       const char *regtype, const char *domain,
                       void *context) {

  switch (errorCode) {
    case kDNSServiceErr_NoError:
      DPRINTF(E_DBG, L_MDNS, "Successfully added mDNS services\n");
      break;

    case kDNSServiceErr_NameConflict:
      DPRINTF(E_DBG, L_MDNS, "Name collision - automatically assigning new name\n");
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

  DPRINTF(E_DBG, L_MDNS, "Adding mDNS service %s/%s\n", name, regtype);

  s = calloc(1, sizeof(*s));
  if (!s)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory registering service.\n");
      return -1;
    }
  s->name = strdup(name);
  if (!(s->name))
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory registering service.\n");
      return mdns_service_free(s);
    }

  s->regtype = strdup(regtype);
  if (!(s->regtype))
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory registering service.\n");
      return mdns_service_free(s);
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
                           s->name, s->regtype, NULL, NULL, htons(port),
                           TXTRecordGetLength(&(s->txtRecord)),
                           TXTRecordGetBytesPtr(&(s->txtRecord)),
                           mdns_register_callback, NULL);

  if (err != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "Error registering service %s\n", name);
      s->sdref = NULL;
      return mdns_service_free(s);
    }

  if (mdns_event_add(&s->sdref, &s->ev) < 0)
    return mdns_service_free(s);

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

static int
mdns_addr_lookup_free(struct mdns_addr_lookup *lu)
{
  if (! lu)
    return -1;

  /* free event, then sdref, then everything else */
  event_free(lu->ev);
  DNSServiceRefDeallocate(lu->sdref);
  keyval_clear(&lu->txt_kv);
  free(lu->domain);
  free(lu->regtype);
  free(lu->name);
  free(lu);

  return -1;
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

      if (!(lu->mb->protocol & kDNSServiceProtocol_IPv4))
        {
          DPRINTF(E_DBG, L_MDNS, "Discarding IPv4, not interested (service %s)\n",
                  lu->name);
          return;
        }
      else if (is_v4ll(&addr->sin_addr))
        {
          DPRINTF(E_WARN, L_MDNS, "Ignoring announcement from %s, address %s is link-local\n",
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

      if (!(lu->mb->protocol & kDNSServiceProtocol_IPv6))
        {
          DPRINTF(E_DBG, L_MDNS, "Discarding IPv6, not interested (service %s)\n",
                  lu->name);
          return;
        }
      else if (is_v6ll(&addr6->sin6_addr))
        {
          DPRINTF(E_WARN, L_MDNS, "Ignoring announcement from %s, address %s is link-local\n",
                  hostname, addr_str);
          return;
        }
    }

  DPRINTF(E_DBG, L_MDNS, "Service %s, hostname %s resolved to %s\n",
          lu->name, hostname, addr_str);

  /* Execute callback (mb->cb) with all the data */
  lu->mb->cb(lu->name, lu->mb->regtype, lu->domain, hostname,
             address->sa_family, addr_str, lu->port, &lu->txt_kv);
}

static void
mdns_lookup_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                     uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                     const char *hostname, const struct sockaddr *address,
                     uint32_t ttl, void *context)
{
  struct mdns_addr_lookup *lu;

  if (errorCode != kDNSServiceErr_NoError )
    {
      DPRINTF(E_LOG, L_MDNS, "Error resolving service address, error %d\n",
              errorCode);
      return;
    }

  lu = context;

  if (flags & kDNSServiceFlagsAdd)
    mdns_browse_call_cb(lu, hostname, address);

  /* If we are done with address lookups for this resolve,
     terminate the address lookup */
  if (!(flags & kDNSServiceFlagsMoreComing))
    mdns_addr_lookup_free(lu);
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
  lu->name = strdup(rs->name);
  if (!lu->name)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating address lookup.\n");
      return mdns_addr_lookup_free(lu);
    }
  lu->regtype = strdup(rs->regtype);
  if (!lu->regtype)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating address lookup.\n");
      return mdns_addr_lookup_free(lu);
    }
  lu->domain = strdup(rs->domain);
  if (!lu->domain)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating address lookup.\n");
      return mdns_addr_lookup_free(lu);
    }
  lu->port = port;
  lu->mb = rs->mb;

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

  if (mdns_event_add(&lu->sdref, &lu->ev) < 0)
    return mdns_addr_lookup_free(lu);

  return 0;
}

static int
mdns_resolver_free(struct mdns_resolver *rs) {

  if (! rs)
    return -1;

  event_free(rs->ev);
  DNSServiceRefDeallocate(rs->sdref);
  free(rs->name);
  free(rs->regtype);
  free(rs->domain);
  free(rs);

  return -1;
}

static void
mdns_resolve_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                      uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                      const char *fullname, const char *hosttarget,
                      uint16_t port, uint16_t txtLen,
                      const unsigned char *txtRecord, void *context )
{
  struct mdns_resolver *rs;

  if (errorCode != kDNSServiceErr_NoError )
    {
      DPRINTF(E_LOG, L_MDNS, "Error resolving mdns service, error %d\n",
              errorCode);
      return;
    }

  rs = context;

  mdns_addr_lookup_start(rs, interfaceIndex, hosttarget, port,
                         txtLen, txtRecord);

  /* If we are done resolving this service, terminate the resolve
     and free the resolver resources */
  if (!(flags & kDNSServiceFlagsMoreComing))
    mdns_resolver_free(rs);
}

static int
mdns_resolve_start(struct mdns_browser *mb, uint32_t interfaceIndex,
                   const char *serviceName, const char *regtype,
                   const char *replyDomain)
{
  DNSServiceErrorType err;
  struct mdns_resolver *rs;

  rs = calloc(1, sizeof(*rs));
  if (!rs)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return -1;
    }

  rs->name = strdup(serviceName);
  if (!rs->name)
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
  if (!rs->name)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service resolver.\n");
      return mdns_resolver_free(rs);
    }
  rs->mb = mb;

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

  if (mdns_event_add(&rs->sdref, &rs->ev) < 0)
    return mdns_resolver_free(rs);

  return 0;
}

static void
mdns_browse_callback(DNSServiceRef sdRef, DNSServiceFlags flags,
                     uint32_t interfaceIndex, DNSServiceErrorType errorCode,
                     const char *serviceName, const char *regtype,
                     const char *replyDomain, void *context )
{
  struct mdns_browser *mb;

  if (errorCode != kDNSServiceErr_NoError)
    {
      DPRINTF(E_LOG, L_MDNS, "DNS-SD browsing error %d\n", errorCode);
      return;
    }

  mb = context;

  if (flags & kDNSServiceFlagsAdd)
    {
      DPRINTF(E_DBG, L_MDNS,
              "DNS-SD Browser: NEW service '%s' type '%s' interface %d\n",
              serviceName, regtype, interfaceIndex);
      mdns_resolve_start(mb, interfaceIndex, serviceName, regtype,
                         replyDomain);
    }
  else
    {
      DPRINTF(E_DBG, L_MDNS,
              "Avahi Browser: REMOVE service '%s' type '%s' interface %d\n",
              serviceName, regtype, interfaceIndex);
      mb->cb(serviceName, regtype, replyDomain, NULL, 0, NULL, -1, NULL);
    }
}

int
mdns_browse(char *regtype, int family, mdns_browse_cb cb)
{
  struct mdns_browser *mb;
  DNSServiceErrorType err;

  DPRINTF(E_DBG, L_MDNS, "Adding service browser for type %s\n", regtype);

  mb = calloc(1, sizeof(*mb));
  if (!mb)
    {
      DPRINTF(E_LOG, L_MDNS, "Out of memory creating service browser.\n");
      return -1;
    }

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

  if (mdns_event_add(&mb->sdref, &mb->ev) < 0)
    return mdns_browser_free(mb);

  mb->next = mdns_browsers;
  mdns_browsers = mb;

  return 0;
}
