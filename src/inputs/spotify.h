#ifndef __SPOTIFY_H__
#define __SPOTIFY_H__

#include <stdbool.h>

struct spotify_status
{
  bool installed;
  bool logged_in;
  char username[64];
};

int
spotify_login(const char *user, const char *password, const char **errmsg);
int
spotify_login_token(const char *username, uint8_t *token, size_t token_len, const char **errmsg);

void
spotify_logout(void);

void
spotify_status_get(struct spotify_status *status);

#endif /* !__SPOTIFY_H__ */
