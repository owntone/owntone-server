
#ifndef __MDNS_H__
#define __MDNS_H__

#include "misc.h"

#define MDNS_WANT_V4    (1 << 0)
#define MDNS_WANT_V4LL  (1 << 1)
#define MDNS_WANT_V6    (1 << 2)
#define MDNS_WANT_V6LL  (1 << 3)

#define MDNS_WANT_DEFAULT (MDNS_WANT_V4 | MDNS_WANT_V6 | MDNS_WANT_V6LL)

typedef void (* mdns_browse_cb)(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt);

/* mDNS interface functions */
/* Call only from the main thread */
int
mdns_init(void);

void
mdns_deinit(void);

int
mdns_register(char *name, char *type, int port, char **txt);

int
mdns_browse(char *type, int flags, mdns_browse_cb cb);

#endif /* !__MDNS_H__ */
