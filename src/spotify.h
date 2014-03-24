
#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include "db.h"
#include <event.h>

int
spotify_playback_play(struct media_file_info *mfi);

void
spotify_playback_pause_nonblock(void);

int
spotify_playback_stop(void);

void
spotify_playback_stop_nonblock(void);

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
