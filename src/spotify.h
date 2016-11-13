
#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http.h>

#include "db.h"

int
spotify_playback_setup(const char *path);

int
spotify_playback_play();

int
spotify_playback_pause();

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

int
spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h);

void
spotify_oauth_interface(struct evbuffer *evbuf, const char *redirect_uri);

void
spotify_oauth_callback(struct evbuffer *evbuf, struct evkeyvalq *param, const char *redirect_uri);

int
scan_metadata_spotify(const char *uri, struct media_file_info *mfi);

void
spotify_uri_register(const char *uri);

void
spotify_login(char *path);

int
spotify_init(void);

void
spotify_deinit(void);

#endif /* !__SPOTIFY_H__ */
