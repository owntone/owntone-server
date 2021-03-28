#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include <stdbool.h>

struct spotify_status
{
  bool installed;
  bool logged_in;
  bool track_opened;
  char username[100];
};

int
spotify_login_user(const char *user, const char *password, const char **errmsg);

void
spotify_login(char **arglist);

void
spotify_logout(void);

void
spotify_status_get(struct spotify_status *status);

#endif /* !__SPOTIFY_H__ */
