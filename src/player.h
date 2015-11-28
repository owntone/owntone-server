
#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <stdint.h>

#include "db.h"
#include "queue.h"

/* AirTunes v2 packet interval in ns */
/* (352 samples/packet * 1e9 ns/s) / 44100 samples/s = 7981859 ns/packet */
# define AIRTUNES_V2_STREAM_PERIOD 7981859

/* AirTunes v2 number of samples per packet */
#define AIRTUNES_V2_PACKET_SAMPLES  352


/* Samples to bytes, bytes to samples */
#define STOB(s) ((s) * 4)
#define BTOS(b) ((b) / 4)

/* Maximum number of previously played songs that are remembered */
#define MAX_HISTORY_COUNT 20

enum play_status {
  PLAY_STOPPED = 2,
  PLAY_PAUSED  = 3,
  PLAY_PLAYING = 4,
};

struct spk_flags {
  unsigned selected:1;
  unsigned has_password:1;

  unsigned has_video:1;
};

struct player_status {
  enum play_status status;
  enum repeat_mode repeat;
  char shuffle;

  int volume;

  /* Playlist id */
  uint32_t plid;
  /* Playlist version
     After startup plversion is 0 and gets incremented after each change of the playlist
     (e. g. after adding/moving/removing items). It is used by mpd clients to recognize if
     they need to update the current playlist. */
  uint32_t plversion;
  /* Playlist length */
  uint32_t playlistlength;
  /* Id of the playing file/item in the files database */
  uint32_t id;
  /* Item-Id of the playing file/item in the queue */
  uint32_t item_id;
  /* Elapsed time in ms of playing item */
  uint32_t pos_ms;
  /* Length in ms of playing item */
  uint32_t len_ms;
  /* Playlist position of playing item*/
  int pos_pl;
  /* Item id of next item in playlist */
  uint32_t next_id;
  /* Item-Id of the next file/item in the queue */
  uint32_t next_item_id;
  /* Playlist position of next item */
  int next_pos_pl;
};

typedef void (*spk_enum_cb)(uint64_t id, const char *name, int relvol, struct spk_flags flags, void *arg);
typedef int (*player_streaming_cb)(uint8_t *rawbuf, size_t size);

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
player_get_current_pos(uint64_t *pos, struct timespec *ts, int commit);

int
player_get_status(struct player_status *status);

int
player_now_playing(uint32_t *id);

char *
player_get_icy_artwork_url(uint32_t id);

void
player_speaker_enumerate(spk_enum_cb cb, void *arg);

int
player_speaker_set(uint64_t *ids);

int
player_playback_start(uint32_t *id);

int
player_playback_start_byindex(int pos, uint32_t *id);

int
player_playback_start_bypos(int pos, uint32_t *id);

int
player_playback_start_byitemid(uint32_t item_id, uint32_t *id);

int
player_playback_stop(void);

int
player_playback_pause(void);

int
player_playback_seek(int ms);

int
player_playback_next(void);

int
player_playback_prev(void);

void
player_streaming_start(player_streaming_cb cb);

void
player_streaming_stop(void);


int
player_volume_set(int vol);

int
player_volume_setrel_speaker(uint64_t id, int relvol);

int
player_volume_setabs_speaker(uint64_t id, int vol);

int
player_repeat_set(enum repeat_mode mode);

int
player_shuffle_set(int enable);


struct queue *
player_queue_get_bypos(int count);

struct queue *
player_queue_get_byindex(int pos, int count);

int
player_queue_add(struct queue_item *items);

int
player_queue_add_next(struct queue_item *items);

int
player_queue_move_bypos(int ps_pos_from, int ps_pos_to);

int
player_queue_move_byitemid(uint32_t item_id, int pos_to);

int
player_queue_remove_bypos(int pos);

int
player_queue_remove_byindex(int pos, int count);

int
player_queue_remove_byitemid(uint32_t id);

void
player_queue_clear(void);

void
player_queue_clear_history(void);

void
player_queue_plid(uint32_t plid);


struct player_history *
player_history_get(void);

int
player_init(void);

void
player_deinit(void);

#endif /* !__PLAYER_H__ */
