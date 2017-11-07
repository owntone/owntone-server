
#ifndef __CACHE_H__
#define __CACHE_H__

#include <event2/buffer.h>

/* ---------------------------- DAAP cache API  --------------------------- */

void
cache_daap_suspend(void);

void
cache_daap_resume(void);

int
cache_daap_get(struct evbuffer *evbuf, const char *query);

void
cache_daap_add(const char *query, const char *ua, int is_remote, int msec);

int
cache_daap_threshold(void);


/* ---------------------------- Artwork cache API  --------------------------- */

#define CACHE_ARTWORK_GROUP 0
#define CACHE_ARTWORK_INDIVIDUAL 1

void
cache_artwork_ping(const char *path, time_t mtime, int del);

int
cache_artwork_delete_by_path(char *path);

int
cache_artwork_purge_cruft(time_t ref);

int
cache_artwork_add(int type, int64_t persistentid, int max_w, int max_h, int format, char *filename, struct evbuffer *evbuf);

int
cache_artwork_get(int type, int64_t persistentid, int max_w, int max_h, int *cached, int *format, struct evbuffer *evbuf);

int
cache_artwork_stash(struct evbuffer *evbuf, char *path, int format);

int
cache_artwork_read(struct evbuffer *evbuf, char *path, int *format);

/* ---------------------------- Cache API  --------------------------- */

int
cache_init(void);

void
cache_deinit(void);

#endif /* !__CACHE_H__ */
