/*
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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/socket.h>

#include <pthread.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <plist/plist.h>

#include "airplay_events.h"
#include "commands.h"
#include "misc.h"
#include "logger.h"
#include "player.h"
#include "pair_ap/pair.h"

#define RTSP_VERSION "RTSP/1.0"

enum airplay_events
{
  AIRPLAY_EVENT_UNKNOWN,
  AIRPLAY_EVENT_PLAY,
  AIRPLAY_EVENT_PAUSE,
  AIRPLAY_EVENT_NEXT,
  AIRPLAY_EVENT_PREV,
};

struct airplay_events_client
{
  char *name;
  int fd;
  struct event *listener;
  struct pair_cipher_context *cipher_ctx;

  struct evbuffer *incoming;
  struct evbuffer *pending;

  struct airplay_events_client *next;
};

struct rtsp_message
{
  int content_length;
  char *content_type;
  char *first_line;
  int cseq;

  const uint8_t *body;
  size_t bodylen;

  const uint8_t *data;
  size_t datalen;
};


static pthread_t thread_id;
static struct event_base *evbase;
static struct commands_base *cmdbase;
static struct airplay_events_client *airplay_events_clients;

// Forwards
static void
incoming_cb(int fd, short what, void *arg);


/* ---------------------------- Client handling ----------------------------- */

static void
client_free(struct airplay_events_client *client)
{
  if (!client)
    return;

  if (client->listener)
    event_free(client->listener);

  evbuffer_free(client->incoming);
  evbuffer_free(client->pending);

  free(client->name);
  pair_cipher_free(client->cipher_ctx);

  free(client);
}

static void
client_remove(struct airplay_events_client *client)
{
  struct airplay_events_client *iter;

  if (client == airplay_events_clients)
    airplay_events_clients = client->next;
  else
    {
      for (iter = airplay_events_clients; iter && (iter->next != client); iter = iter->next)
	; /* EMPTY */

      if (iter)
	iter->next = client->next;
    }

  client_free(client);
}

static int
client_add(const char *name, int fd, const uint8_t *key, size_t key_len)
{
  struct airplay_events_client *client;

  CHECK_NULL(L_AIRPLAY, client = calloc(1, sizeof(struct airplay_events_client)));
  CHECK_NULL(L_AIRPLAY, client->name = strdup(name));
  CHECK_NULL(L_AIRPLAY, client->incoming = evbuffer_new());
  CHECK_NULL(L_AIRPLAY, client->pending = evbuffer_new());

  client->fd = fd;

  client->listener = event_new(evbase, fd, EV_READ | EV_PERSIST, incoming_cb, client);
  if (!client->listener)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not listen for AirPlay events from '%s', invalid fd or out of memory\n", name);
      goto error;
    }

  client->cipher_ctx = pair_cipher_new(PAIR_CLIENT_HOMEKIT_NORMAL, 1, key, key_len);
  if (!client->cipher_ctx)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not listen for AirPlay events from '%s': Could not create ciphering context\n", name);
      goto error;
    }

  event_add(client->listener, NULL);

  client->next = airplay_events_clients;
  airplay_events_clients = client;

  return 0;

 error:
  client_free(client);
  return -1;
}


/* -------------------------------- Ciphering ------------------------------- */

static int
buffer_decrypt(struct evbuffer *output, struct evbuffer *input, struct pair_cipher_context *cipher_ctx)
{
  uint8_t *in;
  size_t in_len;
  ssize_t bytes_decrypted;
  uint8_t *plain;
  size_t plain_len;

  in = evbuffer_pullup(input, -1);
  in_len = evbuffer_get_length(input);

  // Note that bytes_decrypted is not necessarily equal to plain_len
  bytes_decrypted = pair_decrypt(&plain, &plain_len, in, in_len, cipher_ctx);
  if (bytes_decrypted < 0)
    return -1;

  evbuffer_add(output, plain, plain_len);
  evbuffer_drain(input, bytes_decrypted);
  free(plain);
  return 0;
}

static int
buffer_encrypt(struct evbuffer *output, uint8_t *in, size_t in_len, struct pair_cipher_context *cipher_ctx)
{
  uint8_t *out;
  size_t out_len;
  int ret;

  ret = pair_encrypt(&out, &out_len, in, in_len, cipher_ctx);
  if (ret < 0)
    return -1;

  evbuffer_add(output, out, out_len);
  free(out);
  return 0;
}


/* --------------------- Message construction/parsing ----------------------- */

static void
response_headers_add(struct evbuffer *response, int cseq, size_t content_length, const char *content_type)
{
  evbuffer_add_printf(response, "%s 200 OK\r\n", RTSP_VERSION);
  evbuffer_add_printf(response, "Server: %s/1.0\r\n", PACKAGE_NAME);
  if (content_length)
    evbuffer_add_printf(response, "Content-Length: %zu\r\n", content_length);
  if (content_type)
    evbuffer_add_printf(response, "Content-Type: %s\r\n", content_type);
  if (cseq)
    evbuffer_add_printf(response, "CSeq: %d\r\n", cseq);
  evbuffer_add_printf(response, "\r\n");
}

static void
response_create_from_raw(struct evbuffer *response, uint8_t *body, size_t body_len, int cseq, const char *content_type)
{
  response_headers_add(response, cseq, body_len, content_type);

  if (body)
    evbuffer_add(response, body, body_len);
}

static int
body_find(uint8_t **body, size_t *body_len, uint8_t *in, size_t in_len)
{
  const char *plist_header = "bplist";
  size_t plist_header_len = strlen(plist_header);

  *body_len = in_len;
  for (*body = in; *body_len > plist_header_len; (*body)++, (*body_len)--)
    {
      if (memcmp(*body, plist_header, plist_header_len) == 0)
	return 0;
    }

  return -1;
}

static int
rtsp_parse(enum airplay_events *event, uint8_t *in, size_t in_len)
{
  uint8_t *body;
  size_t body_len;
  plist_t request = NULL;
  plist_t item;
  char *type = NULL;
  char *value = NULL;
  int ret;

  DHEXDUMP(E_DBG, L_AIRPLAY, in, in_len, "Incoming event\n");

  ret = body_find(&body, &body_len, in, in_len);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not parse incoming event, no plist body found\n");
      return -1;
    }

  plist_from_bin((char *)body, (uint32_t)body_len, &request);
  if (!request)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not parse incoming event plist\n");
      return -1;
    }

  // TODO remove
  char *xml = NULL;
  uint32_t xml_len;
  plist_to_xml(request, &xml, &xml_len);
  DPRINTF(E_DBG, L_AIRPLAY, "%s\n", xml);

  item = plist_dict_get_item(request, "type");
  if (item)
    {
      plist_get_string_val(item, &type);
    }

  item = plist_dict_get_item(request, "value");
  if (item)
    {
      plist_get_string_val(item, &value);
    }

  if (!type || !value)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "AirPlay event has no type/value: type=%s, value=%s\n", type, value);
      goto error;
    }
  else if (strcmp(type, "sendMediaRemoteCommand") != 0)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "Incoming event not of type sendMediaRemoteCommand\n");
      goto error;
    }

  DPRINTF(E_INFO, L_AIRPLAY, "Received event type '%s', value '%s'\n", type, value);

  if (strcmp(value, "paus") == 0)
    *event = AIRPLAY_EVENT_PAUSE;
  else if (strcmp(value, "play") == 0)
    *event = AIRPLAY_EVENT_PLAY;
  else if (strcmp(value, "nitm") == 0)
    *event = AIRPLAY_EVENT_NEXT;
  else if (strcmp(value, "pitm") == 0)
    *event = AIRPLAY_EVENT_PREV;
  else
    *event = AIRPLAY_EVENT_UNKNOWN;

  free(type);
  free(value);
  plist_free(request);
  return 0;

 error:
  free(type);
  free(value);
  plist_free(request);
  return -1;
}


/* --------------------------- Message handling ----------------------------- */

static void
handle_event(enum airplay_events event)
{
  struct player_status status;

  player_get_status(&status);

  switch (event)
    {
      case AIRPLAY_EVENT_PLAY:
      case AIRPLAY_EVENT_PAUSE:
	if (status.status == PLAY_PLAYING)
	  player_playback_pause();
	else
	  player_playback_start();
	break;
      case AIRPLAY_EVENT_NEXT:
	player_playback_next();
	break;
      case AIRPLAY_EVENT_PREV:
	player_playback_prev();
	break;
      default:
	return;
    }
}

static int
respond(struct airplay_events_client *client)
{
  struct evbuffer *response;
  struct evbuffer *encrypted;
  uint8_t *plain;
  size_t plain_len;
  int ret;

  CHECK_NULL(L_AIRPLAY, response = evbuffer_new());
  response_create_from_raw(response, NULL, 0, 0, NULL);

  plain = evbuffer_pullup(response, -1);
  plain_len = evbuffer_get_length(response);

  CHECK_NULL(L_AIRPLAY, encrypted = evbuffer_new());

  ret = buffer_encrypt(encrypted, plain, plain_len, client->cipher_ctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not encrypt AirPlay event data response: %s\n", pair_cipher_errmsg(client->cipher_ctx));
      return -1;
    }

  do
    {
      ret = evbuffer_write(encrypted, client->fd);
      if (ret <= 0)
        goto error;
    } while (evbuffer_get_length(encrypted) > 0);

  evbuffer_free(encrypted);
  evbuffer_free(response);
  return 0;

 error:
  evbuffer_free(encrypted);
  evbuffer_free(response);
  return -1;
}

static void
incoming_cb(int fd, short what, void *arg)
{
  struct airplay_events_client *client = arg;
  enum airplay_events event;
  uint8_t *plain;
  size_t plain_len;
  int ret;

  DPRINTF(E_DBG, L_AIRPLAY, "AirPlay event from '%s'\n", client->name);

  ret = evbuffer_read(client->incoming, fd, -1);
  if (ret == 0)
    {
      DPRINTF(E_DBG, L_AIRPLAY, "'%s' disconnected from the event channel\n", client->name);
      goto disconnect;
    }
  else if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "AirPlay event connection to '%s' returned an error\n", client->name);
      goto disconnect;
    }

  ret = buffer_decrypt(client->pending, client->incoming, client->cipher_ctx);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not decrypt incoming AirPlay event data: %s\n", pair_cipher_errmsg(client->cipher_ctx));
      goto disconnect;
    }

  plain = evbuffer_pullup(client->pending, -1);
  plain_len = evbuffer_get_length(client->pending);

  ret = rtsp_parse(&event, plain, plain_len);
  if (ret < 0) // A message type we don't know about, so ignore
    {
      evbuffer_drain(client->pending, -1);
      return;
    }
  else if (ret == 1)
    {
      DPRINTF(E_SPAM, L_AIRPLAY, "Incomplete RTSP event message, waiting for more data\n");
      return;
    }

  evbuffer_drain(client->pending, -1);

  handle_event(event);

  ret = respond(client);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_AIRPLAY, "Could not send AirPlay event response\n");
      goto disconnect;
    }

  return;

 disconnect:
  client_remove(client);
  return;
}

/* -------------------- Event loop (thread: airplay events ) ---------------- */

static void *
airplay_events(void *arg)
{
  event_base_dispatch(evbase);

  pthread_exit(NULL);
}


/* ------------------------------- Interface -------------------------------- */

int
airplay_events_listen(const char *name, const char *address, unsigned short port, const uint8_t *key, size_t key_len)
{
  int fd;
  int ret;

  fd = net_connect(address, port, SOCK_STREAM, "AirPlay events");
  if (fd < 0)
    {
      return -1;
    }

  ret = client_add(name, fd, key, key_len);
  if (ret < 0)
    {
      close(fd);
      return -1;
    }

  return fd;
}

/* Thread: main */
int
airplay_events_init(void)
{
  int ret;

  CHECK_NULL(L_AIRPLAY, evbase = event_base_new());
  CHECK_NULL(L_AIRPLAY, cmdbase = commands_base_new(evbase, NULL));

  DPRINTF(E_INFO, L_AIRPLAY, "AirPlay events thread init\n");

  ret = pthread_create(&thread_id, NULL, airplay_events, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not spawn AirPlay events thread: %s\n", strerror(errno));

      goto error;
    }

// TODO  thread_name_set(thread_id, "airplay events");

  return 0;

 error:
  airplay_events_deinit();
  return -1;
}

/* Thread: main */
void
airplay_events_deinit(void)
{
  int ret;

  commands_base_destroy(cmdbase);

  ret = pthread_join(thread_id, NULL);
  if (ret != 0)
    {
      DPRINTF(E_LOG, L_AIRPLAY, "Could not join AirPlay events thread: %s\n", strerror(errno));
      return;
    }

  while (airplay_events_clients)
    {
      client_remove(airplay_events_clients);
    }

  event_base_free(evbase);
}
