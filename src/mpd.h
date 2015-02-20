
#ifndef __MPD_H__
#define __MPD_H__

#ifdef HAVE_LIBEVENT2
# include <event2/event.h>
# include <event2/buffer.h>
# include <event2/bufferevent.h>
# include <event2/listener.h>
#else
# include <event.h>
#endif

int
mpd_init(void);

void
mpd_deinit(void);

#endif /* !__MPD_H__ */
