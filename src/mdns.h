
#ifndef __MDNS_H__
#define __MDNS_H__

#include "misc.h"

enum mdns_options
{
  // Test connection to device and only call back if successful
  MDNS_CONNECTION_TEST = (1 << 1),
};

typedef void (* mdns_browse_cb)(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt);

/*
 * Start a mDNS client
 * Call only from the main thread!
 *
 * @return       0 on success, -1 on error
 */
int
mdns_init(void);

/*
 * Removes registered services, stops service browsers and stop the mDNS client 
 * Call only from the main thread!
 *
 */
void
mdns_deinit(void);

/*
 * Register (announce) a service with mDNS
 * Call only from the main thread!
 *
 * @in  name     Name of service, e.g. "My Music on Debian"
 * @in  type     Type of service to announce, e.g. "_daap._tcp"
 * @in  port     Port of the service
 * @in  txt      Pointer to array of strings with txt key/values ("Version=1")
 *               for DNS-SD TXT. The array must be terminated by a NULL pointer.
 * @return       0 on success, -1 on error
 */
int
mdns_register(char *name, char *type, int port, char **txt);

/*
 * Register a CNAME record, it will be an alias for hostname
 * Call only from the main thread!
 *
 * @in  name     The CNAME alias, e.g. "forked-daapd.local"
 * @return       0 on success, -1 on error
 */
int
mdns_cname(char *name);

/*
 * Start a service browser, a callback will be made when the service changes state
 * Call only from the main thread!
 *
 * @in  type     Type of service to look for, e.g. "_raop._tcp"
 * @in  family   AF_INET to browse for ipv4 services, AF_INET6 for ipv6, 
                 AF_UNSPEC for both
 * @in  flags    See mdns_options (only supported by Avahi implementation)
 * @in  cb       Callback when service state changes (e.g. appears/disappears)
 * @return       0 on success, -1 on error
 */
int
mdns_browse(char *type, int family, mdns_browse_cb cb, enum mdns_options flags);

#endif /* !__MDNS_H__ */
