
#ifndef __PLAYER_H__
#define __PLAYER_H__

#include <stdint.h>

#if defined(__linux__)
/* AirTunes v2 packet interval in ns */
/* (352 samples/packet * 1e9 ns/s) / 44100 samples/s = 7981859 ns/packet */
# define AIRTUNES_V2_STREAM_PERIOD 7981859
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
/* AirTunes v2 packet interval in ms */
# define AIRTUNES_V2_STREAM_PERIOD   8
#endif

/* AirTunes v2 number of samples per packet */
#define AIRTUNES_V2_PACKET_SAMPLES  352


/* Samples to bytes, bytes to samples */
#define STOB(s) ((s) * 4)
#define BTOS(b) ((b) / 4)

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

  uint32_t plid;
  uint32_t id;
  uint32_t pos_ms;
  int pos_pl;
};

typedef void (*spk_enum_cb)(uint64_t id, const char *name, int relvol, struct spk_flags flags, void *arg);
typedef void (*player_status_handler)(void);

struct player_source
{
  uint32_t id;

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

int
player_get_current_pos(uint64_t *pos, struct timespec *ts, int commit);

int
player_get_status(struct player_status *status);

int
player_now_playing(uint32_t *id);

void
player_speaker_enumerate(spk_enum_cb cb, void *arg);

int
player_speaker_set(uint64_t *ids);

int
player_playback_start(uint32_t *idx_id);

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
player_queue_get(void);

int
player_queue_add(struct player_source *ps);

void
player_queue_clear(void);

void
player_queue_plid(uint32_t plid);

void
player_set_update_handler(player_status_handler handler);

int
player_init(void);

void
player_deinit(void);

#endif /* !__PLAYER_H__ */
