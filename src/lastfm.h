
#ifndef __LASTFM_H__
#define __LASTFM_H__

void
lastfm_login(char *path);

int
lastfm_scrobble(int id);

void
lastfm_deinit(void);

#endif /* !__LASTFM_H__ */
