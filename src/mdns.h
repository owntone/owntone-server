
#ifndef __MDNS_H__
#define __MDNS_H__

#include <avahi-common/strlst.h>

typedef void (* mdns_browse_cb)(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, AvahiStringList *txt);

/* mDNS interface functions */
/* Call only from the main thread */
int
mdns_init(void);

void
mdns_deinit(void);

int
mdns_register(char *name, char *type, int port, char **txt);

int
mdns_browse(char *type, mdns_browse_cb cb);

#endif /* !__MDNS_H__ */
