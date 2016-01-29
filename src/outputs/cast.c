/*
 * Copyright (C) 2015-2016 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * TODO Credits
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <endian.h>
#include <gnutls/gnutls.h>
#include <json-c/json.h>

#include <event2/event.h>

#include "mdns.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"
#include "cast_channel.pb-c.h"

// Number of bytes to request from TLS connection
#define MAX_BUF 4096
// CA file location (not very portable...?)
#define CAFILE "/etc/ssl/certs/ca-certificates.crt"

// Namespaces
#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_MEDIA "urn:x-cast:com.google.cast.media"

#define USE_TRANSPORT_ID     (1 << 1)
#define USE_REQUEST_ID       (1 << 2)
#define USE_REQUEST_ID_ONLY  (1 << 3)

#define CALLBACK_REGISTER_SIZE 20

// TODO Find the real IP
#define TEST_STREAM_URL "http://192.168.1.201:3689/stream.mp3"

union sockaddr_all
{
  struct sockaddr_in sin;
  struct sockaddr_in6 sin6;
  struct sockaddr sa;
  struct sockaddr_storage ss;
};

struct cast_session;
struct cast_msg_payload;

typedef void (*cast_reply_cb)(struct cast_session *cs, struct cast_msg_payload *payload);

struct cast_session
{
  enum output_device_state state;

  // Connection fd and session, and listener event
  int server_fd;
  gnutls_session_t tls_session;
  struct event *ev;

  char *devname;
  char *address;
  unsigned short port;

  float volume;

  // Outgoing request which have the USE_REQUEST_ID flag get a new id, and a
  // callback is registered. The callback is called when an incoming message
  // from the peer with that request id arrives.
  int request_id;
  cast_reply_cb callback_register[CALLBACK_REGISTER_SIZE];

  // Session info from the ChromeCast
  char *transport_id;
  char *session_id;

  /* Do not dereference - only passed to the status cb */
  struct output_device *device;
  struct output_session *output_session;
  output_status_cb status_cb;

  struct cast_session *next;
};

enum cast_msg_types
{
  UNKNOWN,
  PING,
  PONG,
  CONNECT,
  CLOSE,
  GET_STATUS,
  RECEIVER_STATUS,
  LAUNCH,
  MEDIA_CONNECT,
  MEDIA_GET_STATUS,
  MEDIA_STATUS,
  MEDIA_LOAD,
  MEDIA_STOP,
  MEDIA_LOAD_FAILED,
  SET_VOLUME,
};

struct cast_msg_basic
{
  enum cast_msg_types type;
  char *tag;       // Used for looking up incoming message type
  char *namespace;
  char *payload;

  int flags;
};

struct cast_msg_payload
{
  enum cast_msg_types type;
  int request_id;
  const char *session_id;
  const char *transport_id;
};

// Array of the cast messages that we use. Must be in sync with cast_msg_types.
struct cast_msg_basic cast_msg[] =
{
  {
    .type = UNKNOWN,
    .namespace = "",
    .payload = "",
  },
  {
    .type = PING,
    .tag = "PING",
    .namespace = NS_HEARTBEAT,
    .payload = "{'type':'PING'}",
  },
  {
    .type = PONG,
    .tag = "PONG",
    .namespace = NS_HEARTBEAT,
    .payload = "{'type':'PONG'}",
  },
  {
    .type = CONNECT,
    .namespace = NS_CONNECTION,
    .payload = "{'type':'CONNECT'}",
//	msg.payload_utf8 = "{\"origin\":{},\"userAgent\":\"forked-daapd\",\"type\":\"CONNECT\",\"senderInfo\":{\"browserVersion\":\"44.0.2403.30\",\"version\":\"15.605.1.3\",\"connectionType\":1,\"platform\":4,\"sdkType\":2,\"systemVersion\":\"Macintosh; Intel Mac OS X10_10_3\"}}";
  },
  {
    .type = CLOSE,
    .tag = "CLOSE",
    .namespace = NS_CONNECTION,
    .payload = "{'type':'CLOSE'}",
  },
  {
    .type = GET_STATUS,
    .namespace = NS_RECEIVER,
    .payload = "{'type':'GET_STATUS','requestId':%d}",
    .flags = USE_REQUEST_ID_ONLY,
  },
  {
    .type = RECEIVER_STATUS,
    .tag = "RECEIVER_STATUS",
  },
  {
    .type = LAUNCH,
    .namespace = NS_RECEIVER,
    .payload = "{'type':'LAUNCH','requestId':%d,'appId':'CC1AD845'}",
    .flags = USE_REQUEST_ID_ONLY,
  },
  {
    .type = MEDIA_CONNECT,
    .namespace = NS_CONNECTION,
    .payload = "{'type':'CONNECT'}",
    .flags = USE_TRANSPORT_ID,
  },
  {
    .type = MEDIA_GET_STATUS,
    .namespace = NS_MEDIA,
    .payload = "{'type':'GET_STATUS','requestId':%d}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID_ONLY,
  },
  {
    .type = MEDIA_STATUS,
    .tag = "MEDIA_STATUS",
  },
  {
    .type = MEDIA_LOAD,
    .namespace = NS_MEDIA,
    .payload = "{'currentTime':0,'media':{'contentId':'%s','streamType':'LIVE','contentType':'audio/mp3'},'customData':{},'sessionId':'%s','requestId':%d,'type':'LOAD','autoplay':1}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = MEDIA_STOP,
    .namespace = NS_RECEIVER,
    .payload = "{'mediaSessionId':1,'sessionId':'%s','type':'STOP','requestId':%d}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = MEDIA_LOAD_FAILED,
    .tag = "LOAD_FAILED",
  },
  {
    .type = SET_VOLUME,
    .namespace = NS_RECEIVER,
    .payload = "{'type':'SET_VOLUME','volume':{'level':%.2f,'muted':0},'requestId':%d}",
    .flags = USE_REQUEST_ID,
  },
  {
    .type = 0,
  },
};

/* From player.c */
extern struct event_base *evbase_player;

/* Globals */
static gnutls_certificate_credentials_t tls_credentials;
static struct cast_session *sessions;



static int
tcp_connect(const char *address, unsigned int port, int family)
{
  union sockaddr_all sa;
  int fd;
  int len;
  int ret;

  // TODO Open non-block right away so we don't block the player while connecting
  // and during TLS handshake (we would probably need to introduce a deferredev)
  fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not create socket: %s\n", strerror(errno));
      return -1;
    }

  switch (family)
    {
      case AF_INET:
	sa.sin.sin_port = htons(port);
	ret = inet_pton(AF_INET, address, &sa.sin.sin_addr);
	len = sizeof(sa.sin);
	break;

      case AF_INET6:
	sa.sin6.sin6_port = htons(port);
	ret = inet_pton(AF_INET6, address, &sa.sin6.sin6_addr);
	len = sizeof(sa.sin6);
	break;

      default:
	DPRINTF(E_WARN, L_CAST, "Unknown family %d\n", family);
	close(fd);
	return -1;
    }

  if (ret <= 0)
    {
      DPRINTF(E_LOG, L_CAST, "Device address not valid (%s)\n", address);
      close(fd);
      return -1;
    }

  sa.ss.ss_family = family;

  ret = connect(fd, &sa.sa, len);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "connect() to [%s]:%u failed: %s\n", address, port, strerror(errno));
      close(fd);
      return -1;
    }

  return fd;
}

static void
tcp_close(int fd)
{
  /* no more receptions */
  shutdown(fd, SHUT_RDWR);
  close(fd);
}

static char *
squote_to_dquote(char *buf)
{
  char *ptr;

  for (ptr = buf; *ptr != '\0'; ptr++)
    if (*ptr == '\'')
      *ptr = '"';

  return buf;
}

/*static char *
copy_tag_value(const char *json, const char *key)
{
  char full_key[MAX_BUF];
  char *ptr;
  char *end;
  char *out;
  int len;

  //TODO check return
  snprintf(full_key, sizeof(full_key), "\"%s\":", key);

  ptr = strstr(json, full_key);
  if (!ptr)
    return NULL;

  len = strlen(json);

  // Advance ptr from beginning of key to beginning of value (first " after key)
  for (ptr = ptr + strlen(full_key); (ptr - json < len - 1) && (*ptr != '"') && (*ptr != '\0'); ptr++);
  if (*ptr != '"')
    return NULL;

  ptr++;
  for (end = ptr + 1; (end - json < len - 1) && (*end != '"') && (*end != '\0'); end++);
  if (*end != '"')
    return NULL;

  // If value is for instance "test", ptr will now be t and end will be "
  len = (end - ptr);

  out = calloc(1, len + 1);
  if (!out)
    return NULL;

  memcpy(out, ptr, len);

  printf("Value is %s\n", out);

  return out;
}*/


static int
cast_msg_send(struct cast_session *cs, enum cast_msg_types type, cast_reply_cb reply_cb)
{
  Extensions__CoreApi__CastChannel__CastMessage msg = EXTENSIONS__CORE_API__CAST_CHANNEL__CAST_MESSAGE__INIT;
  char msg_buf[MAX_BUF];
  uint8_t buf[MAX_BUF];
  uint32_t be;
  size_t len;
  int ret;

  DPRINTF(E_DBG, L_CAST, "Preparing to send message type %d to %s\n", type, cs->devname);

  msg.source_id = "sender-0";
  msg.namespace_ = cast_msg[type].namespace;

  if ((cast_msg[type].flags & USE_TRANSPORT_ID) && !cs->transport_id)
    {
      DPRINTF(E_LOG, L_CAST, "Error, didn't get transportId for message (type %d) to '%s'\n", type, cs->devname);
      return -1;
    }

  if (cast_msg[type].flags & USE_TRANSPORT_ID)
    msg.destination_id = cs->transport_id;
  else
    msg.destination_id = "receiver-0";

  if (cast_msg[type].flags & (USE_REQUEST_ID | USE_REQUEST_ID_ONLY))
    {
      cs->request_id++;
      if (reply_cb)
	cs->callback_register[cs->request_id % CALLBACK_REGISTER_SIZE] = reply_cb;
    }

  // Special handling of some message types
  if (cast_msg[type].flags & USE_REQUEST_ID_ONLY)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->request_id);
  else if (type == MEDIA_LOAD)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, TEST_STREAM_URL, cs->session_id, cs->request_id);
  else if (type == MEDIA_STOP)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->session_id, cs->request_id);
  else if (type == SET_VOLUME)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->volume, cs->request_id);
  else
    snprintf(msg_buf, sizeof(msg_buf), "%s", cast_msg[type].payload);

  squote_to_dquote(msg_buf);
  msg.payload_utf8 = msg_buf;

  len = extensions__core_api__cast_channel__cast_message__get_packed_size(&msg);
  if (len <= 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send message (type %d), invalid length: %d\n", type, len);
      return -1;
    }

  // The message must be prefixed with Big-Endian 32 bit length
  be = htobe32(len);
  memcpy(buf, &be, 4);

  // Now add the packed message and send it
  extensions__core_api__cast_channel__cast_message__pack(&msg, buf + 4);

  ret = gnutls_record_send(cs->tls_session, buf, len + 4);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send message, TLS error\n");
      return -1;
    }
  else if (ret != len + 4)
    {
      DPRINTF(E_LOG, L_CAST, "BUG! Message partially sent, and we are not able to send the rest\n");
      return -1;
    }

  DPRINTF(E_DBG, L_CAST, "TX %d %s %s %s %s\n", len, msg.source_id, msg.destination_id, msg.namespace_, msg.payload_utf8);

  return 0;
}

static void *
cast_msg_parse(struct cast_msg_payload *payload, char *s)
{
  json_object *haystack;
  json_object *somehay;
  json_object *needle;
  const char *val;
  int i;

  haystack = json_tokener_parse(s);
  if (!haystack)
    {
      DPRINTF(E_LOG, L_CAST, "JSON parser returned an error\n");
      return NULL;
    }

  payload->type = UNKNOWN;
  if (json_object_object_get_ex(haystack, "type", &needle))
    {
      val = json_object_get_string(needle);
      for (i = 1; cast_msg[i].type; i++)
	{
	  if (cast_msg[i].tag && (strcmp(val, cast_msg[i].tag) == 0))
	    {
	      payload->type = cast_msg[i].type;
	      break;
	    }
	}
    }

  if (json_object_object_get_ex(haystack, "requestId", &needle))
    payload->request_id = json_object_get_int(needle);

  // Might be done now
  if (payload->type != RECEIVER_STATUS)
    return haystack;

  // Isn't this marvelous
  if ( ! (json_object_object_get_ex(haystack, "status", &somehay) &&
          json_object_object_get_ex(somehay, "applications", &needle) &&
          (json_object_get_type(needle) == json_type_array) &&
          (somehay = json_object_array_get_idx(needle, 0))) )
    return haystack;

  if ( json_object_object_get_ex(somehay, "sessionId", &needle) &&
       (json_object_get_type(needle) == json_type_string) )
    payload->session_id = json_object_get_string(needle);

  if ( json_object_object_get_ex(somehay, "transportId", &needle) &&
       (json_object_get_type(needle) == json_type_string) )
    payload->transport_id = json_object_get_string(needle);

  return haystack;
}

static void
cast_msg_parse_free(void *haystack)
{
  if (json_object_put((json_object *)haystack) != 1)
    DPRINTF(E_LOG, L_CAST, "Memleak: JSON parser did not free object\n");
}

static void
cast_msg_process(struct cast_session *cs, const uint8_t *data, size_t len)
{
  Extensions__CoreApi__CastChannel__CastMessage *reply;
  struct cast_msg_payload payload = { 0 };
  void *hdl;
  int i;

  reply = extensions__core_api__cast_channel__cast_message__unpack(NULL, len, data);
  if (!reply)
    {
      DPRINTF(E_LOG, L_CAST, "Could not unpack message!\n");
      return;
    }

  DPRINTF(E_DBG, L_CAST, "RX %d %s %s %s %s\n", len, reply->source_id, reply->destination_id, reply->namespace_, reply->payload_utf8);

  hdl = cast_msg_parse(&payload, reply->payload_utf8);
  if (!hdl)
    {
      DPRINTF(E_DBG, L_CAST, "Could not parse message: %s\n", reply->payload_utf8);
      goto out_free_unpacked;
    }

  if (payload.type == PING)
    {
      cast_msg_send(cs, PONG, NULL);
      goto out_free_parsed;
    }

  if (payload.type == UNKNOWN)
    goto out_free_parsed;

  // TODO: UPDATE SESSION STATUS AND READ BROADCASTS

  i = payload.request_id % CALLBACK_REGISTER_SIZE;
  if (i > 0 && cs->callback_register[i])
    {
      cs->callback_register[i](cs, &payload);
      cs->callback_register[i] = NULL;
      goto out_free_parsed;
    }

 out_free_parsed:
  cast_msg_parse_free(hdl);
 out_free_unpacked:
  extensions__core_api__cast_channel__cast_message__free_unpacked(reply, NULL);
}

static void
cast_session_free(struct cast_session *cs)
{
  event_free(cs->ev);

  // TODO How will this work if device disconnected?
  gnutls_bye(cs->tls_session, GNUTLS_SHUT_RDWR);

  close(cs->server_fd);

  gnutls_deinit(cs->tls_session);

  if (cs->address)
    free(cs->address);
  if (cs->devname)
    free(cs->devname);

  if (cs->session_id)
    free(cs->session_id);
  if (cs->transport_id)
    free(cs->transport_id);

  free(cs->output_session);

  free(cs);
}

static void
cast_session_cleanup(struct cast_session *cs)
{
  struct cast_session *s;

  if (cs == sessions)
    sessions = sessions->next;
  else
    {
      for (s = sessions; s && (s->next != cs); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_CAST, "WARNING: struct cast_session not found in list; BUG!\n");
      else
	s->next = cs->next;
    }

  cast_session_free(cs);
}

static void
cast_session_failure(struct cast_session *cs)
{
  // Session failed, let our user know
  cs->status_cb(cs->device, cs->output_session, OUTPUT_STATE_FAILED);

  cast_session_cleanup(cs);
}

static void
cast_cb_startup_volume(struct cast_session *cs, struct cast_msg_payload *payload)
{
  output_status_cb status_cb;

  cs->state = OUTPUT_STATE_CONNECTED;

  /* Session startup and setup is done, tell our user */
  DPRINTF(E_DBG, L_CAST, "Session ready\n");

  status_cb = cs->status_cb;
  cs->status_cb = NULL;

  status_cb(cs->device, cs->output_session, cs->state);
}

static void
cast_cb_startup_media(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (payload->type != MEDIA_STATUS)
    {
      DPRINTF(E_LOG, L_CAST, "No receiver status or transport id?\n");
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
      return;
    }

  // TODO Send a volume message with the cb
  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_startup_volume);
  if (ret < 0)
    {
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
    }
}

static void
cast_cb_startup_launch(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (payload->type != RECEIVER_STATUS || !payload->transport_id)
    {
      DPRINTF(E_LOG, L_CAST, "No receiver status or transport id?\n");
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
      return;
    }

  if (cs->transport_id)
    DPRINTF(E_LOG, L_CAST, "Ooops, memleaking...\n");

  cs->transport_id = strdup(payload->transport_id);

  ret = cast_msg_send(cs, MEDIA_CONNECT, NULL);
  if (ret == 0)
    ret = cast_msg_send(cs, MEDIA_GET_STATUS, cast_cb_startup_media);

  if (ret < 0)
    {
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
    }
}

static void
cast_cb_startup_connect(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (payload->type != RECEIVER_STATUS || !payload->session_id)
    {
      DPRINTF(E_LOG, L_CAST, "No receiver status or session id? (type: %d, id: %s)\n", payload->type, payload->session_id);
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
      return;
    }

  if (cs->session_id)
    DPRINTF(E_LOG, L_CAST, "Ooops, memleaking...\n");

  cs->session_id = strdup(payload->session_id);

  ret = cast_msg_send(cs, LAUNCH, cast_cb_startup_launch);
  if (ret < 0)
    {
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
    }
}

static void
cast_cb_close(struct cast_session *cs, struct cast_msg_payload *payload)
{
}

static void
cast_cb_load(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (payload->type == MEDIA_LOAD_FAILED)
    {
      DPRINTF(E_LOG, L_CAST, "The device %s could not start playback\n", cs->devname);
      cast_msg_send(cs, CLOSE, NULL);
      cast_session_failure(cs);
      return;
    }

  cs->state = OUTPUT_STATE_STREAMING;

  DPRINTF(E_DBG, L_CAST, "Media loaded\n");

  cs->status_cb(cs->device, cs->output_session, cs->state);
}

static void
cast_cb_volume(struct cast_session *cs, struct cast_msg_payload *payload)
{
  output_status_cb status_cb;

  status_cb = cs->status_cb;
  cs->status_cb = NULL;
  status_cb(cs->device, cs->output_session, cs->state);
}



static void
cast_listen_cb(int fd, short what, void *arg)
{
  struct cast_session *cs;
  uint8_t buffer[MAX_BUF + 1]; // Not sure about the +1, but is copied from gnutls examples
  uint32_t be;
  size_t len;
  int processed;
  int ret;

  cs = (struct cast_session *)arg;

  DPRINTF(E_DBG, L_CAST, "New data from %s\n", cs->devname);

  processed = 0;
  while ((ret = gnutls_record_recv(cs->tls_session, buffer + processed, MAX_BUF - processed)) > 0)
    {
      DPRINTF(E_DBG, L_CAST, "Received %d bytes\n", ret);

      if (ret == 4)
	{
	  memcpy(&be, buffer, 4);
	  len = be32toh(be);
	  DPRINTF(E_DBG, L_CAST, "Incoming %d bytes\n", len);
	}
      else
	{
	  processed += ret;
	}

      if (processed >= MAX_BUF)
	{
	  DPRINTF(E_LOG, L_CAST, "Receive buffer exhausted!\n");
	  cast_session_failure(cs);
	  return;
	}
    }

  if ((ret != GNUTLS_E_INTERRUPTED) && (ret != GNUTLS_E_AGAIN))
    {
      DPRINTF(E_LOG, L_CAST, "Session error: %s\n", gnutls_strerror(ret));
      cast_session_failure(cs);
      return;
    }

  if (processed)
    cast_msg_process(cs, buffer, processed);
}

static struct cast_session *
cast_session_make(struct output_device *device, int family, output_status_cb cb)
{
  struct output_session *os;
  struct cast_session *cs;
  const char *proto;
  const char *err;
  char *address;
  unsigned short port;
  int flags;
  int ret;

  switch (family)
    {
      case AF_INET:
	/* We always have the v4 services, so no need to check */
	if (!device->v4_address)
	  return NULL;

	address = device->v4_address;
	port = device->v4_port;
	break;

      case AF_INET6:
	if (!device->v6_address)
	  return NULL;

	address = device->v6_address;
	port = device->v6_port;
	break;

      default:
	return NULL;
    }

  os = calloc(1, sizeof(struct output_session));
  cs = calloc(1, sizeof(struct cast_session));
  if (!os || !cs)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for TLS session\n");
      return NULL;
    }

  os->session = cs;
  os->type = device->type;

  cs->output_session = os;
  cs->state = OUTPUT_STATE_STOPPED;
  cs->device = device;
  cs->status_cb = cb;

  /* Init TLS session, use default priorities and put the x509 credentials to the current session */
  if ( ((ret = gnutls_init(&cs->tls_session, GNUTLS_CLIENT)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_priority_set_direct(cs->tls_session, "PERFORMANCE", &err)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_credentials_set(cs->tls_session, GNUTLS_CRD_CERTIFICATE, tls_credentials)) != GNUTLS_E_SUCCESS) )
    {
      DPRINTF(E_LOG, L_CAST, "Could not initialize GNUTLS session: %s\n", gnutls_strerror(ret));
      goto out_free_session;
    }

  cs->server_fd = tcp_connect(address, port, family);
  if (cs->server_fd < 0)
    goto out_deinit_gnutls;

  // TODO Add a timeout to detect connection problems
  cs->ev = event_new(evbase_player, cs->server_fd, EV_READ | EV_PERSIST, cast_listen_cb, cs);
  if (!cs->ev)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for listener event\n");
      goto out_close_connection;
    }

  gnutls_transport_set_ptr(cs->tls_session, (gnutls_transport_ptr_t)cs->server_fd);
  ret = gnutls_handshake(cs->tls_session);
  if (ret != GNUTLS_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_CAST, "Could not attach TLS to TCP connection: %s\n", gnutls_strerror(ret));
      goto out_free_ev;
    }

  flags = fcntl(cs->server_fd, F_GETFL, 0);
  fcntl(cs->server_fd, F_SETFL, flags | O_NONBLOCK);

  event_add(cs->ev, NULL);

  cs->devname = strdup(device->name);
  cs->address = strdup(address);

  cs->volume = 0.01 * device->volume;

  cs->next = sessions;
  sessions = cs;

  proto = gnutls_protocol_get_name(gnutls_protocol_get_version(cs->tls_session));

  DPRINTF(E_INFO, L_CAST, "Connection to %s established using %s\n", cs->devname, proto);

  return cs;

 out_free_ev:
  event_free(cs->ev);
 out_close_connection:
  tcp_close(cs->server_fd);
 out_deinit_gnutls:
  gnutls_deinit(cs->tls_session);
 out_free_session:
  free(cs);

  return NULL;
}

static int
cast_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime);

static void
cast_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct output_device *device;
  const char *p;
  uint32_t id;

  p = keyval_get(txt, "id");
  if (p)
    id = djb_hash(p, strlen(p));
  else
    id = 0;

  if (!id)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not extract ChromeCast device ID (%s)\n", name);

      return;
    }

  DPRINTF(E_DBG, L_PLAYER, "Event for Chromecast device %s (port %d, id %" PRIu32 ")\n", name, port, id);

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_PLAYER, "Out of memory for new Chromecast device\n");

      return;
    }

  device->id = id;
  device->name = strdup(name);
  device->type = OUTPUT_TYPE_CAST;
  device->type_name = outputs_name(device->type);

  if (port < 0)
    {
      /* Device stopped advertising */
      switch (family)
	{
	  case AF_INET:
	    device->v4_port = 1;
	    break;

	  case AF_INET6:
	    device->v6_port = 1;
	    break;
	}

      player_device_remove(device);

      return;
    }

  DPRINTF(E_INFO, L_PLAYER, "Adding Chromecast device %s\n", name);

  device->advertised = 1;

  switch (family)
    {
      case AF_INET:
	device->v4_address = strdup(address);
	device->v4_port = port;
	break;

      case AF_INET6:
	device->v6_address = strdup(address);
	device->v6_port = port;
	break;
    }

  player_device_add(device);
}

static int
cast_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  struct cast_session *cs;
  int ret;

  DPRINTF(E_LOG, L_CAST, "Got start request for %s\n", device->name);

  cs = cast_session_make(device, AF_INET6, cb);
  if (cs)
    {
      ret = cast_msg_send(cs, CONNECT, NULL);
      if (ret == 0)
	{
	  cast_msg_send(cs, GET_STATUS, cast_cb_startup_connect);
	  return 0;
	}
      else
	{
	  DPRINTF(E_WARN, L_CAST, "Could not send CONNECT request on IPv6 (start)\n");

	  cast_session_cleanup(cs);
	}
    }

  cs = cast_session_make(device, AF_INET, cb);
  if (!cs)
    return -1;

  ret = cast_msg_send(cs, CONNECT, NULL);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send CONNECT request on IPv4 (start)\n");

      cast_session_cleanup(cs);
      return -1;
    }

  // TODO Check return value
  cast_msg_send(cs, GET_STATUS, cast_cb_startup_connect);

  return 0;
}

static void
cast_device_stop(struct output_session *session)
{
  struct cast_session *cs = session->session;

  if (!(cs->state & OUTPUT_STATE_F_CONNECTED))
    cast_session_cleanup(cs);
  else
    cast_msg_send(cs, CLOSE, cast_cb_close);
}

static int
cast_volume_set(struct output_device *device, output_status_cb cb)
{
  struct cast_session *cs;
  int ret;

  if (!device->session || !device->session->session)
    return 0;

  cs = device->session->session;

  if (!(cs->state & OUTPUT_STATE_F_CONNECTED))
    return 0;

  cs->volume = 0.01 * device->volume;

  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_volume);
  if (ret < 0)
    {
      cast_session_failure(cs);
      return 0;
    }

  cs->status_cb = cb;

  return 1;
}

static void
cast_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  struct cast_session *cs;

  for (cs = sessions; cs; cs = cs->next)
    {
      if (cs->state == OUTPUT_STATE_CONNECTED)
	cast_msg_send(cs, MEDIA_LOAD, cast_cb_load);
    }
}

static void
cast_set_status_cb(struct output_session *session, output_status_cb cb)
{
  struct cast_session *cs = session->session;

  cs->status_cb = cb;
}


static int
cast_init(void)
{
  int mdns_flags;
  int i;
  int ret;

  // Sanity check
  for (i = 1; cast_msg[i].type; i++)
    {
      if (cast_msg[i].type != i)
	{
	  DPRINTF(E_LOG, L_CAST, "BUG! Cast messages and types are misaligned (type %d!=%d). Could not initialize.\n", cast_msg[i].type, i);
	  return -1;
	}
    }

  // TODO Setting the cert file may not be required
  if ( ((ret = gnutls_global_init()) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_certificate_allocate_credentials(&tls_credentials)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_certificate_set_x509_trust_file(tls_credentials, CAFILE, GNUTLS_X509_FMT_PEM)) < 0) )
    {
      DPRINTF(E_LOG, L_CAST, "Could not initialize GNUTLS: %s\n", gnutls_strerror(ret));
      return -1;
    }

  mdns_flags = MDNS_WANT_V4 | MDNS_WANT_V6 | MDNS_WANT_V6LL;

  ret = mdns_browse("_googlecast._tcp", mdns_flags, cast_device_cb);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_PLAYER, "Could not add mDNS browser for Chromecast devices\n");
      goto out_tls_deinit;
    }

  return 0;

 out_tls_deinit:
  gnutls_certificate_free_credentials(tls_credentials);
  gnutls_global_deinit();

  return -1;
}

static void
cast_deinit(void)
{
  struct cast_session *cs;

  for (cs = sessions; sessions; cs = sessions)
    {
      sessions = cs->next;
      cast_session_free(cs);
    }

  gnutls_certificate_free_credentials(tls_credentials);
  gnutls_global_deinit();
}

struct output_definition output_cast =
{
  .name = "Chromecast",
  .type = OUTPUT_TYPE_CAST,
  .priority = 2,
  .disabled = 0,
  .init = cast_init,
  .deinit = cast_deinit,
  .device_start = cast_device_start,
  .device_stop = cast_device_stop,
/*  .device_probe = cast_device_probe,
  .device_free_extra = cast_device_free_extra,*/
  .device_volume_set = cast_volume_set,
  .playback_start = cast_playback_start,
/*  .playback_stop = cast_playback_stop,
  .write = cast_write,
  .flush = cast_flush,*/
  .status_cb = cast_set_status_cb,
/*  .metadata_prepare = cast_metadata_prepare,
  .metadata_send = cast_metadata_send,
  .metadata_purge = cast_metadata_purge,
  .metadata_prune = cast_metadata_prune,
*/
};
