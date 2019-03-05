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

#include "misc.h"
#include "conffile.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

#define FIFO_BUFFER_SIZE 65536 // pipe capacity on Linux >= 2.6.11
#define FIFO_PACKET_SIZE 1408  // 352 samples/packet * 16 bit/sample * 2 channels

struct fifo_packet
{
  /* pcm data */
  uint8_t *samples;
  size_t samples_size;

  /* Presentation timestamp of the first sample */
  struct timespec pts;

  struct fifo_packet *next;
  struct fifo_packet *prev;
};

struct fifo_buffer
{
  struct fifo_packet *head;
  struct fifo_packet *tail;
};

static struct fifo_buffer buffer;

static struct media_quality fifo_quality = { 44100, 16, 2 };


static void
free_buffer()
{
  struct fifo_packet *packet;
  struct fifo_packet *tmp;

  packet = buffer.tail;
  while (packet)
    {
      tmp = packet;
      packet = packet->next;
      free(tmp);
    }

  buffer.tail = NULL;
  buffer.head = NULL;
}

struct fifo_session
{
  enum output_device_state state;

  char *path;

  int input_fd;
  int output_fd;

  int created;

  uint64_t device_id;
  int callback_id;
};

static struct fifo_session *sessions;


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
  if (!fifo_session)
    return;

  free(fifo_session);
  free_buffer();
}

static void
fifo_session_cleanup(struct fifo_session *fifo_session)
{
  // Normally some here code to remove from linked list - here we just say:
  sessions = NULL;

  outputs_device_session_remove(fifo_session->device_id);

  fifo_session_free(fifo_session);
}

static struct fifo_session *
fifo_session_make(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session;

  CHECK_NULL(L_FIFO, fifo_session = calloc(1, sizeof(struct fifo_session)));

  fifo_session->state = OUTPUT_STATE_CONNECTED;
  fifo_session->device_id = device->id;
  fifo_session->callback_id = callback_id;

  fifo_session->created = 0;
  fifo_session->path = device->extra_device_info;
  fifo_session->input_fd = -1;
  fifo_session->output_fd = -1;

  sessions = fifo_session;

  outputs_device_session_add(device->id, fifo_session);

  return fifo_session;
}


/* ---------------------------- STATUS HANDLERS ----------------------------- */

static void
fifo_status(struct fifo_session *fifo_session)
{
  outputs_cb(fifo_session->callback_id, fifo_session->device_id, fifo_session->state);

  if (fifo_session->state == OUTPUT_STATE_STOPPED)
    fifo_session_cleanup(fifo_session);
}

/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
fifo_device_start(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session;
  int ret;

  ret = outputs_quality_subscribe(&fifo_quality);
  if (ret < 0)
    return -1;

  fifo_session = fifo_session_make(device, callback_id);
  if (!fifo_session)
    return -1;

  ret = fifo_open(fifo_session);
  if (ret < 0)
    return -1;

  fifo_status(fifo_session);

  return 0;
}

static int
fifo_device_stop(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session = device->session;

  outputs_quality_unsubscribe(&fifo_quality);

  fifo_session->callback_id = callback_id;

  fifo_close(fifo_session);
  free_buffer();

  fifo_session->state = OUTPUT_STATE_STOPPED;
  fifo_status(fifo_session);

  return 0;
}

static int
fifo_device_flush(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session = device->session;

  fifo_empty(fifo_session);
  free_buffer();

  fifo_session->callback_id = callback_id;
  fifo_session->state = OUTPUT_STATE_CONNECTED;
  fifo_status(fifo_session);

  return 0;
}

static int
fifo_device_probe(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session;
  int ret;

  fifo_session = fifo_session_make(device, callback_id);
  if (!fifo_session)
    return -1;

  ret = fifo_open(fifo_session);
  if (ret < 0)
    {
      fifo_session_cleanup(fifo_session);
      return -1;
    }

  fifo_close(fifo_session);

  fifo_session->callback_id = callback_id;
  fifo_session->state = OUTPUT_STATE_STOPPED;

  fifo_status(fifo_session);

  return 0;
}

static int
fifo_device_volume_set(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session = device->session;

  if (!fifo_session)
    return 0;

  fifo_session->callback_id = callback_id;
  fifo_status(fifo_session);

  return 1;
}

static void
fifo_device_cb_set(struct output_device *device, int callback_id)
{
  struct fifo_session *fifo_session = device->session;

  fifo_session->callback_id = callback_id;
}

static void
fifo_write(struct output_buffer *obuf)
{
  struct fifo_session *fifo_session = sessions;
  struct fifo_packet *packet;
  struct timespec now;
  ssize_t bytes;
  int i;

  if (!fifo_session)
    return;

  for (i = 0; obuf->data[i].buffer; i++)
    {
      if (quality_is_equal(&fifo_quality, &obuf->data[i].quality))
        break;
    }

  if (!obuf->data[i].buffer)
    {
      DPRINTF(E_LOG, L_FIFO, "Bug! Did not get audio in quality required\n");
      return;
    }

  fifo_session->state = OUTPUT_STATE_STREAMING;

  CHECK_NULL(L_FIFO, packet = calloc(1, sizeof(struct fifo_packet)));
  CHECK_NULL(L_FIFO, packet->samples = malloc(obuf->data[i].bufsize));

  memcpy(packet->samples, obuf->data[i].buffer, obuf->data[i].bufsize);
  packet->samples_size = obuf->data[i].bufsize;
  packet->pts = obuf->pts;

  if (buffer.head)
    {
      buffer.head->next = packet;
      packet->prev = buffer.head;
    }

  buffer.head = packet;
  if (!buffer.tail)
    buffer.tail = packet;

  now.tv_sec = obuf->pts.tv_sec - OUTPUTS_BUFFER_DURATION;
  now.tv_nsec = obuf->pts.tv_sec;

  while (buffer.tail && (timespec_cmp(buffer.tail->pts, now) == -1))
    {
      bytes = write(fifo_session->output_fd, buffer.tail->samples, buffer.tail->samples_size);
      if (bytes > 0)
	{
	  packet = buffer.tail;
	  buffer.tail = buffer.tail->next;
	  free(packet->samples);
	  free(packet);
	  return;
	}

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

  memset(&buffer, 0, sizeof(struct fifo_buffer));

  CHECK_NULL(L_FIFO, device = calloc(1, sizeof(struct output_device)));

  device->id = 100;
  device->name = strdup(nickname);
  device->type = OUTPUT_TYPE_FIFO;
  device->type_name = outputs_name(device->type);
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
  .device_flush = fifo_device_flush,
  .device_probe = fifo_device_probe,
  .device_volume_set = fifo_device_volume_set,
  .device_cb_set = fifo_device_cb_set,
  .write = fifo_write,
};
