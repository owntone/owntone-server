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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
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
#include "commands.h"
#include "mxml-compat.h"

// Maximum number of pipes to watch for data
#define PIPE_MAX_WATCH 4
// Max number of bytes to read from a pipe at a time
#define PIPE_READ_MAX 65536
// Max number of bytes to buffer from metadata pipes
#define PIPE_METADATA_BUFLEN_MAX 262144

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
  enum pipetype type;   // PCM (audio) or metadata
  event_callback_fn cb; // Callback when there is data to read
  struct event *ev;     // Event for the callback

  struct pipe *next;
};

union pipe_arg
{
  uint32_t id;
  struct pipe *pipelist;
};

// The usual thread stuff
static pthread_t tid_pipe;
static struct event_base *evbase_pipe;
static struct commands_base *cmdbase;

// From config - should we watch library pipes for data or only start on request
static int pipe_autostart;
// The mfi id of the pipe autostarted by the pipe thread
static int pipe_autostart_id;

// Global list of pipes we are watching (if watching/autostart is enabled)
static struct pipe *pipe_watch_list;

// Single pipe that we start watching for metadata after playback starts
static struct pipe *pipe_metadata;
// We read metadata into this evbuffer
static struct evbuffer *pipe_metadata_buf;
// Parsed metadata goes here
static struct input_metadata pipe_metadata_parsed;
// Mutex to share the parsed metadata
static pthread_mutex_t pipe_metadata_lock;
// True if there is new metadata to push to the player
static bool pipe_metadata_is_new;

/* -------------------------------- HELPERS ------------------------------- */

static struct pipe *
pipe_create(const char *path, int id, enum pipetype type, event_callback_fn cb)
{
  struct pipe *pipe;

  pipe = calloc(1, sizeof(struct pipe));
  pipe->path  = strdup(path);
  pipe->id    = id;
  pipe->fd    = -1;
  pipe->type  = type;
  pipe->cb    = cb;

  return pipe;
}

static void
pipe_free(struct pipe *pipe)
{
  free(pipe->path);
  free(pipe);
}

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
watch_add(struct pipe *pipe)
{
  bool silent;

  silent = (pipe->type == PIPE_METADATA);
  pipe->fd = pipe_open(pipe->path, silent);
  if (pipe->fd < 0)
    return -1;

  pipe->ev = event_new(evbase_pipe, pipe->fd, EV_READ, pipe->cb, pipe);
  if (!pipe->ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not watch pipe for new data '%s'\n", pipe->path);
      pipe_close(pipe->fd);
      return -1;
    }

  event_add(pipe->ev, NULL);

  return 0;
}

static void
watch_del(struct pipe *pipe)
{
  if (pipe->ev)
    event_free(pipe->ev);

  pipe_close(pipe->fd);

  pipe->fd = -1;
}

// If a read on pipe returns 0 it is an EOF, and we must close it and reopen it
// for renewed watching. The event will be freed and reallocated by this.
static int
watch_reset(struct pipe *pipe)
{
  watch_del(pipe);

  return watch_add(pipe);
}

static void
pipelist_add(struct pipe **list, struct pipe *pipe)
{
  pipe->next = *list;
  *list = pipe;
}

static void
pipelist_remove(struct pipe **list, struct pipe *pipe)
{
  struct pipe *prev = NULL;
  struct pipe *p;

  for (p = *list; p; p = p->next)
    {
      if (p->id == pipe->id)
	break;

      prev = p;
    }

  if (!p)
    return;

  if (!prev)
    *list = pipe->next;
  else
    prev->next = pipe->next;

  pipe_free(pipe);
}

static struct pipe *
pipelist_find(struct pipe *list, int id)
{
  struct pipe *p;

  for (p = list; p; p = p->next)
    {
      if (id == p->id)
	return p;
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

static void
parse_volume(const char *volume)
{
  char *volume_next;
  float airplay_volume;
  int local_volume;

  errno = 0;
  airplay_volume = strtof(volume, &volume_next);

  if ((errno == ERANGE) || (volume == volume_next))
    {
      DPRINTF(E_LOG, L_PLAYER, "Invalid Shairport airplay volume in string (%s): %s\n", volume,
	      (errno == ERANGE ? strerror(errno) : "First token is not a number."));
      return;
    }

  if (strcmp(volume_next, ",0.00,0.00,0.00") != 0)
    {
      DPRINTF(E_DBG, L_PLAYER, "Not applying Shairport airplay volume while software volume control is enabled (%s)\n", volume);
      return;
    }

  if (((int) airplay_volume) == -144)
    {
      DPRINTF(E_DBG, L_PLAYER, "Applying Shairport airplay volume ('mute', value: %.2f)\n", airplay_volume);
      player_volume_set(0);
    }
  else if (airplay_volume >= -30.0 && airplay_volume <= 0.0)
    {
      local_volume = (int)(100.0 + (airplay_volume / 30.0 * 100.0));

      DPRINTF(E_DBG, L_PLAYER, "Applying Shairport airplay volume (percent: %d, value: %.2f)\n", local_volume, airplay_volume);
      player_volume_set(local_volume);
    }
  else
    DPRINTF(E_LOG, L_PLAYER, "Shairport airplay volume out of range (-144.0, [-30.0 - 0.0]): %.2f\n", airplay_volume);
}

// returns 1 on metadata found, 0 on nothing, -1 on error
static int
parse_item(struct input_metadata *m, const char *item)
{
  mxml_node_t *xml;
  mxml_node_t *haystack;
  mxml_node_t *needle;
  const char *s;
  uint32_t type;
  uint32_t code;
  char *progress;
  char *volume;
  char **data;
  int ret;

  ret = 0;
  xml = mxmlNewXML("1.0");
  if (!xml)
    return -1;

//  DPRINTF(E_DBG, L_PLAYER, "Parsing %s\n", item);

  haystack = mxmlLoadString(xml, item, MXML_NO_CALLBACK);
  if (!haystack)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not parse pipe metadata\n");
      goto out_error;
    }

  type = 0;
  if ( (needle = mxmlFindElement(haystack, haystack, "type", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    sscanf(s, "%8x", &type);

  code = 0;
  if ( (needle = mxmlFindElement(haystack, haystack, "code", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    sscanf(s, "%8x", &code);

  if (!type || !code)
    {
      DPRINTF(E_LOG, L_PLAYER, "No type (%d) or code (%d) in pipe metadata, aborting\n", type, code);
      goto out_error;
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
  else if (code == dmapval("pvol"))
    data = &volume;
  else
    goto out_nothing;

  if ( (needle = mxmlFindElement(haystack, haystack, "data", NULL, NULL, MXML_DESCEND)) &&
       (s = mxmlGetText(needle, NULL)) )
    {
      pthread_mutex_lock(&pipe_metadata_lock);

      if (data != &progress && data != &volume)
	free(*data);

      *data = b64_decode(s);
      if (!*data)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Base64 decode of '%s' failed\n", s);

	  pthread_mutex_unlock(&pipe_metadata_lock);
	  goto out_error;
	}

      DPRINTF(E_DBG, L_PLAYER, "Read Shairport metadata (type=%8x, code=%8x): '%s'\n", type, code, *data);

      if (data == &progress)
	{
	  parse_progress(m, progress);
	  free(*data);
	}
      else if (data == &volume)
	{
	  parse_volume(volume);
	  free(*data);
	}

      pthread_mutex_unlock(&pipe_metadata_lock);

      ret = 1;
    }

 out_nothing:
  mxmlDelete(xml);
  return ret;

 out_error:
  mxmlDelete(xml);
  return -1;
}

static char *
extract_item(struct evbuffer *evbuf)
{
  struct evbuffer_ptr evptr;
  size_t size;
  char *item;

  evptr = evbuffer_search(evbuf, "</item>", strlen("</item>"), NULL);
  if (evptr.pos < 0)
    return NULL;

  size = evptr.pos + strlen("</item>") + 1;
  item = malloc(size);
  if (!item)
    return NULL;

  evbuffer_remove(evbuf, item, size - 1);
  item[size - 1] = '\0';

  return item;
}

static int
pipe_metadata_parse(struct input_metadata *m, struct evbuffer *evbuf)
{
  char *item;
  int found;
  int ret;

  found = 0;
  while ((item = extract_item(evbuf)))
    {
      ret = parse_item(m, item);
      free(item);
      if (ret < 0)
	return -1;
      if (ret > 0)
	found = 1;
    }

  return found;
}


/* ----------------------------- PIPE WATCHING ---------------------------- */
/*                                Thread: pipe                              */

// Some data arrived on a pipe we watch - let's autostart playback
static void
pipe_read_cb(evutil_socket_t fd, short event, void *arg)
{
  struct pipe *pipe = arg;
  struct player_status status;
  int ret;

  ret = player_get_status(&status);
  if (status.id == pipe->id)
    {
      DPRINTF(E_INFO, L_PLAYER, "Pipe '%s' already playing\n", pipe->path);
      return; // We are already playing the pipe
    }
  else if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Pipe autostart of '%s' failed because state of player is unknown\n", pipe->path);
      return;
    }

  DPRINTF(E_INFO, L_PLAYER, "Autostarting pipe '%s' (fd %d)\n", pipe->path, fd);

  player_playback_stop();

  ret = player_playback_start_byid(pipe->id);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Autostarting pipe '%s' (fd %d) failed\n", pipe->path, fd);
      return;
    }

  pipe_autostart_id = pipe->id;
}

static enum command_state
pipe_watch_reset(void *arg, int *retval)
{
  union pipe_arg *cmdarg = arg;
  struct pipe *pipe;

  pipe_autostart_id = 0;

  pipe = pipelist_find(pipe_watch_list, cmdarg->id);

  *retval = watch_reset(pipe);

  return COMMAND_END;
}

static enum command_state
pipe_watch_update(void *arg, int *retval)
{
  union pipe_arg *cmdarg = arg;
  struct pipe *pipelist;
  struct pipe *pipe;
  struct pipe *next;
  int count;

  if (cmdarg)
    pipelist = cmdarg->pipelist;
  else
    pipelist = NULL;

  // Removes pipes that are gone from the watchlist
  for (pipe = pipe_watch_list; pipe; pipe = next)
    {
      next = pipe->next;

      if (!pipelist_find(pipelist, pipe->id))
	{
	  DPRINTF(E_DBG, L_PLAYER, "Pipe watch deleted: '%s'\n", pipe->path);
	  watch_del(pipe);
	  pipelist_remove(&pipe_watch_list, pipe); // Will free pipe
	}
    }

  // Looks for new pipes and adds them to the watchlist
  for (pipe = pipelist, count = 0; pipe; pipe = next, count++)
    {
      next = pipe->next;

      if (count > PIPE_MAX_WATCH)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Max open pipes reached (%d), will not watch '%s'\n", PIPE_MAX_WATCH, pipe->path);
	  pipe_free(pipe);
	  continue;
	}

      if (!pipelist_find(pipe_watch_list, pipe->id))
	{
	  DPRINTF(E_DBG, L_PLAYER, "Pipe watch added: '%s'\n", pipe->path);
	  watch_add(pipe);
	  pipelist_add(&pipe_watch_list, pipe); // Changes pipe->next
	}
      else
	{
	  DPRINTF(E_DBG, L_PLAYER, "Pipe watch exists: '%s'\n", pipe->path);
	  pipe_free(pipe);
	}
    }

  *retval = 0;
  return COMMAND_END;
}

static void *
pipe_thread_run(void *arg)
{
  event_base_dispatch(evbase_pipe);

  pthread_exit(NULL);
}


/* -------------------------- METADATA PIPE HANDLING ---------------------- */
/*                               Thread: worker                             */

static void
pipe_metadata_watch_del(void *arg)
{
  if (!pipe_metadata)
    return;

  evbuffer_free(pipe_metadata_buf);
  watch_del(pipe_metadata);
  pipe_free(pipe_metadata);
  pipe_metadata = NULL;
}

// Some metadata arrived on a pipe we watch
static void
pipe_metadata_read_cb(evutil_socket_t fd, short event, void *arg)
{
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
      // Reset the pipe
      ret = watch_reset(pipe_metadata);
      if (ret < 0)
	return;
      goto readd;
    }

  if (evbuffer_get_length(pipe_metadata_buf) > PIPE_METADATA_BUFLEN_MAX)
    {
      DPRINTF(E_LOG, L_PLAYER, "Can't process data from metadata pipe, reading will stop\n");
      pipe_metadata_watch_del(NULL);
      return;
    }

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

  pipe_metadata = pipe_create(path, 0, PIPE_METADATA, pipe_metadata_read_cb);
  if (!pipe_metadata)
    return;

  pipe_metadata_buf = evbuffer_new();

  ret = watch_add(pipe_metadata);
  if (ret < 0)
    {
      evbuffer_free(pipe_metadata_buf);
      pipe_free(pipe_metadata);
      pipe_metadata = NULL;
      return;
    }
}


/* ---------------------- PIPE WATCH THREAD START/STOP -------------------- */
/*                            Thread: filescanner                           */

static void
pipe_thread_start(void)
{
  CHECK_NULL(L_PLAYER, evbase_pipe = event_base_new());
  CHECK_NULL(L_PLAYER, cmdbase = commands_base_new(evbase_pipe, NULL));
  CHECK_ERR(L_PLAYER, pthread_create(&tid_pipe, NULL, pipe_thread_run, NULL));

#if defined(HAVE_PTHREAD_SETNAME_NP)
  pthread_setname_np(tid_pipe, "pipe");
#elif defined(HAVE_PTHREAD_SET_NAME_NP)
  pthread_set_name_np(tid_pipe, "pipe");
#endif
}

static void
pipe_thread_stop(void)
{
  if (!tid_pipe)
    return;

  commands_exec_sync(cmdbase, pipe_watch_update, NULL, NULL);

  commands_base_destroy(cmdbase);
  pthread_join(tid_pipe, NULL);
  event_base_free(evbase_pipe);
  tid_pipe = 0;
}

// Makes a pipelist with pipe items from the db, returns NULL on no pipes
static struct pipe *
pipelist_create(void)
{
  struct query_params qp;
  struct db_media_file_info dbmfi;
  struct pipe *head;
  struct pipe *pipe;
  char filter[32];
  int id;
  int ret;

  memset(&qp, 0, sizeof(struct query_params));
  qp.type = Q_ITEMS;
  qp.filter = filter;

  snprintf(filter, sizeof(filter), "f.data_kind = %d", DATA_KIND_PIPE);

  ret = db_query_start(&qp);
  if (ret < 0)
    return NULL;

  head = NULL;
  while (((ret = db_query_fetch_file(&qp, &dbmfi)) == 0) && (dbmfi.id))
    {
      ret = safe_atoi32(dbmfi.id, &id);
      if (ret < 0)
	continue;

      pipe = pipe_create(dbmfi.path, id, PIPE_PCM, pipe_read_cb);
      pipelist_add(&head, pipe);
    }

  db_query_end(&qp);

  return head;
}

// Queries the db to see if any pipes are present in the library. If so, starts
// the pipe thread to watch the pipes. If no pipes in library, it will shut down
// the pipe thread.
static void
pipe_listener_cb(short event_mask)
{
  union pipe_arg *cmdarg;

  cmdarg = malloc(sizeof(union pipe_arg));
  if (!cmdarg)
    return;

  cmdarg->pipelist = pipelist_create();
  if (!cmdarg->pipelist)
    {
      pipe_thread_stop();
      free(cmdarg);
      return;
    }

  if (!tid_pipe)
    pipe_thread_start();

  commands_exec_async(cmdbase, pipe_watch_update, cmdarg);
}


/* -------------------------- PIPE INPUT INTERFACE ------------------------ */
/*                            Thread: player/input                          */

static int
setup(struct player_source *ps)
{
  struct pipe *pipe;
  int fd;

  fd = pipe_open(ps->path, 0);
  if (fd < 0)
    return -1;

  CHECK_NULL(L_PLAYER, pipe = pipe_create(ps->path, ps->id, PIPE_PCM, NULL));
  pipe->fd = fd;
  pipe->is_autostarted = (ps->id == pipe_autostart_id);

  worker_execute(pipe_metadata_watch_add, ps->path, strlen(ps->path) + 1, 0);

  ps->input_ctx = pipe;
  ps->setup_done = 1;

  return 0;
}

static int
start(struct player_source *ps)
{
  struct pipe *pipe = ps->input_ctx;
  struct evbuffer *evbuf;
  short flags;
  int ret;

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
	  DPRINTF(E_LOG, L_PLAYER, "Could not read from pipe '%s': %s\n", ps->path, strerror(errno));
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
  struct pipe *pipe = ps->input_ctx;
  union pipe_arg *cmdarg;

  DPRINTF(E_DBG, L_PLAYER, "Stopping pipe\n");

  pipe_close(pipe->fd);

  // Reset the pipe and start watching it again for new data. Must be async or
  // we will deadlock from the stop in pipe_read_cb().
  if (pipe_autostart && (cmdarg = malloc(sizeof(union pipe_arg))))
    {
      cmdarg->id = pipe->id;
      commands_exec_async(cmdbase, pipe_watch_reset, cmdarg);
    }

  if (pipe_metadata)
    worker_execute(pipe_metadata_watch_del, NULL, 0, 0);

  pipe_free(pipe);

  ps->input_ctx = NULL;
  ps->setup_done = 0;

  return 0;
}

static int
metadata_get(struct input_metadata *metadata, struct player_source *ps, uint64_t rtptime)
{
  pthread_mutex_lock(&pipe_metadata_lock);

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

  pthread_mutex_unlock(&pipe_metadata_lock);

  return 0;
}

// Thread: main
static int
init(void)
{
  CHECK_ERR(L_PLAYER, mutex_init(&pipe_metadata_lock));

  pipe_autostart = cfg_getbool(cfg_getsec(cfg, "library"), "pipe_autostart");
  if (pipe_autostart)
    {
      pipe_listener_cb(0);
      CHECK_ERR(L_PLAYER, listener_add(pipe_listener_cb, LISTENER_DATABASE));
    }

  return 0;
}

static void
deinit(void)
{
  if (pipe_autostart)
    {
      listener_remove(pipe_listener_cb);
      pipe_thread_stop();
    }

  CHECK_ERR(L_PLAYER, pthread_mutex_destroy(&pipe_metadata_lock));
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
