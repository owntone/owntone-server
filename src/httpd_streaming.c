/*
 * Copyright (C) 2023 Espen Jürgensen <espenjurgensen@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uninorm.h>
#include <unistd.h>
#include <errno.h>

#include <event2/event.h>
#include <event2/buffer.h>

#include "httpd_internal.h"
#include "outputs/streaming.h"
#include "logger.h"
#include "conffile.h"

struct streaming_session {
  struct httpd_request *hreq;

  int fd;
  struct event *readev;
  bool require_icy;
  size_t bytes_sent;
};

static struct media_quality streaming_default_quality = {
  .sample_rate = 44100,
  .bits_per_sample = 16,
  .channels = 2,
  .bit_rate = 128000,
};

/* As streaming quality goes up, we send more data to the remote client.  With a
 * smaller ICY_METAINT value we have to splice metadata more frequently - on
 * some devices with small input buffers, a higher quality stream and low
 * ICY_METAINT can lead to stuttering as observed on a Roku Soundbridge
 */
static unsigned short streaming_icy_metaint = 16384;


/* ----------------------------- Session helpers ---------------------------- */

static void
session_free(struct streaming_session *session)
{
  if (!session)
    return;

  if (session->readev)
    {
      streaming_session_deregister(session->fd);
      event_free(session->readev);
    }

  free(session);
}

static struct streaming_session *
session_new(struct httpd_request *hreq, bool require_icy)
{
  struct streaming_session *session;

  CHECK_NULL(L_STREAMING, session = calloc(1, sizeof(struct streaming_session)));

  session->hreq = hreq;
  session->require_icy = require_icy;

  return session;
}

static void
session_end(struct streaming_session *session)
{
  DPRINTF(E_INFO, L_STREAMING, "Stopping mp3 streaming to %s:%d\n", session->hreq->peer_address, (int)session->hreq->peer_port);

  // Valgrind says libevent doesn't free the request on disconnect (even though
  // it owns it - libevent bug?), so we do it with a reply end. This also makes
  // sure the hreq gets freed.
  httpd_send_reply_end(session->hreq);
  session_free(session);
}


/* ----------------------------- Event callbacks ---------------------------- */

static void
conn_close_cb(httpd_connection *conn, void *arg)
{
  struct streaming_session *session = arg;

  session_end(session);
}

static void
read_cb(evutil_socket_t fd, short event, void *arg)
{
  struct streaming_session *session = arg;
  struct httpd_request *hreq;
  int len;

  CHECK_NULL(L_STREAMING, hreq = session->hreq);

  len = evbuffer_read(hreq->out_body, fd, -1);
  if (len < 0 && errno != EAGAIN)
    {
      httpd_request_closecb_set(hreq, NULL, NULL);
      session_end(session);
      return;
    }

  httpd_send_reply_chunk(hreq, hreq->out_body, NULL, NULL);

  session->bytes_sent += len;
}


/* -------------------------- Module implementation ------------------------- */

static int
streaming_mp3_handler(struct httpd_request *hreq)
{
  struct streaming_session *session;
  struct event_base *evbase;
  const char *name = cfg_getstr(cfg_getsec(cfg, "library"), "name");
  const char *param;
  bool require_icy;
  char buf[9];

  param = httpd_header_find(hreq->in_headers, "Icy-MetaData");
  require_icy = (param && strcmp(param, "1") == 0);
  if (require_icy)
    {
      httpd_header_add(hreq->out_headers, "icy-name", name);
      snprintf(buf, sizeof(buf)-1, "%d", streaming_icy_metaint);
      httpd_header_add(hreq->out_headers, "icy-metaint", buf);
    }

  session = session_new(hreq, require_icy);
  if (!session)
    return -1;

  // Ask streaming output module for a fd to read mp3 from
  session->fd = streaming_session_register(STREAMING_FORMAT_MP3, streaming_default_quality);

  CHECK_NULL(L_STREAMING, evbase = httpd_request_evbase_get(hreq));
  CHECK_NULL(L_STREAMING, session->readev = event_new(evbase, session->fd, EV_READ | EV_PERSIST, read_cb, session));
  event_add(session->readev, NULL);

  httpd_request_closecb_set(hreq, conn_close_cb, session);

  httpd_header_add(hreq->out_headers, "Content-Type", "audio/mpeg");
  httpd_header_add(hreq->out_headers, "Server", PACKAGE_NAME "/" VERSION);
  httpd_header_add(hreq->out_headers, "Cache-Control", "no-cache");
  httpd_header_add(hreq->out_headers, "Pragma", "no-cache");
  httpd_header_add(hreq->out_headers, "Expires", "Mon, 31 Aug 2015 06:00:00 GMT");

  httpd_send_reply_start(hreq, HTTP_OK, "OK");

  return 0;
}

static struct httpd_uri_map streaming_handlers[] =
  {
    {
      .regexp = "^/stream.mp3$",
      .handler = streaming_mp3_handler
    },
    {
      .regexp = NULL,
      .handler = NULL
    }
  };

static void
streaming_request(struct httpd_request *hreq)
{
  int ret;

  if (!hreq->handler)
    {
      DPRINTF(E_LOG, L_STREAMING, "Unrecognized path in streaming request: '%s'\n", hreq->uri);

      httpd_send_error(hreq, HTTP_NOTFOUND, NULL);
      return;
    }

  ret = hreq->handler(hreq);
  if (ret < 0)
    httpd_send_error(hreq, HTTP_INTERNAL, NULL);
}

static int
streaming_init(void)
{
  int val;

  val = cfg_getint(cfg_getsec(cfg, "streaming"), "sample_rate");
  // Validate against the variations of libmp3lame's supported sample rates: 32000/44100/48000
  if (val % 11025 > 0 && val % 12000 > 0 && val % 8000 > 0)
    DPRINTF(E_LOG, L_STREAMING, "Unsupported streaming sample_rate=%d, defaulting\n", val);
  else
    streaming_default_quality.sample_rate = val;

  val = cfg_getint(cfg_getsec(cfg, "streaming"), "bit_rate");
  switch (val)
  {
    case  64:
    case  96:
    case 128:
    case 192:
    case 320:
      streaming_default_quality.bit_rate = val*1000;
      break;

    default:
      DPRINTF(E_LOG, L_STREAMING, "Unsuppported streaming bit_rate=%d, supports: 64/96/128/192/320, defaulting\n", val);
  }

  DPRINTF(E_INFO, L_STREAMING, "Streaming quality: %d/%d/%d @ %dkbps\n",
    streaming_default_quality.sample_rate, streaming_default_quality.bits_per_sample,
    streaming_default_quality.channels, streaming_default_quality.bit_rate/1000);

  val = cfg_getint(cfg_getsec(cfg, "streaming"), "icy_metaint");
  // Too low a value forces server to send more meta than data
  if (val >= 4096 && val <= 131072)
    streaming_icy_metaint = val;
  else
    DPRINTF(E_INFO, L_STREAMING, "Unsupported icy_metaint=%d, supported range: 4096..131072, defaulting to %d\n", val, streaming_icy_metaint);

  return 0;
}

struct httpd_module httpd_streaming =
{
  .name = "Streaming",
  .type = MODULE_STREAMING,
  .logdomain = L_STREAMING,
  .fullpaths = { "/stream.mp3", NULL },
  .handlers = streaming_handlers,
  .init = streaming_init,
  .request = streaming_request,
};
