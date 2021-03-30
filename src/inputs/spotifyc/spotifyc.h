#ifndef __SPOTIFYC_H__
#define __SPOTIFYC_H__

#include <inttypes.h>

struct sp_session;

enum sp_bitrates
{
  SP_BITRATE_96,
  SP_BITRATE_160,
  SP_BITRATE_320,
};

struct sp_credentials
{
  char *username;
  char *password;

  uint8_t stored_cred[256]; // Actual size is 146, but leave room for some more
  size_t stored_cred_len;
  uint8_t token[256]; // Actual size is 190, but leave room for some more
  size_t token_len;
};

struct sp_callbacks
{
  void (*logged_in)(struct sp_session *session, void *cb_arg, struct sp_credentials *credentials);
  void (*logged_out)(struct sp_session *session, void *cb_arg, struct sp_credentials *credentials);
  void (*track_opened)(struct sp_session *session, void *cb_arg, int fd);
  void (*track_closed)(struct sp_session *session, void *cb_arg, int fd);
  void (*track_seeked)(struct sp_session *session, void *cb_arg, int fd);
  void (*error)(struct sp_session *session, void *cb_arg, int err, const char *errmsg);

  // Bring your own https client
  int (*https_get)(char **body, const char *url);

  // Debugging
  void (*hexdump)(const char *msg, uint8_t *data, size_t data_len);
  void (*logmsg)(const char *fmt, ...);
};


// Async interface

struct sp_session *
spotifyc_login_password(const char *username, const char *password);

struct sp_session *
spotifyc_login_stored_cred(const char *username, uint8_t *stored_cred, size_t stored_cred_len);

struct sp_session *
spotifyc_login_token(const char *username, uint8_t *token, size_t token_len);

void
spotifyc_logout(struct sp_session *session);

int
spotifyc_open(const char *path, struct sp_session *session);

void
spotifyc_bitrate_set(enum sp_bitrates bitrate, struct sp_session *session);

// Starts writing audio to the file descriptor
int
spotifyc_play(int fd);

int
spotifyc_seek(int seek_ms, int fd);

int
spotifyc_stop(int fd);


// Sync interface

const char *
spotifyc_last_errmsg(void);

// This Spotify implementation is entirely async, so first the caller must set
// up callbacks
int
spotifyc_init(struct sp_callbacks *callbacks, void *cb_arg);

void
spotifyc_deinit(void);

#endif /* !__SPOTIFYC_H__ */
