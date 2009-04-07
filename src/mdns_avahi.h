
#ifndef __MDNS_AVAHI_H__
#define __MDNS_AVAHI_H__

/* mDNS interface functions */
/* Call only from the main thread */
int
mdns_init(void);

void
mdns_deinit(void);

int
mdns_register(char *name, char *type, int port, char *txt);

#endif /* !__MDNS_AVAHI_H__ */
