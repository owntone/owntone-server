
#ifndef __LISTENBRAINZ_H__
#define __LISTENBRAINZ_H__

struct listenbrainz_status {
  bool disabled;
  char *user_name;
  bool token_valid;
  char *message;
};

int
listenbrainz_scrobble(int mfi_id);
int
listenbrainz_token_set(const char *token);
int
listenbrainz_token_delete(void);
int
listenbrainz_status_get(struct listenbrainz_status *status);
void
listenbrainz_status_free(struct listenbrainz_status *status, bool content_only);
int
listenbrainz_init(void);

#endif /* !__LISTENBRAINZ_H__ */
