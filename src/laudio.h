
#ifndef __LAUDIO_H__
#define __LAUDIO_H__

#define LAUDIO_F_STARTED  (1 << 15)

enum laudio_state
  {
    LAUDIO_CLOSED    = 0,
    LAUDIO_STOPPING  = 1,
    LAUDIO_OPEN      = 2,
    LAUDIO_STARTED   = LAUDIO_F_STARTED,
    LAUDIO_RUNNING   = LAUDIO_F_STARTED | 0x01,

    LAUDIO_FAILED    = -1,
  };

typedef void (*laudio_status_cb)(enum laudio_state status);

void
laudio_write(uint8_t *buf, uint64_t rtptime);

uint64_t
laudio_get_pos(void);

void
laudio_set_volume(int vol);

int
laudio_start(uint64_t cur_pos, uint64_t next_pkt);

void
laudio_stop(void);

int
laudio_open(void);

void
laudio_close(void);

int
laudio_init(laudio_status_cb cb);

void
laudio_deinit(void);

#endif /* !__LAUDIO_H__ */
