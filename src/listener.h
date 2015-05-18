
#ifndef __LISTENER_H__
#define __LISTENER_H__

enum listener_event_type
{
  LISTENER_PLAYER    = (1 << 0),
  LISTENER_PLAYLIST  = (1 << 1),
  LISTENER_VOLUME    = (1 << 2),
  LISTENER_SPEAKER   = (1 << 3),
  LISTENER_OPTIONS   = (1 << 4),
  LISTENER_DATABASE  = (1 << 5),
};

typedef void (*notify)(enum listener_event_type type);

int
listener_add(notify notify_cb, short events);

int
listener_remove(notify notify_cb);

int
listener_notify(enum listener_event_type type);

#endif /* !__LISTENER_H__ */
