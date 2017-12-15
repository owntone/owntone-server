
#ifndef __LISTENER_H__
#define __LISTENER_H__

enum listener_event_type
{
  /* The player has been started, stopped or seeked */
  LISTENER_PLAYER    = (1 << 0),
  /* The current playback queue has been modified */
  LISTENER_QUEUE     = (1 << 1),
  /* The volume has been changed */
  LISTENER_VOLUME    = (1 << 2),
  /* Speaker status changes (enabled/disabled or verification status) */
  LISTENER_SPEAKER   = (1 << 3),
  /* Options like repeat, random has been changed */
  LISTENER_OPTIONS   = (1 << 4),
  /* The library has been modified */
  LISTENER_DATABASE  = (1 << 5),
  /* A stored playlist has been modified (create, delete, add, rename) */
  LISTENER_STORED_PLAYLIST = (1 << 6),
  /* A library update has started or finished */
  LISTENER_UPDATE = (1 << 7),
  /* A pairing request has started or finished */
  LISTENER_PAIRING = (1 << 8),
  /* Spotify status changes (login, logout) */
  LISTENER_SPOTIFY = (1 << 9),
  /* Last.fm status changes (enable/disable scrobbling) */
  LISTENER_LASTFM = (1 << 10),
  /* Song rating changes */
  LISTENER_RATING = (1 << 11),
};

typedef void (*notify)(short event_mask);

/*
 * Registers the given callback function to the given event types.
 * This function is not thread safe. Listeners must be added once at startup.
 *
 * @param notify_cb Callback function
 * @param event_mask Event mask, one or more of LISTENER_*
 * @return 0 on success, -1 on failure
 */
int
listener_add(notify notify_cb, short event_mask);

/*
 * Removes the given callback function
 * This function is not thread safe. Listeners must be removed once at shutdown.
 *
 * @param notify_cb Callback function
 * @return 0 on success, -1 if the callback was not registered
 */
int
listener_remove(notify notify_cb);

/*
 * Calls the callback function of the registered listeners listening for the
 * given type of event.
 *
 * @param type The event type, on of the LISTENER_* values
 *
 */
void
listener_notify(enum listener_event_type type);

#endif /* !__LISTENER_H__ */
