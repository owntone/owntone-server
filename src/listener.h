
#ifndef __LISTENER_H__
#define __LISTENER_H__

enum listener_event_type
{
  LISTENER_NONE = 0,
  LISTENER_DATABASE,
  LISTENER_PLAYER,
  LISTENER_PLAYLIST,
  LISTENER_VOLUME,
  LISTENER_SPEAKER,
  LISTENER_OPTIONS,
};

typedef void (*notify)(enum listener_event_type type);

int
listener_add(notify notify_cb);

int
listener_remove(notify notify_cb);

int
listener_notify(enum listener_event_type type);

#endif /* !__LISTENER_H__ */
