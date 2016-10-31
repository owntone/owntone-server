/*
 * Copyright (C) 2016 Christian Meffert <christian.meffert@googlemail.com>
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


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <event2/event.h>

#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

#define FIFO_BUFFER_SIZE 65536 /* pipe capacity on Linux >= 2.6.11 */

struct fifo_session
{
  enum output_device_state state;

  char *path;

  int input_fd;
  int output_fd;

  int created;

  struct event *deferredev;
  output_status_cb defer_cb;

  /* Do not dereference - only passed to the status cb */
  struct output_device *device;
  struct output_session *output_session;
  output_status_cb status_cb;
};

/* From player.c */
extern struct event_base *evbase_player;

static struct fifo_session *sessions;

/* Forwards */
static void
defer_cb(int fd, short what, void *arg);


/* ---------------------------- FIFO HANDLING ---------------------------- */

static void
fifo_delete(struct fifo_session *fifo_session)
{
  DPRINTF(E_DBG, L_FIFO, "Removing FIFO \"%s\"\n", fifo_session->path);

  if (unlink(fifo_session->path) < 0)
    {
      DPRINTF(E_WARN, L_FIFO, "Could not remove FIFO \"%s\": %d\n", fifo_session->path, errno);
      return;
    }

  fifo_session->created = 0;
}

static void
fifo_close(struct fifo_session *fifo_session)
{
  struct stat st;

  if (fifo_session->input_fd > 0)
    {
      close(fifo_session->input_fd);
      fifo_session->input_fd = -1;
    }

  if (fifo_session->output_fd > 0)
    {
      close(fifo_session->output_fd);
      fifo_session->output_fd = -1;
    }

  if (fifo_session->created && (stat(fifo_session->path, &st) == 0))
    fifo_delete(fifo_session);
}

static int
fifo_make(struct fifo_session *fifo_session)
{
  DPRINTF(E_DBG, L_FIFO, "Creating FIFO \"%s\"\n", fifo_session->path);

  if (mkfifo(fifo_session->path, 0666) < 0)
    {
      DPRINTF(E_LOG, L_FIFO, "Could not create FIFO \"%s\": %d\n", fifo_session->path, errno);
      return -1;
    }

  fifo_session->created = 1;

  return 0;
}

static int
fifo_check(struct fifo_session *fifo_session)
{
  struct stat st;

  if (stat(fifo_session->path, &st) < 0)
    {
      if (errno == ENOENT)
	{
	  /* Path doesn't exist */
	  return fifo_make(fifo_session);
	}

      DPRINTF(E_LOG, L_FIFO, "Failed to stat FIFO \"%s\": %d\n", fifo_session->path, errno);
      return -1;
    }

  if (!S_ISFIFO(st.st_mode))
    {
      DPRINTF(E_LOG, L_FIFO, "\"%s\" already exists, but is not a FIFO\n", fifo_session->path);
      return -1;
    }

  return 0;
}

static int
fifo_open(struct fifo_session *fifo_session)
{
  int ret;

  ret = fifo_check(fifo_session);
  if (ret < 0)
    return -1;

  fifo_session->input_fd = open(fifo_session->path, O_RDONLY | O_NONBLOCK, 0);
  if (fifo_session->input_fd < 0)
    {
      DPRINTF(E_LOG, L_FIFO, "Could not open FIFO \"%s\" for reading: %d\n", fifo_session->path, errno);
      fifo_close(fifo_session);
      return -1;
    }

  fifo_session->output_fd = open(fifo_session->path, O_WRONLY | O_NONBLOCK, 0);
  if (fifo_session->output_fd < 0)
    {
      DPRINTF(E_LOG, L_FIFO, "Could not open FIFO \"%s\" for writing: %d\n", fifo_session->path, errno);
      fifo_close(fifo_session);
      return -1;
    }

  return 0;
}

static void
fifo_empty(struct fifo_session *fifo_session)
{
  char buf[FIFO_BUFFER_SIZE];
  int bytes = 1;

  while (bytes > 0 && errno != EINTR)
    bytes = read(fifo_session->input_fd, buf, FIFO_BUFFER_SIZE);

  if (bytes < 0 && errno != EAGAIN)
    {
      DPRINTF(E_LOG, L_FIFO, "Flush of FIFO \"%s\" failed: %d\n", fifo_session->path, errno);
    }
}

/* ---------------------------- SESSION HANDLING ---------------------------- */

static void
fifo_session_free(struct fifo_session *fifo_session)
{
  event_free(fifo_session->deferredev);

  free(fifo_session->output_session);
  free(fifo_session);

  fifo_session = NULL;
}

static void
fifo_session_cleanup(struct fifo_session *fifo_session)
{
  // Normally some here code to remove from linked list - here we just say:
  sessions = NULL;

  fifo_session_free(fifo_session);
}

static struct fifo_session *
fifo_session_make(struct output_device *device, output_status_cb cb)
{
  struct output_session *output_session;
  struct fifo_session *fifo_session;

  output_session = calloc(1, sizeof(struct output_session));
  fifo_session = calloc(1, sizeof(struct fifo_session));
  if (!output_session || !fifo_session)
    {
      DPRINTF(E_LOG, L_FIFO, "Out of memory for fifo session\n");
      return NULL;
    }

  fifo_session->deferredev = evtimer_new(evbase_player, defer_cb, fifo_session);
  if (!fifo_session->deferredev)
    {
      DPRINTF(E_LOG, L_FIFO, "Out of memory for fifo deferred event\n");
      free(output_session);
      free(fifo_session);
      return NULL;
    }

  output_session->session = fifo_session;
  output_session->type = device->type;

  fifo_session->output_session = output_session;
  fifo_session->state = OUTPUT_STATE_CONNECTED;
  fifo_session->device = device;
  fifo_session->status_cb = cb;

  fifo_session->created = 0;
  fifo_session->path = device->extra_device_info;
  fifo_session->input_fd = -1;
  fifo_session->output_fd = -1;

  sessions = fifo_session;

  return fifo_session;
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

// Maps our internal state to the generic output state and then makes a callback
// to the player to tell that state
static void
defer_cb(int fd, short what, void *arg)
{
  struct fifo_session *ds = arg;

  if (ds->defer_cb)
    ds->defer_cb(ds->device, ds->output_session, ds->state);

  if (ds->state == OUTPUT_STATE_STOPPED)
    fifo_session_cleanup(ds);
}

static void
fifo_status(struct fifo_session *fifo_session)
{
  fifo_session->defer_cb = fifo_session->status_cb;
  event_active(fifo_session->deferredev, 0, 0);
  fifo_session->status_cb = NULL;
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
fifo_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  struct fifo_session *fifo_session;
  int ret;

  fifo_session = fifo_session_make(device, cb);
  if (!fifo_session)
    return -1;

  ret = fifo_open(fifo_session);
  if (ret < 0)
    return -1;

  fifo_status(fifo_session);

  return 0;
}

static void
fifo_device_stop(struct output_session *output_session)
{
  struct fifo_session *fifo_session = output_session->session;

  fifo_close(fifo_session);

  fifo_session->state = OUTPUT_STATE_STOPPED;
  fifo_status(fifo_session);
}

static int
fifo_device_probe(struct output_device *device, output_status_cb cb)
{
  struct fifo_session *fifo_session;
  int ret;

  fifo_session = fifo_session_make(device, cb);
  if (!fifo_session)
    return -1;

  ret = fifo_open(fifo_session);
  if (ret < 0)
    {
      fifo_session_cleanup(fifo_session);
      return -1;
    }

  fifo_close(fifo_session);

  fifo_session->status_cb = cb;
  fifo_session->state = OUTPUT_STATE_STOPPED;

  fifo_status(fifo_session);

  return 0;
}

static int
fifo_device_volume_set(struct output_device *device, output_status_cb cb)
{
  struct fifo_session *fifo_session;

  if (!device->session || !device->session->session)
    return 0;

  fifo_session = device->session->session;

  fifo_session->status_cb = cb;
  fifo_status(fifo_session);

  return 1;
}

static void
fifo_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  struct fifo_session *fifo_session = sessions;

  if (!fifo_session)
    return;

  fifo_session->state = OUTPUT_STATE_STREAMING;
  fifo_status(fifo_session);
}

static void
fifo_playback_stop(void)
{
  struct fifo_session *fifo_session = sessions;

  if (!fifo_session)
    return;

  fifo_session->state = OUTPUT_STATE_CONNECTED;
  fifo_status(fifo_session);
}

static int
fifo_flush(output_status_cb cb, uint64_t rtptime)
{
  struct fifo_session *fifo_session = sessions;

  if (!fifo_session)
    return 0;

  fifo_empty(fifo_session);

  fifo_session->status_cb = cb;
  fifo_session->state = OUTPUT_STATE_CONNECTED;
  fifo_status(fifo_session);
  return 0;
}

static void
fifo_write(uint8_t *buf, uint64_t rtptime)
{
  struct fifo_session *fifo_session = sessions;
  size_t length = STOB(AIRTUNES_V2_PACKET_SAMPLES);
  ssize_t bytes;

  if (!fifo_session || !fifo_session->device->selected)
    return;

  while (1)
    {
      bytes = write(fifo_session->output_fd, buf, length);
      if (bytes > 0)
	return;

      if (bytes < 0)
	{
	  switch (errno)
	    {
	      case EAGAIN:
		/* The pipe is full, so empty it */
		fifo_empty(fifo_session);
		continue;
	      case EINTR:
		continue;
	    }

	  DPRINTF(E_LOG, L_FIFO, "Failed to write to FIFO %s: %d\n", fifo_session->path, errno);
	  return;
	}
    }
}

static void
fifo_set_status_cb(struct output_session *session, output_status_cb cb)
{
  struct fifo_session *fifo_session = session->session;

  fifo_session->status_cb = cb;
}

static int
fifo_init(void)
{
  struct output_device *device;
  cfg_t *cfg_fifo;
  char *nickname;
  char *path;

  cfg_fifo = cfg_getsec(cfg, "fifo");
  if (!cfg_fifo)
    return -1;

  path = cfg_getstr(cfg_fifo, "path");
  if (!path)
    return -1;

  nickname = cfg_getstr(cfg_fifo, "nickname");

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_FIFO, "Out of memory for fifo device\n");
      return -1;
    }

  device->id = 100;
  device->name = strdup(nickname);
  device->type = OUTPUT_TYPE_FIFO;
  device->type_name = outputs_name(device->type);
  device->advertised = 1;
  device->has_video = 0;
  device->extra_device_info = path;
  DPRINTF(E_INFO, L_FIFO, "Adding fifo output device '%s' with path '%s'\n", nickname, path);

  player_device_add(device);

  return 0;
}

static void
fifo_deinit(void)
{
  return;
}

struct output_definition output_fifo =
{
  .name = "fifo",
  .type = OUTPUT_TYPE_FIFO,
  .priority = 98,
  .disabled = 0,
  .init = fifo_init,
  .deinit = fifo_deinit,
  .device_start = fifo_device_start,
  .device_stop = fifo_device_stop,
  .device_probe = fifo_device_probe,
  .device_volume_set = fifo_device_volume_set,
  .playback_start = fifo_playback_start,
  .playback_stop = fifo_playback_stop,
  .write = fifo_write,
  .flush = fifo_flush,
  .status_cb = fifo_set_status_cb,
};
