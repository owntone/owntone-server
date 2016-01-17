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
#include <endian.h>
#include <gnutls/gnutls.h>

#include <event2/event.h>

#include "logger.h"
#include "cast_channel.pb-c.h"
#include "cast.h"

// Number of bytes to request from TLS connection
#define MAX_BUF 4096
// CA file location (not very portable...?)
#define CAFILE "/etc/ssl/certs/ca-certificates.crt"

// Namespaces
#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_MEDIA "urn:x-cast:com.google.cast.media"

struct cast_session
{
  struct event *ev;

  gnutls_session_t tls_session;

  char *devname;
  char *address;
  unsigned short port;

  int volume;

  int server_fd;

  /* Do not dereference - only passed to the status cb */
  struct raop_device *dev;
  raop_status_cb status_cb;

  struct cast_session *next;
};

enum cast_msg_type
{
  UNKNOWN,
  PING,
  PONG,
  CONNECT,
  CLOSE,
  GET_STATUS,
  LAUNCH,
  MEDIA_CONNECT,
  MEDIA_LOAD,
  MEDIA_GET_STATUS,
  MEDIA_PLAY,
  MEDIA_STOP,
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
tcp_close (int fd)
{
  /* no more receptions */
  shutdown(fd, SHUT_RDWR);
  close(fd);
}


enum cast_msg_type
cast_msg_unpack(const uint8_t *data, size_t len)
{
  enum cast_msg_type type;
  Extensions__CoreApi__CastChannel__CastMessage *reply;
//  char *val;

  reply = extensions__core_api__cast_channel__cast_message__unpack(NULL, len, data);

  DPRINTF(E_DBG, L_CAST, "RX %d %s %s %s %s\n", len, reply->source_id, reply->destination_id, reply->namespace_, reply->payload_utf8);

  type = UNKNOWN;

/*  val = copy_tag_value(reply->payload_utf8, "type");
  if (val && (strncmp(val, "PING", 4) == 0))
    type = PING;
  free(val);

  val = copy_tag_value(reply->payload_utf8, "sessionId");
  if (val)
    {
      if (session_id)
	free(session_id);
      session_id = val;
    }

  val = copy_tag_value(reply->payload_utf8, "transportId");
  if (val)
    {
      if (appid)
	free(appid);
      appid = val;
    }
*/
  extensions__core_api__cast_channel__cast_message__free_unpacked(reply, NULL);

  return type;
}

static void
cast_session_free(struct cast_session *cs)
{
  event_free(cs->ev);

  gnutls_bye(cs->tls_session, GNUTLS_SHUT_RDWR);

  close(cs->server_fd);

  gnutls_deinit(cs->tls_session);

  if (cs->address)
    free(cs->address);
  if (cs->devname)
    free(cs->devname);

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
  /* Session failed, let our user know */
  cs->status_cb(cs->dev, NULL, RAOP_FAILED);

  cast_session_cleanup(cs);
}

static void
cast_listen_cb(int fd, short what, void *arg)
{
  struct cast_session *cs;
  enum cast_msg_type type;
  uint8_t buffer[MAX_BUF + 1]; // Not sure about the +1, but is copied from gnutls examples
  uint32_t be;
  size_t len;
  int ret;

  cs = (struct cast_session *)arg;

  DPRINTF(E_DBG, L_CAST, "New data from %s\n", cs->devname);

  len = 0;
  while ( (ret = gnutls_record_recv(cs->tls_session, buffer, MAX_BUF)) > 0)
    {
      DPRINTF(E_DBG, L_CAST, "Received %d bytes\n", ret);

      if (ret == 4)
	{
	  memcpy(&be, buffer, 4);
	  len = be32toh(be);
	  DPRINTF(E_DBG, L_CAST, "Incoming %d bytes\n", len);
	}
      else if (len > 0)
	{
	  len = 0;

	  type = cast_msg_unpack(buffer, ret);
/*	  if (type == PING)
	    {
	      send_message(&session, PONG);
	      exec = 1;
	      n++;
	    }
*/
	}
      else
	DPRINTF(E_WARN, L_CAST, "Unknown reponse from %s\n", cs->devname);
    }

  cast_session_failure(cs);
}

static struct cast_session *
cast_session_make(struct raop_device *rd, int family, raop_status_cb cb)
{
  struct cast_session *cs;
  const char *proto;
  const char *err;
  char *address;
  unsigned short port;
  int ret;

  switch (family)
    {
      case AF_INET:
	/* We always have the v4 services, so no need to check */
	if (!rd->v4_address)
	  return NULL;

	address = rd->v4_address;
	port = rd->v4_port;
	break;

      case AF_INET6:
	if (!rd->v6_address)
	  return NULL;

	address = rd->v6_address;
	port = rd->v6_port;
	break;

      default:
	return NULL;
    }

  cs = calloc(1, sizeof(struct cast_session));
  if (!cs)
    {
      DPRINTF(E_LOG, L_CAST, "Out of memory for TLS session\n");
      return NULL;
    }

  cs->dev = rd;
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

  cs->ev = event_new(evbase_player, cs->server_fd, EV_READ, cast_listen_cb, cs);
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

  event_add(cs->ev, NULL);

  cs->devname = strdup(rd->name);
  cs->address = strdup(address);

  cs->volume = rd->volume;

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

int
cast_device_start(struct raop_device *rd, raop_status_cb cb)
{
  DPRINTF(E_LOG, L_CAST, "Got start request for %s\n", rd->name);

  struct cast_session *cs;
  int ret;

  cs = cast_session_make(rd, AF_INET6, cb);
  if (cs)
    {
      return 0;
/*      ret = raop_send_req_options(rs, raop_cb_startup_options);
      if (ret == 0)
	return 0;
      else
	{
	  DPRINTF(E_WARN, L_RAOP, "Could not send OPTIONS request on IPv6 (start)\n");

	  raop_session_cleanup(rs);
	}
*/
    }

  cs = cast_session_make(rd, AF_INET, cb);
  if (!cs)
    return -1;

/*  ret = raop_send_req_options(rs, raop_cb_startup_options);
  if (ret < 0)
    {
      DPRINTF(E_WARN, L_RAOP, "Could not send OPTIONS request on IPv4 (start)\n");

      raop_session_cleanup(rs);
      return -1;
    }
*/
  return 0;
}

int
cast_init(void)
{
  int ret;

  // TODO Setting the cert file may not be required
  if ( ((ret = gnutls_global_init()) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_certificate_allocate_credentials(&tls_credentials)) != GNUTLS_E_SUCCESS) ||
       ((ret = gnutls_certificate_set_x509_trust_file(tls_credentials, CAFILE, GNUTLS_X509_FMT_PEM)) < 0) )
    {
      DPRINTF(E_LOG, L_CAST, "Could not initialize GNUTLS: %s\n", gnutls_strerror(ret));
      return -1;
    }

  return 0;
}

void
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
