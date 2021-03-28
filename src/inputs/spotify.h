#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include <stdbool.h>


struct spotify_status_info
{
  bool libspotify_installed;
  bool libspotify_logged_in;
  char libspotify_user[100];
};

int
spotify_login_user(const char *user, const char *password, char **errmsg);

void
spotify_login(char **arglist);

void
spotify_logout(void);

void
spotify_status_info_get(struct spotify_status_info *info);

#endif /* !__SPOTIFY_H__ */
