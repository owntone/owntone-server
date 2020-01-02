
#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <stdbool.h>
#include <stdint.h>

#include "db.h"

// Maximum number of previously played songs that are remembered
#define MAX_HISTORY_COUNT 20

enum play_status {
  PLAY_STOPPED = 2,
  PLAY_PAUSED  = 3,
  PLAY_PLAYING = 4,
};

enum repeat_mode {
  REPEAT_OFF  = 0,
  REPEAT_SONG = 1,
  REPEAT_ALL  = 2,
};

enum player_seek_mode {
  PLAYER_SEEK_POSITION = 1,
  PLAYER_SEEK_RELATIVE = 2,
};

struct player_speaker_info {
  uint64_t id;
  uint32_t active_remote;
  char name[255];
  char output_type[50];
  int relvol;
  int absvol;

  bool selected;
  bool has_password;
  bool requires_auth;
  bool needs_auth_key;

  bool has_video;
};

struct player_status {
  enum play_status status;
  enum repeat_mode repeat;
  char shuffle;
  char consume;

  int volume;

  /* Playlist id */
  uint32_t plid;
  /* Id of the playing file/item in the files database */
  uint32_t id;
  /* Item-Id of the playing file/item in the queue */
  uint32_t item_id;
  /* Elapsed time in ms of playing item */
  uint32_t pos_ms;
  /* Length in ms of playing item */
  uint32_t len_ms;
};

typedef void (*spk_enum_cb)(struct player_speaker_info *spk, void *arg);

struct player_history
{
  /* Buffer index of the oldest remembered song */
  unsigned int start_index;

  /* Count of song ids in the buffer */
  unsigned int count;

  /* Circular buffer of song ids previously played by forked-daapd */
  uint32_t id[MAX_HISTORY_COUNT];
  uint32_t item_id[MAX_HISTORY_COUNT];
};

int
player_get_status(struct player_status *status);

int
player_playing_now(uint32_t *id);

void
player_speaker_enumerate(spk_enum_cb cb, void *arg);

int
player_speaker_set(uint64_t *ids);

int
player_speaker_get_byid(uint64_t id, struct player_speaker_info *spk);

int
player_speaker_get_byactiveremote(struct player_speaker_info *spk, uint32_t active_remote);

int
player_speaker_enable(uint64_t id);

int
player_speaker_disable(uint64_t id);

int
player_playback_start(void);

int
player_playback_start_byitem(struct db_queue_item *queue_item);

int
player_playback_start_byid(uint32_t id);

int
player_playback_stop(void);

int
player_playback_pause(void);

int
player_playback_seek(int seek_ms, enum player_seek_mode seek_mode);

int
player_playback_next(void);

int
player_playback_prev(void);

int
player_volume_set(int vol);

int
player_volume_setrel_speaker(uint64_t id, int relvol);

int
player_volume_setabs_speaker(uint64_t id, int vol);

int
player_volume_update_speaker(uint64_t id, const char *value);

int
player_repeat_set(enum repeat_mode mode);

int
player_shuffle_set(int enable);

int
player_consume_set(int enable);

void
player_queue_clear_history(void);

void
player_queue_plid(uint32_t plid);

struct player_history *
player_history_get(void);

int
player_device_add(void *device);

int
player_device_remove(void *device);

void
player_raop_verification_kickoff(char **arglist);

const char *
player_pmap(void *p);

int
player_init(void);

void
player_deinit(void);

#endif /* !__PLAYER_H__ */
