/*
 * Copyright (C) 2015 Christian Meffert <christian.meffert@googlemail.com>
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
#include <string.h>

#include "logger.h"
#include "listener.h"

struct listener
{
  notify notify_cb;
  struct listener *next;
};

struct listener *listener_list = NULL;

int
listener_add(notify notify_cb)
{
  struct listener *listener;

  listener = (struct listener*)malloc(sizeof(struct listener));
  listener->notify_cb = notify_cb;
  listener->next = listener_list;
  listener_list = listener;

  return 0;
}

int
listener_remove(notify notify_cb)
{
  struct listener *listener;
  struct listener *prev;

  listener = listener_list;
  prev = NULL;

  while (listener)
    {
      if (listener->notify_cb == notify_cb)
	{
	  if (prev)
	    prev->next = listener->next;
	  else
	    listener_list = NULL;

	  free(listener);
	  return 0;
	}

      prev = listener;
      listener = listener->next;
    }

  return -1;
}

int
listener_notify(enum listener_event_type type)
{
  struct listener *listener;

  DPRINTF(E_DBG, L_MPD, "Notify event type %d\n", type);

  listener = listener_list;
  while (listener)
    {
      listener->notify_cb(type);
      listener = listener->next;
    }

  return 0;
}
