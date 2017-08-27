
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

  bool webapi_token_valid;
  char webapi_user[100];
};

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
spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h);

void
spotify_oauth_interface(struct evbuffer *evbuf, const char *redirect_uri);

void
spotify_oauth_callback(struct evbuffer *evbuf, struct evkeyvalq *param, const char *redirect_uri);

int
spotify_login_user(const char *user, const char *password, char **errmsg);

void
spotify_login(char **arglist);

void
spotify_status_info_get(struct spotify_status_info *info);

int
spotify_init(void);

void
spotify_deinit(void);

#endif /* !__SPOTIFY_H__ */
