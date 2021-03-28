
#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/http.h>
#include <stdbool.h>


struct spotify_status_info
{
  bool libspotify_installed;
  bool libspotify_logged_in;
  char libspotify_user[100];
};

#define SPOTIFY_SETUP_ERROR_IS_LOADING -2

int
spotify_playback_setup(const char *path);

int
spotify_playback_play();

int
spotify_playback_pause();

//void
//spotify_playback_pause_nonblock(void);

int
spotify_playback_stop(void);

//void
//spotify_playback_stop_nonblock(void);

int
spotify_playback_seek(int ms);

//int
//spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h);

int
spotify_relogin();

int
spotify_login_user(const char *user, const char *password, char **errmsg);

void
spotify_login(char **arglist);

void
spotify_logout(void);

void
spotify_status_info_get(struct spotify_status_info *info);

void
spotify_uri_register(const char *uri);

int
spotify_init(void);

void
spotify_deinit(void);

#endif /* !__SPOTIFY_H__ */
