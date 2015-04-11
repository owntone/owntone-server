
#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <stdint.h>

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

enum repeat_mode {
  REPEAT_OFF  = 0,
  REPEAT_SONG = 1,
  REPEAT_ALL  = 2,
};

enum source_type {
  SOURCE_FILE = 0,
  SOURCE_SPOTIFY,
  SOURCE_PIPE,
  SOURCE_HTTP,
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
  /* Playlist length */
  uint32_t playlistlength;
  /* Playing song id*/
  uint32_t id;
  /* Elapsed time in ms of playing item */
  uint32_t pos_ms;
  /* Length in ms of playing item */
  uint32_t len_ms;
  /* Playlist position of playing item*/
  int pos_pl;
  /* Item id of next item in playlist */
  uint32_t next_id;
  /* Playlist position of next item */
  int next_pos_pl;
};

typedef void (*spk_enum_cb)(uint64_t id, const char *name, int relvol, struct spk_flags flags, void *arg);
typedef void (*player_status_handler)(void);

struct player_source
{
  uint32_t id;
  uint32_t len_ms;

  enum source_type type;
  int setup_done;

  uint64_t stream_start;
  uint64_t output_start;
  uint64_t end;

  struct transcode_ctx *ctx;

  struct player_source *pl_next;
  struct player_source *pl_prev;

  struct player_source *shuffle_next;
  struct player_source *shuffle_prev;

  struct player_source *play_next;
};

struct player_queue
{
  // The item id of the current playing item
  uint32_t playingid;
  // The number of items in the queue
  unsigned int length;

  // The position in the queue for the first item in the queue array
  unsigned int start_pos;
  // The number of items in the queue array
  unsigned int count;
  // The queue array (array of item ids)
  uint32_t *queue;
};

struct player_history
{
  /* Buffer index of the oldest remembered song */
  unsigned int start_index;

  /* Count of song ids in the buffer */
  unsigned int count;

  /* Circular buffer of song ids previously played by forked-daapd */
  uint32_t id[MAX_HISTORY_COUNT];
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
player_playback_start(uint32_t *idx_id);

int
player_playback_startpos(int pos, uint32_t *itemid);

int
player_playback_startid(uint32_t id, uint32_t *itemid);

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

int
player_queue_make_daap(struct player_source **head, const char *query, const char *queuefilter, const char *sort, int quirk);

struct player_source *
player_queue_make_pl(int plid, uint32_t *id);

struct player_source *
player_queue_make_mpd(char *path, int recursive);

struct player_queue *
player_queue_get(int start_pos, int end_pos, char shuffle);

void
queue_free(struct player_queue *queue);

int
player_queue_add(struct player_source *ps);

int
player_queue_add_next(struct player_source *ps);

int
player_queue_move(int ps_pos_from, int ps_pos_to);

int
player_queue_remove(int ps_pos_remove);

int
player_queue_removeid(uint32_t id);

void
player_queue_clear(void);

void
player_queue_empty(int clear_hist);

void
player_queue_plid(uint32_t plid);

struct player_history *
player_history_get(void);

void
player_set_update_handler(player_status_handler handler);

int
player_init(void);

void
player_deinit(void);

#endif /* !__PLAYER_H__ */
