
#ifndef __DAAP_CACHE_H__
#define __DAAP_CACHE_H__

#ifdef HAVE_LIBEVENT2
# include <event2/event.h>
# include <event2/buffer.h>
#else
# include <event.h>
#endif

void
daapcache_trigger(void);

struct evbuffer *
daapcache_get(const char *query);

int
daapcache_init(void);

void
daapcache_deinit(void);

#endif /* !__DAAP_CACHE_H__ */
