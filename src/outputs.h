
#ifndef __OUTPUTS_H__
#define __OUTPUTS_H__

#include <time.h>
#include <event2/buffer.h>
#include "misc.h"

/* Outputs is a generic interface between the player and a media output method,
 * like for instance AirPlay (raop) or ALSA. The purpose of the interface is to
 * make it easier to add new outputs without messing too much with the player or
 * existing output methods.
 * 
 * An output method will have a general type, and it will be able to detect
 * supported devices that are available for output. A device will be typically
 * be something like an AirPlay speaker.
 *
 * When a device is started the output backend will typically create a session.
 * This session is only passed around as an opaque object in this interface.
 *
 * Here is the sequence of commands from the player to the outputs, and the
 * callback from the output once the command has been executed. Commands marked
 * with * may make multiple callbacks if multiple sessions are affected.
 * (TODO should callbacks always be deferred?)
 *
 * PLAYER              OUTPUT               PLAYER CB
 * speaker_activate    -> device_start      -> device_activate_cb
 *   -> (if playback)  -> playback_start    -> device_streaming_cb* (or no cb)
 *   -> (else if playback not active)       -> device_streaming_cb
 *   -> (fail)         -> device_stop       -> device_lost_cb
 * speaker_activate    -> device_probe      -> device_probe_cb
 * speaker_deactivate  -> device_stop       -> device_shutdown_cb
 * volume_set          -> device_volume_set -> device_command_cb
 *   ->                                     -> device_streaming_cb
 * (volume_setrel/abs_speaker is the same)
 * playback_start_item -> device_start      -> device_restart_cb
 *   -> (success)                           -> device_streaming_cb
 *   -> (fail)         -> device_stop       -> device_lost_cb
 * playback_start_bh   -> playback_start    -> device_streaming_cb* (or no cb)
 * playback_stop       -> flush             -> device_command_cb*
 *   ->                                     -> device_streaming_cb*
 *   ->                -> playback_stop     -> device_streaming_cb*
 * playback_pause      -> flush             -> device_command_cb*
 *   ->                                     -> device_streaming_cb*
 *   ->                -> playback_stop     -> device_streaming_cb*
 * playback_abort      -> playback_stop     -> device_streaming_cb* (or no cb)
 * device_streaming_cb                      -> device_streaming_cb (re-add)
 *
 */

// If an output requires a specific quality (like Airplay 1 devices often
// require 44100/16) then it should make a subscription request to the output
// module, which will then make sure to include this quality when it writes the
// audio. The below sets the maximum number of *different* subscriptions
// allowed. Note that multiple outputs requesting the *same* quality only counts
// as one.
#define OUTPUTS_MAX_QUALITY_SUBSCRIPTIONS 5

// Number of seconds the outputs should buffer before starting playback. Note
// this value cannot freely be changed because 1) some Airplay devices ignore
// the values we give and stick to 2 seconds, 2) those devices that can handle
// different values can only do so within a limited range (maybe max 3 secs)
#define OUTPUTS_BUFFER_DURATION 2

// Must be in sync with outputs[] in outputs.c
enum output_types
{
  OUTPUT_TYPE_RAOP,
  OUTPUT_TYPE_STREAMING,
  OUTPUT_TYPE_DUMMY,
  OUTPUT_TYPE_FIFO,
#ifdef HAVE_ALSA
  OUTPUT_TYPE_ALSA,
#endif
#ifdef HAVE_LIBPULSE
  OUTPUT_TYPE_PULSE,
#endif
#ifdef CHROMECAST
  OUTPUT_TYPE_CAST,
#endif
};

/* Output session state */
enum output_device_state
{
  // Device is stopped (no session)
  OUTPUT_STATE_STOPPED   = 0,
  // Device is starting up
  OUTPUT_STATE_STARTUP   = 1,
  // Session established (streaming ready and commands are possible)
  OUTPUT_STATE_CONNECTED = 2,
  // Media data is being sent
  OUTPUT_STATE_STREAMING = 3,
  // Session is failed, couldn't startup or error occurred
  OUTPUT_STATE_FAILED    = -1,
  // Password issue: unknown password or bad password
  OUTPUT_STATE_PASSWORD  = -2,
};

/* Linked list of device info used by the player for each device
 */
struct output_device
{
  // Device id
  uint64_t id;

  // Name of the device, e.g. "Living Room"
  char *name;

  // Type of the device, will be used to determine which output backend to call
  enum output_types type;

  // Type of output (string)
  const char *type_name;

  // Misc device flags 
  unsigned selected:1;
  unsigned advertised:1;
  unsigned has_password:1;
  unsigned has_video:1;
  unsigned requires_auth:1;
  unsigned v6_disabled:1;

  // Credentials if relevant
  const char *password;
  char *auth_key;

  // Device volume
  int volume;
  int relvol;

  // Quality of audio output
  struct media_quality quality;

  // Address
  char *v4_address;
  char *v6_address;
  short v4_port;
  short v6_port;

  // Opaque pointers to device and session data
  void *extra_device_info;
  void *session;

  struct output_device *next;
};

// Linked list of metadata prepared by each output backend
struct output_metadata
{
  enum output_types type;
  void *metadata;
  struct output_metadata *next;
};

struct output_frame
{
  struct media_quality quality;
  struct evbuffer *evbuf;
  uint8_t *buffer;
  size_t bufsize;
  int samples;
};

struct output_buffer
{
  uint32_t write_counter; // REMOVE ME? not used for anything
  struct timespec pts;
  struct output_frame frames[OUTPUTS_MAX_QUALITY_SUBSCRIPTIONS + 1];
} output_buffer;


typedef void (*output_status_cb)(struct output_device *device, enum output_device_state status);

struct output_definition
{
  // Name of the output
  const char *name;

  // Type of output
  enum output_types type;

  // Priority to give this output when autoselecting an output, 1 is highest
  // 1 = highest priority, 0 = don't autoselect
  int priority;

  // Set to 1 if the output initialization failed
  int disabled;

  // Initialization function called during startup
  // Output must call device_cb when an output device becomes available/unavailable
  int (*init)(void);

  // Deinitialization function called at shutdown
  void (*deinit)(void);

  // Prepare a playback session on device and call back
  int (*device_start)(struct output_device *device, output_status_cb cb);

  // Close a session prepared by device_start and call back
  int (*device_stop)(struct output_device *device, output_status_cb cb);

  // Test the connection to a device and call back
  int (*device_probe)(struct output_device *device, output_status_cb cb);

  // Free the private device data
  void (*device_free_extra)(struct output_device *device);

  // Set the volume and call back
  int (*device_volume_set)(struct output_device *device, output_status_cb cb);

  // Convert device internal representation of volume to our pct scale
  int (*device_volume_to_pct)(struct output_device *device, const char *volume);

  // Request a change of quality from the device
  int (*quality_set)(struct output_device *device, struct media_quality *quality);

  // Change the call back associated with a device
  void (*device_set_cb)(struct output_device *device, output_status_cb cb);

  // Start/stop playback on devices that were started
  void (*playback_start)(uint64_t next_pkt, struct timespec *ts);
  void (*playback_stop)(void);

  // Write stream data to the output devices
  void (*write)(struct output_buffer *buffer);

  // Flush all sessions, the return must be number of sessions pending the flush
  int (*flush)(output_status_cb cb);

  // Authorize an output with a pin-code (probably coming from the filescanner)
  void (*authorize)(const char *pin);

  // Metadata
  void *(*metadata_prepare)(int id);
  void (*metadata_send)(void *metadata, uint64_t rtptime, uint64_t offset, int startup);
  void (*metadata_purge)(void);
  void (*metadata_prune)(uint64_t rtptime);
};

int
outputs_device_start(struct output_device *device, output_status_cb cb);

int
outputs_device_stop(struct output_device *device, output_status_cb cb);

int
outputs_device_probe(struct output_device *device, output_status_cb cb);

void
outputs_device_free(struct output_device *device);

int
outputs_device_volume_set(struct output_device *device, output_status_cb cb);

int
outputs_device_volume_to_pct(struct output_device *device, const char *value);

// TODO should this function have a callback?
int
outputs_device_quality_set(struct output_device *device, struct media_quality *quality);

void
outputs_device_set_cb(struct output_device *device, output_status_cb cb);

void
outputs_playback_stop(void);

void
outputs_write(void *buf, size_t bufsize, struct media_quality *quality, int nsamples, struct timespec *pts);

int
outputs_flush(output_status_cb cb);

struct output_metadata *
outputs_metadata_prepare(int id);

void
outputs_metadata_send(struct output_metadata *omd, uint64_t rtptime, uint64_t offset, int startup);

void
outputs_metadata_purge(void);

void
outputs_metadata_prune(uint64_t rtptime);

void
outputs_metadata_free(struct output_metadata *omd);

void
outputs_authorize(enum output_types type, const char *pin);

int
outputs_quality_subscribe(struct media_quality *quality);

void
outputs_quality_unsubscribe(struct media_quality *quality);

int
outputs_priority(struct output_device *device);

const char *
outputs_name(enum output_types type);

int
outputs_init(void);

void
outputs_deinit(void);

#endif /* !__OUTPUTS_H__ */
