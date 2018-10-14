/*
 * Copyright (C) 2015-2016 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * Credit goes to the authors of pychromecast and those before that who have
 * discovered how to do this.
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
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#elif defined(HAVE_LIBKERN_OSBYTEORDER_H)
#include <libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#endif
#include <gnutls/gnutls.h>
#include <event2/event.h>
#include <json.h>

#include "conffile.h"
#include "mdns.h"
#include "logger.h"
#include "player.h"
#include "outputs.h"

#ifdef HAVE_PROTOBUF_OLD
#include "cast_channel.v0.pb-c.h"
#else
#include "cast_channel.pb-c.h"
#endif

// Number of bytes to request from TLS connection
#define MAX_BUF 4096
// CA file location (not very portable...?)
#define CAFILE "/etc/ssl/certs/ca-certificates.crt"

// Seconds without a heartbeat from the Chromecast before we close the session
#define HEARTBEAT_TIMEOUT 8
// Seconds after a flush (pause) before we close the session
#define FLUSH_TIMEOUT 30
// Seconds to wait for a reply before making the callback requested by caller
#define REPLY_TIMEOUT 5

// ID of the default receiver app
#define CAST_APP_ID "CC1AD845"

// Namespaces
#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_MEDIA "urn:x-cast:com.google.cast.media"

#define USE_TRANSPORT_ID     (1 << 1)
#define USE_REQUEST_ID       (1 << 2)
#define USE_REQUEST_ID_ONLY  (1 << 3)

#define CALLBACK_REGISTER_SIZE 32

//#define DEBUG_CONNECTION 1

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

// Session is starting up
#define CAST_STATE_F_STARTUP         (1 << 13)
// The default receiver app is ready
#define CAST_STATE_F_MEDIA_CONNECTED (1 << 14)
// Media is loaded in the receiver app
#define CAST_STATE_F_MEDIA_LOADED    (1 << 15)
// Media is playing in the receiver app
#define CAST_STATE_F_MEDIA_PLAYING   (1 << 16)

// Beware, the order of this enum has meaning
enum cast_state
{
  // Something bad happened during a session
  CAST_STATE_FAILED          = 0,
  // No session allocated
  CAST_STATE_NONE            = 1,
  // Session allocated, but no connection
  CAST_STATE_DISCONNECTED    = CAST_STATE_F_STARTUP | 0x01,
  // TCP connect, TLS handshake, CONNECT and GET_STATUS request
  CAST_STATE_CONNECTED       = CAST_STATE_F_STARTUP | 0x02,
  // Default media receiver app is launched
  CAST_STATE_MEDIA_LAUNCHED  = CAST_STATE_F_STARTUP | 0x03,
  // CONNECT and GET_STATUS made to receiver app
  CAST_STATE_MEDIA_CONNECTED = CAST_STATE_F_MEDIA_CONNECTED,
  // Receiver app has loaded our media
  CAST_STATE_MEDIA_LOADED    = CAST_STATE_F_MEDIA_CONNECTED | CAST_STATE_F_MEDIA_LOADED,
  // After PAUSE
  CAST_STATE_MEDIA_PAUSED    = CAST_STATE_F_MEDIA_CONNECTED | CAST_STATE_F_MEDIA_LOADED | 0x01,
  // After LOAD
  CAST_STATE_MEDIA_BUFFERING = CAST_STATE_F_MEDIA_CONNECTED | CAST_STATE_F_MEDIA_LOADED | CAST_STATE_F_MEDIA_PLAYING,
  // After PLAY
  CAST_STATE_MEDIA_PLAYING   = CAST_STATE_F_MEDIA_CONNECTED | CAST_STATE_F_MEDIA_LOADED | CAST_STATE_F_MEDIA_PLAYING | 0x01,
};

struct cast_session
{
  // Current state
  enum cast_state state;

  // Used to register a target state if we are transitioning from one to another
  enum cast_state wanted_state;

  // Connection fd and session, and listener event
  int server_fd;
  gnutls_session_t tls_session;
  struct event *ev;

  char *devname;
  char *address;
  unsigned short port;

  // ChromeCast uses a float between 0 - 1
  float volume;

  // IP address URL of forked-daapd's mp3 stream
  char stream_url[128];

  // Outgoing request which have the USE_REQUEST_ID flag get a new id, and a
  // callback is registered. The callback is called when an incoming message
  // from the peer with that request id arrives. If nothing arrives within
  // REPLY_TIMEOUT we make the callback with a NULL payload pointer.
  int request_id;
  cast_reply_cb callback_register[CALLBACK_REGISTER_SIZE];
  struct event *reply_timeout;

  // This is used to work around a bug where no response is given by the device.
  // For certain requests, we will then retry, e.g. by checking status. We
  // register our retry so that we on only retry once.
  int retry;

  // Session info from the ChromeCast
  char *transport_id;
  char *session_id;
  int media_session_id;

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
  STOP,
  MEDIA_CONNECT,
  MEDIA_CLOSE,
  MEDIA_GET_STATUS,
  MEDIA_STATUS,
  MEDIA_LOAD,
  MEDIA_PLAY,
  MEDIA_PAUSE,
  MEDIA_STOP,
  MEDIA_LOAD_FAILED,
  MEDIA_LOAD_CANCELLED,
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
  const char *app_id;
  const char *session_id;
  const char *transport_id;
  const char *player_state;
  int media_session_id;
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
    .payload = "{'type':'LAUNCH','requestId':%d,'appId':'" CAST_APP_ID "'}",
    .flags = USE_REQUEST_ID_ONLY,
  },
  {
    .type = STOP,
    .namespace = NS_RECEIVER,
    .payload = "{'type':'STOP','sessionId':'%s','requestId':%d}",
    .flags = USE_REQUEST_ID,
  },
  {
    .type = MEDIA_CONNECT,
    .namespace = NS_CONNECTION,
    .payload = "{'type':'CONNECT'}",
    .flags = USE_TRANSPORT_ID,
  },
  {
    .type = MEDIA_CLOSE,
    .namespace = NS_CONNECTION,
    .payload = "{'type':'CLOSE'}",
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
    .type = MEDIA_PLAY,
    .namespace = NS_MEDIA,
    .payload = "{'mediaSessionId':%d,'sessionId':'%s','type':'PLAY','requestId':%d}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = MEDIA_PAUSE,
    .namespace = NS_MEDIA,
    .payload = "{'mediaSessionId':%d,'sessionId':'%s','type':'PAUSE','requestId':%d}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = MEDIA_STOP,
    .namespace = NS_MEDIA,
    .payload = "{'mediaSessionId':%d,'sessionId':'%s','type':'STOP','requestId':%d}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = MEDIA_LOAD_FAILED,
    .tag = "LOAD_FAILED",
  },
  {
    .type = MEDIA_LOAD_CANCELLED,
    .tag = "LOAD_CANCELLED",
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
static struct event *flush_timer;
static struct timeval heartbeat_timeout = { HEARTBEAT_TIMEOUT, 0 };
static struct timeval flush_timeout = { FLUSH_TIMEOUT, 0 };
static struct timeval reply_timeout = { REPLY_TIMEOUT, 0 };


/* ------------------------------- MISC HELPERS ----------------------------- */

static int
tcp_connect(const char *address, unsigned int port, int family)
{
  union sockaddr_all sa;
  int fd;
  int len;
  int ret;

  // TODO Open non-block right away so we don't block the player while connecting
  // and during TLS handshake (we would probably need to introduce a deferredev)
#ifdef SOCK_CLOEXEC
  fd = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  fd = socket(family, SOCK_STREAM, 0);
#endif
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

static int
stream_url_make(char *out, size_t len, const char *peer_addr, int family)
{
  struct ifaddrs *ifap;
  struct ifaddrs *ifa;
  union sockaddr_all haddr;
  union sockaddr_all hmask;
  union sockaddr_all paddr;
  char host_addr[128];
  unsigned short port;
  int found;
  int ret;

  if (family == AF_INET)
    ret =  inet_pton(AF_INET, peer_addr, &paddr.sin.sin_addr);
  else
    ret =  inet_pton(AF_INET6, peer_addr, &paddr.sin6.sin6_addr);

  if (ret != 1)
    return -1;

  found = 0;
  ret = getifaddrs(&ifap);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not get interface address: %s\n", strerror(errno));
      return -1;
    }

  for (ifa = ifap; !found && ifa; ifa = ifa->ifa_next)
    {
      if (!ifa->ifa_addr)
	{
	  DPRINTF(E_LOG, L_CAST, "Skipping null address from getifaddrs()\n");
	  continue;
	}

      if (ifa->ifa_addr->sa_family != family)
	continue;

      if (family == AF_INET)
	{
	  memcpy(&haddr.sin, ifa->ifa_addr, sizeof(struct sockaddr_in));
	  memcpy(&hmask.sin, ifa->ifa_netmask, sizeof(struct sockaddr_in));
          found = ((haddr.sin.sin_addr.s_addr & hmask.sin.sin_addr.s_addr) ==
                   (paddr.sin.sin_addr.s_addr & hmask.sin.sin_addr.s_addr));
	  if (found)
	    inet_ntop(family, &haddr.sin.sin_addr, host_addr, sizeof(host_addr));
	}
      else if (family == AF_INET6)
	{
	  memcpy(&haddr.sin6, ifa->ifa_addr, sizeof(struct sockaddr_in6));
          found = (memcmp(&haddr.sin6.sin6_addr.s6_addr, &paddr.sin6.sin6_addr.s6_addr, 8) == 0);
	  if (found)
	    inet_ntop(family, &haddr.sin6.sin6_addr, host_addr, sizeof(host_addr));
	}
    }

  freeifaddrs(ifap);

  if (!found)
    return -1;

  port = cfg_getint(cfg_getsec(cfg, "library"), "port");
  if (family == AF_INET)
    snprintf(out, len, "http://%s:%d/stream.mp3", host_addr, port);
  else
    snprintf(out, len, "http://[%s]:%d/stream.mp3", host_addr, port);

  return 0;
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

/* ----------------------------- SESSION CLEANUP ---------------------------- */

static void
cast_session_free(struct cast_session *cs)
{
  if (!cs)
    return;

  event_free(cs->reply_timeout);
  event_free(cs->ev);

  if (cs->server_fd >= 0)
    tcp_close(cs->server_fd);

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

// Forward
static void
cast_session_shutdown(struct cast_session *cs, enum cast_state wanted_state);


/* --------------------------- CAST MESSAGE HANDLING ------------------------ */

static int
cast_msg_send(struct cast_session *cs, enum cast_msg_types type, cast_reply_cb reply_cb)
{
  Extensions__CoreApi__CastChannel__CastMessage msg = EXTENSIONS__CORE_API__CAST_CHANNEL__CAST_MESSAGE__INIT;
  char msg_buf[MAX_BUF];
  uint8_t buf[MAX_BUF];
  uint32_t be;
  size_t len;
  int ret;

#ifdef DEBUG_CONNECTION
  DPRINTF(E_DBG, L_CAST, "Preparing to send message type %d to '%s'\n", type, cs->devname);
#endif

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
	{
	  cs->callback_register[cs->request_id % CALLBACK_REGISTER_SIZE] = reply_cb;
	  event_add(cs->reply_timeout, &reply_timeout);
	}
    }

  // Special handling of some message types
  if (cast_msg[type].flags & USE_REQUEST_ID_ONLY)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->request_id);
  else if (type == STOP)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->session_id, cs->request_id);
  else if (type == MEDIA_LOAD)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->stream_url, cs->session_id, cs->request_id);
  else if ((type == MEDIA_PLAY) || (type == MEDIA_PAUSE) || (type == MEDIA_STOP))
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->media_session_id, cs->session_id, cs->request_id);
  else if (type == SET_VOLUME)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->volume, cs->request_id);
  else
    snprintf(msg_buf, sizeof(msg_buf), "%s", cast_msg[type].payload);

  squote_to_dquote(msg_buf);
  msg.payload_utf8 = msg_buf;

  len = extensions__core_api__cast_channel__cast_message__get_packed_size(&msg);
  if (len <= 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send message (type %d), invalid length: %zu\n", type, len);
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

  if (type != PONG)
    DPRINTF(E_DBG, L_CAST, "TX %zu %s %s %s %s\n", len, msg.source_id, msg.destination_id, msg.namespace_, msg.payload_utf8);

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
  if ((payload->type != RECEIVER_STATUS) && (payload->type != MEDIA_STATUS))
    return haystack;

  // Isn't this marvelous
  if ( json_object_object_get_ex(haystack, "status", &needle) &&
       (json_object_get_type(needle) == json_type_array) &&
       (somehay = json_object_array_get_idx(needle, 0)) )
    {
      if ( json_object_object_get_ex(somehay, "mediaSessionId", &needle) &&
           (json_object_get_type(needle) == json_type_int) )
	payload->media_session_id = json_object_get_int(needle);

      if ( json_object_object_get_ex(somehay, "playerState", &needle) &&
           (json_object_get_type(needle) == json_type_string) )
	payload->player_state = json_object_get_string(needle);
    }


  if ( json_object_object_get_ex(haystack, "status", &somehay) &&
       json_object_object_get_ex(somehay, "applications", &needle) &&
       (json_object_get_type(needle) == json_type_array) &&
       (somehay = json_object_array_get_idx(needle, 0)) )
    {
      if ( json_object_object_get_ex(somehay, "appId", &needle) &&
           (json_object_get_type(needle) == json_type_string) )
	payload->app_id = json_object_get_string(needle);

      if ( json_object_object_get_ex(somehay, "sessionId", &needle) &&
           (json_object_get_type(needle) == json_type_string) )
	payload->session_id = json_object_get_string(needle);

      if ( json_object_object_get_ex(somehay, "transportId", &needle) &&
           (json_object_get_type(needle) == json_type_string) )
	payload->transport_id = json_object_get_string(needle);
    }

  return haystack;
}

static void
cast_msg_parse_free(void *haystack)
{
#ifdef HAVE_JSON_C_OLD
  json_object_put((json_object *)haystack);
#else
  if (json_object_put((json_object *)haystack) != 1)
    DPRINTF(E_LOG, L_CAST, "Memleak: JSON parser did not free object\n");
#endif
}

static void
cast_msg_process(struct cast_session *cs, const uint8_t *data, size_t len)
{
  Extensions__CoreApi__CastChannel__CastMessage *reply;
  cast_reply_cb reply_cb;
  struct cast_msg_payload payload = { 0 };
  void *hdl;
  int unknown_app_id;
  int unknown_session_id;
  int i;

#ifdef DEBUG_CONNECTION
  char *b64 = b64_encode(data, len);
  if (b64)
    {
      DPRINTF(E_DBG, L_CAST, "Reply dump (len %zu): %s\n", len, b64);
      free(b64);
    }
#endif

  reply = extensions__core_api__cast_channel__cast_message__unpack(NULL, len, data);
  if (!reply)
    {
      DPRINTF(E_LOG, L_CAST, "Could not unpack message!\n");
      return;
    }

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

  DPRINTF(E_DBG, L_CAST, "RX %zu %s %s %s %s\n", len, reply->source_id, reply->destination_id, reply->namespace_, reply->payload_utf8);

  if (payload.type == UNKNOWN)
    goto out_free_parsed;

  i = payload.request_id % CALLBACK_REGISTER_SIZE;
  if (payload.request_id && cs->callback_register[i])
    {
      reply_cb = cs->callback_register[i];
      cs->callback_register[i] = NULL;

      // Cancel the timeout if no pending callbacks
      for (i = 0; (i < CALLBACK_REGISTER_SIZE) && (!cs->callback_register[i]); i++);

      if (i == CALLBACK_REGISTER_SIZE)
	evtimer_del(cs->reply_timeout);

      reply_cb(cs, &payload);

      goto out_free_parsed;
    }

  // TODO Should we read volume and playerstate changes from the Chromecast?

  if (payload.type == RECEIVER_STATUS && (cs->state & CAST_STATE_F_MEDIA_CONNECTED))
    {
      unknown_app_id = payload.app_id && (strcmp(payload.app_id, CAST_APP_ID) != 0);
      unknown_session_id = payload.session_id && (strcmp(payload.session_id, cs->session_id) != 0);
      if (unknown_app_id || unknown_session_id)
	{
	  DPRINTF(E_WARN, L_CAST, "Our session on '%s' was hijacked\n", cs->devname);

	  // Downgrade state, we don't have the receiver app any more
	  cs->state = CAST_STATE_CONNECTED;
	  cast_session_shutdown(cs, CAST_STATE_FAILED);
	  goto out_free_parsed;
	}
    }

  if (payload.type == MEDIA_STATUS && (cs->state & CAST_STATE_F_MEDIA_PLAYING))
    {
      if (payload.player_state && (strcmp(payload.player_state, "PAUSED") == 0))
	{
	  DPRINTF(E_WARN, L_CAST, "Something paused our session on '%s'\n", cs->devname);

/*	  cs->state = CAST_STATE_MEDIA_CONNECTED;
	  // Kill the session, the player will need to restart it
	  cast_session_shutdown(cs, CAST_STATE_NONE);
	  goto out_free_parsed;
*/	}
    }

 out_free_parsed:
  cast_msg_parse_free(hdl);
 out_free_unpacked:
  extensions__core_api__cast_channel__cast_message__free_unpacked(reply, NULL);
}


/* -------------------------------- CALLBACKS ------------------------------- */

/* Maps our internal state to the generic output state and then makes a callback
 * to the player to tell that state
 */
static void
cast_status(struct cast_session *cs)
{
  output_status_cb status_cb = cs->status_cb;
  enum output_device_state state;

  switch (cs->state)
    {
      case CAST_STATE_FAILED:
	state = OUTPUT_STATE_FAILED;
	break;
      case CAST_STATE_NONE:
	state = OUTPUT_STATE_STOPPED;
	break;
      case CAST_STATE_DISCONNECTED ... CAST_STATE_MEDIA_LAUNCHED:
	state = OUTPUT_STATE_STARTUP;
	break;
      case CAST_STATE_MEDIA_CONNECTED:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case CAST_STATE_MEDIA_LOADED ... CAST_STATE_MEDIA_PAUSED:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case CAST_STATE_MEDIA_BUFFERING ... CAST_STATE_MEDIA_PLAYING:
	state = OUTPUT_STATE_STREAMING;
	break;
      default:
	DPRINTF(E_LOG, L_CAST, "Bug! Unhandled state in cast_status()\n");
	state = OUTPUT_STATE_FAILED;
    }

  cs->status_cb = NULL;
  if (status_cb)
    status_cb(cs->device, cs->output_session, state);
}

/* cast_cb_stop*: Callback chain for shutting down a session */
static void
cast_cb_stop(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our STOP - will continue anyway\n");
  else if (payload->type != RECEIVER_STATUS)
    DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our STOP (got type: %d) - will continue anyway\n", payload->type);

  cs->state = CAST_STATE_CONNECTED;

  if (cs->state == cs->wanted_state)
    cast_status(cs);
  else
    cast_session_shutdown(cs, cs->wanted_state);
}

static void
cast_cb_stop_media(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    DPRINTF(E_LOG, L_CAST, "No MEDIA_STATUS reply to our STOP - will continue anyway\n");
  else if (payload->type != MEDIA_STATUS)
    DPRINTF(E_LOG, L_CAST, "No MEDIA_STATUS reply to our STOP (got type: %d) - will continue anyway\n", payload->type);

  cs->state = CAST_STATE_MEDIA_CONNECTED;

  if (cs->state == cs->wanted_state)
    cast_status(cs);
  else
    cast_session_shutdown(cs, cs->wanted_state);
}


/* cast_cb_startup*: Callback chain for starting a session */
static void
cast_cb_startup_volume(struct cast_session *cs, struct cast_msg_payload *payload)
{
  /* Session startup and setup is done, tell our user */
  DPRINTF(E_DBG, L_CAST, "Session ready\n");

  cast_status(cs);
}

static void
cast_cb_startup_media(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No MEDIA_STATUS reply to our GET_STATUS - aborting\n");
      goto error;
    }
  else if (payload->type != MEDIA_STATUS)
    {
      DPRINTF(E_LOG, L_CAST, "No MEDIA_STATUS reply to our GET_STATUS (got type: %d) - aborting\n", payload->type);
      goto error;
    }

  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_startup_volume);
  if (ret < 0)
    goto error;

  cs->state = CAST_STATE_MEDIA_CONNECTED;

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

static void
cast_cb_startup_launch(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  // Sometimes the response to a LAUNCH is just a broadcast RECEIVER_STATUS
  // without our requestId. That won't be registered by our response handler,
  // and we get an empty callback due to timeout. In this case we send a
  // GET_STATUS to see if we are good to go anyway.
  if (!payload && !cs->retry)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our LAUNCH - trying GET_STATUS instead\n");
      cs->retry++;
      ret = cast_msg_send(cs, GET_STATUS, cast_cb_startup_launch);
      if (ret != 0)
	goto error;

      return;
    }

  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our LAUNCH - aborting\n");
      goto error;
    }

  if (payload->type != RECEIVER_STATUS)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our LAUNCH (got type: %d) - aborting\n", payload->type);
      goto error;
    }

  if (!payload->transport_id || !payload->session_id)
    {
      DPRINTF(E_LOG, L_CAST, "Missing session id or transport id in RECEIVER_STATUS - aborting\n");
      goto error;
    }

  if (cs->session_id || cs->transport_id)
    DPRINTF(E_LOG, L_CAST, "Bug! Memleaking...\n");

  cs->session_id = strdup(payload->session_id);
  cs->transport_id = strdup(payload->transport_id);

  cs->retry = 0;

  ret = cast_msg_send(cs, MEDIA_CONNECT, NULL);
  if (ret == 0)
    ret = cast_msg_send(cs, MEDIA_GET_STATUS, cast_cb_startup_media);

  if (ret < 0)
    goto error;

  cs->state = CAST_STATE_MEDIA_LAUNCHED;

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

static void
cast_cb_startup_connect(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our GET_STATUS - aborting\n");
      goto error;
    }
  else if (payload->type != RECEIVER_STATUS)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our GET_STATUS (got type: %d) - aborting\n", payload->type);
      goto error;
    }

  ret = cast_msg_send(cs, LAUNCH, cast_cb_startup_launch);
  if (ret < 0)
    goto error;

  cs->state = CAST_STATE_CONNECTED;

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

/* cast_cb_probe: Callback from cast_device_probe */
static void
cast_cb_probe(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our GET_STATUS - aborting\n");
      goto error;
    }
  else if (payload->type != RECEIVER_STATUS)
    {
      DPRINTF(E_LOG, L_CAST, "No RECEIVER_STATUS reply to our GET_STATUS (got type: %d) - aborting\n", payload->type);
      goto error;
    }

  cs->state = CAST_STATE_CONNECTED;

  cast_status(cs);

  cast_session_shutdown(cs, CAST_STATE_NONE);

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

/* cast_cb_load: Callback from starting playback */
static void
cast_cb_load(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No reply from '%s' to our LOAD request\n", cs->devname);
      goto error;
    }
  else if ((payload->type == MEDIA_LOAD_FAILED) || (payload->type == MEDIA_LOAD_CANCELLED))
    {
      DPRINTF(E_LOG, L_CAST, "The device '%s' could not start playback\n", cs->devname);
      goto error;
    }
  else if (!payload->media_session_id)
    {
      DPRINTF(E_LOG, L_CAST, "Missing media session id in MEDIA_STATUS - aborting\n");
      goto error;
    }

  cs->media_session_id = payload->media_session_id;
  // We autoplay for the time being
  cs->state = CAST_STATE_MEDIA_PLAYING;

  cast_status(cs);

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

static void
cast_cb_volume(struct cast_session *cs, struct cast_msg_payload *payload)
{
  cast_status(cs);
}

static void
cast_cb_flush(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    DPRINTF(E_LOG, L_CAST, "No reply to PAUSE request from '%s' - will continue\n", cs->devname);
  else if (payload->type != MEDIA_STATUS)
    DPRINTF(E_LOG, L_CAST, "Unexpected reply to PAUSE request from '%s' - will continue\n", cs->devname);

  cs->state = CAST_STATE_MEDIA_PAUSED;

  cast_status(cs);
}

/* The core of this module. Libevent makes a callback to this function whenever
 * there is new data to be read on the fd from the ChromeCast. If everything is
 * good then the data will be passed to cast_msg_process() that will then 
 * parse and make callbacks, if relevant.
 */
static void
cast_listen_cb(int fd, short what, void *arg)
{
  struct cast_session *cs;
  uint8_t buffer[MAX_BUF + 1]; // Not sure about the +1, but is copied from gnutls examples
  uint32_t be;
  size_t len;
  int received;
  int ret;

  for (cs = sessions; cs; cs = cs->next)
    {
      if (cs == (struct cast_session *)arg)
	break;
    }

  if (!cs)
    {
      DPRINTF(E_INFO, L_CAST, "Callback on dead session, ignoring\n");
      return;
    }

  if (what == EV_TIMEOUT)
    {
      DPRINTF(E_LOG, L_CAST, "No heartbeat from '%s', shutting down\n", cs->devname);
      goto fail;
    }

#ifdef DEBUG_CONNECTION
  DPRINTF(E_DBG, L_CAST, "New data from '%s'\n", cs->devname);
#endif

  // We first read the 4 byte header and then the actual message. The header
  // will be the length of the message.
  ret = gnutls_record_recv(cs->tls_session, buffer, 4);
  if (ret != 4)
    goto no_read;

  memcpy(&be, buffer, 4);
  len = be32toh(be);
  if ((len == 0) || (len > MAX_BUF))
    {
      DPRINTF(E_LOG, L_CAST, "Bad length of incoming message, aborting (len=%zu, size=%d)\n", len, MAX_BUF);
      goto fail;
    }

  received = 0;
  while (received < len)
    {
      ret = gnutls_record_recv(cs->tls_session, buffer + received, len - received);
      if (ret <= 0)
	goto no_read;

      received += ret;

#ifdef DEBUG_CONNECTION
      DPRINTF(E_DBG, L_CAST, "Received %d bytes out of expected %zu bytes\n", received, len);
#endif
    }

  ret = gnutls_record_check_pending(cs->tls_session);

  // Process the message - note that this may result in cs being invalidated
  cast_msg_process(cs, buffer, len);

  // In the event there was more data waiting for us we go again
  if (ret > 0)
    {
      DPRINTF(E_INFO, L_CAST, "More data pending from device (%d bytes)\n", ret);
      cast_listen_cb(fd, what, arg);
    }

  return;

 no_read:
  if ((ret != GNUTLS_E_INTERRUPTED) && (ret != GNUTLS_E_AGAIN))
    {
      DPRINTF(E_LOG, L_CAST, "Session error: %s\n", gnutls_strerror(ret));
      goto fail;
    }

  DPRINTF(E_DBG, L_CAST, "Return value from tls is %d (GNUTLS_E_AGAIN is %d)\n", ret, GNUTLS_E_AGAIN);

  return;

 fail:
  // Downgrade state to make cast_session_shutdown perform an exit which is
  // quick and won't require a reponse from the device
  cs->state = CAST_STATE_CONNECTED;
  cast_session_shutdown(cs, CAST_STATE_FAILED);
}

static void
cast_reply_timeout_cb(int fd, short what, void *arg)
{
  struct cast_session *cs;
  int i;

  cs = (struct cast_session *)arg;
  i = cs->request_id % CALLBACK_REGISTER_SIZE;

  DPRINTF(E_LOG, L_CAST, "Request %d timed out, will run empty callback\n", i);

  if (cs->callback_register[i])
    {
      cs->callback_register[i](cs, NULL);
      cs->callback_register[i] = NULL;
    }
}

static void
cast_device_cb(const char *name, const char *type, const char *domain, const char *hostname, int family, const char *address, int port, struct keyval *txt)
{
  struct output_device *device;
  const char *friendly_name;
  uint32_t id;

  id = djb_hash(name, strlen(name));
  if (!id)
    {
      DPRINTF(E_LOG, L_CAST, "Could not hash ChromeCast device name (%s)\n", name);
      return;
    }

  friendly_name = keyval_get(txt, "fn");
  if (friendly_name)
    name = friendly_name;

  DPRINTF(E_DBG, L_CAST, "Event for Chromecast device '%s' (port %d, id %" PRIu32 ")\n", name, port, id);

  device = calloc(1, sizeof(struct output_device));
  if (!device)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for new Chromecast device\n");
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

  DPRINTF(E_INFO, L_CAST, "Adding Chromecast device '%s'\n", name);

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


/* --------------------- SESSION CONSTRUCTION AND SHUTDOWN ------------------ */

// Allocates a session and sets of the startup sequence until the session reaches
// the CAST_STATE_MEDIA_CONNECTED status (so it is ready to load media)
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
  if (!os)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory (os)\n");
      return NULL;
    }

  cs = calloc(1, sizeof(struct cast_session));
  if (!cs)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory (cs)\n");
      free(os);
      return NULL;
    }

  os->session = cs;
  os->type = device->type;

  cs->output_session = os;
  cs->state = CAST_STATE_DISCONNECTED;
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
    {
      DPRINTF(E_LOG, L_CAST, "Could not connect to %s\n", device->name);
      goto out_deinit_gnutls;
    }

  ret = stream_url_make(cs->stream_url, sizeof(cs->stream_url), address, family);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Bug! Could find a network interface on same subnet as %s\n", device->name);
      goto out_close_connection;
    }

  cs->ev = event_new(evbase_player, cs->server_fd, EV_READ | EV_PERSIST, cast_listen_cb, cs);
  if (!cs->ev)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for listener event\n");
      goto out_close_connection;
    }

  cs->reply_timeout = evtimer_new(evbase_player, cast_reply_timeout_cb, cs);
  if (!cs->reply_timeout)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for reply_timeout\n");
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

  event_add(cs->ev, &heartbeat_timeout);

  cs->devname = strdup(device->name);
  cs->address = strdup(address);

  cs->volume = 0.01 * device->volume;

  cs->next = sessions;
  sessions = cs;

  proto = gnutls_protocol_get_name(gnutls_protocol_get_version(cs->tls_session));

  DPRINTF(E_INFO, L_CAST, "Connection to '%s' established using %s\n", cs->devname, proto);

  return cs;

 out_free_ev:
  event_free(cs->reply_timeout);
  event_free(cs->ev);
 out_close_connection:
  tcp_close(cs->server_fd);
 out_deinit_gnutls:
  gnutls_deinit(cs->tls_session);
 out_free_session:
  free(cs);

  return NULL;
}

// Attempts to "nicely" bring down a session to wanted_state, and then issues
// the callback. If wanted_state is CAST_STATE_NONE/FAILED then the session is purged.
static void
cast_session_shutdown(struct cast_session *cs, enum cast_state wanted_state)
{
  int pending;
  int ret;

  if (cs->state == wanted_state)
    {
      cast_status(cs);
      return;
    }
  else if (cs->state < wanted_state)
    {
      DPRINTF(E_LOG, L_CAST, "Bug! Shutdown request got wanted_state (%d) that is higher than current state (%d)\n", wanted_state, cs->state);
      return;
    }

  cs->wanted_state = wanted_state;

  pending = 0;
  switch (cs->state)
    {
      case CAST_STATE_MEDIA_LOADED ... CAST_STATE_MEDIA_PLAYING:
	ret = cast_msg_send(cs, MEDIA_STOP, cast_cb_stop_media);
	pending = 1;
	break;

      case CAST_STATE_MEDIA_CONNECTED:
	ret = cast_msg_send(cs, MEDIA_CLOSE, NULL);
	cs->state = CAST_STATE_MEDIA_LAUNCHED;
	if ((ret < 0) || (wanted_state >= CAST_STATE_MEDIA_LAUNCHED))
	  break;

	/* FALLTHROUGH */

      case CAST_STATE_MEDIA_LAUNCHED:
	ret = cast_msg_send(cs, STOP, cast_cb_stop);
	pending = 1;
	break;

      case CAST_STATE_CONNECTED:
	ret = cast_msg_send(cs, CLOSE, NULL);
	if (ret == 0)
	  gnutls_bye(cs->tls_session, GNUTLS_SHUT_RDWR);
	tcp_close(cs->server_fd);
	cs->server_fd = -1;
	cs->state = CAST_STATE_DISCONNECTED;
	break;

      case CAST_STATE_DISCONNECTED:
	ret = 0;
	break;

      default:
	DPRINTF(E_LOG, L_CAST, "Bug! Shutdown doesn't know how to handle current state\n");
	ret = -1;
    }

  // We couldn't talk to the device, tell the user and clean up
  if (ret < 0)
    {
      cs->state = CAST_STATE_FAILED;
      cast_status(cs);
      cast_session_cleanup(cs);
      return;
    }

  // If pending callbacks then we let them take care of the rest
  if (pending)
    return;

  // Asked to destroy the session
  if (wanted_state == CAST_STATE_NONE || wanted_state == CAST_STATE_FAILED)
    {
      cs->state = wanted_state;
      cast_status(cs);
      cast_session_cleanup(cs);
      return;
    }

  cast_status(cs);
}


/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
cast_device_start(struct output_device *device, output_status_cb cb, uint64_t rtptime)
{
  struct cast_session *cs;
  int ret;

  cs = cast_session_make(device, AF_INET6, cb);
  if (cs)
    {
      ret = cast_msg_send(cs, CONNECT, NULL);
      if (ret == 0)
	ret = cast_msg_send(cs, GET_STATUS, cast_cb_startup_connect);

      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv6 (start)\n");
	  cast_session_cleanup(cs);
	}
      else
	return 0;
    }

  cs = cast_session_make(device, AF_INET, cb);
  if (!cs)
    return -1;

  ret = cast_msg_send(cs, CONNECT, NULL);
  if (ret == 0)
    ret = cast_msg_send(cs, GET_STATUS, cast_cb_startup_connect);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv4 (start)\n");
      cast_session_cleanup(cs);
      return -1;
    }

  return 0;
}

static void
cast_device_stop(struct output_session *session)
{
  struct cast_session *cs = session->session;

  cast_session_shutdown(cs, CAST_STATE_NONE);
}

static int
cast_device_probe(struct output_device *device, output_status_cb cb)
{
  struct cast_session *cs;
  int ret;

  cs = cast_session_make(device, AF_INET6, cb);
  if (cs)
    {
      ret = cast_msg_send(cs, CONNECT, NULL);
      if (ret == 0)
	ret = cast_msg_send(cs, GET_STATUS, cast_cb_probe);

      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv6 (start)\n");
	  cast_session_cleanup(cs);
	}
      else
	return 0;
    }

  cs = cast_session_make(device, AF_INET, cb);
  if (!cs)
    return -1;

  ret = cast_msg_send(cs, CONNECT, NULL);
  if (ret == 0)
    ret = cast_msg_send(cs, GET_STATUS, cast_cb_probe);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv4 (start)\n");
      cast_session_cleanup(cs);
      return -1;
    }

  return 0;
}

static int
cast_volume_set(struct output_device *device, output_status_cb cb)
{
  struct cast_session *cs;
  int ret;

  if (!device->session || !device->session->session)
    return 0;

  cs = device->session->session;

  if (!(cs->state & CAST_STATE_F_MEDIA_CONNECTED))
    return 0;

  cs->volume = 0.01 * device->volume;

  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_volume);
  if (ret < 0)
    {
      cast_session_shutdown(cs, CAST_STATE_FAILED);
      return 0;
    }

  // Setting it here means it will not be used for the above cast_session_shutdown
  cs->status_cb = cb;

  return 1;
}

static void
cast_playback_start(uint64_t next_pkt, struct timespec *ts)
{
  struct cast_session *cs;

  if (evtimer_pending(flush_timer, NULL))
    event_del(flush_timer);

  // TODO Maybe we could avoid reloading and instead support play->pause->play
  for (cs = sessions; cs; cs = cs->next)
    {
      if (cs->state & CAST_STATE_F_MEDIA_CONNECTED)
	cast_msg_send(cs, MEDIA_LOAD, cast_cb_load);
    }
}

static void
cast_playback_stop(void)
{
  struct cast_session *cs;
  struct cast_session *next;

  for (cs = sessions; cs; cs = next)
    {
      next = cs->next;
      if (cs->state & CAST_STATE_F_MEDIA_CONNECTED)
	cast_session_shutdown(cs, CAST_STATE_NONE);
    }
}

static void
cast_flush_timer_cb(int fd, short what, void *arg)
{
  DPRINTF(E_DBG, L_CAST, "Flush timer expired; tearing down all sessions\n");

  cast_playback_stop();
}

static int
cast_flush(output_status_cb cb, uint64_t rtptime)
{
  struct cast_session *cs;
  struct cast_session *next;
  int pending;
  int ret;

  pending = 0;
  for (cs = sessions; cs; cs = next)
    {
      next = cs->next;

      if (!(cs->state & CAST_STATE_F_MEDIA_PLAYING))
	continue;

      ret = cast_msg_send(cs, MEDIA_PAUSE, cast_cb_flush);
      if (ret < 0)
	{
	  cast_session_shutdown(cs, CAST_STATE_FAILED);
	  continue;
	}

      cs->status_cb = cb;
      pending++;
    }

  if (pending > 0)
    evtimer_add(flush_timer, &flush_timeout);

  return pending;
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
  int family;
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

  // Setting the cert file seems not to be required
  if ( ((ret = gnutls_global_init()) != GNUTLS_E_SUCCESS)
       || ((ret = gnutls_certificate_allocate_credentials(&tls_credentials)) != GNUTLS_E_SUCCESS)
//     || ((ret = gnutls_certificate_set_x509_trust_file(tls_credentials, CAFILE, GNUTLS_X509_FMT_PEM)) < 0)
     )
    {
      DPRINTF(E_LOG, L_CAST, "Could not initialize GNUTLS: %s\n", gnutls_strerror(ret));
      return -1;
    }

  flush_timer = evtimer_new(evbase_player, cast_flush_timer_cb, NULL);
  if (!flush_timer)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for flush timer\n");
      goto out_tls_deinit;
    }

  if (cfg_getbool(cfg_getsec(cfg, "general"), "ipv6"))
    family = AF_UNSPEC;
  else
    family = AF_INET;

  ret = mdns_browse("_googlecast._tcp", family, cast_device_cb, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not add mDNS browser for Chromecast devices\n");
      goto out_free_flush_timer;
    }

  return 0;

 out_free_flush_timer:
  event_free(flush_timer);
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

  event_free(flush_timer);

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
  .device_probe = cast_device_probe,
//  .device_free_extra is unset - nothing to free
  .device_volume_set = cast_volume_set,
  .playback_start = cast_playback_start,
  .playback_stop = cast_playback_stop,
//  .write is unset - we don't write, the Chromecast will read our mp3 stream
  .flush = cast_flush,
  .status_cb = cast_set_status_cb,
/* TODO metadata support
  .metadata_prepare = cast_metadata_prepare,
  .metadata_send = cast_metadata_send,
  .metadata_purge = cast_metadata_purge,
  .metadata_prune = cast_metadata_prune,
*/
};
