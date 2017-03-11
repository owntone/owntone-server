
#ifndef __INPUT_H__
#define __INPUT_H__

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <event2/buffer.h>
#include "transcode.h"

// Must be in sync with inputs[] in input.c
enum input_types
{
  INPUT_TYPE_FILE,
  INPUT_TYPE_HTTP,
  INPUT_TYPE_PIPE,
#ifdef HAVE_SPOTIFY_H
  INPUT_TYPE_SPOTIFY,
#endif
};

enum input_flags
{
  // Write to input buffer must not block
  INPUT_FLAG_NONBLOCK = (1 << 0),
  // Flags end of file
  INPUT_FLAG_EOF      = (1 << 1),
  // Flags error reading file
  INPUT_FLAG_ERROR    = (1 << 2),
  // Flags possible new stream metadata
  INPUT_FLAG_METADATA = (1 << 3),
};

struct player_source
{
  /* Id of the file/item in the files database */
  uint32_t id;

  /* Item-Id of the file/item in the queue */
  uint32_t item_id;

  /* Length of the file/item in milliseconds */
  uint32_t len_ms;

  enum data_kind data_kind;
  enum media_kind media_kind;
  char *path;

  /* Start time of the media item as rtp-time
     The stream-start is the rtp-time the media item did or would have
     started playing (after seek or pause), therefor the elapsed time of the
     media item is always:
     elapsed time = current rtptime - stream-start */
  uint64_t stream_start;

  /* Output start time of the media item as rtp-time
     The output start time is the rtp-time of the first audio packet send
     to the audio outputs.
     It differs from stream-start especially after a seek, where the first audio
     packet has the next rtp-time as output start and stream start becomes the
     rtp-time the media item would have been started playing if the seek did
     not happen. */
  uint64_t output_start;

  /* End time of media item as rtp-time
     The end time is set if the reading (source_read) of the media item reached
     end of file, until then it is 0. */
  uint64_t end;

  /* Opaque pointer to data that the input sets up when called with setup(), and
   * which is cleaned up by the input with stop()
   */
  void *input_ctx;

  /* Input has completed setup of the source
   */
  int setup_done;

  struct player_source *play_next;
};

typedef int (*input_cb)(void);

struct input_metadata
{
  uint32_t item_id;

  int startup;

  uint64_t rtptime;
  uint64_t offset;

  // The player will update queue_item with the below
  uint32_t song_length;

  char *artist;
  char *title;
  char *album;
  char *genre;
  char *artwork_url;
};

struct input_definition
{
  // Name of the input
  const char *name;

  // Type of input
  enum input_types type;

  // Set to 1 if the input initialization failed
  char disabled;

  // Prepare a playback session
  int (*setup)(struct player_source *ps);

  // Starts playback loop (must be defined)
  int (*start)(struct player_source *ps);

  // Cleans up when playback loop has ended
  int (*stop)(struct player_source *ps);

  // Changes the playback position
  int (*seek)(struct player_source *ps, int seek_ms);

  // Return metadata
  int (*metadata_get)(struct input_metadata *metadata, struct player_source *ps, uint64_t rtptime);

  // Initialization function called during startup
  int (*init)(void);

  // Deinitialization function called at shutdown
  void (*deinit)(void);

};

/*
 * Input modules should use this to test if playback should end
 */
int input_loop_break;

/*
 * Transfer stream data to the player's input buffer. The input evbuf will be
 * drained on succesful write. This is to avoid copying memory. If the player's
 * input buffer is full the function will block until the write can be made
 * (unless INPUT_FILE_NONBLOCK is set).
 *
 * @in  evbuf    Raw audio data to write
 * @in  flags    One or more INPUT_FLAG_*
 * @return       0 on success, EAGAIN if buffer was full (and _NONBLOCK is set),
 *               -1 on error
 */
int
input_write(struct evbuffer *evbuf, short flags);

/*
 * Input modules can use this to wait in the playback loop (like input_write()
 * would have done)
 */
void
input_wait(void);

/*
 * Move a chunk of stream data from the player's input buffer to an output
 * buffer. Should only be called by the player thread. Will not block.
 *
 * @in  data     Output buffer
 * @in  size     How much data to move to the output buffer
 * @out flags    Flags INPUT_FLAG_*
 * @return       Number of bytes moved, -1 on error
 */
int
input_read(void *data, size_t size, short *flags);

/*
 * Player can set this to get a callback from the input when the input buffer
 * is full. The player may use this to resume playback after an underrun.
 *
 * @in  cb       The callback
 */
void
input_buffer_full_cb(input_cb cb);

/*
 * Initializes the given player source for playback
 */
int
input_setup(struct player_source *ps);

/*
 * Tells the input to start or resume playback, i.e. after calling this function
 * the input buffer will begin to fill up, and should be read periodically with
 * input_read(). Before calling this input_setup() must have been called.
 */
int
input_start(struct player_source *ps);

/*
 * Pauses playback of the given player source (stops playback loop) and flushes
 * the input buffer
 */
int
input_pause(struct player_source *ps);

/*
 * Stops playback loop (if running), flushes input buffer and cleans up the
 * player source
 */
int
input_stop(struct player_source *ps);

/*
 * Seeks playback position to seek_ms. Returns actual seek position, 0 on
 * unseekable, -1 on error. May block.
 */
int
input_seek(struct player_source *ps, int seek_ms);

/*
 * Flush input buffer. Output flags will be the same as input_read().
 */
void
input_flush(short *flags);

/*
 * Gets metadata from the input, returns 0 if metadata is set, otherwise -1
 */
int
input_metadata_get(struct input_metadata *metadata, struct player_source *ps, int startup, uint64_t rtptime);

/*
 * Free the entire struct
 */
void
input_metadata_free(struct input_metadata *metadata, int content_only);

/*
 * Called by player_init (so will run in main thread)
 */
int
input_init(void);

/*
 * Called by player_deinit (so will run in main thread)
 */
void
input_deinit(void);

#endif /* !__INPUT_H__ */
