
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

typedef struct
{
  // Identifier of th audio output
  char *name;

  // Initialization function called during startup
  int (*init)(laudio_status_cb cb, cfg_t *cfg_audio);

  // Deinitialization function called at shutdown
  void (*deinit)(void);

  // Function to open the output called at playback start or speaker activiation
  int (*open)(void);

  // Function called after opening the output (during playback start or speaker activiation
  int (*start)(uint64_t cur_pos, uint64_t next_pkt);

  // block of samples
  void (*write)(uint8_t *buf, uint64_t rtptime);

  // Stopping audio playback
  void (*stop)(void);

  // Closes the output
  void (*close)(void);

  // Returns the rtptime of the packet thats is currently playing
  uint64_t (*pos)();

  // Sets the volum for the output
  void (*volume)(int vol);
} audio_output;

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
