/*
 * Copyright (C) 2017 Christian Meffert <christian.meffert@googlemail.com>
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
#include <json.h>
#include <libwebsockets.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
# include <pthread_np.h>
#endif
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "conffile.h"
#include "listener.h"
#include "logger.h"


static struct lws_context *context;
static pthread_t tid_websocket;

static const char *websocket_interface;
static int websocket_port;
static bool websocket_exit = false;

// Event mask of events to notify websocket clients
static short websocket_events;
// Event mask of events processed by the writeable callback
static short websocket_write_events;
// Counter for events to keep track of when to write
static unsigned short websocket_write_events_counter;



/* Thread: library (the thread the event occurred) */
static void
listener_cb(short event_mask)
{
  // Add event to the event mask, clients will be notified at the next break of the libwebsockets service loop
  websocket_events |= event_mask;
}

/*
 * Libwebsocket requires the HTTP protocol to be the first supported protocol
 *
 * This adds an empty implementation, because we are serving HTTP over libevent in httpd.h.
 */
static int
callback_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
  return 0;
}

/*
 * Each session of the "notify" protocol holds this event mask
 *
 * The client sends the events it wants to be notified of and the event mask is
 * set accordingly translating them to the LISTENER enum (see listener.h)
 */
struct ws_session_data_notify
{
  short events;
  unsigned short counter; // to keep track of whether this user has already written
};

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
process_notify_request(struct ws_session_data_notify *session_data, void *in, size_t len)
{
  json_tokener *tokener;
  json_object *request;
  json_object *item;
  int count, i;
  enum json_tokener_error jerr;
  json_object *needle;
  const char *event_type;

  memset(session_data, 0, sizeof(struct ws_session_data_notify));

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
		  session_data->events |= LISTENER_UPDATE;
		}
	      else if (0 == strcmp(event_type, "database"))
		{
		  session_data->events |= LISTENER_DATABASE;
		}
	      else if (0 == strcmp(event_type, "pairing"))
		{
		  session_data->events |= LISTENER_PAIRING;
		}
	      else if (0 == strcmp(event_type, "spotify"))
		{
		  session_data->events |= LISTENER_SPOTIFY;
		}
	      else if (0 == strcmp(event_type, "lastfm"))
		{
		  session_data->events |= LISTENER_LASTFM;
		}
	      else if (0 == strcmp(event_type, "ouputs"))
		{
		  session_data->events |= LISTENER_SPEAKER;
		}
	      else if (0 == strcmp(event_type, "player"))
		{
		  session_data->events |= LISTENER_PLAYER;
		}
	      else if (0 == strcmp(event_type, "options"))
		{
		  session_data->events |= LISTENER_OPTIONS;
		}
	      else if (0 == strcmp(event_type, "volume"))
		{
		  session_data->events |= LISTENER_VOLUME;
		}
	      else if (0 == strcmp(event_type, "queue"))
		{
		  session_data->events |= LISTENER_QUEUE;
		}
	    }
	}
    }

  json_tokener_free(tokener);
  json_object_put(request);

  return 0;
}

/*
 * Notify clients of the notify-protocol about occurred events
 *
 * Sends a JSON message of the form:
 *
 * {
 *   "notify": [ "update" ]
 * }
 */
static void
send_notify_reply(short events, struct lws* wsi)
{
  unsigned char* buf;
  const char* json_response;
  json_object* reply;
  json_object* notify;

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

  json_response = json_object_to_json_string(reply);

  buf = malloc(LWS_PRE + strlen(json_response));
  memcpy(&buf[LWS_PRE], json_response, strlen(json_response));
  lws_write(wsi, &buf[LWS_PRE], strlen(json_response), LWS_WRITE_TEXT);

  free(buf);
  json_object_put(reply);
}

/*
 * Callback for the "notify" protocol
 */
static int
callback_notify(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
  struct ws_session_data_notify *session_data = user;
  int ret = 0;

  DPRINTF(E_DBG, L_WEB, "notify callback reason: %d\n", reason);
  switch (reason)
    {
      case LWS_CALLBACK_ESTABLISHED:
	// Initialize session data for new connections
	memset(session_data, 0, sizeof(struct ws_session_data_notify));
	session_data->counter = websocket_write_events_counter;
	break;

      case LWS_CALLBACK_RECEIVE:
	ret = process_notify_request(session_data, in, len);
	break;

      case LWS_CALLBACK_SERVER_WRITEABLE:
	if (websocket_write_events && (websocket_write_events_counter != session_data->counter))
	  {
	    send_notify_reply(websocket_write_events, wsi);
	    session_data->counter = websocket_write_events_counter;
	  }
	break;

      default:
	break;
    }

  return ret;
}

/*
 * Supported protocols of the websocket, needs to be in line with the protocols array
 */
enum ws_protocols
{
  WS_PROTOCOL_HTTP = 0,
  WS_PROTOCOL_NOTIFY,
};

static struct lws_protocols protocols[] =
{
  // The first protocol must always be the HTTP handler
  {
    "http-only",	// Protocol name
    callback_http,	// Callback function
    0,			// Size of per session data
    0,			// Frame size / rx buffer (0 = max frame size)
  },
  {
    "notify",
    callback_notify,
    sizeof(struct ws_session_data_notify),
    0,
  },
  { NULL, NULL, 0, 0 } // terminator
};


/* Thread: websocket */
static void *
websocket(void *arg)
{
  listener_add(listener_cb, LISTENER_UPDATE | LISTENER_DATABASE | LISTENER_PAIRING | LISTENER_SPOTIFY | LISTENER_LASTFM | LISTENER_SPEAKER
	       | LISTENER_PLAYER | LISTENER_OPTIONS | LISTENER_VOLUME | LISTENER_QUEUE);

  while(!websocket_exit)
    {
      lws_service(context, 1000);
      if (websocket_events)
	{
	  websocket_write_events = websocket_events;
	  websocket_write_events_counter++;
	  websocket_events = 0;
	  lws_callback_on_writable_all_protocol(context, &protocols[WS_PROTOCOL_NOTIFY]);
	}
    }

  lws_context_destroy(context);
  pthread_exit(NULL);
}

static void
logger_libwebsockets(int level, const char *line)
{
  int severity;

  switch (level)
    {
      case LLL_ERR:
	severity = E_LOG;
	break;

      case LLL_WARN:
	severity = E_WARN;
	break;

      case LLL_NOTICE:
	severity = E_DBG;
	break;

      case LLL_INFO:
      case LLL_DEBUG:
	severity = E_SPAM;
	break;

      default:
	severity = E_LOG;
	break;
    }

  DPRINTF(severity, L_WEB, "LWS %s", line);
}

int
websocket_init(void)
{
  struct lws_context_creation_info info;
  int ret;

  websocket_interface = cfg_getstr(cfg_getsec(cfg, "general"), "websocket_interface");
  websocket_port = cfg_getint(cfg_getsec(cfg, "general"), "websocket_port");

  if (websocket_port <= 0)
    {
      DPRINTF(E_LOG, L_WEB, "Websocket disabled. To enable it, set websocket_port in config to a valid port number.\n");
      return 0;
    }

  memset(&info, 0, sizeof(info));
  info.port = websocket_port;
  info.iface = websocket_interface;
  info.protocols = protocols;
  if (!cfg_getbool(cfg_getsec(cfg, "general"), "ipv6"))
    info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
  info.gid = -1;
  info.uid = -1;

  // Set header space to avoid "LWS Ran out of header data space" error
  info.max_http_header_data = 4096;

  // Log levels below NOTICE are only emmited if libwebsockets was built with DEBUG defined
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG,
		    logger_libwebsockets);

  context = lws_create_context(&info);
  if (context == NULL)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to create websocket context\n");
      return -1;
    }

  websocket_write_events_counter = 0;
  ret = pthread_create(&tid_websocket, NULL, websocket, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Could not spawn websocket thread: %s\n", strerror(errno));

      lws_context_destroy(context);
      return -1;
    }

  return 0;
}

void
websocket_deinit(void)
{
  if (websocket_port > 0)
    {
      websocket_exit = true;
      pthread_join(tid_websocket, NULL);
    }
}
