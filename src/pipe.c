/*
 * Copyright (C) 2014 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "pipe.h"
#include "logger.h"

#define PIPE_BUFFER_SIZE 8192

static int g_fd = -1;
static uint16_t *g_buf = NULL;

int
pipe_setup(struct media_file_info *mfi)
{
  struct stat sb;

  if (!mfi->path)
    {
      DPRINTF(E_LOG, L_PLAYER, "Path to pipe is NULL\n");
      return -1;
    }

  DPRINTF(E_DBG, L_PLAYER, "Setting up pipe: %s\n", mfi->path);

  if (lstat(mfi->path, &sb) < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not lstat() '%s': %s\n", mfi->path, strerror(errno));
      return -1;
    }

  if (!S_ISFIFO(sb.st_mode))
    {
      DPRINTF(E_LOG, L_PLAYER, "Source type is pipe, but path is not a fifo: %s\n", mfi->path);
      return -1;
    }

  pipe_cleanup();

  g_fd = open(mfi->path, O_RDONLY | O_NONBLOCK);
  if (g_fd < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not open pipe for reading '%s': %s\n", mfi->path, strerror(errno));
      return -1;
    }

  g_buf = (uint16_t *)malloc(PIPE_BUFFER_SIZE);
  if (!g_buf)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for buffer\n");
      return -1;
    }

  return 1;
}

void
pipe_cleanup(void)
{
  if (g_fd >= 0)
    close(g_fd);
  g_fd = -1;

  if (g_buf)
    free(g_buf);
  g_buf = NULL;

  return;
}

int
pipe_audio_get(struct evbuffer *evbuf, int wanted)
{
  int got;

  if (wanted > PIPE_BUFFER_SIZE)
    wanted = PIPE_BUFFER_SIZE;

  got = read(g_fd, g_buf, wanted);

  if ((got < 0) && (errno != EAGAIN))
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not read from pipe: %s\n", strerror(errno));
      return -1;
    }

  // If the other end of the pipe is not writing or the read was blocked,
  // we just return silence
  if (got <= 0)
    {
      memset(g_buf, 0, wanted);
      got = wanted;
    }

  evbuffer_add(evbuf, g_buf, got);

  return got;
}

