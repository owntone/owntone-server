
#ifndef __INPUT_H__
#define __INPUT_H__

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <event2/buffer.h>
#include "db.h"
#include "misc.h"

// Must be in sync with inputs[] in input.c
enum input_types
{
  INPUT_TYPE_FILE,
  INPUT_TYPE_HTTP,
  INPUT_TYPE_PIPE,
  INPUT_TYPE_TIMER,
#ifdef SPOTIFY
  INPUT_TYPE_SPOTIFY,
#endif
};

enum input_flags
{
  // Flags that input is closing current source
  INPUT_FLAG_START_NEXT = (1 << 0),
  // Flags end of file
  INPUT_FLAG_EOF        = (1 << 1),
  // Flags error reading file
  INPUT_FLAG_ERROR      = (1 << 2),
  // Flags possible new stream metadata
  INPUT_FLAG_METADATA   = (1 << 3),
  // Flags new stream quality
  INPUT_FLAG_QUALITY    = (1 << 4),
};

struct input_source
{
  // Type of input
  enum input_types type;

  // Item-Id of the file/item in the queue
  uint32_t item_id;

  // Id of the file/item in the files database
  uint32_t id;

  // Length of the file/item in milliseconds
  uint32_t len_ms;

  enum data_kind data_kind;
  enum media_kind media_kind;
  char *path;

  // Flags that the input has been opened (i.e. needs to be closed)
  bool open;

  // The below is private data for the input backend. It is optional for the
  // backend to use, so nothing in the input or player should depend on it!
  //
  // Opaque pointer to data that the input backend sets up when start() is
  // called, and that is cleaned up by the backend when stop() is called
  void *input_ctx;
  // Private evbuf. Alloc'ed by backend at start() and free'd at stop()
  struct evbuffer *evbuf;
  // Private source quality storage
  struct media_quality quality;
};

typedef int (*input_cb)(void);

struct input_metadata
{
  // queue_item id
  uint32_t item_id;

  // Input can override the default player progress by setting this. For the
  // other fields the receiver can check whether an update happened by checking
  // if it is non-zero/null, but not for pos_ms since 0 and even negative values
  // are valid.
  bool pos_is_updated;
  int32_t pos_ms;

  // Sets new song length (input will also update queue_item)
  uint32_t len_ms;

  // Input can update queue_item with the below
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
  int (*setup)(struct input_source *source);

  // One iteration of the playback loop (= a read operation from source)
  int (*play)(struct input_source *source);

  // Cleans up (only required when stopping source before it ends itself)
  int (*stop)(struct input_source *source);

  // Changes the playback position
  int (*seek)(struct input_source *source, int seek_ms);

  // Return metadata
  int (*metadata_get)(struct input_metadata *metadata, struct input_source *source);

  // Initialization function called during startup
  int (*init)(void);

  // Deinitialization function called at shutdown
  void (*deinit)(void);
};


/* ---------------------- Interface towards input backends ------------------ */
/*                           Thread: input and spotify                        */

/*
 * Transfer stream data to the player's input buffer. Data must be PCM-LE
 * samples. The input evbuf will be drained on succesful write. This is to avoid
 * copying memory. Thread-safe.
 *
 * @in  evbuf    Raw PCM_LE audio data to write
 * @in  evbuf    Quality of the PCM (sample rate etc.)
 * @in  flags    One or more INPUT_FLAG_*
 * @return       0 on success, EAGAIN if buffer was full, -1 on error
 */
int
input_write(struct evbuffer *evbuf, struct media_quality *quality, short flags);

/*
 * Input modules can use this to wait for the player to read, so the module's
 * playback-loop doesn't spin out of control.
 */
int
input_wait(void);

/* ---------------------- Interface towards player thread ------------------- */
/*                                Thread: player                              */

/*
 * Move a chunk of stream data from the player's input buffer to an output
 * buffer. Should only be called by the player thread. Will not block.
 *
 * @in  data     Output buffer
 * @in  size     How much data to move to the output buffer
 * @out flag     Flag INPUT_FLAG_*
 * @out flagdata Data associated with the flag, e.g. quality or metadata struct
 * @return       Number of bytes moved, -1 on error
 */
int
input_read(void *data, size_t size, short *flag, void **flagdata);

/*
 * Player can set this to get a callback from the input when the input buffer
 * is full. The player may use this to resume playback after an underrun.
 *
 * @in  cb       The callback
 */
void
input_buffer_full_cb(input_cb cb);

/*
 * Tells the input to start, i.e. after calling this function the input buffer
 * will begin to fill up, and should be read periodically with input_read(). If
 * called while another item is still open, it will be closed and the input
 * buffer will be flushed. This operation blocks.
 *
 * @in  item_id  Queue item id to start playing
 * @in  seek_ms  Position to start playing
 * @return       Actual seek position if seekable, 0 otherwise, -1 on error
 */
int
input_seek(uint32_t item_id, int seek_ms);

/*
 * Same as input_seek(), just non-blocking and does not offer seek.
 *
 * @in  item_id  Queue item id to start playing
 */
void
input_start(uint32_t item_id);

/*
 * Same as input_seek(), but non-blocking and if the item is already being read
 * we don't do anything (no flush & seek)
 *
 * @in  item_id  Queue item id to start playing
 * @in  seek_ms  Position to start playing
 */
void
input_resume(uint32_t item_id, int seek_ms);

/*
 * Stops the input and clears everything. Flushes the input buffer.
 */
void
input_stop(void);

/*
 * Flush input buffer. Output flags will be the same as input_read(). Call with
 * null pointer is valid.
 */
void
input_flush(short *flags);

/*
 * Free input_metadata
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
