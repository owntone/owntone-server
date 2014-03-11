
#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include "db.h"
#include "evhttp/evhttp.h"

int
spotify_playback_play(struct media_file_info *mfi);

int
spotify_playback_pause(void);

int
spotify_playback_stop(void);

int
spotify_playback_seek(int ms);

int
spotify_audio_get(struct evbuffer *evbuf, int wanted);

void
spotify_login(char *path);

int
spotify_init(void);

void
spotify_deinit(void);

#endif /* !__SPOTIFY_H__ */
