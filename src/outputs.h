
#ifndef __OUTPUTS_H__
#define __OUTPUTS_H__

#include <stdbool.h>
#include <time.h>
#include <event2/event.h>
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
 * Many of the functions here use callbacks to the player to support async setup
 * etc. The general concept is that the player initiates an action, e.g. volume
 * change, and then the return value from the output function is the number of
 * callbacks the player should wait for. The output backend *must* make all the
 * callbacks, otherwise the player may hang.
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

// Whether the device should be *displayed* as selected is not given by
// device->selected, since that means "has the user selected the device",
// without taking into account whether it is working or available. This macro
// is a compound of the factors that determine how to display speaker selection.
#define OUTPUTS_DEVICE_DISPLAY_SELECTED(device) ((device)->selected && (device)->state >= OUTPUT_STATE_STOPPED && !(device)->busy && !(device)->prevent_playback)

// Forward declarations
struct output_device;
struct output_metadata;
enum output_device_state;

typedef void (*output_status_cb)(struct output_device *device, enum output_device_state status);
typedef int (*output_metadata_finalize_cb)(struct output_metadata *metadata);

// Must be in sync with outputs[] in outputs.c
enum output_types
{
  OUTPUT_TYPE_RAOP,
  OUTPUT_TYPE_AIRPLAY,
  OUTPUT_TYPE_STREAMING,
  OUTPUT_TYPE_DUMMY,
  OUTPUT_TYPE_FIFO,
  OUTPUT_TYPE_RCP,
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

  // Last state that the backend returned to the handlers in outputs.c. This
  // field must only be set in outputs.c (not in the backends/player).
  enum output_device_state state;

  // Misc device flags 
  unsigned selected:1;
  unsigned advertised:1;
  unsigned has_password:1;
  unsigned has_video:1;
  unsigned requires_auth:1;
  unsigned v6_disabled:1;
  unsigned prevent_playback:1;
  unsigned busy:1;
  unsigned resurrect:1;

  // Credentials if relevant
  const char *password;
  char *auth_key;

  // Device volume
  int volume;
  int relvol;
  int max_volume;

  // Quality of audio output
  struct media_quality quality;

  // Address
  char *v4_address;
  char *v6_address;
  short v4_port;
  short v6_port;

  struct event *stop_timer;

  // Opaque pointers to device and session data
  void *extra_device_info;
  void *session;

  struct output_device *next;
};

struct output_metadata
{
  enum output_types type;
  uint32_t item_id;

  // Progress data, filled out by finalize_cb()
  uint32_t pos_ms;
  uint32_t len_ms;
  struct timespec pts;
  bool startup;

  // Private output data made by the metadata_prepare()
  void *priv;

  struct event *ev;

  // Finalize before right before sending, e.g. set playback position
  output_metadata_finalize_cb finalize_cb;
};

struct output_data
{
  struct media_quality quality;
  struct evbuffer *evbuf;
  uint8_t *buffer;
  size_t bufsize;
  int samples;
};

struct output_buffer
{
  struct timespec pts;
  // The array is two larger than max quality subscriptions because element 0
  // holds the original, untranscoded, data (which might not have any
  // subscribers, and the last element is a zero terminator.
  struct output_data data[OUTPUTS_MAX_QUALITY_SUBSCRIPTIONS + 2];
};

struct output_definition
{
  // Name of the output
  const char *name;

  // Type of output
  enum output_types type;

  // Priority to give this output when autoselecting an output, or when
  // selectinga which output definition to use for a device that has multiple,
  // e.g. AirPlay 1 and 2.
  // 1 = highest priority, 0 = don't autoselect
  int priority;

  // Set to 1 if the output initialization failed
  int disabled;

  // Initialization function called during startup
  // Output must call device_cb when an output device becomes available/unavailable
  int (*init)(void);

  // Deinitialization function called at shutdown
  void (*deinit)(void);

  // For all the below that take callbacks, the return values are:
  // - negative: error
  // - zero:     ok, won't make a callback
  // - positive: number of callbacks that will be made

  // Prepare a playback session on device and call back
  int (*device_start)(struct output_device *device, int callback_id);

  // Close a session prepared by device_start and call back
  int (*device_stop)(struct output_device *device, int callback_id);

  // Flush device session and call back
  int (*device_flush)(struct output_device *device, int callback_id);

  // Test the connection to a device and call back
  int (*device_probe)(struct output_device *device, int callback_id);

  // Set the volume and call back
  int (*device_volume_set)(struct output_device *device, int callback_id);

  // Convert device internal representation of volume to our pct scale
  int (*device_volume_to_pct)(struct output_device *device, const char *volume);

  // Request a change of quality from the device
  int (*device_quality_set)(struct output_device *device, struct media_quality *quality, int callback_id);

  // Authorize the server to use the device
  int (*device_authorize)(struct output_device *device, const char *pin, int callback_id);

  // Change the call back associated with a device
  void (*device_cb_set)(struct output_device *device, int callback_id);

  // Free the private device data
  void (*device_free_extra)(struct output_device *device);

  // Write stream data to the output devices
  void (*write)(struct output_buffer *buffer);

  // Called from worker thread for async preparation of metadata (e.g. getting
  // artwork, which might involce downloading image data). The prepared data is
  // saved to metadata->priv, which metadata_send() can use.
  void *(*metadata_prepare)(struct output_metadata *metadata);

  // Send metadata to outputs. Ownership of *metadata is transferred.
  void (*metadata_send)(struct output_metadata *metadata);

  // Output will cleanup all metadata (so basically like flush but for metadata)
  void (*metadata_purge)(void);
};

/* ------------------------------- General use ------------------------------ */

struct output_device *
outputs_device_get(uint64_t device_id);

/* ----------------------- Called by backend modules ------------------------ */

int
outputs_device_session_add(uint64_t device_id, void *session);

void
outputs_device_session_remove(uint64_t device_id);

int
outputs_quality_subscribe(struct media_quality *quality);

void
outputs_quality_unsubscribe(struct media_quality *quality);

void
outputs_cb(int callback_id, uint64_t device_id, enum output_device_state);

void
outputs_metadata_free(struct output_metadata *metadata);

/* ---------------------------- Called by player ---------------------------- */

// Ownership of *add is transferred, so don't address after calling. Instead you
// can address the return value (which is not the same if the device was already
// in the list).
struct output_device *
outputs_device_add(struct output_device *add, bool new_deselect);

void
outputs_device_remove(struct output_device *remove);

void
outputs_device_select(struct output_device *device, int max_volume);

void
outputs_device_deselect(struct output_device *device);

int
outputs_device_start(struct output_device *device, output_status_cb cb, bool only_probe);

int
outputs_device_stop(struct output_device *device, output_status_cb cb);

int
outputs_device_stop_delayed(struct output_device *device, output_status_cb cb);

int
outputs_device_flush(struct output_device *device, output_status_cb cb);

void
outputs_device_volume_register(struct output_device *device, int absvol, int relvol);

int
outputs_device_volume_set(struct output_device *device, output_status_cb cb);

int
outputs_device_volume_to_pct(struct output_device *device, const char *value);

int
outputs_device_quality_set(struct output_device *device, struct media_quality *quality, output_status_cb cb);

int
outputs_device_authorize(struct output_device *device, const char *pin, output_status_cb cb);

void
outputs_device_cb_set(struct output_device *device, output_status_cb cb);

void
outputs_device_free(struct output_device *device);

int
outputs_start(output_status_cb started_cb, output_status_cb stopped_cb, bool only_probe);

int
outputs_stop(output_status_cb cb);

int
outputs_flush(output_status_cb cb);

int
outputs_volume_get(void);

int
outputs_volume_set(int volume, output_status_cb cb);

int
outputs_stop_delayed_cancel(void);

int
outputs_sessions_count(void);

void
outputs_write(void *buf, size_t bufsize, int nsamples, struct media_quality *quality, struct timespec *pts);

void
outputs_metadata_send(uint32_t item_id, bool startup, output_metadata_finalize_cb cb);

void
outputs_metadata_purge(void);

int
outputs_priority(struct output_device *device);

const char *
outputs_name(enum output_types type);

struct output_device *
outputs_list(void);

int
outputs_init(void);

void
outputs_deinit(void);

#endif /* !__OUTPUTS_H__ */
