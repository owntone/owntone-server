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
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <event2/event.h>
#include <event2/buffer.h>

#include "misc.h"
#include "logger.h"
#include "db.h"
#include "conffile.h"
#include "listener.h"
#include "player.h"
#include "input.h"

// Maximum number of pipes to watch for data
#define PIPE_MAX_OPEN 4
// Max number of bytes to read from a pipe at a time (
#define PIPE_READ_MAX 65536

// filescanner event base, from filescanner.c
// TODO don't use filescanner thread/base
extern struct event_base *evbase_scan;

enum pipestate
{
  PIPE_NEW  = (1 << 0),
  PIPE_DEL  = (1 << 1),
  PIPE_OPEN = (1 << 2),
};

struct pipe
{
  int id;
  int fd;
  char *path;
  enum pipestate state;
  struct event *ev;
  // TODO mutex

  struct pipe *next;
};

// From config - should we watch library pipes for data or only start on request
static int pipe_autostart;

// Global list of pipes we are watching. If watching/autostart is disabled this
// will just point to the currently playing pipe (if any).
static struct pipe *pipelist;


/* -------------------------------- HELPERS ------------------------------- */

static struct pipe *
pipe_new(const char *path, int id)
{
  struct pipe *pipe;

  pipe = calloc(1, sizeof(struct pipe));
  pipe->path  = strdup(path);
  pipe->id    = id;
  pipe->fd    = -1;
  pipe->state = PIPE_NEW;
  pipe->next  = pipelist;
  pipelist    = pipe;

  return pipe;
}

static void
pipe_free(struct pipe *pipe)
{
  free(pipe->path);
  free(pipe);
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


/* ----------------------------- PIPE WATCHING ---------------------------- */
/*                            Thread: filescanner                           */

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

      pipe = pipe_new(dbmfi.path, id);
    }

  db_query_end(&qp);

  return count;
}

// Some data arrived on a pipe we watch - let's autostart playback
static void
pipe_read_cb(evutil_socket_t fd, short event, void *arg)
{
  struct pipe *pipe = arg;
  struct player_status status;
  struct db_queue_item *queue_item;
  int ret;

  DPRINTF(E_DBG, L_PLAYER, "Autostarting pipe %d, %d\n", (int) fd, (int)event);

  ret = player_get_status(&status);
  if ((ret < 0) || (status.status == PLAY_PLAYING))
    return;

  db_queue_clear();

  ret = db_queue_add_by_fileid(pipe->id, 0, 0);
  if (ret < 0)
    return;

  queue_item = db_queue_fetch_byfileid(pipe->id);
  if (!queue_item)
    return;

  player_playback_start_byitem(queue_item);

  free_queue_item(queue_item, 0);
}

/* Opens a pipe and starts watching it for data */
static int
pipe_open(struct pipe *pipe)
{
  struct stat sb;

  DPRINTF(E_DBG, L_PLAYER, "(Re)opening pipe: '%s'\n", pipe->path);

  if (lstat(pipe->path, &sb) < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not lstat() '%s': %s\n", pipe->path, strerror(errno));
      return -1;
    }

  if (!S_ISFIFO(sb.st_mode))
    {
      DPRINTF(E_LOG, L_PLAYER, "Source type is pipe, but path is not a fifo: %s\n", pipe->path);
      return -1;
    }

  pipe->fd = open(pipe->path, O_RDONLY | O_NONBLOCK);
  if (pipe->fd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not open pipe for reading '%s': %s\n", pipe->path, strerror(errno));
      return -1;
    }

  pipe->state = PIPE_OPEN;

  if (!pipe_autostart)
    return 0; // All done

  pipe->ev = event_new(evbase_scan, pipe->fd, EV_READ, pipe_read_cb, pipe);
  if (!pipe->ev)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not watch pipe for new data '%s'\n", pipe->path);
      return -1;
    }

  event_add(pipe->ev, NULL);

  return 0;
}

static void
pipe_close(struct pipe *pipe)
{
  if (pipe->fd < 0)
    return;

  if (pipe->ev)
    event_free(pipe->ev);

  close(pipe->fd);

  pipe->fd = -1;
}

static void
pipe_remove(struct pipe *pipe)
{
  struct pipe *p;

  pipe_close(pipe);

  if (pipe == pipelist)
    pipelist = pipe->next;
  else
    {
      for (p = pipelist; p && (p->next != pipe); p = p->next)
	; /* EMPTY */

      if (!p)
	{
	  DPRINTF(E_LOG, L_REMOTE, "WARNING: pipe not found in list; BUG!\n");
	  return;
	}

      p->next = pipe->next;
    }

  pipe_free(pipe);
}

static void
pipe_listener_cb(enum listener_event_type type)
{
  struct pipe *pipe;
  struct pipe *next;
  int count;

  count = pipe_enum(); // Count does not include pipes with state PIPE_DEL
  if (count < 0)
    return;

  for (pipe = pipelist; pipe; pipe = next)
    {
      next = pipe->next;

      DPRINTF(E_DBG, L_PLAYER, "Processing pipe '%s', state is %d\n", pipe->path, pipe->state);

      if ((pipe->state == PIPE_NEW) && (count > PIPE_MAX_OPEN))
	DPRINTF(E_LOG, L_PLAYER, "Max open pipes reached, will not watch %s\n", pipe->path);
      else if (pipe->state == PIPE_NEW)
	pipe_open(pipe);
      else if (pipe->state == PIPE_DEL)
	pipe_remove(pipe); // Note: Will free pipe
    }
}


/* -------------------------- PIPE INPUT INTERFACE ------------------------ */
/*                            Thread: player/input                          */

static int
pipe_metadata_open(struct pipe *pipe)
{
//  char md_pipe_path[PATH_MAX]; //TODO PATH_MAX - limits.h?
/*  snprintf(md_pipe_path, sizeof(md_pipe_path), "%s.metadata", pipe->path);

  fd = pipe_open(md_pipe_path); // TODO avoid lstat error
  if (fd < 0)
*/
  return 0;
}

static int
setup(struct player_source *ps)
{
  struct pipe *pipe;
  int ret;

  if (pipe_autostart)
    {
      pipe = pipe_find(ps->id);
      if (!pipe)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Unknown pipe '%s'\n", ps->path);
	  return -1;
	}
    }
  else
    pipe = pipe_new(ps->path, ps->id);

  if (pipe->state != PIPE_OPEN)
    {
      ret = pipe_open(pipe);
      if (ret < 0)
	return -1;
    }

  pipe_metadata_open(pipe);

  if (pipe->ev)
    event_del(pipe->ev); // Avoids autostarting pipe if manually started by user

  ps->setup_done = 1;

  return 0;
}

static int
start(struct player_source *ps)
{
  struct pipe *pipe;
  struct evbuffer *evbuf;
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
      if ( (ret == 0) || ((ret < 0) && (errno == EAGAIN)) )
	{
	  input_wait();
	  continue;
	}
      else if (ret < 0)
	{
	  DPRINTF(E_LOG, L_PLAYER, "Could not read from pipe: %s\n", strerror(errno));
	  break;
	}

      ret = input_write(evbuf, 0);
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
  if (pipe_autostart)
    {
      // Reopen pipe since if I just readd the event it instantly makes the
      // callback (probably something I have missed...)
      pipe_close(pipe);
      pipe_open(pipe);
    }
  else
    {
      pipe_remove(pipe);
    }

  ps->setup_done = 0;

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
      pipe_remove(pipe);
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
  .init = init,
  .deinit = deinit,
};

