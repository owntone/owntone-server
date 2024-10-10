/*
 * Copyright (C) 2024 Christian Meffert <christian.meffert@googlemail.com>
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
#include <config.h>
#endif

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/http.h>
#include <json.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef HAVE_LIBEVENT22
#include <event2/ws.h>
#endif

#include "conffile.h"
#include "listener.h"
#include "logger.h"
#include "misc.h"

#ifdef HAVE_LIBEVENT22

/*
 * Each session of the "notify" protocol holds this event mask
 *
 * The client sends the events it wants to be notified of and the event mask is
 * set accordingly translating them to the LISTENER enum (see listener.h)
 */
struct client
{
  struct evws_connection *evws;
  char name[INET6_ADDRSTRLEN];
  short requested_events;
  struct client *next;
};
static struct client *clients = NULL;

/*
 * Notify clients of the notify-protocol about occurred events
 *
 * Sends a JSON message of the form:
 *
 * {
 *   "notify": [ "update" ]
 * }
 */
static char *
create_notify_reply(short events, short *requested_events)
{
  char *json_response;
  json_object *reply;
  json_object *notify;

  DPRINTF(E_DBG, L_WEB, "notify callback reply: %d\n", events);

  notify = json_object_new_array();
  if (events & LISTENER_UPDATE)
    {
      json_object_array_add(notify, json_object_new_string("update"));
    }
  if (events & LISTENER_DATABASE)
    {
      json_object_array_add(notify, json_object_new_string("database"));
    }
  if (events & LISTENER_PAIRING)
    {
      json_object_array_add(notify, json_object_new_string("pairing"));
    }
  if (events & LISTENER_SPOTIFY)
    {
      json_object_array_add(notify, json_object_new_string("spotify"));
    }
  if (events & LISTENER_LASTFM)
    {
      json_object_array_add(notify, json_object_new_string("lastfm"));
    }
  if (events & LISTENER_SPEAKER)
    {
      json_object_array_add(notify, json_object_new_string("outputs"));
    }
  if (events & LISTENER_PLAYER)
    {
      json_object_array_add(notify, json_object_new_string("player"));
    }
  if (events & LISTENER_OPTIONS)
    {
      json_object_array_add(notify, json_object_new_string("options"));
    }
  if (events & LISTENER_VOLUME)
    {
      json_object_array_add(notify, json_object_new_string("volume"));
    }
  if (events & LISTENER_QUEUE)
    {
      json_object_array_add(notify, json_object_new_string("queue"));
    }

  reply = json_object_new_object();
  json_object_object_add(reply, "notify", notify);

  json_response = strdup(json_object_to_json_string(reply));

  json_object_put(reply);

  return json_response;
}

/* Thread: library (the thread the event occurred) */
static void
listener_cb(short event_mask)
{
  struct client *client = NULL;
  char *reply = NULL;

  for (client = clients; client; client = clients->next)
    {
      reply = create_notify_reply(event_mask, &client->requested_events);
      evws_send_text(client->evws, reply);
      free(reply);
    }
}

/*
 * Processes client requests to the notify-protocol
 *
 * Expects the message in "in" to be a JSON string of the form:
 *
 * {
 *   "notify": [ "update" ]
 * }
 */
static int
process_notify_request(short *requested_events, const char *in, size_t len)
{
  json_tokener *tokener;
  json_object *request;
  json_object *item;
  int count, i;
  enum json_tokener_error jerr;
  json_object *needle;
  const char *event_type;

  *requested_events = 0;

  tokener = json_tokener_new();
  request = json_tokener_parse_ex(tokener, in, len);
  jerr = json_tokener_get_error(tokener);

  if (jerr != json_tokener_success)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to parse incoming request: %s\n", json_tokener_error_desc(jerr));
      json_tokener_free(tokener);
      return -1;
    }

  DPRINTF(E_DBG, L_WEB, "notify callback request: %s\n", json_object_to_json_string(request));

  if (json_object_object_get_ex(request, "notify", &needle) && json_object_get_type(needle) == json_type_array)
    {
      count = json_object_array_length(needle);
      for (i = 0; i < count; i++)
	{
	  item = json_object_array_get_idx(needle, i);

	  if (json_object_get_type(item) == json_type_string)
	    {
	      event_type = json_object_get_string(item);
	      DPRINTF(E_DBG, L_WEB, "notify callback event received: %s\n", event_type);

	      if (0 == strcmp(event_type, "update"))
		{
		  *requested_events |= LISTENER_UPDATE;
		}
	      else if (0 == strcmp(event_type, "database"))
		{
		  *requested_events |= LISTENER_DATABASE;
		}
	      else if (0 == strcmp(event_type, "pairing"))
		{
		  *requested_events |= LISTENER_PAIRING;
		}
	      else if (0 == strcmp(event_type, "spotify"))
		{
		  *requested_events |= LISTENER_SPOTIFY;
		}
	      else if (0 == strcmp(event_type, "lastfm"))
		{
		  *requested_events |= LISTENER_LASTFM;
		}
	      else if (0 == strcmp(event_type, "outputs"))
		{
		  *requested_events |= LISTENER_SPEAKER;
		}
	      else if (0 == strcmp(event_type, "player"))
		{
		  *requested_events |= LISTENER_PLAYER;
		}
	      else if (0 == strcmp(event_type, "options"))
		{
		  *requested_events |= LISTENER_OPTIONS;
		}
	      else if (0 == strcmp(event_type, "volume"))
		{
		  *requested_events |= LISTENER_VOLUME;
		}
	      else if (0 == strcmp(event_type, "queue"))
		{
		  *requested_events |= LISTENER_QUEUE;
		}
	    }
	}
    }

  json_tokener_free(tokener);
  json_object_put(request);

  return 0;
}

static void
on_msg_cb(struct evws_connection *evws, int type, const unsigned char *data, size_t len, void *arg)
{
  struct client *self = arg;
  const char *msg = (const char *)data;

  process_notify_request(&self->requested_events, msg, len);
}

static void
on_close_cb(struct evws_connection *evws, void *arg)
{
  struct client *client = NULL;
  struct client *prev = NULL;

  for (client = clients; client && client != arg; client = clients->next)
    {
      prev = client;
    }

  if (client)
    {
      if (prev)
	prev->next = client->next;
      else
	clients = client->next;

      free(client);
    }
}

static void
gencb_ws(struct evhttp_request *req, void *arg)
{
  struct client *client;

  client = calloc(1, sizeof(*client));

  client->evws = evws_new_session(req, on_msg_cb, client, 0);
  if (!client->evws)
    {
      free(client);
      return;
    }

  evws_connection_set_closecb(client->evws, on_close_cb, client);
  client->next = clients;
  clients = client;
}

int
websocketev_init(struct evhttp *evhttp)
{
  int websocket_port = cfg_getint(cfg_getsec(cfg, "general"), "websocket_port");

  if (websocket_port > 0)
    {
      DPRINTF(E_DBG, L_WEB,
	      "Libevent websocket disabled, using libwebsockets instead. Set "
	      "websocket_port to 0 to enable it.\n");
      return 0;
    }

  evhttp_set_cb(evhttp, "/ws", gencb_ws, NULL);

  listener_add(listener_cb, LISTENER_UPDATE | LISTENER_DATABASE | LISTENER_PAIRING | LISTENER_SPOTIFY | LISTENER_LASTFM
				| LISTENER_SPEAKER | LISTENER_PLAYER | LISTENER_OPTIONS | LISTENER_VOLUME
				| LISTENER_QUEUE);

  return 0;
}

void
websocketev_deinit(void)
{
  listener_remove(listener_cb);
}

#else

int
websocketev_init(struct evhttp *evhttp)
{
  return 0;
}

void
websocketev_deinit(void)
{
}

#endif
