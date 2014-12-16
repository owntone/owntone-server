/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
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
#include "evhttp_compat.h"

struct evhttp_connection *
evhttp_connection_base_new(struct event_base *base, void *ignore, const char *address, unsigned short port)
{
  struct evhttp_connection *evcon;

  if (!base || !address || !port)
    return NULL;

  evcon = evhttp_connection_new(address, port);
  if (evcon)
    evhttp_connection_set_base(evcon, base);

  return evcon;
}

void
evhttp_request_set_header_cb(struct evhttp_request *req, int (*cb)(struct evhttp_request *, void *))
{
  req->header_cb = cb;
}

