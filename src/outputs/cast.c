/*
 * Copyright (C) 2015-2019 Espen Jürgensen <espenjurgensen@gmail.com>
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
#include "transcode.h"
#include "logger.h"
#include "player.h"
#include "rtp_common.h"
#include "outputs.h"

#ifdef HAVE_PROTOBUF_OLD
#include "cast_channel.v0.pb-c.h"
#else
#include "cast_channel.pb-c.h"
#endif

// This implementation of Chromecast uses the same mirroring app that Chromium
// uses. This app supports RTP-streaming, which has the advantage of quick
// startup and similarity with Airplay. However, I have not found much
// documentation for it, so the reference is Chromium itself. Here's how to
// start a Chromium mirroring session with verbose logging:
//
// 1) chromium --user-data-dir=~/chromium --enable-logging --v=1 ~/Music/test.mp3
// 2) right-click, select Cast, select device
// 3) log will now be in ~/chromium/chrome_debug.log

// Number of bytes to request from TLS connection
#define MAX_BUF 4096
// CA file location (not very portable...?)
#define CAFILE "/etc/ssl/certs/ca-certificates.crt"

// Seconds without a heartbeat from the Chromecast before we close the session
//#define HEARTBEAT_TIMEOUT 30
// Seconds to wait for a reply before making the callback requested by caller
#define REPLY_TIMEOUT 5

// ID of the audio mirroring app used by Chrome (Google Home)
#define CAST_APP_ID "85CDB22F"
// Old mirroring app (Chromecast)
#define CAST_APP_ID_OLD "0F5096E8"

// Namespaces
#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_MEDIA "urn:x-cast:com.google.cast.media"
#define NS_WEBRTC "urn:x-cast:com.google.cast.webrtc"

#define USE_TRANSPORT_ID     (1 << 1)
#define USE_REQUEST_ID       (1 << 2)
#define USE_REQUEST_ID_ONLY  (1 << 3)

#define CALLBACK_REGISTER_SIZE 32

// Chromium will send OPUS encoded 10 ms packets (48kHz), about 120 bytes. We
// use a 20 ms packet, so 50 pkts/sec, because that's the default for ffmpeg.
// A 20 ms audio packet at 48000 kHz makes this number 48000 * (20 / 1000)
#define CAST_SAMPLES_PER_PACKET 960

#define CAST_QUALITY_SAMPLE_RATE_DEFAULT     48000
#define CAST_QUALITY_BITS_PER_SAMPLE_DEFAULT 16
#define CAST_QUALITY_CHANNELS_DEFAULT        2

// This makes the rtp session buffer 4 seconds of audio (4 sec * 50 pkts/sec),
// which can be used for delayed transmission (and retransmission)
#define CAST_PACKET_BUFFER_SIZE 200

// Max (absolute) value the user is allowed to set offset_ms in the config file
#define CAST_OFFSET_MAX 1000

// This is just my measurement of how much extra delay is required to start at
// the same time as Airplay. The value was found experimentally.
#define CAST_DEVICE_START_DELAY_MS 100

// See cast_packet_header_make()
#define CAST_HEADER_SIZE 11

/* Notes
 * OFFER/ANSWER <-webrtc
 * RTCP/RTP
 * XR custom receiver report
 * Control and data on same UDP connection
 * OPUS encoded
 */

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

static struct encode_ctx *cast_encode_ctx;
static struct evbuffer *cast_encoded_data;

typedef void (*cast_reply_cb)(struct cast_session *cs, struct cast_msg_payload *payload);

// Session is starting up
#define CAST_STATE_F_STARTUP         (1 << 13)
// The receiver app is ready
#define CAST_STATE_F_MEDIA_CONNECTED (1 << 14)
// Media is playing in the receiver app
#define CAST_STATE_F_MEDIA_STREAMING (1 << 15)

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
  // Receiver app has been launched
  CAST_STATE_MEDIA_LAUNCHED  = CAST_STATE_F_STARTUP | 0x03,
  // CONNECT, GET_STATUS and OFFER made to receiver app
  CAST_STATE_MEDIA_CONNECTED = CAST_STATE_F_MEDIA_CONNECTED,
  // Buffering packets (playback not started yet)
  CAST_STATE_MEDIA_BUFFERING = CAST_STATE_F_MEDIA_CONNECTED | 0x01,
  // Streaming (playback started)
  CAST_STATE_MEDIA_STREAMING = CAST_STATE_F_MEDIA_CONNECTED | CAST_STATE_F_MEDIA_STREAMING,
};

struct cast_master_session
{
  struct evbuffer *evbuf;
  int evbuf_samples;

  struct rtp_session *rtp_session;

  struct media_quality quality;

  uint8_t *rawbuf;
  size_t rawbuf_size;
  int samples_per_packet;
};

struct cast_session
{
  uint64_t device_id;
  int callback_id;

  struct cast_master_session *master_session;

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
  int family;
  unsigned short port;

  // ChromeCast uses a float between 0 - 1
  float volume;

  uint32_t ssrc_id;

  // For initial buffering (delay playback to achieve some sort of sync).
  struct timespec start_pts;
  struct timespec offset_ts;
  uint16_t seqnum_next;

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

  // Session info from the Chromecast
  char *transport_id;
  char *session_id;
  int media_session_id;

  int udp_fd;
  unsigned short udp_port;

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
  LAUNCH_OLD,
  LAUNCH_ERROR,
  STOP,
  MEDIA_CONNECT,
  MEDIA_CLOSE,
  OFFER,
  ANSWER,
  MEDIA_GET_STATUS,
  MEDIA_STATUS,
  MEDIA_LOAD,
  MEDIA_PLAY,
  MEDIA_PAUSE,
  MEDIA_STOP,
  MEDIA_LOAD_FAILED,
  MEDIA_LOAD_CANCELLED,
  SET_VOLUME,
  PRESENTATION,
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
  const char *result;
  int media_session_id;
  unsigned short udp_port;
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
    .type = LAUNCH_OLD,
    .namespace = NS_RECEIVER,
    .payload = "{'type':'LAUNCH','requestId':%d,'appId':'" CAST_APP_ID_OLD "'}",
    .flags = USE_REQUEST_ID_ONLY,
  },
  {
    .type = LAUNCH_ERROR,
    .tag = "LAUNCH_ERROR",
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
    .type = OFFER,
    .namespace = NS_WEBRTC,
    // codecName can be aac or opus, ssrc should be random
    // We don't set 'aesKey' and 'aesIvMask'
    // sampleRate seems to be ignored
    // storeTime unknown meaning - perhaps size of buffer?
    // targetDelay - should be RTP delay in ms, but doesn't seem to change anything?
    .payload = "{'type':'OFFER','seqNum':%d,'offer':{'castMode':'mirroring','supportedStreams':[{'index':0,'type':'audio_source','codecName':'opus','rtpProfile':'cast','rtpPayloadType':127,'ssrc':%d,'storeTime':400,'targetDelay':400,'bitRate':128000,'sampleRate':48000,'timeBase':'1/48000','channels':2,'receiverRtcpEventLog':false}]}}",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = ANSWER,
    .tag = "ANSWER",
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
    .type = PRESENTATION,
    .namespace = NS_WEBRTC,
    .payload = "{'type':'PRESENTATION','sessionId':'%s',seqnum:%d,'title':'forked-daapd','icons':[{'url':'http://www.gyfgafguf.dk/images/fugl.jpg'}] }",
    .flags = USE_TRANSPORT_ID | USE_REQUEST_ID,
  },
  {
    .type = 0,
  },
};

/* From player.c */
extern struct event_base *evbase_player;

/* Globals */
static gnutls_certificate_credentials_t tls_credentials;
static struct cast_session *cast_sessions;
static struct cast_master_session *cast_master_session;
//static struct timeval heartbeat_timeout = { HEARTBEAT_TIMEOUT, 0 };
static struct timeval reply_timeout = { REPLY_TIMEOUT, 0 };
static struct media_quality cast_quality_default = { CAST_QUALITY_SAMPLE_RATE_DEFAULT, CAST_QUALITY_BITS_PER_SAMPLE_DEFAULT, CAST_QUALITY_CHANNELS_DEFAULT, 0 };


/* ------------------------------- MISC HELPERS ----------------------------- */

static int
cast_connect(const char *address, unsigned short port, int family, int type)
{
  union sockaddr_all sa;
  int fd;
  int len;
  int ret;

  DPRINTF(E_DBG, L_CAST, "Connecting to %s (family=%d), port %u\n", address, family, port);

  // TODO Open non-block right away so we don't block the player while connecting
  // and during TLS handshake (we would probably need to introduce a deferredev)
#ifdef SOCK_CLOEXEC
  fd = socket(family, type | SOCK_CLOEXEC, 0);
#else
  fd = socket(family, type, 0);
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
cast_disconnect(int fd)
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

/* ----------------------------- SESSION CLEANUP ---------------------------- */

static void
master_session_free(struct cast_master_session *cms)
{
  if (!cms)
    return;

  outputs_quality_unsubscribe(&cms->rtp_session->quality);
  rtp_session_free(cms->rtp_session);
  evbuffer_free(cms->evbuf);
  free(cms->rawbuf);
  free(cms);
}

static void
master_session_cleanup(struct cast_master_session *cms)
{
  struct cast_session *cs;

  // First check if any other session is using the master session
  for (cs = cast_sessions; cs; cs=cs->next)
    {
      if (cs->master_session == cms)
	return;
    }

  if (cms == cast_master_session)
    cast_master_session = NULL;

  master_session_free(cms);
}

static void
cast_session_free(struct cast_session *cs)
{
  if (!cs)
    return;

  master_session_cleanup(cs->master_session);

  event_free(cs->reply_timeout);
  event_free(cs->ev);

  if (cs->server_fd >= 0)
    cast_disconnect(cs->server_fd);
  if (cs->udp_fd >= 0)
    cast_disconnect(cs->udp_fd);

  gnutls_deinit(cs->tls_session);

  if (cs->address)
    free(cs->address);
  if (cs->devname)
    free(cs->devname);

  if (cs->session_id)
    free(cs->session_id);
  if (cs->transport_id)
    free(cs->transport_id);

  free(cs);
}

static void
cast_session_cleanup(struct cast_session *cs)
{
  struct cast_session *s;

  if (cs == cast_sessions)
    cast_sessions = cast_sessions->next;
  else
    {
      for (s = cast_sessions; s && (s->next != cs); s = s->next)
	; /* EMPTY */

      if (!s)
	DPRINTF(E_WARN, L_CAST, "WARNING: struct cast_session not found in list; BUG!\n");
      else
	s->next = cs->next;
    }

  outputs_device_session_remove(cs->device_id);

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
  else if (type == OFFER)
    snprintf(msg_buf, sizeof(msg_buf), cast_msg[type].payload, cs->request_id, cs->ssrc_id);
  else if (type == PRESENTATION)
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
  else if (json_object_object_get_ex(haystack, "seqNum", &needle))
    payload->request_id = json_object_get_int(needle);

  if (json_object_object_get_ex(haystack, "answer", &somehay) &&
      json_object_object_get_ex(somehay, "udpPort", &needle) &&
      json_object_get_type(needle) == json_type_int )
    payload->udp_port = json_object_get_int(needle);

  if (json_object_object_get_ex(haystack, "result", &needle) &&
      json_object_get_type(needle) == json_type_string )
    payload->result = json_object_get_string(needle);

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
      unknown_session_id = payload.session_id && (strcmp(payload.session_id, cs->session_id) != 0);
      if (unknown_session_id)
	{
	  DPRINTF(E_LOG, L_CAST, "Our session '%s' on '%s' was lost to session '%s'\n", cs->session_id, cs->devname, payload.session_id);

	  // Downgrade state, we don't have the receiver app any more
	  cs->state = CAST_STATE_CONNECTED;
	  cast_session_shutdown(cs, CAST_STATE_FAILED);
	  goto out_free_parsed;
	}
    }

  if (payload.type == CLOSE && (cs->state & CAST_STATE_F_MEDIA_CONNECTED))
    {
      // Downgrade state, we can't write any more
      cs->state = CAST_STATE_CONNECTED;
      cast_session_shutdown(cs, CAST_STATE_FAILED);
      goto out_free_parsed;
    }

  if (payload.type == MEDIA_STATUS && (cs->state & CAST_STATE_F_MEDIA_STREAMING))
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
      case CAST_STATE_MEDIA_CONNECTED ... CAST_STATE_MEDIA_BUFFERING:
	state = OUTPUT_STATE_CONNECTED;
	break;
      case CAST_STATE_MEDIA_STREAMING:
	state = OUTPUT_STATE_STREAMING;
	break;
      default:
	DPRINTF(E_LOG, L_CAST, "Bug! Unhandled state in cast_status()\n");
	state = OUTPUT_STATE_FAILED;
    }

  outputs_cb(cs->callback_id, cs->device_id, state);
  cs->callback_id = -1;
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
cast_cb_startup_offer(struct cast_session *cs, struct cast_msg_payload *payload)
{
  int ret;

  if (!payload)
    {
      DPRINTF(E_LOG, L_CAST, "No reply from '%s' to our OFFER request\n", cs->devname);
      goto error;
    }
  else if (payload->type != ANSWER)
    {
      DPRINTF(E_LOG, L_CAST, "The device '%s' did not give us an ANSWER to our OFFER\n", cs->devname);
      goto error;
    }
  else if (!payload->udp_port || strcmp(payload->result, "ok") != 0)
    {
      DPRINTF(E_LOG, L_CAST, "Missing UDP port (or unexpected result '%s') in ANSWER - aborting\n", payload->result);
      goto error;
    }

  DPRINTF(E_INFO, L_CAST, "UDP port in ANSWER is %d\n", payload->udp_port);

  cs->udp_port = payload->udp_port;

  cs->udp_fd = cast_connect(cs->address, cs->udp_port, cs->family, SOCK_DGRAM);
  if (cs->udp_fd < 0)
    goto error;

  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_startup_volume);
  if (ret < 0)
    goto error;

  cs->state = CAST_STATE_MEDIA_CONNECTED;

  return;

 error:
  cast_session_shutdown(cs, CAST_STATE_FAILED);
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

  ret = cast_msg_send(cs, OFFER, cast_cb_startup_offer);
  if (ret < 0)
    goto error;

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

  if (payload->type == LAUNCH_ERROR && !cs->retry)
    {
      DPRINTF(E_WARN, L_CAST, "Device '%s' could not launch app id '%s', trying '%s' instead\n", cs->devname, CAST_APP_ID, CAST_APP_ID_OLD);
      cs->retry++;
      ret = cast_msg_send(cs, LAUNCH_OLD, cast_cb_startup_launch);
      if (ret < 0)
	goto error;

      return;
    }
  else if (payload->type == LAUNCH_ERROR)
    {
      DPRINTF(E_LOG, L_CAST, "Device '%s' could not launch app id '%s' nor '%s' - aborting\n", cs->devname, CAST_APP_ID, CAST_APP_ID_OLD);
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

static void
cast_cb_volume(struct cast_session *cs, struct cast_msg_payload *payload)
{
  cast_status(cs);
}

/*
static void
cast_cb_presentation(struct cast_session *cs, struct cast_msg_payload *payload)
{
  if (!payload)
    DPRINTF(E_LOG, L_CAST, "No reply to PRESENTATION request from '%s' - will continue\n", cs->devname);
  else if (payload->type != MEDIA_STATUS)
    DPRINTF(E_LOG, L_CAST, "Unexpected reply to PRESENTATION request from '%s' - will continue\n", cs->devname);
}
*/

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

  for (cs = cast_sessions; cs; cs = cs->next)
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
  cfg_t *chromecast;
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

  chromecast = cfg_gettsec(cfg, "chromecast", name);
  if (chromecast && cfg_getbool(chromecast, "exclude"))
    {
      DPRINTF(E_LOG, L_CAST, "Excluding Chromecast device '%s' as set in config\n", name);
      return;
    }

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


/* --------------------------------- METADATA ------------------------------- */

/*
static void
metadata_send(struct cast_session *cs)
{
  cast_msg_send(cs, PRESENTATION, cast_cb_presentation);
}
*/

/* --------------------- SESSION CONSTRUCTION AND SHUTDOWN ------------------ */

static struct cast_master_session *
master_session_make(struct media_quality *quality)
{
  struct cast_master_session *cms;
  int ret;

  // First check if we already have a master session, then just use that
  if (cast_master_session)
    return cast_master_session;

  // Let's create a master session
  ret = outputs_quality_subscribe(quality);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not subscribe to required audio quality (%d/%d/%d)\n", quality->sample_rate, quality->bits_per_sample, quality->channels);
      return NULL;
    }

  CHECK_NULL(L_CAST, cms = calloc(1, sizeof(struct cast_master_session)));

  cms->rtp_session = rtp_session_new(quality, CAST_PACKET_BUFFER_SIZE, 0);
  if (!cms->rtp_session)
    {
      outputs_quality_unsubscribe(quality);
      free(cms);
      return NULL;
    }

  cms->quality = *quality;
  cms->samples_per_packet = CAST_SAMPLES_PER_PACKET;
  cms->rawbuf_size = STOB(cms->samples_per_packet, quality->bits_per_sample, quality->channels);

  CHECK_NULL(L_CAST, cms->rawbuf = malloc(cms->rawbuf_size));
  CHECK_NULL(L_CAST, cms->evbuf = evbuffer_new());

  cast_master_session = cms;

  return cms;
}

static struct cast_session *
cast_session_make(struct output_device *device, int family, int callback_id)
{
  struct cast_session *cs;
  cfg_t *chromecast;
  const char *proto;
  const char *err;
  char *address;
  unsigned short port;
  uint64_t offset_ms;
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

  CHECK_NULL(L_CAST, cs = calloc(1, sizeof(struct cast_session)));

  cs->state = CAST_STATE_DISCONNECTED;
  cs->device_id = device->id;
  cs->callback_id = callback_id;

  cs->master_session = master_session_make(&cast_quality_default);
  if (!cs->master_session)
    {
      DPRINTF(E_LOG, L_CAST, "Could not attach a master session for device '%s'\n", device->name);
      goto out_free_session;
    }

  cs->ssrc_id = cs->master_session->rtp_session->ssrc_id;

  /* Init TLS session, use default priorities and put the x509 credentials to the current session */
  if ( ((ret = gnutls_init(&cs->tls_session, GNUTLS_CLIENT)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_priority_set_direct(cs->tls_session, "PERFORMANCE", &err)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_credentials_set(cs->tls_session, GNUTLS_CRD_CERTIFICATE, tls_credentials)) != GNUTLS_E_SUCCESS) )
    {
      DPRINTF(E_LOG, L_CAST, "Could not initialize GNUTLS session: %s\n", gnutls_strerror(ret));
      goto out_free_master_session;
    }

  cs->server_fd = cast_connect(address, port, family, SOCK_STREAM);
  if (cs->server_fd < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not connect to %s\n", device->name);
      goto out_deinit_gnutls;
    }

  chromecast = cfg_gettsec(cfg, "chromecast", device->name);

  offset_ms = chromecast ? cfg_getint(chromecast, "offset_ms") : 0;
  if (abs(offset_ms) > CAST_OFFSET_MAX)
    {
      DPRINTF(E_LOG, L_CAST, "Ignoring invalid configuration of Chromecast offset (%" PRIu64 " ms)\n", offset_ms);
      offset_ms = 0;
    }

  offset_ms += OUTPUTS_BUFFER_DURATION * 1000 + CAST_DEVICE_START_DELAY_MS;

  cs->offset_ts.tv_sec  = (offset_ms / 1000);
  cs->offset_ts.tv_nsec = (offset_ms % 1000) * 1000000UL;

  DPRINTF(E_DBG, L_CAST, "Offset is set to %lu:%lu\n", cs->offset_ts.tv_sec, cs->offset_ts.tv_nsec);

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

  gnutls_transport_set_int(cs->tls_session, cs->server_fd);
  ret = gnutls_handshake(cs->tls_session);
  if (ret != GNUTLS_E_SUCCESS)
    {
      DPRINTF(E_LOG, L_CAST, "Could not attach TLS to TCP connection: %s\n", gnutls_strerror(ret));
      goto out_free_ev;
    }

  flags = fcntl(cs->server_fd, F_GETFL, 0);
  fcntl(cs->server_fd, F_SETFL, flags | O_NONBLOCK);

  event_add(cs->ev, NULL); // &heartbeat_timeout

  cs->devname = strdup(device->name);
  cs->address = strdup(address);
  cs->family = family;

  cs->udp_fd = -1;

  cs->volume = 0.01 * device->volume;

  cs->next = cast_sessions;
  cast_sessions = cs;

  // cs is now the official device session
  outputs_device_session_add(device->id, cs);

  proto = gnutls_protocol_get_name(gnutls_protocol_get_version(cs->tls_session));

  DPRINTF(E_INFO, L_CAST, "Connection to '%s' established using %s\n", cs->devname, proto);

  return cs;

 out_free_ev:
  event_free(cs->reply_timeout);
  event_free(cs->ev);
 out_close_connection:
  cast_disconnect(cs->server_fd);
 out_deinit_gnutls:
  gnutls_deinit(cs->tls_session);
 out_free_master_session:
  master_session_cleanup(cs->master_session);
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
      case CAST_STATE_MEDIA_STREAMING:
	ret = cast_msg_send(cs, MEDIA_STOP, cast_cb_stop_media);
	pending = 1;
	break;

      case CAST_STATE_MEDIA_BUFFERING:
      case CAST_STATE_MEDIA_CONNECTED:
	cast_disconnect(cs->udp_fd);
	cs->udp_fd = -1;
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
	cast_disconnect(cs->server_fd);
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


/* ------------------ PREPARING AND SENDING CAST RTP PACKETS ---------------- */

// Makes a Cast RTP packet (source: Chromium's media/cast/net/rtp/rtp_packetizer.cc)
//
// A Cast RTP packet is made of:
//   RTP header (12 bytes)
//   Cast header (7 bytes)
//   Extension data (4 bytes)
//   Packet data
//
// The Cast header + extension (optional?) consists of:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |k|r|   n_ext   |   frame_id    |          packet id            |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |         max_packet_id         | ref_frame_id  |   ext_type    |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |   ext_size    |      new_playout_delay_ms     |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// k: Is the frame a key frame?
// r: Is there a reference frame id?
// n_ext: Number of Cast extensions (Chromium uses 1: Adaptive Latency)
// ext_type: 0x04 Adaptive Latency extension
// ext_size: 0x02 -> 2 bytes
// new_playout_delay_ms: ??

// OPUS encodes the rawbuf payload
static int
payload_encode(struct evbuffer *evbuf, uint8_t *rawbuf, size_t rawbuf_size, int nsamples, struct media_quality *quality)
{
  transcode_frame *frame;
  int len;

  frame = transcode_frame_new(rawbuf, rawbuf_size, nsamples, quality);
  if (!frame)
    {
      DPRINTF(E_LOG, L_CAST, "Could not convert raw PCM to frame (bufsize=%zu)\n", rawbuf_size);
      return -1;
    }

  len = transcode_encode(evbuf, cast_encode_ctx, frame, 0);
  transcode_frame_free(frame);
  if (len < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not Opus encode frame\n");
      return -1;
    }

  return len;
}

static int
packet_prepare(struct rtp_packet *pkt, struct evbuffer *evbuf)
{

  // Cast header
  memset(pkt->payload, 0, CAST_HEADER_SIZE);
  pkt->payload[0] = 0xc1; // k = 1, r = 1 and one extension
  pkt->payload[1] = (char)pkt->seqnum;
  // packet_id and max_packet_id don't seem to be used, so leave them at 0
  pkt->payload[6] = (char)pkt->seqnum;
  pkt->payload[7] = 0x04; // kCastRtpExtensionAdaptiveLatency has id (1 << 2)
  pkt->payload[8] = 0x02; // Extension will use two bytes
  // leave extension values at 0, but Chromium sets them to:
  //   (frame.new_playout_delay_ms >> 8) and frame.new_playout_delay_ms (normal byte values are 0x03 0x20)

  // Copy payload
  return evbuffer_remove(evbuf, pkt->payload + CAST_HEADER_SIZE, pkt->payload_len - CAST_HEADER_SIZE);
}

static int
packet_send(struct cast_session *cs, struct rtp_packet *pkt)
{
  int ret;

  ret = send(cs->udp_fd, pkt->data, pkt->data_len, 0);
  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Send error for '%s': %s\n", cs->devname, strerror(errno));
      return -1;
    }
  else if (ret != pkt->data_len)
    {
      DPRINTF(E_WARN, L_CAST, "Partial send (%d) for '%s'\n", ret, cs->devname);
      return 0;
    }

/*  DPRINTF(E_DBG, L_CAST, "RTP PACKET seqnum %u, rtptime %u, payload 0x%x, pktbuf_s %zu\n",
    cs->master_session->rtp_session->seqnum,
    cs->master_session->rtp_session->pos,
    pkt->header[1],
    cs->master_session->rtp_session->pktbuf_len
    );
*/
  return 0;
}

static inline int
packets_send(struct cast_session *cs, struct rtp_session *rtp_session)
{
  struct rtp_packet *pkt;
  int ret;

  // Note that the loop must work even though seqnum wraps around, so we use !=, not <
  for (; cs->seqnum_next != rtp_session->seqnum; cs->seqnum_next++)
    {
      pkt = rtp_packet_get(rtp_session, cs->seqnum_next);
      if (!pkt)
	{
	  DPRINTF(E_WARN, L_CAST, "Packet to '%s' is missing in our buffer\n", cs->devname);
	  return 0; // Don't fail session over a missing packet (or should we?)
	}

      ret = packet_send(cs, pkt);
      if (ret < 0)
	return -1;
    }

  return 0;
}

static int
packet_make(struct cast_master_session *cms)
{
  struct rtp_packet *pkt;
  int len;
  int ret;

  // Encode payload into cast_encoded_data
  len = payload_encode(cast_encoded_data, cms->rawbuf, cms->rawbuf_size, cms->samples_per_packet, &cms->quality);
  if (len < 0)
    return -1;

  // Chromium uses a RTP payload type that is 0xff
  pkt = rtp_packet_next(cms->rtp_session, CAST_HEADER_SIZE + len, cms->samples_per_packet, 0xff);

  // Creates Cast header + adds payload
  ret = packet_prepare(pkt, cast_encoded_data);
  if (ret < 0)
    return -1;

  // Commits packet to retransmit buffer, and prepares the session for the next packet
  rtp_packet_commit(cms->rtp_session, pkt);

  return 0;
}

static inline int
packets_make(struct cast_master_session *cms, struct output_data *odata)
{
  int ret;
  int npkts;

  // TODO avoid this copy
  evbuffer_add(cms->evbuf, odata->buffer, odata->bufsize);
  cms->evbuf_samples += odata->samples;

  // Make as many packets as we have data for (one packet requires rawbuf_size bytes)
  npkts = 0;
  while (evbuffer_get_length(cms->evbuf) >= cms->rawbuf_size)
    {
      evbuffer_remove(cms->evbuf, cms->rawbuf, cms->rawbuf_size);
      cms->evbuf_samples -= cms->samples_per_packet;

      ret = packet_make(cms);
      if (ret == 0)
	npkts++;
    }

  return npkts;
}


/* TODO This does not currently work - need to investigate what sync the devices support
static void
packets_sync_send(struct cast_master_session *cms, struct timespec pts)
{
  struct rtp_packet *sync_pkt;
  struct cast_session *cs;
  struct rtcp_timestamp cur_stamp;
  struct timespec ts;
  bool is_sync_time;

  // Check if it is time send a sync packet to sessions that are already running
  is_sync_time = rtp_sync_is_time(cms->rtp_session);

  // (See raop.c for more comments on sync packets)
  cur_stamp.ts.tv_sec = pts.tv_sec;
  cur_stamp.ts.tv_nsec = pts.tv_nsec;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  cur_stamp.pos = cms->rtp_session->pos + cms->evbuf_samples - cms->output_buffer_samples;

  for (cs = cast_sessions; cs; cs = cs->next)
    {
      if (cs->master_session != cms)
	continue;

      // A device has joined and should get an init sync packet
      if (cs->state == CAST_STATE_MEDIA_CONNECTED)
	{
	  sync_pkt = rtp_sync_packet_next(cms->rtp_session, &cur_stamp, 0x80);
	  packet_send(cs, sync_pkt);

	  DPRINTF(E_DBG, L_CAST, "Start sync packet sent to '%s': cur_pos=%" PRIu32 ", cur_ts=%lu:%lu, now=%lu:%lu, rtptime=%" PRIu32 ",\n",
	    cs->devname, cur_stamp.pos, cur_stamp.ts.tv_sec, cur_stamp.ts.tv_nsec, ts.tv_sec, ts.tv_nsec, cms->rtp_session->pos);
	}
      else if (is_sync_time && cs->state == CAST_STATE_MEDIA_STREAMING)
	{
	  sync_pkt = rtp_sync_packet_next(cms->rtp_session, &cur_stamp, 0x80);
	  packet_send(cs, sync_pkt);
	}
    }
}
*/

/* ------------------ INTERFACE FUNCTIONS CALLED BY OUTPUTS.C --------------- */

static int
cast_device_start_generic(struct output_device *device, int callback_id, cast_reply_cb reply_cb)
{
  struct cast_session *cs;
  int ret;

  cs = cast_session_make(device, AF_INET6, callback_id);
  if (cs)
    {
      ret = cast_msg_send(cs, CONNECT, NULL);
      if (ret == 0)
	ret = cast_msg_send(cs, GET_STATUS, reply_cb);

      if (ret < 0)
	{
	  DPRINTF(E_WARN, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv6 (start)\n");
	  cast_session_cleanup(cs);
	}
      else
	return 1;
    }

  cs = cast_session_make(device, AF_INET, callback_id);
  if (!cs)
    return -1;

  ret = cast_msg_send(cs, CONNECT, NULL);
  if (ret == 0)
    ret = cast_msg_send(cs, GET_STATUS, reply_cb);

  if (ret < 0)
    {
      DPRINTF(E_LOG, L_CAST, "Could not send CONNECT or GET_STATUS request on IPv4 (start)\n");
      cast_session_cleanup(cs);
      return -1;
    }

  return 1;
}

static int
cast_device_start(struct output_device *device, int callback_id)
{
  return cast_device_start_generic(device, callback_id, cast_cb_startup_connect);
}

static int
cast_device_probe(struct output_device *device, int callback_id)
{
  return cast_device_start_generic(device, callback_id, cast_cb_probe);
}

static int
cast_device_stop(struct output_device *device, int callback_id)
{
  struct cast_session *cs = device->session;

  cs->callback_id = callback_id;

  cast_session_shutdown(cs, CAST_STATE_NONE);

  return 1;
}

static int
cast_device_flush(struct output_device *device, int callback_id)
{
  struct cast_session *cs = device->session;

  cs->callback_id = callback_id;
  cs->state = CAST_STATE_MEDIA_CONNECTED;
  cast_status(cs);

  return 1;
}

static void
cast_device_cb_set(struct output_device *device, int callback_id)
{
  struct cast_session *cs = device->session;

  cs->callback_id = callback_id;
}

static int
cast_device_volume_set(struct output_device *device, int callback_id)
{
  struct cast_session *cs = device->session;
  int ret;

  if (!cs || !(cs->state & CAST_STATE_F_MEDIA_CONNECTED))
    return 0;

  cs->volume = 0.01 * device->volume;

  ret = cast_msg_send(cs, SET_VOLUME, cast_cb_volume);
  if (ret < 0)
    {
      cast_session_shutdown(cs, CAST_STATE_FAILED);
      return 0;
    }

  // Setting it here means it will not be used for the above cast_session_shutdown
  cs->callback_id = callback_id;

  return 1;
}

static void
cast_write(struct output_buffer *obuf)
{
  struct cast_session *cs;
  struct cast_session *next;
  struct timespec ts;
  int i;
  int ret;

  if (!cast_sessions)
    return;

  for (i = 0; obuf->data[i].buffer; i++)
    {
      if (quality_is_equal(&obuf->data[i].quality, &cast_quality_default))
	break;
    }

  if (!obuf->data[i].buffer)
    {
      DPRINTF(E_LOG, L_CAST, "Bug! Output not delivering required data quality\n");
      return;
    }

  // Converts the raw audio in the output_buffer to Chromecast packets
  packets_make(cast_master_session, &obuf->data[i]);

  for (cs = cast_sessions; cs; cs = next)
    {
      next = cs->next;

      if (!(cs->state & CAST_STATE_F_MEDIA_CONNECTED))
	continue;

      if (cs->state == CAST_STATE_MEDIA_CONNECTED)
	{
	  // Sets that playback will start at time = start_pts with the packet that comes after seqnum_last
	  cs->start_pts = timespec_add(obuf->pts, cs->offset_ts);
	  cs->seqnum_next = cast_master_session->rtp_session->seqnum;
	  cs->state = CAST_STATE_MEDIA_BUFFERING;

	  clock_gettime(CLOCK_MONOTONIC, &ts);
	  DPRINTF(E_DBG, L_CAST, "Start time is %lu:%lu, current time is %lu:%lu\n", cs->start_pts.tv_sec, cs->start_pts.tv_nsec, ts.tv_sec, ts.tv_nsec);
	}

      if (cs->state == CAST_STATE_MEDIA_BUFFERING)
	{
	  clock_gettime(CLOCK_MONOTONIC, &ts);
	  if (timespec_cmp(cs->start_pts, ts) > 0)
	    continue; // Keep buffering
	  cs->state = CAST_STATE_MEDIA_STREAMING;
	}

      ret = packets_send(cs, cast_master_session->rtp_session);
      if (ret < 0)
        {
	  // Downgrade state immediately to avoid further write attempts (session shutdown is async)
	  cs->state = CAST_STATE_MEDIA_LAUNCHED;
	  cast_session_shutdown(cs, CAST_STATE_FAILED);
	}
    }
}

/* Doesn't work, but left here so it can be fixed
static void
cast_metadata_send(struct output_metadata *metadata)
{
  struct cast_session *cs;
  struct cast_session *next;

  for (cs = cast_sessions; cs; cs = next)
    {
      next = cs->next;

      if (cs->state != CAST_STATE_MEDIA_CONNECTED)
	continue;

      metadata_send(cs);
    }

  // TODO free the metadata
}
*/

static int
cast_init(void)
{
  struct decode_ctx *decode_ctx;
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

  decode_ctx = transcode_decode_setup_raw(XCODE_PCM16, &cast_quality_default);
  if (!decode_ctx)
    {
      DPRINTF(E_LOG, L_CAST, "Could not create decoding context\n");
      goto out_tls_deinit;
    }

  cast_encode_ctx = transcode_encode_setup(XCODE_OPUS, &cast_quality_default, decode_ctx, NULL, 0, 0);
  transcode_decode_cleanup(&decode_ctx);
  if (!cast_encode_ctx)
    {
      DPRINTF(E_LOG, L_CAST, "Will not be able to stream Chromecast, libav does not support Opus encoding\n");
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
      goto out_encode_ctx_free;
    }

  CHECK_NULL(L_CAST, cast_encoded_data = evbuffer_new());

  return 0;

 out_encode_ctx_free:
  transcode_encode_cleanup(&cast_encode_ctx);
 out_tls_deinit:
  gnutls_certificate_free_credentials(tls_credentials);
  gnutls_global_deinit();

  return -1;
}

static void
cast_deinit(void)
{
  struct cast_session *cs;

  for (cs = cast_sessions; cast_sessions; cs = cast_sessions)
    {
      cast_sessions = cs->next;
      cast_session_free(cs);
    }

  evbuffer_free(cast_encoded_data);
  transcode_encode_cleanup(&cast_encode_ctx);

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
  .device_probe = cast_device_probe,
  .device_stop = cast_device_stop,
  .device_flush = cast_device_flush,
  .device_cb_set = cast_device_cb_set,
  .device_volume_set = cast_device_volume_set,
  .write = cast_write,
//  .metadata_prepare = cast_metadata_prepare,
//  .metadata_send = cast_metadata_send,
//  .metadata_purge = cast_metadata_purge,
};
