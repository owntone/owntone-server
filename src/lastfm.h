
#ifndef __LASTFM_H__
#define __LASTFM_H__

#include <stdbool.h>

int
lastfm_login_user(const char *user, const char *password, char **errmsg);

void
lastfm_login(char **arglist);

void
lastfm_logout(void);

int
lastfm_scrobble(int id);

bool
lastfm_is_enabled(void);

int
lastfm_init(void);

#endif /* !__LASTFM_H__ */
