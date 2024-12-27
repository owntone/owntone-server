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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "conffile.h"
#include "listener.h"
#include "logger.h"
#include "misc.h"


static struct lws_context *websocket_context;
static bool websocket_is_initialized;
static pthread_t tid_websocket;

static const char *websocket_interface;
static int websocket_port;
static bool websocket_exit = false;

// Lock the event mask of events processed by the writeable callback
static pthread_mutex_t websocket_write_event_lock;
// Event mask of events processed by the writeable callback
static short websocket_write_events;


/* Thread: library, player, etc. (the thread the event occurred) */
static void
listener_cb(short event_mask, void *ctx)
{
  pthread_mutex_lock(&websocket_write_event_lock);
  websocket_write_events |= event_mask;
  pthread_mutex_unlock(&websocket_write_event_lock);

  lws_cancel_service(websocket_context);
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
struct per_session_data {
  struct per_session_data *pss_list;
  struct lws *wsi;
  short requested_events;
  short write_events;
};

/* one of these is created for each vhost our protocol is used with */
struct per_vhost_data {
  struct per_session_data *pss_list; /* linked-list of live pss*/
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
process_notify_request(short *requested_events, void *in, size_t len)
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
	      DPRINTF(E_SPAM, L_WEB, "notify callback event received: %s\n", event_type);

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
  struct per_session_data *pss = user;
#if LWS_LIBRARY_VERSION_MAJOR < 3
  struct per_session_data **ppss = NULL;
#endif
  struct per_vhost_data *vhd = lws_protocol_vh_priv_get(
#if LWS_LIBRARY_VERSION_MAJOR >= 3
    lws_get_vhost(wsi),
#else
    lws_vhost_get(wsi),
#endif
    lws_get_protocol(wsi));
  short events = 0;
  int ret = 0;

  DPRINTF(E_SPAM, L_WEB, "notify callback reason: %d\n", reason);

  switch (reason)
  {
    case LWS_CALLBACK_PROTOCOL_INIT:
      vhd = lws_protocol_vh_priv_zalloc(
#if LWS_LIBRARY_VERSION_MAJOR >= 3
        lws_get_vhost(wsi),
#else
        lws_vhost_get(wsi),
#endif
        lws_get_protocol(wsi),
        sizeof(struct per_vhost_data));
      if (!vhd)
      {
        DPRINTF(E_LOG, L_WEB, "Failed to allocate websocket per-vhoststorage\n");
        return 1;
      }
      break;

    case LWS_CALLBACK_ESTABLISHED:
      /* add ourselves to the list of live pss held in the vhd */
      if (vhd)
      {
#if LWS_LIBRARY_VERSION_MAJOR >= 3
        lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
#else
        pss->pss_list = vhd->pss_list;
        vhd->pss_list = pss;
#endif
        pss->wsi = wsi;
      }
      break;

    case LWS_CALLBACK_CLOSED:
      /* remove our closing pss from the list of live pss */
      if (vhd)
      {
#if LWS_LIBRARY_VERSION_MAJOR >= 3
        lws_ll_fwd_remove(struct per_session_data, pss_list, pss, vhd->pss_list);
#else
        ppss = &(vhd->pss_list);
        while (*ppss) {
          if (*ppss == pss) {
            *ppss = pss->pss_list;
            break;
          }
          ppss = &(*ppss)->pss_list;
        }
#endif
      }
      break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
#if LWS_LIBRARY_VERSION_MAJOR < 3
      pthread_mutex_lock(&websocket_write_event_lock);
      events = websocket_write_events;
      websocket_write_events = 0;
      pthread_mutex_unlock(&websocket_write_event_lock);
      if (vhd && events)
      {
        ppss = &(vhd->pss_list);
        while (*ppss) {
          (*ppss)->write_events |= events;
          ppss = &(*ppss)->pss_list;
        }
      }
#endif
      if (pss->requested_events & pss->write_events)
      {
        events = pss->requested_events & pss->write_events;
        send_notify_reply(events, wsi);
        pss->write_events = 0;
      }
      break;

    case LWS_CALLBACK_RECEIVE:
      ret = process_notify_request(&pss->requested_events, in, len);
      break;

#if LWS_LIBRARY_VERSION_MAJOR >= 3
    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      if (vhd)
      {
        pthread_mutex_lock(&websocket_write_event_lock);
        events = websocket_write_events;
        websocket_write_events = 0;
        pthread_mutex_unlock(&websocket_write_event_lock);
        lws_start_foreach_llp(struct per_session_data **, ppss, vhd->pss_list)
        {
          (*ppss)->write_events |= events;
          lws_callback_on_writable((*ppss)->wsi);
        } lws_end_foreach_llp(ppss, pss_list);
      }
      break;
#endif

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
    sizeof(struct per_session_data),
    0,
  },
  { NULL, NULL, 0, 0 } // terminator
};


/* Thread: websocket */
static void *
websocket(void *arg)
{
  listener_add(listener_cb, LISTENER_UPDATE | LISTENER_DATABASE | LISTENER_PAIRING | LISTENER_SPOTIFY | LISTENER_LASTFM | LISTENER_SPEAKER
               | LISTENER_PLAYER | LISTENER_OPTIONS | LISTENER_VOLUME | LISTENER_QUEUE, NULL);

  while(!websocket_exit)
  {
#if LWS_LIBRARY_VERSION_MAJOR >= 3
    if (lws_service(websocket_context, 0))
      websocket_exit = true;
#else
    lws_service(websocket_context, 10000);
    if (websocket_write_events)
      lws_callback_on_writable_all_protocol(websocket_context, &protocols[WS_PROTOCOL_NOTIFY]);
#endif
  }

  listener_remove(listener_cb);

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
#ifdef HAVE_LIBEVENT22
      DPRINTF(E_DBG, L_WEB, "Libwebsocket disabled, using libevent websocket instead. To enable it, set websocket_port in config to a valid port number.\n");
#else
      DPRINTF(E_LOG, L_WEB, "Websocket disabled. To enable it, set websocket_port in config to a valid port number.\n");
#endif
      return 0;
    }

  memset(&info, 0, sizeof(info));
  info.port = websocket_port;
  info.iface = websocket_interface;
  info.protocols = protocols;
#ifdef LWS_SERVER_OPTION_IPV6_V6ONLY_MODIFY // Debian Buster's libwebsockets does not have this flag
  if (cfg_getbool(cfg_getsec(cfg, "general"), "ipv6"))
    info.options |= LWS_SERVER_OPTION_IPV6_V6ONLY_MODIFY; // Assures dual stack is enabled by switching off IPV6_V6ONLY
  else
    info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
#else
  if (!cfg_getbool(cfg_getsec(cfg, "general"), "ipv6"))
    info.options |= LWS_SERVER_OPTION_DISABLE_IPV6;
#endif
  info.gid = -1;
  info.uid = -1;

  // Set header space to avoid "LWS Ran out of header data space" error
  info.max_http_header_data = 4096;

  // Log levels below NOTICE are only emmited if libwebsockets was built with DEBUG defined
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG,
                    logger_libwebsockets);

  websocket_context = lws_create_context(&info);
  if (websocket_context == NULL)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to create websocket context\n");
      return -1;
    }

  ret = mutex_init(&websocket_write_event_lock);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Failed to initialize mutex: %s\n", strerror(ret));
      lws_context_destroy(websocket_context);
      return -1;
    }

  ret = pthread_create(&tid_websocket, NULL, websocket, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_WEB, "Could not spawn websocket thread (%d): %s\n", ret, strerror(ret));
      pthread_mutex_destroy(&websocket_write_event_lock);
      lws_context_destroy(websocket_context);
      return -1;
    }

  thread_setname(tid_websocket, "websocket");

  websocket_is_initialized = true;

  return 0;
}

void
websocket_deinit(void)
{
  int ret;

  if (!websocket_is_initialized)
    return;

  websocket_is_initialized = false;
  websocket_exit = true;
  lws_cancel_service(websocket_context);
  ret = pthread_join(tid_websocket, NULL);
  if (ret < 0)
    DPRINTF(E_LOG, L_WEB, "Error joining websocket thread (%d): %s\n", ret, strerror(ret));

  lws_context_destroy(websocket_context);
  pthread_mutex_destroy(&websocket_write_event_lock);
}
