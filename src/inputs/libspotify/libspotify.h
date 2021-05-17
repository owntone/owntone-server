
#ifndef __LIBSPOTIFY_H__
#define __LIBSPOTIFY_H__

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

#define LIBSPOTIFY_SETUP_ERROR_IS_LOADING -2

int
libspotify_playback_setup(const char *path);

int
libspotify_playback_play(void);

int
libspotify_playback_pause(void);

//void
//spotify_playback_pause_nonblock(void);

int
libspotify_playback_stop(void);

//void
//spotify_playback_stop_nonblock(void);

int
libspotify_playback_seek(int ms);

//int
//spotify_artwork_get(struct evbuffer *evbuf, char *path, int max_w, int max_h);

int
libspotify_relogin(void);

int
libspotify_login(const char *user, const char *password, const char **errmsg);

void
libspotify_logout(void);

void
libspotify_status_info_get(struct spotify_status_info *info);

void
libspotify_uri_register(const char *uri);

int
libspotify_init(void);

void
libspotify_deinit(void);

#endif /* !__LIBSPOTIFY_H__ */
