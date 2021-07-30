#ifndef __AIRPLAY_EVENTS_H__
#define __AIRPLAY_EVENTS_H__

int
airplay_events_listen(const char *name, const char *address, unsigned short port, const uint8_t *key, size_t key_len);

int
airplay_events_init(void);

void
airplay_events_deinit(void);

#endif  /* !__AIRPLAY_EVENTS_H__ */
