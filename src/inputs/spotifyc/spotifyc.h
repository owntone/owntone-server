#ifndef __SPOTIFYC_H__
#define __SPOTIFYC_H__

#include <inttypes.h>
#include <stddef.h>

struct sp_session;

enum sp_bitrates
{
  SP_BITRATE_96,
  SP_BITRATE_160,
  SP_BITRATE_320,
};

typedef void (*sp_progress_cb)(int fd, void *arg, size_t received, size_t len);

struct sp_credentials
{
  char username[32];
  char password[32];

  uint8_t stored_cred[256]; // Actual size is 146, but leave room for some more
  size_t stored_cred_len;
  uint8_t token[256]; // Actual size is 190, but leave room for some more
  size_t token_len;
};

struct sp_metadata
{
  size_t file_len;
};

struct sp_callbacks
{
  // Bring your own https client and tcp connector
  int (*https_get)(char **body, const char *url);
  int (*tcp_connect)(const char *address, unsigned short port);
  void (*tcp_disconnect)(int fd);

  // Debugging
  void (*hexdump)(const char *msg, uint8_t *data, size_t data_len);
  void (*logmsg)(const char *fmt, ...);
};



struct sp_session *
spotifyc_login_password(const char *username, const char *password);

struct sp_session *
spotifyc_login_stored_cred(const char *username, uint8_t *stored_cred, size_t stored_cred_len);

struct sp_session *
spotifyc_login_token(const char *username, uint8_t *token, size_t token_len);

int
spotifyc_logout(struct sp_session *session);

int
spotifyc_bitrate_set(enum sp_bitrates bitrate, struct sp_session *session);

int
spotifyc_credentials_get(struct sp_credentials *credentials, struct sp_session *session);

// Returns a file descriptor (in non-blocking mode) from which caller can read
// one chunk of data. To get more data written/start playback loop, call
// spotifyc_play().
int
spotifyc_open(const char *path, struct sp_session *session);

// Continues writing data to the file descriptor until error or end of track.
// A read of the fd that returns 0 means end of track, and a negative read
// return value means error. progress_cb and cb_arg optional.
void
spotifyc_write(int fd, sp_progress_cb progress_cb, void *cb_arg);

// Seeks to pos (measured in bytes, so must not exceed file_len), flushes old
// data from the fd and prepares one chunk of data for reading.
int
spotifyc_seek(int fd, size_t pos);

// Closes a track download, incl. the fd.
int
spotifyc_close(int fd);

int
spotifyc_metadata_get(struct sp_metadata *metadata, int fd);

const char *
spotifyc_last_errmsg(void);

int
spotifyc_init(struct sp_callbacks *callbacks);

void
spotifyc_deinit(void);

#endif /* !__SPOTIFYC_H__ */
