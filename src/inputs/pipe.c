/*
 * Copyright (C) 2017 Espen Jurgensen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * About pipe.c
 * --------------
 * This module will read a PCM16 stream from a named pipe and write it to the
 * input buffer. The user may start/stop playback from a pipe by selecting it
 * through a client. If the user has configured pipe_autostart, then pipes in
 * the library will also be watched for data, and playback will start/stop
 * automatically.
 *
 * The module will also look for pipes with a .metadata suffix, and if found,
 * the metadata will be parsed and fed to the player. The metadata must be in
 * the format Shairport uses for this purpose.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <mxml.h>

#include "input.h"
#include "misc.h"
#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "listener.h"
#include "player.h"
#include "worker.h"
#include "mxml-compat.h"

// Maximum number of pipes to watch for data
#define PIPE_MAX_WATCH 4
// Max number of bytes to read from a pipe at a time
#define PIPE_READ_MAX 65536
// Max number of bytes to buffer from metadata pipes
#define PIPE_METADATA_BUFLEN_MAX 262144

extern struct event_base *evbase_worker;

enum pipestate
{
  PIPE_NEW,
  PIPE_DEL,
  PIPE_OPEN,
};

enum pipetype
{
  PIPE_PCM,
  PIPE_METADATA,
};

struct pipe
{
  int id;               // The mfi id of the pipe
  int fd;               // File descriptor
  bool is_autostarted;  // We autostarted the pipe (and we will autostop)
  char *path;           // Path
  enum pipestate state; // Newly appeared, marked for deletion, open/ready
  enum pipetype type;   // PCM (audio) or metadata
  event_callback_fn cb; // Callback when there is data to read
  struct event *ev;     // Event for the callback
  // TODO mutex

  struct pipe *next;
};

// From config - should we watch library pipes for data or only start on request
static int pipe_autostart;

// Global list of pipes we are watching. If watching/autostart is disabled this
// will just point to the currently playing pipe (if any).
static struct pipe *pipelist;

// Single pipe that we start watching for metadata after playback starts
static struct pipe *pipe_metadata;
// We read metadata into this evbuffer
static struct evbuffer *pipe_metadata_buf;
// Parsed metadata goes here
static struct input_metadata pipe_metadata_parsed;
// True if there is new metadata to push to the player
static bool pipe_metadata_is_new;

/* ------------------------------- FORWARDS ------------------------------- */

static void
pipe_watch_reset(struct pipe *pipe);

static void
pipe_metadata_watch_del(void *arg);

/* -------------------------------- HELPERS ------------------------------- */

static struct pipe *
pipe_new(const char *path, int id, enum pipetype type, event_callback_fn cb)
{
  struct pipe *pipe;

  pipe = calloc(1, sizeof(struct pipe));
  pipe->path  = strdup(path);
  pipe->id    = id;
  pipe->fd    = -1;
  pipe->state = PIPE_NEW;
  pipe->type  = type;
  pipe->cb    = cb;

  if (type == PIPE_PCM)
    {
      pipe->next  = pipelist;
      pipelist    = pipe;
    }

  return pipe;
}

static void
pipe_free(struct pipe *pipe)
{
  free(pipe->path);
  free(pipe);
}

static void
pipelist_prune(void)
{
  struct pipe *pipe;
  struct pipe *next;

  for (pipe = pipelist; pipe; pipe = next)
    {
      next = pipe->next;

      if (pipelist->state == PIPE_DEL)
	{
	  pipe_free(pipelist);
	  pipelist = next;
	}
      else if (next && (next->state == PIPE_DEL))
	{
	  pipe->next = next->next;
	  pipe_free(next);
	  next = pipe->next;
	}
    }
}

static struct pipe *
pipe_find(int id)
{
  struct pipe *pipe;

  for (pipe = pipelist; pipe; pipe = pipe->next)
    {
      if (id == pipe->id)
	return pipe;
    }

  return NULL;
}

// Convert to macro?
static inline uint32_t
dmapval(const char s[4])
{
  return ((s[0] << 24) | (s[1] << 16) | (s[2] << 8) | (s[3] << 0));
}

static void
parse_progress(struct input_metadata *m, char *progress)
{
  char *s;
  char *ptr;
  uint64_t start;
  uint64_t pos;
  uint64_t end;

  if (!(s = strtok_r(progress, "/", &ptr)))
    return;
  safe_atou64(s, &start);

  if (!(s = strtok_r(NULL, "/", &ptr)))
    return;
  safe_atou64(s, &pos);

  if (!(s = strtok_r(NULL, "/", &ptr)))
    return;
  safe_atou64(s, &end);

  if (!start || !pos || !end)
    return;

  m->rtptime = start; // Not actually used - we have our own rtptime
  m->offset = (pos > start) ? (pos - start) : 0;
  m->song_length = (end - start) * 10 / 441; // Convert to ms based on 44100
}

// returns 0 on metadata found, otherwise -1
static int
parse_item(struct input_metadata *m, mxml_node_t *item)
{
  mxml_node_t *needle;
  const char *s;
  uint32_t type;
  uint32_t code;
  char *progress;
  char **data;

  type = 0;
  if ( (needle = mxmlFindElement(item, item, "type", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    sscanf(s, "%8x", &type);

  code = 0;
  if ( (needle = mxmlFindElement(item, item, "code", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    sscanf(s, "%8x", &code);

  if (!type || !code)
    {
      DPRINTF(E_WARN, L_PLAYER, "No type (%d) or code (%d) in pipe metadata\n", type, code);
      return -1;
    }

  if (code == dmapval("asal"))
    data = &m->album;
  else if (code == dmapval("asar"))
    data = &m->artist;
  else if (code == dmapval("minm"))
    data = &m->title;
  else if (code == dmapval("asgn"))
    data = &m->genre;
  else if (code == dmapval("prgr"))
    data = &progress;
  else
    return -1;

  if ( (needle = mxmlFindElement(item, item, "data", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    {
      if (data != &progress)
	free(*data);

      *data = b64_decode(s);

      DPRINTF(E_DBG, L_PLAYER, "Read Shairport metadata (type=%8x, code=%8x): '%s'\n", type, code, *data);

      if (data == &progress)
	{
	  parse_progress(m, progress);
	  free(*data);
	}

      return 0;
    }

  return -1;
}

static int
pipe_metadata_parse(struct input_metadata *m, struct evbuffer *evbuf)
{
  mxml_node_t *xml;
  mxml_node_t *haystack;
  mxml_node_t *item;
  const char *s;
  int found;

  xml = mxmlNewXML("1.0");
  if (!xml)
    return -1;

  s = (char *)evbuffer_pullup(evbuf, -1);
  haystack = mxmlLoadString(xml, s, MXML_NO_CALLBACK);
  if (!haystack)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not parse pipe metadata\n");
      mxmlDelete(xml);
      evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
      return -1;
    }

  evbuffer_drain(evbuf, evbuffer_get_length(evbuf));

//  DPRINTF(E_DBG, L_PLAYER, "Parsing %s\n", s);

  found = 0;
  for (item = mxmlGetFirstChild(haystack); item; item = mxmlWalkNext(item, haystack, MXML_NO_DESCEND))
    {
      if (mxmlGetType(item) != 0)
        continue;

      if (parse_item(m, item) == 0)
	found = 1;
    }

  mxmlDelete(xml);
  return found;
}

/* ---------------------------- GENERAL PIPE I/O -------------------------- */
/*                               Thread: worker                             */

// Some data arrived on a pipe we watch - let's autostart playback
static void
pipe_read_cb(evutil_socket_t fd, short event, void *arg)
{
  struct pipe *pipe = arg;
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  ret = player_get_status(&status);
  if ((ret < 0) || (status.status == PLAY_PLAYING))
    {
      DPRINTF(E_LOG, L_PLAYER, "Data arrived on pipe '%s', but player is busy\n", pipe->path);
      pipe_watch_reset(pipe);
      return;
    }

  DPRINTF(E_INFO, L_PLAYER, "Autostarting pipe '%s' (fd %d)\n", pipe->path, fd);

  db_queue_clear();

  ret = db_queue_add_by_fileid(pipe->id, 0, 0);
  if (ret < 0)
    return;

  queue_item = db_queue_fetch_byfileid(pipe->id);
  if (!queue_item)
    return;

  player_playback_start_byitem(queue_item);

  pipe->is_autostarted = 1;

  free_queue_item(queue_item, 0);
}

// Updates pipelist with pipe items from the db. Pipes that are no longer in
// the db get marked with PIPE_DEL. Returns count of pipes that should be
// watched (may or may not equal length of pipelist)
static int
pipe_enum(void)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct pipe *pipe;
  char filter[32];
  int count;
  int id;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_ITEMS;
  qp.filter = filter;

  snprintf(filter, sizeof(filter), "f.data_kind = %d", DATA_KIND_PIPE);

  ret = db_query_start(&qp);
  if (ret < 0)
    return -1;

  for (pipe = pipelist; pipe; pipe = pipe->next)
    pipe->state = PIPE_DEL;

  count = 0;
  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = safe_atoi32(dbmfi.id, &id);
      if (ret < 0)
	continue;

      count++;

      if ((pipe = pipe_find(id)))
	{
	  pipe->state = PIPE_OPEN;
	  continue;
	}

      pipe = pipe_new(dbmfi.path, id, PIPE_PCM, pipe_read_cb);
    }

  db_query_end(&qp);

  return count;
}

// Opens a pipe and starts watching it for data if autostart is configured
static int
pipe_open(const char *path, bool silent)
{
  struct stat sb;
  int fd;

  DPRINTF(E_DBG, L_PLAYER, "(Re)opening pipe: '%s'\n", path);

  if (lstat(path, &sb) < 0)
    {
      if (!silent)
	DPRINTF(E_LOG, L_PLAYER, "Could not lstat() '%s': %s\n", path, strerror(errno));
      return -1;
    }

  if (!S_ISFIFO(sb.st_mode))
    {
      DPRINTF(E_LOG, L_PLAYER, "Source type is pipe, but path is not a fifo: %s\n", path);
      return -1;
    }

  fd = open(path, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not open pipe for reading '%s': %s\n", path, strerror(errno));
      return -1;
    }

  return fd;
}

static void
pipe_close(int fd)
{
  if (fd >= 0)
    close(fd);
}

static int
pipe_watch_add(struct pipe *pipe)
{
  bool silent;

  silent = (pipe->type == PIPE_METADATA);
  pipe->fd = pipe_open(pipe->path, silent);
  if (pipe->fd < 0)
    return -1;

  pipe->ev = event_new(evbase_worker, pipe->fd, EV_READ, pipe->cb, pipe);
  if (!pipe->ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not watch pipe for new data '%s'\n", pipe->path);
      pipe_close(pipe->fd);
      return -1;
    }

  event_add(pipe->ev, NULL);

  pipe->state = PIPE_OPEN;

  return 0;
}

static void
pipe_watch_del(struct pipe *pipe)
{
  if (pipe->ev)
    event_free(pipe->ev);

  pipe_close(pipe->fd);

  pipe->fd = -1;
}

// If a read on pipe returns 0 it is an EOF, and we must close it and reopen it
// for renewed watching. The event will be freed and reallocated by this.
static void
pipe_watch_reset(struct pipe *pipe)
{
  pipe_watch_del(pipe);
  pipe_watch_add(pipe);
}

static void
pipe_watch_update(void *arg)
{
  struct pipe *pipe;
  int count;

  count = pipe_enum(); // Count does not include pipes with state PIPE_DEL
  if (count < 0)
    return;

  for (pipe = pipelist; pipe; pipe = pipe->next)
    {
      DPRINTF(E_DBG, L_PLAYER, "Processing pipe '%s', state is %d\n", pipe->path, pipe->state);

      if ((pipe->state == PIPE_NEW) && (count > PIPE_MAX_WATCH))
	DPRINTF(E_LOG, L_PLAYER, "Max open pipes reached, will not watch %s\n", pipe->path);
      else if (pipe->state == PIPE_NEW)
	pipe_watch_add(pipe);
      else if (pipe->state == PIPE_DEL)
	pipe_watch_del(pipe);
    }

  pipelist_prune();
}

// Thread: filescanner
static void
pipe_listener_cb(enum listener_event_type type)
{
  worker_execute(pipe_watch_update, NULL, 0, 0);
}

/* -------------------------- METADATA PIPE HANDLING ---------------------- */
/*                               Thread: worker                             */

// Some metadata arrived on a pipe we watch
static void
pipe_metadata_read_cb(evutil_socket_t fd, short event, void *arg)
{
  struct evbuffer_ptr evptr;
  int ret;

  ret = evbuffer_read(pipe_metadata_buf, pipe_metadata->fd, PIPE_READ_MAX);
  if (ret < 0)
    {
      if (errno != EAGAIN)
	pipe_metadata_watch_del(NULL);
      return;
    }
  else if (ret == 0)
    {
      pipe_watch_reset(pipe_metadata);
      goto readd;
    }

  if (evbuffer_get_length(pipe_metadata_buf) > PIPE_METADATA_BUFLEN_MAX)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't process data from metadata pipe, reading will stop\n");
      pipe_metadata_watch_del(NULL);
      return;
    }

  // Did we get the end tag? If not return to wait for more data
  evptr = evbuffer_search(pipe_metadata_buf, "</item>", strlen("</item>"), NULL);
  if (evptr.pos < 0)
    {
      goto readd;
    }

  // NULL-terminate the buffer
  evbuffer_add(pipe_metadata_buf, "", 1);

  ret = pipe_metadata_parse(&pipe_metadata_parsed, pipe_metadata_buf);
  if (ret < 0)
    {
      pipe_metadata_watch_del(NULL);
      return;
    }
  else if (ret > 0)
    {
      // Trigger notification in playback loop
      pipe_metadata_is_new = 1;
    }

 readd:
  if (pipe_metadata && pipe_metadata->ev)
    event_add(pipe_metadata->ev, NULL);
}

static void
pipe_metadata_watch_add(void *arg)
{
  char *base_path = arg;
  char path[PATH_MAX];
  int ret;

  ret = snprintf(path, sizeof(path), "%s.metadata", base_path);
  if ((ret < 0) || (ret > sizeof(path)))
    return;

  pipe_metadata = pipe_new(path, 0, PIPE_METADATA, pipe_metadata_read_cb);
  if (!pipe_metadata)
    return;

  pipe_metadata_buf = evbuffer_new();

  ret = pipe_watch_add(pipe_metadata);
  if (ret < 0)
    {
      evbuffer_free(pipe_metadata_buf);
      pipe_free(pipe_metadata);
      pipe_metadata = NULL;
      return;
    }
}

static void
pipe_metadata_watch_del(void *arg)
{
  if (!pipe_metadata)
    return;

  evbuffer_free(pipe_metadata_buf);
  pipe_watch_del(pipe_metadata);
  pipe_free(pipe_metadata);
  pipe_metadata = NULL;
}


/* -------------------------- PIPE INPUT INTERFACE ------------------------ */
/*                            Thread: player/input                          */

static int
setup(struct player_source *ps)
{
  struct pipe *pipe;

  // If autostart is disabled then this is the first time we encounter the pipe
  if (!pipe_autostart)
    pipe_new(ps->path, ps->id, PIPE_PCM, NULL);

  pipe = pipe_find(ps->id);
  if (!pipe)
    {
      DPRINTF(E_LOG, L_PLAYER, "Unknown pipe '%s'\n", ps->path);
      return -1;
    }
// TODO pipe mutex here
  if (pipe->state != PIPE_OPEN)
    {
      pipe->fd = pipe_open(pipe->path, 0);
      if (pipe->fd < 0)
	return -1;
      pipe->state = PIPE_OPEN;
    }

  if (pipe->ev)
    event_del(pipe->ev); // Avoids autostarting pipe if manually started by user

  worker_execute(pipe_metadata_watch_add, pipe->path, strlen(pipe->path) + 1, 0);

  ps->setup_done = 1;

  return 0;
}

static int
start(struct player_source *ps)
{
  struct pipe *pipe;
  struct evbuffer *evbuf;
  short flags;
  int ret;

  pipe = pipe_find(ps->id);
  if (!pipe)
    return -1;

  evbuf = evbuffer_new();
  if (!evbuf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for pipe evbuf\n");
      return -1;
    }

  ret = -1;
  while (!input_loop_break)
    {
      ret = evbuffer_read(evbuf, pipe->fd, PIPE_READ_MAX);
      if ((ret == 0) && (pipe->is_autostarted))
	{
	  input_write(evbuf, INPUT_FLAG_EOF); // Autostop
	  break;
	}
      else if ((ret == 0) || ((ret < 0) && (errno == EAGAIN)))
	{
	  input_wait();
	  continue;
	}
      else if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not read from pipe: %s\n", strerror(errno));
	  break;
	}

      flags = (pipe_metadata_is_new ? INPUT_FLAG_METADATA : 0);
      pipe_metadata_is_new = 0;

      ret = input_write(evbuf, flags);
      if (ret < 0)
	break;
    }

  evbuffer_free(evbuf);

  return ret;
}

static int
stop(struct player_source *ps)
{
  struct pipe *pipe;

  DPRINTF(E_DBG, L_PLAYER, "Stopping pipe\n");

  pipe = pipe_find(ps->id);
  if (!pipe)
    {
      DPRINTF(E_LOG, L_PLAYER, "Unknown pipe '%s'\n", ps->path);
      return -1;
    }

  if (!pipe_autostart)
    {
      // Since autostart is disabled we are now done with the pipe
      pipe_close(pipe->fd);
      pipe->state = PIPE_DEL;
      pipelist_prune();
    }
  else
    {
      // Reset the pipe and start watching it again for new data
      pipe->is_autostarted = 0;
      pipe_watch_reset(pipe);
    }

  if (pipe_metadata)
    worker_execute(pipe_metadata_watch_del, NULL, 0, 0);

  ps->setup_done = 0;

  return 0;
}

// FIXME Thread safety of pipe_metadata_parsed
static int
metadata_get(struct input_metadata *metadata, struct player_source *ps, uint64_t rtptime)
{
  if (pipe_metadata_parsed.artist)
    swap_pointers(&metadata->artist, &pipe_metadata_parsed.artist);
  if (pipe_metadata_parsed.title)
    swap_pointers(&metadata->title, &pipe_metadata_parsed.title);
  if (pipe_metadata_parsed.album)
    swap_pointers(&metadata->album, &pipe_metadata_parsed.album);
  if (pipe_metadata_parsed.genre)
    swap_pointers(&metadata->genre, &pipe_metadata_parsed.genre);
  if (pipe_metadata_parsed.artwork_url)
    swap_pointers(&metadata->artwork_url, &pipe_metadata_parsed.artwork_url);

  if (pipe_metadata_parsed.song_length)
    {
      if (rtptime > ps->stream_start)
	metadata->rtptime = rtptime - pipe_metadata_parsed.offset;
      metadata->offset = pipe_metadata_parsed.offset;
      metadata->song_length = pipe_metadata_parsed.song_length;
    }

  input_metadata_free(&pipe_metadata_parsed, 1);

  return 0;
}

// Thread: main
static int
init(void)
{
  pipe_autostart = cfg_getbool(cfg_getsec(cfg, "library"), "pipe_autostart");

  if (pipe_autostart)
    return listener_add(pipe_listener_cb, LISTENER_DATABASE);
  else
    return 0;
}

static void
deinit(void)
{
  struct pipe *pipe;

  for (pipe = pipelist; pipelist; pipe = pipelist)
    {
      pipelist = pipe->next;
      pipe_watch_del(pipe);
      pipe_free(pipe);
    }

  if (pipe_autostart)
    listener_remove(pipe_listener_cb);
}

struct input_definition input_pipe =
{
  .name = "pipe",
  .type = INPUT_TYPE_PIPE,
  .disabled = 0,
  .setup = setup,
  .start = start,
  .stop = stop,
  .metadata_get = metadata_get,
  .init = init,
  .deinit = deinit,
};

