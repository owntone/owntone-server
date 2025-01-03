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

#include "listener.h"

struct listener
{
  notify notify_cb;
  short events;
  void *ctx;
  struct listener *next;
};

struct listener *listener_list = NULL;

int
listener_add(notify notify_cb, short events, void *ctx)
{
  struct listener *listener;

  listener = (struct listener*)malloc(sizeof(struct listener));
  if (!listener)
    {
      return -1;
    }
  listener->notify_cb = notify_cb;
  listener->events = events;
  listener->ctx = ctx;
  listener->next = listener_list;
  listener_list = listener;

  return 0;
}

int
listener_remove(notify notify_cb)
{
  struct listener *listener;
  struct listener *prev;

  prev = NULL;
  for (listener = listener_list; listener; listener = listener->next)
    {
      if (listener->notify_cb == notify_cb)
	break;

      prev = listener;
    }

  if (!listener)
    {
      return -1;
    }

  if (prev)
    prev->next = listener->next;
  else
    listener_list = listener->next;

  free(listener);
  return 0;
}

void
listener_notify(short event_mask)
{
  struct listener *listener;

  listener = listener_list;
  while (listener)
    {
      if (event_mask & listener->events)
	listener->notify_cb(event_mask & listener->events, listener->ctx);
      listener = listener->next;
    }
}
