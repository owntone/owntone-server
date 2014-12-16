/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
 * Based on evhttp from libevent 1.4.x
 *
 * Copyright (c) 2002-2006 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <event.h>

#ifdef _EVENT_HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef _EVENT_HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef _EVENT_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef _EVENT_HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif

#ifndef WIN32
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include <sys/queue.h>

#ifndef WIN32
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif

#ifdef WIN32
#include <winsock2.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <syslog.h>
#endif
#include <signal.h>
#include <time.h>
#ifdef _EVENT_HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef _EVENT_HAVE_FCNTL_H
#include <fcntl.h>
#endif

#undef timeout_pending
#undef timeout_initialized

#include "evrtsp.h"
/* #define USE_DEBUG */
#include "log.h"
#include "rtsp-internal.h"

#ifdef WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strdup _strdup
#endif

#ifndef _EVENT_HAVE_GETNAMEINFO
#define NI_MAXSERV 32
#define NI_MAXHOST 1025

#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2

static int
fake_getnameinfo(const struct sockaddr *sa, size_t salen, char *host, 
	size_t hostlen, char *serv, size_t servlen, int flags)
{
        struct sockaddr_in *sin = (struct sockaddr_in *)sa;
        int ret;
        
        if (serv != NULL) {
				char tmpserv[16];
				evutil_snprintf(tmpserv, sizeof(tmpserv),
					"%d", ntohs(sin->sin_port));
                ret = evutil_snprintf(serv, servlen, "%s", tmpserv);
                if ((ret < 0) || (ret >= servlen))
                        return (-1);
        }

        if (host != NULL) {
                if (flags & NI_NUMERICHOST) {
                        ret = evutil_snprintf(host, hostlen, "%s", inet_ntoa(sin->sin_addr));
                        if ((ret < 0) || (ret >= hostlen))
                                return (-1);
                        else
                                return (0);
                } else {
						struct hostent *hp;
                        hp = gethostbyaddr((char *)&sin->sin_addr, 
                            sizeof(struct in_addr), AF_INET);
                        if (hp == NULL)
                                return (-2);
                        
                        ret = evutil_snprintf(host, hostlen, "%s", hp->h_name);
                        if ((ret < 0) || (ret >= hostlen))
                                return (-1);
                        else
                                return (0);
                }
        }
        return (0);
}

#endif

#ifndef _EVENT_HAVE_GETADDRINFO
struct addrinfo {
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	size_t ai_addrlen;
	struct sockaddr *ai_addr;
	struct addrinfo *ai_next;
};
static int
fake_getaddrinfo(const char *hostname, struct addrinfo *ai)
{
	struct hostent *he = NULL;
	struct sockaddr_in *sa;
	if (hostname) {
		he = gethostbyname(hostname);
		if (!he)
			return (-1);
	}
	ai->ai_family = he ? he->h_addrtype : AF_INET;
	ai->ai_socktype = SOCK_STREAM;
	ai->ai_protocol = 0;
	ai->ai_addrlen = sizeof(struct sockaddr_in);
	if (NULL == (ai->ai_addr = malloc(ai->ai_addrlen)))
		return (-1);
	sa = (struct sockaddr_in*)ai->ai_addr;
	memset(sa, 0, ai->ai_addrlen);
	if (he) {
		sa->sin_family = he->h_addrtype;
		memcpy(&sa->sin_addr, he->h_addr_list[0], he->h_length);
	} else {
		sa->sin_family = AF_INET;
		sa->sin_addr.s_addr = INADDR_ANY;
	}
	ai->ai_next = NULL;
	return (0);
}
static void
fake_freeaddrinfo(struct addrinfo *ai)
{
	free(ai->ai_addr);
}
#endif

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

/* wrapper for setting the base from the rtsp server */
#define EVRTSP_BASE_SET(x, y) do { \
	if ((x)->base != NULL) event_base_set((x)->base, y);	\
} while (0) 

extern int debug;

static int socket_connect(int fd, const char *address, unsigned short port);
static int bind_socket_ai(int family, struct addrinfo *, int reuse);
static int bind_socket(int family, const char *, u_short, int reuse);
static void name_from_addr(struct sockaddr *, socklen_t, char **, char **);
static void evrtsp_connection_start_detectclose(
	struct evrtsp_connection *evcon);
static void evrtsp_connection_stop_detectclose(
	struct evrtsp_connection *evcon);
static void evrtsp_request_dispatch(struct evrtsp_connection* evcon);
static void evrtsp_read_firstline(struct evrtsp_connection *evcon,
				  struct evrtsp_request *req);
static void evrtsp_read_header(struct evrtsp_connection *evcon,
    struct evrtsp_request *req);
static int evrtsp_add_header_internal(struct evkeyvalq *headers,
    const char *key, const char *value);

void evrtsp_read(int, short, void *);
void evrtsp_write(int, short, void *);

#ifndef _EVENT_HAVE_STRSEP
/* strsep replacement for platforms that lack it.  Only works if
 * del is one character long. */
static char *
strsep(char **s, const char *del)
{
	char *d, *tok;
	assert(strlen(del) == 1);
	if (!s || !*s)
		return NULL;
	tok = *s;
	d = strstr(tok, del);
	if (d) {
		*d = '\0';
		*s = d + 1;
	} else
		*s = NULL;
	return tok;
}
#endif

const char *
evrtsp_method(enum evrtsp_cmd_type type)
{
	const char *method;

	switch (type) {
	case EVRTSP_REQ_ANNOUNCE:
	  method = "ANNOUNCE";
	  break;

	case EVRTSP_REQ_OPTIONS:
	  method = "OPTIONS";
	  break;

	case EVRTSP_REQ_SETUP:
	  method = "SETUP";
	  break;

	case EVRTSP_REQ_RECORD:
	  method = "RECORD";
	  break;

	case EVRTSP_REQ_PAUSE:
	  method = "PAUSE";
	  break;

	case EVRTSP_REQ_GET_PARAMETER:
	  method = "GET_PARAMETER";
	  break;

	case EVRTSP_REQ_SET_PARAMETER:
	  method = "SET_PARAMETER";
	  break;

	case EVRTSP_REQ_FLUSH:
	  method = "FLUSH";
	  break;

	case EVRTSP_REQ_TEARDOWN:
	  method = "TEARDOWN";
	  break;

	default:
	  method = NULL;
	  break;
	}

	return (method);
}

static void
evrtsp_add_event(struct event *ev, int timeout, int default_timeout)
{
	if (timeout != 0) {
		struct timeval tv;
		
		evutil_timerclear(&tv);
		tv.tv_sec = timeout != -1 ? timeout : default_timeout;
		event_add(ev, &tv);
	} else {
		event_add(ev, NULL);
	}
}

void
evrtsp_write_buffer(struct evrtsp_connection *evcon,
    void (*cb)(struct evrtsp_connection *, void *), void *arg)
{
	event_debug(("%s: preparing to write buffer\n", __func__));

	/* Set call back */
	evcon->cb = cb;
	evcon->cb_arg = arg;

	/* check if the event is already pending */
	if (event_pending(&evcon->ev, EV_WRITE|EV_TIMEOUT, NULL))
		event_del(&evcon->ev);

	event_assign(&evcon->ev, evcon->base, evcon->fd, EV_WRITE, evrtsp_write, evcon);
	evrtsp_add_event(&evcon->ev, evcon->timeout, RTSP_WRITE_TIMEOUT);
}

static int
evrtsp_connected(struct evrtsp_connection *evcon)
{
	switch (evcon->state) {
	case EVCON_DISCONNECTED:
	case EVCON_CONNECTING:
		return (0);
	case EVCON_IDLE:
	case EVCON_READING_FIRSTLINE:
	case EVCON_READING_HEADERS:
	case EVCON_READING_BODY:
	case EVCON_READING_TRAILER:
	case EVCON_WRITING:
	default:
		return (1);
	}
}

/*
 * Create the headers needed for an RTSP request
 */
static void
evrtsp_make_header_request(struct evrtsp_connection *evcon,
    struct evrtsp_request *req)
{
	const char *method;

	/* Generate request line */
	method = evrtsp_method(req->type);
	evbuffer_add_printf(evcon->output_buffer, "%s %s RTSP/%d.%d\r\n",
	    method, req->uri, req->major, req->minor);

	/* Content-Length is mandatory, absent means 0 */
	if ((evbuffer_get_length(req->output_buffer) > 0)
	    && (evrtsp_find_header(req->output_headers, "Content-Length") == NULL))
	  {
	    char size[12];
	    evutil_snprintf(size, sizeof(size), "%ld",
			    (long)evbuffer_get_length(req->output_buffer));
	    evrtsp_add_header(req->output_headers, "Content-Length", size);
	  }
}

void
evrtsp_make_header(struct evrtsp_connection *evcon, struct evrtsp_request *req)
{
	struct evkeyval *header;

	evrtsp_make_header_request(evcon, req);

	TAILQ_FOREACH(header, req->output_headers, next) {
		evbuffer_add_printf(evcon->output_buffer, "%s: %s\r\n",
		    header->key, header->value);
	}
	evbuffer_add(evcon->output_buffer, "\r\n", 2);

	if (evbuffer_get_length(req->output_buffer) > 0) {
		evbuffer_add_buffer(evcon->output_buffer, req->output_buffer);
	}
}

/* Separated host, port and file from URI */

int /* FIXME: needed? */
evrtsp_hostportfile(char *url, char **phost, u_short *pport, char **pfile)
{
	/* XXX not threadsafe. */
	static char host[1024];
	static char file[1024];
	char *p;
	const char *p2;
	int len;
	int ret;
	u_short port;

	len = strlen(RTSP_PREFIX);
	if (strncasecmp(url, RTSP_PREFIX, len))
		return (-1);

	url += len;

	/* We might overrun */
	ret = evutil_snprintf(host, sizeof(host), "%s", url);
	if ((ret < 0) || (ret >= sizeof(host)))
		return (-1);

	p = strchr(host, '/');
	if (p != NULL) {
		*p = '\0';
		p2 = p + 1;
	} else
		p2 = NULL;

	if (pfile != NULL) {
		/* Generate request file */
		if (p2 == NULL)
			p2 = "";
		evutil_snprintf(file, sizeof(file), "/%s", p2);
	}

	p = strchr(host, ':');
	if (p != NULL) {
		*p = '\0';
		port = atoi(p + 1);

		if (port == 0)
			return (-1);
	} else
	  return -1;

	if (phost != NULL)
		*phost = host;
	if (pport != NULL)
		*pport = port;
	if (pfile != NULL)
		*pfile = file;

	return (0);
}

void
evrtsp_connection_fail(struct evrtsp_connection *evcon,
    enum evrtsp_connection_error error)
{
	struct evrtsp_request* req = TAILQ_FIRST(&evcon->requests);
	void (*cb)(struct evrtsp_request *, void *);
	void *cb_arg;
	assert(req != NULL);

	/* save the callback for later; the cb might free our object */
	cb = req->cb;
	cb_arg = req->cb_arg;

	TAILQ_REMOVE(&evcon->requests, req, next);
	evrtsp_request_free(req);

	/* xxx: maybe we should fail all requests??? */

	/* reset the connection */
	evrtsp_connection_reset(evcon);
	
	/* We are trying the next request that was queued on us */
	if (TAILQ_FIRST(&evcon->requests) != NULL)
		evrtsp_connection_connect(evcon);

	/* inform the user */
	if (cb != NULL)
		(*cb)(NULL, cb_arg);
}

void
evrtsp_write(int fd, short what, void *arg)
{
	struct evrtsp_connection *evcon = arg;
	int n;

	if (what == EV_TIMEOUT) {
		evrtsp_connection_fail(evcon, EVCON_RTSP_TIMEOUT);
		return;
	}

	n = evbuffer_write(evcon->output_buffer, fd);
	if (n == -1) {
		event_debug(("%s: evbuffer_write", __func__));
		evrtsp_connection_fail(evcon, EVCON_RTSP_EOF);
		return;
	}

	if (n == 0) {
		event_debug(("%s: write nothing", __func__));
		evrtsp_connection_fail(evcon, EVCON_RTSP_EOF);
		return;
	}

	if (evbuffer_get_length(evcon->output_buffer) != 0) {
		evrtsp_add_event(&evcon->ev, 
		    evcon->timeout, RTSP_WRITE_TIMEOUT);
		return;
	}

	/* Activate our call back */
	if (evcon->cb != NULL)
		(*evcon->cb)(evcon, evcon->cb_arg);
}

/**
 * Advance the connection state.
 * - If this is an outgoing connection, we've just processed the response;
 *   idle or close the connection.
 */
static void
evrtsp_connection_done(struct evrtsp_connection *evcon)
{
	struct evrtsp_request *req = TAILQ_FIRST(&evcon->requests);

	/* idle or close the connection */
	TAILQ_REMOVE(&evcon->requests, req, next);
	req->evcon = NULL;

	evcon->state = EVCON_IDLE;

	if (TAILQ_FIRST(&evcon->requests) != NULL) {
	  /*
	   * We have more requests; reset the connection
	   * and deal with the next request.
	   */
	  if (!evrtsp_connected(evcon))
	    evrtsp_connection_connect(evcon);
	  else
	    evrtsp_request_dispatch(evcon);
	} else {
	  /*
	   * The connection is going to be persistent, but we
	   * need to detect if the other side closes it.
	   */
	  evrtsp_connection_start_detectclose(evcon);
	}

	/* notify the user of the request */
	(*req->cb)(req, req->cb_arg);

	evrtsp_request_free(req);
}

static void /* FIXME: needed? */
evrtsp_read_trailer(struct evrtsp_connection *evcon, struct evrtsp_request *req)
{
	struct evbuffer *buf = evcon->input_buffer;

	switch (evrtsp_parse_headers(req, buf)) {
	case DATA_CORRUPTED:
		evrtsp_connection_fail(evcon, EVCON_RTSP_INVALID_HEADER);
		break;
	case ALL_DATA_READ:
		event_del(&evcon->ev);
		evrtsp_connection_done(evcon);
		break;
	case MORE_DATA_EXPECTED:
	default:
		evrtsp_add_event(&evcon->ev, evcon->timeout,
		    RTSP_READ_TIMEOUT);
		break;
	}
}

static void
evrtsp_read_body(struct evrtsp_connection *evcon, struct evrtsp_request *req)
{
	struct evbuffer *buf = evcon->input_buffer;

	if (req->ntoread < 0) {
		/* Read until connection close. */
		evbuffer_add_buffer(req->input_buffer, buf);
	} else if (evbuffer_get_length(buf) >= req->ntoread) {
		/* Completed content length */
		evbuffer_add(req->input_buffer, evbuffer_pullup(buf,-1),
		    (size_t)req->ntoread);
		evbuffer_drain(buf, (size_t)req->ntoread);
		req->ntoread = 0;
		evrtsp_connection_done(evcon);
		return;
	}
	/* Read more! */
	event_assign(&evcon->ev, evcon->base, evcon->fd, EV_READ, evrtsp_read, evcon);
	evrtsp_add_event(&evcon->ev, evcon->timeout, RTSP_READ_TIMEOUT);
}

/*
 * Reads data into a buffer structure until no more data
 * can be read on the file descriptor or we have read all
 * the data that we wanted to read.
 * Execute callback when done.
 */

void
evrtsp_read(int fd, short what, void *arg)
{
	struct evrtsp_connection *evcon = arg;
	struct evrtsp_request *req = TAILQ_FIRST(&evcon->requests);
	struct evbuffer *buf = evcon->input_buffer;
	int n;

	if (what == EV_TIMEOUT) {
		evrtsp_connection_fail(evcon, EVCON_RTSP_TIMEOUT);
		return;
	}
	n = evbuffer_read(buf, fd, -1);
	event_debug(("%s: got %d on %d\n", __func__, n, fd));
	
	if (n == -1) {
		if (errno != EINTR && errno != EAGAIN) {
			event_debug(("%s: evbuffer_read", __func__));
			evrtsp_connection_fail(evcon, EVCON_RTSP_EOF);
		} else {
			evrtsp_add_event(&evcon->ev, evcon->timeout,
			    RTSP_READ_TIMEOUT);	       
		}
		return;
	} else if (n == 0) {
		/* Connection closed */
		evcon->state = EVCON_DISCONNECTED;
		evrtsp_connection_done(evcon);
		return;
	}

	switch (evcon->state) {
	case EVCON_READING_FIRSTLINE:
		evrtsp_read_firstline(evcon, req);
		break;
	case EVCON_READING_HEADERS:
		evrtsp_read_header(evcon, req);
		break;
	case EVCON_READING_BODY:
		evrtsp_read_body(evcon, req);
		break;
	case EVCON_READING_TRAILER:
		evrtsp_read_trailer(evcon, req);
		break;
	case EVCON_DISCONNECTED:
	case EVCON_CONNECTING:
	case EVCON_IDLE:
	case EVCON_WRITING:
	default:
		event_errx(1, "%s: illegal connection state %d",
			   __func__, evcon->state);
	}
}

static void
evrtsp_write_connectioncb(struct evrtsp_connection *evcon, void *arg)
{
	/* This is after writing the request to the server */
	struct evrtsp_request *req = TAILQ_FIRST(&evcon->requests);
	assert(req != NULL);

	assert(evcon->state == EVCON_WRITING);

	/* We are done writing our header and are now expecting the response */
	req->kind = EVRTSP_RESPONSE;

	evrtsp_start_read(evcon);
}



/*
 * Clean up a connection object
 */

void
evrtsp_connection_free(struct evrtsp_connection *evcon)
{
	struct evrtsp_request *req;

	/* notify interested parties that this connection is going down */
	if (evcon->fd != -1) {
		if (evrtsp_connected(evcon) && evcon->closecb != NULL)
			(*evcon->closecb)(evcon, evcon->closecb_arg);
	}

	/* remove all requests that might be queued on this connection */
	while ((req = TAILQ_FIRST(&evcon->requests)) != NULL) {
		TAILQ_REMOVE(&evcon->requests, req, next);
		evrtsp_request_free(req);
	}

	if (event_initialized(&evcon->close_ev))
		event_del(&evcon->close_ev);

	if (event_initialized(&evcon->ev))
		event_del(&evcon->ev);
	
	if (evcon->fd != -1)
		EVUTIL_CLOSESOCKET(evcon->fd);

	if (evcon->bind_address != NULL)
		free(evcon->bind_address);

	if (evcon->address != NULL)
		free(evcon->address);

	if (evcon->input_buffer != NULL)
		evbuffer_free(evcon->input_buffer);

	if (evcon->output_buffer != NULL)
		evbuffer_free(evcon->output_buffer);

	free(evcon);
}

static void
evrtsp_request_dispatch(struct evrtsp_connection* evcon)
{
	struct evrtsp_request *req = TAILQ_FIRST(&evcon->requests);

	/* this should not usually happy but it's possible */
	if (req == NULL)
		return;

	/* delete possible close detection events */
	evrtsp_connection_stop_detectclose(evcon);
	
	/* we assume that the connection is connected already */
	assert(evcon->state == EVCON_IDLE);

	evcon->state = EVCON_WRITING;

	/* Create the header from the store arguments */
	evrtsp_make_header(evcon, req);

	evrtsp_write_buffer(evcon, evrtsp_write_connectioncb, NULL);
}

/* Reset our connection state */
void
evrtsp_connection_reset(struct evrtsp_connection *evcon)
{
	if (event_initialized(&evcon->ev))
		event_del(&evcon->ev);

	if (evcon->fd != -1) {
		/* inform interested parties about connection close */
		if (evrtsp_connected(evcon) && evcon->closecb != NULL)
			(*evcon->closecb)(evcon, evcon->closecb_arg);

		EVUTIL_CLOSESOCKET(evcon->fd);
		evcon->fd = -1;
	}
	evcon->state = EVCON_DISCONNECTED;

	evbuffer_drain(evcon->input_buffer,
	    evbuffer_get_length(evcon->input_buffer));
	evbuffer_drain(evcon->output_buffer,
	    evbuffer_get_length(evcon->output_buffer));
}

static void
evrtsp_detect_close_cb(int fd, short what, void *arg)
{
	struct evrtsp_connection *evcon = arg;

	evrtsp_connection_reset(evcon);
}

static void
evrtsp_connection_start_detectclose(struct evrtsp_connection *evcon)
{
	evcon->flags |= EVRTSP_CON_CLOSEDETECT;

	if (event_initialized(&evcon->close_ev))
		event_del(&evcon->close_ev);
	event_assign(&evcon->close_ev, evcon->base, evcon->fd, EV_READ,
	    evrtsp_detect_close_cb, evcon);
	event_add(&evcon->close_ev, NULL);
}

static void
evrtsp_connection_stop_detectclose(struct evrtsp_connection *evcon)
{
	evcon->flags &= ~EVRTSP_CON_CLOSEDETECT;
	event_del(&evcon->close_ev);
}

/*
 * Call back for asynchronous connection attempt.
 */

static void
evrtsp_connectioncb(int fd, short what, void *arg)
{
  struct evrtsp_connection *evcon = arg;
  int error;
  socklen_t errsz = sizeof(error);

  if (what == EV_TIMEOUT) {
    event_debug(("%s: connection timeout for \"%s:%d\" on %d",
		 __func__, evcon->address, evcon->port, evcon->fd));
    goto cleanup;
  }

  /* Check if the connection completed */
  if (getsockopt(evcon->fd, SOL_SOCKET, SO_ERROR, (void*)&error,
		 &errsz) == -1) {
    event_debug(("%s: getsockopt for \"%s:%d\" on %d",
		 __func__, evcon->address, evcon->port, evcon->fd));
    goto cleanup;
  }

  if (error) {
    event_debug(("%s: connect failed for \"%s:%d\" on %d: %s",
		 __func__, evcon->address, evcon->port, evcon->fd,
		 strerror(error)));
    goto cleanup;
  }

  /* We are connected to the server now */
  event_debug(("%s: connected to \"%s:%d\" on %d\n",
	       __func__, evcon->address, evcon->port, evcon->fd));

  evcon->state = EVCON_IDLE;

  /* try to start requests that have queued up on this connection */
  evrtsp_request_dispatch(evcon);
  return;

 cleanup:
  evrtsp_connection_reset(evcon);

  /* for now, we just signal all requests by executing their callbacks */
  while (TAILQ_FIRST(&evcon->requests) != NULL) {
    struct evrtsp_request *request = TAILQ_FIRST(&evcon->requests);
    TAILQ_REMOVE(&evcon->requests, request, next);
    request->evcon = NULL;

    /* we might want to set an error here */
    request->cb(request, request->cb_arg);
    evrtsp_request_free(request);
  }
}

/*
 * Check if we got a valid response code.
 */

static int
evrtsp_valid_response_code(int code)
{
	if (code == 0)
		return (0);

	return (1);
}

/* Parses the status line of an RTSP server */

static int
evrtsp_parse_response_line(struct evrtsp_request *req, char *line)
{
	char *protocol;
	char *number;
	const char *readable = "";

	protocol = strsep(&line, " ");
	if (line == NULL)
		return (-1);
	number = strsep(&line, " ");
	if (line != NULL)
		readable = line;

	if (strcmp(protocol, "RTSP/1.0") == 0) {
		req->major = 1;
		req->minor = 0;
	} else if (strcmp(protocol, "RTSP/1.1") == 0) {
		req->major = 1;
		req->minor = 1;
	} else {
		event_debug(("%s: bad protocol \"%s\"",
			__func__, protocol));
		return (-1);
	}

	req->response_code = atoi(number);
	if (!evrtsp_valid_response_code(req->response_code)) {
		event_debug(("%s: bad response code \"%s\"",
			__func__, number));
		return (-1);
	}

	if ((req->response_code_line = strdup(readable)) == NULL)
		event_err(1, "%s: strdup", __func__);

	return (0);
}

const char *
evrtsp_find_header(const struct evkeyvalq *headers, const char *key)
{
	struct evkeyval *header;

	TAILQ_FOREACH(header, headers, next) {
		if (strcasecmp(header->key, key) == 0)
			return (header->value);
	}

	return (NULL);
}

void
evrtsp_clear_headers(struct evkeyvalq *headers)
{
	struct evkeyval *header;

	for (header = TAILQ_FIRST(headers);
	    header != NULL;
	    header = TAILQ_FIRST(headers)) {
		TAILQ_REMOVE(headers, header, next);
		free(header->key);
		free(header->value);
		free(header);
	}
}

/*
 * Returns 0,  if the header was successfully removed.
 * Returns -1, if the header could not be found.
 */

int
evrtsp_remove_header(struct evkeyvalq *headers, const char *key)
{
	struct evkeyval *header;

	TAILQ_FOREACH(header, headers, next) {
		if (strcasecmp(header->key, key) == 0)
			break;
	}

	if (header == NULL)
		return (-1);

	/* Free and remove the header that we found */
	TAILQ_REMOVE(headers, header, next);
	free(header->key);
	free(header->value);
	free(header);

	return (0);
}

static int
evrtsp_header_is_valid_value(const char *value)
{
	const char *p = value;

	while ((p = strpbrk(p, "\r\n")) != NULL) {
		/* we really expect only one new line */
		p += strspn(p, "\r\n");
		/* we expect a space or tab for continuation */
		if (*p != ' ' && *p != '\t')
			return (0);
	}
	return (1);
}

int
evrtsp_add_header(struct evkeyvalq *headers,
    const char *key, const char *value)
{
	event_debug(("%s: key: %s val: %s\n", __func__, key, value));

	if (strchr(key, '\r') != NULL || strchr(key, '\n') != NULL) {
		/* drop illegal headers */
		event_debug(("%s: dropping illegal header key\n", __func__));
		return (-1);
	}
	
	if (!evrtsp_header_is_valid_value(value)) {
		event_debug(("%s: dropping illegal header value\n", __func__));
		return (-1);
	}

	return (evrtsp_add_header_internal(headers, key, value));
}

static int
evrtsp_add_header_internal(struct evkeyvalq *headers,
    const char *key, const char *value)
{
	struct evkeyval *header = calloc(1, sizeof(struct evkeyval));

	if (header == NULL) {
		event_warn("%s: calloc", __func__);
		return (-1);
	}
	if ((header->key = strdup(key)) == NULL) {
		free(header);
		event_warn("%s: strdup", __func__);
		return (-1);
	}
	if ((header->value = strdup(value)) == NULL) {
		free(header->key);
		free(header);
		event_warn("%s: strdup", __func__);
		return (-1);
	}

	TAILQ_INSERT_TAIL(headers, header, next);

	return (0);
}

/*
 * Parses header lines from a request or a response into the specified
 * request object given an event buffer.
 *
 * Returns
 *   DATA_CORRUPTED      on error
 *   MORE_DATA_EXPECTED  when we need to read more headers
 *   ALL_DATA_READ       when all headers have been read.
 */

enum message_read_status
evrtsp_parse_firstline(struct evrtsp_request *req, struct evbuffer *buffer)
{
	char *line;
	enum message_read_status status = ALL_DATA_READ;

	line = evbuffer_readln(buffer, NULL, EVBUFFER_EOL_ANY);
	if (line == NULL)
		return (MORE_DATA_EXPECTED);

	switch (req->kind) {
	case EVRTSP_RESPONSE:
		if (evrtsp_parse_response_line(req, line) == -1)
			status = DATA_CORRUPTED;
		break;
	default:
		status = DATA_CORRUPTED;
	}

	free(line);
	return (status);
}

static int
evrtsp_append_to_last_header(struct evkeyvalq *headers, const char *line)
{
	struct evkeyval *header = TAILQ_LAST(headers, evkeyvalq);
	char *newval;
	size_t old_len, line_len;

	if (header == NULL)
		return (-1);

	old_len = strlen(header->value);
	line_len = strlen(line);

	newval = realloc(header->value, old_len + line_len + 1);
	if (newval == NULL)
		return (-1);

	memcpy(newval + old_len, line, line_len + 1);
	header->value = newval;

	return (0);
}

enum message_read_status
evrtsp_parse_headers(struct evrtsp_request *req, struct evbuffer *buffer)
{
	char *line;
	enum message_read_status status = MORE_DATA_EXPECTED;

	struct evkeyvalq *headers = req->input_headers;
	while ((line = evbuffer_readln(buffer, NULL, EVBUFFER_EOL_CRLF))
	       != NULL) {
		char *skey, *svalue;

		if (*line == '\0') { /* Last header - Done */
			status = ALL_DATA_READ;
			free(line);
			break;
		}

		/* Check if this is a continuation line */
		if (*line == ' ' || *line == '\t') {
			if (evrtsp_append_to_last_header(headers, line) == -1)
				goto error;
			free(line);
			continue;
		}

		/* Processing of header lines */
		svalue = line;
		skey = strsep(&svalue, ":");
		if (svalue == NULL)
			goto error;

		svalue += strspn(svalue, " ");

		if (evrtsp_add_header(headers, skey, svalue) == -1)
			goto error;

		free(line);
	}

	return (status);

 error:
	free(line);
	return (DATA_CORRUPTED);
}

static int
evrtsp_get_body_length(struct evrtsp_request *req)
{
	struct evkeyvalq *headers = req->input_headers;
	const char *content_length;

	content_length = evrtsp_find_header(headers, "Content-Length");

	if (content_length == NULL) {
	  /* If there is no Content-Length: header, a value of 0 is assumed, per spec. */
		req->ntoread = 0;
	} else {
		char *endp;
		ev_int64_t ntoread = evutil_strtoll(content_length, &endp, 10);
		if (*content_length == '\0' || *endp != '\0' || ntoread < 0) {
			event_debug(("%s: illegal content length: %s",
				__func__, content_length));
			return (-1);
		}
		req->ntoread = ntoread;
	}
		
	event_debug(("%s: bytes to read: %lld (in buffer %ld)\n",
		__func__, req->ntoread,
		evbuffer_get_length(req->evcon->input_buffer)));

	return (0);
}

static void
evrtsp_get_body(struct evrtsp_connection *evcon, struct evrtsp_request *req)
{
	evcon->state = EVCON_READING_BODY;
	if (evrtsp_get_body_length(req) == -1) {
	  evrtsp_connection_fail(evcon, EVCON_RTSP_INVALID_HEADER);
	  return;
	}

	evrtsp_read_body(evcon, req);
}

static void
evrtsp_read_firstline(struct evrtsp_connection *evcon,
		      struct evrtsp_request *req)
{
	enum message_read_status res;

	res = evrtsp_parse_firstline(req, evcon->input_buffer);
	if (res == DATA_CORRUPTED) {
		/* Error while reading, terminate */
		event_debug(("%s: bad header lines on %d\n",
			__func__, evcon->fd));
		evrtsp_connection_fail(evcon, EVCON_RTSP_INVALID_HEADER);
		return;
	} else if (res == MORE_DATA_EXPECTED) {
		/* Need more header lines */
		evrtsp_add_event(&evcon->ev, 
                    evcon->timeout, RTSP_READ_TIMEOUT);
		return;
	}

	evcon->state = EVCON_READING_HEADERS;
	evrtsp_read_header(evcon, req);
}

static void
evrtsp_read_header(struct evrtsp_connection *evcon, struct evrtsp_request *req)
{
	enum message_read_status res;
	int fd = evcon->fd;

	res = evrtsp_parse_headers(req, evcon->input_buffer);
	if (res == DATA_CORRUPTED) {
		/* Error while reading, terminate */
		event_debug(("%s: bad header lines on %d\n", __func__, fd));
		evrtsp_connection_fail(evcon, EVCON_RTSP_INVALID_HEADER);
		return;
	} else if (res == MORE_DATA_EXPECTED) {
		/* Need more header lines */
		evrtsp_add_event(&evcon->ev, 
		    evcon->timeout, RTSP_READ_TIMEOUT);
		return;
	}

	/* Done reading headers, do the real work */
	switch (req->kind) {
	case EVRTSP_RESPONSE:
	  event_debug(("%s: start of read body on %d\n",
		       __func__, fd));
	  evrtsp_get_body(evcon, req);
	  break;

	default:
	  event_warnx("%s: bad header on %d", __func__, fd);
	  evrtsp_connection_fail(evcon, EVCON_RTSP_INVALID_HEADER);
	  break;
	}
}

/*
 * Creates a TCP connection to the specified port and executes a callback
 * when finished.  Failure or sucess is indicate by the passed connection
 * object.
 *
 * Although this interface accepts a hostname, it is intended to take
 * only numeric hostnames so that non-blocking DNS resolution can
 * happen elsewhere.
 */

struct evrtsp_connection *
evrtsp_connection_new(const char *address, unsigned short port)
{
	struct evrtsp_connection *evcon = NULL;
	char *intf;
	char *addr;
	unsigned char scratch[16];
	int family;

	if ((addr = strdup(address)) == NULL) {
		event_warn("%s: strdup failed", __func__);
		goto error;
	}

	intf = strchr(addr, '%');
	if (intf)
	  *intf = '\0';

	if (inet_pton(AF_INET6, addr, scratch) == 1)
	  family = AF_INET6;
	else if (inet_pton(AF_INET, addr, scratch) == 1)
	  family = AF_INET;
	else {
	  free(addr);
	  event_warn("%s: address is neither IPv6 nor IPv4", __func__);
	  return NULL;
	}

	if (intf)
	  *intf = '%';

	event_debug(("Attempting connection to %s:%d\n", address, port));

	if ((evcon = calloc(1, sizeof(struct evrtsp_connection))) == NULL) {
		free(addr);
		event_warn("%s: calloc failed", __func__);
		goto error;
	}

	evcon->fd = -1;
	evcon->port = port;

	evcon->timeout = -1;

	evcon->cseq = 1;

	evcon->family = family;
	evcon->address = addr;

	if ((evcon->input_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new failed", __func__);
		goto error;
	}

	if ((evcon->output_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new failed", __func__);
		goto error;
	}
	
	evcon->state = EVCON_DISCONNECTED;
	TAILQ_INIT(&evcon->requests);

	return (evcon);
	
 error:
	if (evcon != NULL)
		evrtsp_connection_free(evcon);
	return (NULL);
}

void evrtsp_connection_set_base(struct evrtsp_connection *evcon,
    struct event_base *base)
{
	assert(evcon->base == NULL);
	assert(evcon->state == EVCON_DISCONNECTED);
	evcon->base = base;
}

void
evrtsp_connection_set_timeout(struct evrtsp_connection *evcon,
    int timeout_in_secs)
{
	evcon->timeout = timeout_in_secs;
}

void
evrtsp_connection_set_closecb(struct evrtsp_connection *evcon,
    void (*cb)(struct evrtsp_connection *, void *), void *cbarg)
{
	evcon->closecb = cb;
	evcon->closecb_arg = cbarg;
}

void
evrtsp_connection_get_local_address(struct evrtsp_connection *evcon,
    char **address, u_short *port)
{
  union {
    struct sockaddr_storage ss;
    struct sockaddr sa;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
  } addr;
  socklen_t slen;
  int ret;

  *address = NULL;
  *port = 0;

  if (!evrtsp_connected(evcon))
    return;

  slen = sizeof(struct sockaddr_storage);
  ret = getsockname(evcon->fd, &addr.sa, &slen);
  if (ret < 0)
    return;

  name_from_addr(&addr.sa, slen, address, NULL);

  if (!*address)
    return;

  switch (addr.ss.ss_family)
    {
      case AF_INET:
	*port = ntohs(addr.sin.sin_port);
	break;

#ifdef AF_INET6
      case AF_INET6:
	*port = ntohs(addr.sin6.sin6_port);
	break;
#endif

      default:
	free(*address);
	address = NULL;

	event_err(1, "%s: unhandled address family\n", __func__);
	return;
    }
}

void
evrtsp_connection_get_peer(struct evrtsp_connection *evcon,
    char **address, u_short *port)
{
	*address = evcon->address;
	*port = evcon->port;
}

int
evrtsp_connection_connect(struct evrtsp_connection *evcon)
{
	if (evcon->state == EVCON_CONNECTING)
		return (0);
	
	evrtsp_connection_reset(evcon);

	evcon->fd = bind_socket(evcon->family,
		evcon->bind_address, evcon->bind_port, 0 /*reuse*/);
	if (evcon->fd == -1) {
		event_debug(("%s: failed to bind to \"%s\"",
			__func__, evcon->bind_address));
		return (-1);
	}

	if (socket_connect(evcon->fd, evcon->address, evcon->port) == -1) {
		EVUTIL_CLOSESOCKET(evcon->fd); evcon->fd = -1;
		return (-1);
	}

	/* Set up a callback for successful connection setup */
	event_assign(&evcon->ev, evcon->base, evcon->fd, EV_WRITE, evrtsp_connectioncb, evcon);
	evrtsp_add_event(&evcon->ev, evcon->timeout, RTSP_CONNECT_TIMEOUT);

	evcon->state = EVCON_CONNECTING;
	
	return (0);
}

/*
 * Starts an RTSP request on the provided evrtsp_connection object.
 * If the connection object is not connected to the server already,
 * this will start the connection.
 */

int
evrtsp_make_request(struct evrtsp_connection *evcon,
    struct evrtsp_request *req,
    enum evrtsp_cmd_type type, const char *uri)
{
	/* We are making a request */
	req->kind = EVRTSP_REQUEST;
	req->type = type;
	if (req->uri != NULL)
		free(req->uri);
	if ((req->uri = strdup(uri)) == NULL)
		event_err(1, "%s: strdup", __func__);

	/* Set the protocol version if it is not supplied */
	if (!req->major && !req->minor) {
		req->major = 1;
		req->minor = 0;
	}
	
	assert(req->evcon == NULL);
	req->evcon = evcon;
	assert(!(req->flags & EVRTSP_REQ_OWN_CONNECTION));
	
	TAILQ_INSERT_TAIL(&evcon->requests, req, next);

	/* If the connection object is not connected; make it so */
	if (!evrtsp_connected(evcon))
		return (evrtsp_connection_connect(evcon));

	/*
	 * If it's connected already and we are the first in the queue,
	 * then we can dispatch this request immediately.  Otherwise, it
	 * will be dispatched once the pending requests are completed.
	 */
	if (TAILQ_FIRST(&evcon->requests) == req)
		evrtsp_request_dispatch(evcon);

	return (0);
}

/*
 * Reads data from file descriptor into request structure
 * Request structure needs to be set up correctly.
 */

void
evrtsp_start_read(struct evrtsp_connection *evcon)
{
	/* Set up an event to read the headers */
	if (event_initialized(&evcon->ev))
		event_del(&evcon->ev);
	event_assign(&evcon->ev, evcon->base, evcon->fd, EV_READ, evrtsp_read, evcon);
	evrtsp_add_event(&evcon->ev, evcon->timeout, RTSP_READ_TIMEOUT);
	evcon->state = EVCON_READING_FIRSTLINE;
}

static void
evrtsp_send_done(struct evrtsp_connection *evcon, void *arg)
{
	struct evrtsp_request *req = TAILQ_FIRST(&evcon->requests);
	TAILQ_REMOVE(&evcon->requests, req, next);

	/* delete possible close detection events */
	evrtsp_connection_stop_detectclose(evcon);
	
	assert(req->flags & EVRTSP_REQ_OWN_CONNECTION);
	evrtsp_request_free(req);
}

/* Requires that headers and response code are already set up */

static inline void
evrtsp_send(struct evrtsp_request *req, struct evbuffer *databuf)
{
	struct evrtsp_connection *evcon = req->evcon;

	if (evcon == NULL) {
		evrtsp_request_free(req);
		return;
	}

	assert(TAILQ_FIRST(&evcon->requests) == req);

	/* xxx: not sure if we really should expose the data buffer this way */
	if (databuf != NULL)
		evbuffer_add_buffer(req->output_buffer, databuf);
	
	/* Adds headers to the response */
	evrtsp_make_header(evcon, req);

	evrtsp_write_buffer(evcon, evrtsp_send_done, NULL);
}

/*
 * Request related functions
 */

struct evrtsp_request *
evrtsp_request_new(void (*cb)(struct evrtsp_request *, void *), void *arg)
{
	struct evrtsp_request *req = NULL;

	/* Allocate request structure */
	if ((req = calloc(1, sizeof(struct evrtsp_request))) == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}

	req->kind = EVRTSP_RESPONSE;
	req->input_headers = calloc(1, sizeof(struct evkeyvalq));
	if (req->input_headers == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}
	TAILQ_INIT(req->input_headers);

	req->output_headers = calloc(1, sizeof(struct evkeyvalq));
	if (req->output_headers == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}
	TAILQ_INIT(req->output_headers);

	if ((req->input_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new", __func__);
		goto error;
	}

	if ((req->output_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new", __func__);
		goto error;
	}

	req->cb = cb;
	req->cb_arg = arg;

	return (req);

 error:
	if (req != NULL)
		evrtsp_request_free(req);
	return (NULL);
}

void
evrtsp_request_free(struct evrtsp_request *req)
{
	if (req->uri != NULL)
		free(req->uri);
	if (req->response_code_line != NULL)
		free(req->response_code_line);

	evrtsp_clear_headers(req->input_headers);
	free(req->input_headers);

	evrtsp_clear_headers(req->output_headers);
	free(req->output_headers);

	if (req->input_buffer != NULL)
		evbuffer_free(req->input_buffer);

	if (req->output_buffer != NULL)
		evbuffer_free(req->output_buffer);

	free(req);
}

/*
 * Allows for inspection of the request URI
 */

const char *
evrtsp_request_uri(struct evrtsp_request *req) {
	if (req->uri == NULL)
		event_debug(("%s: request %p has no uri\n", __func__, req));
	return (req->uri);
}

/*
 * Network helper functions that we do not want to export to the rest of
 * the world.
 */
#if 0 /* Unused */
static struct addrinfo *
addr_from_name(char *address)
{
#ifdef _EVENT_HAVE_GETADDRINFO
        struct addrinfo ai, *aitop;
        int ai_result;

        memset(&ai, 0, sizeof(ai));
        ai.ai_family = AF_INET;
        ai.ai_socktype = SOCK_RAW;
        ai.ai_flags = 0;
        if ((ai_result = getaddrinfo(address, NULL, &ai, &aitop)) != 0) {
                if ( ai_result == EAI_SYSTEM )
                        event_warn("getaddrinfo");
                else
                        event_warnx("getaddrinfo: %s", gai_strerror(ai_result));
        }

	return (aitop);
#else
	assert(0);
	return NULL; /* XXXXX Use gethostbyname, if this function is ever used. */
#endif
}
#endif

static void
name_from_addr(struct sockaddr *sa, socklen_t salen,
    char **phost, char **pport)
{
	char ntop[NI_MAXHOST];
	char strport[NI_MAXSERV];
	int ni_result;

#ifdef _EVENT_HAVE_GETNAMEINFO
	ni_result = getnameinfo(sa, salen,
		ntop, sizeof(ntop), strport, sizeof(strport),
		NI_NUMERICHOST|NI_NUMERICSERV);
	
	if (ni_result != 0) {
		if (ni_result == EAI_SYSTEM)
			event_err(1, "getnameinfo failed");
		else
			event_errx(1, "getnameinfo failed: %s", gai_strerror(ni_result));
		return;
	}
#else
	ni_result = fake_getnameinfo(sa, salen,
		ntop, sizeof(ntop), strport, sizeof(strport),
		NI_NUMERICHOST|NI_NUMERICSERV);
	if (ni_result != 0)
			return;
#endif
	if (phost)
	  *phost = strdup(ntop);
	if (pport)
	  *pport = strdup(strport);
}

/* Create a non-blocking socket and bind it */
/* todo: rename this function */
static int
bind_socket_ai(int family, struct addrinfo *ai, int reuse)
{
        int fd, on = 1, r;
	int serrno;

	if (ai)
	  family = ai->ai_family;

        /* Create listen socket */
        fd = socket(family, SOCK_STREAM, 0);
        if (fd == -1) {
                event_warn("socket");
                return (-1);
        }

        if (evutil_make_socket_nonblocking(fd) < 0)
                goto out;

#ifndef WIN32
        if (fcntl(fd, F_SETFD, 1) == -1) {
                event_warn("fcntl(F_SETFD)");
                goto out;
        }
#endif

	if (family == AF_INET6)
	  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));

        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on));
	if (reuse) {
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		    (void *)&on, sizeof(on));
	}

	if (ai != NULL) {
		r = bind(fd, ai->ai_addr, ai->ai_addrlen);
		if (r == -1)
			goto out;
	}

	return (fd);

 out:
	serrno = EVUTIL_SOCKET_ERROR();
	EVUTIL_CLOSESOCKET(fd);
	EVUTIL_SET_SOCKET_ERROR(serrno);
	return (-1);
}

static struct addrinfo *
make_addrinfo(const char *address, u_short port)
{
        struct addrinfo *aitop = NULL;

#ifdef _EVENT_HAVE_GETADDRINFO
        struct addrinfo ai;
        char strport[NI_MAXSERV];
        int ai_result;

        memset(&ai, 0, sizeof(ai));
        ai.ai_family = AF_UNSPEC;
        ai.ai_socktype = SOCK_STREAM;
        ai.ai_flags = AI_PASSIVE;  /* turn NULL host name into INADDR_ANY */
        evutil_snprintf(strport, sizeof(strport), "%d", port);
        if ((ai_result = getaddrinfo(address, strport, &ai, &aitop)) != 0) {
                if ( ai_result == EAI_SYSTEM )
                        event_warn("getaddrinfo");
                else
                        event_warnx("getaddrinfo: %s", gai_strerror(ai_result));
		return (NULL);
        }
#else
	static int cur;
	static struct addrinfo ai[2]; /* We will be returning the address of some of this memory so it has to last even after this call. */
	if (++cur == 2) cur = 0;   /* allow calling this function twice */

	if (fake_getaddrinfo(address, &ai[cur]) < 0) {
		event_warn("fake_getaddrinfo");
		return (NULL);
	}
	aitop = &ai[cur];
	((struct sockaddr_in *) aitop->ai_addr)->sin_port = htons(port);
#endif

	return (aitop);
}

static int
bind_socket(int family, const char *address, u_short port, int reuse)
{
	int fd;
	struct addrinfo *aitop = NULL;

	/* just create an unbound socket */
	if (address == NULL && port == 0)
		return bind_socket_ai(family, NULL, 0);
		
	aitop = make_addrinfo(address, port);

	if (aitop == NULL)
		return (-1);

	fd = bind_socket_ai(family, aitop, reuse);

#ifdef _EVENT_HAVE_GETADDRINFO
	freeaddrinfo(aitop);
#else
	fake_freeaddrinfo(aitop);
#endif

	return (fd);
}

static int
socket_connect(int fd, const char *address, unsigned short port)
{
	struct addrinfo *ai = make_addrinfo(address, port);
	int res = -1;

	if (ai == NULL) {
		event_debug(("%s: make_addrinfo: \"%s:%d\"",
			__func__, address, port));
		return (-1);
	}

	if (connect(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
#ifdef WIN32
		int tmp_error = WSAGetLastError();
		if (tmp_error != WSAEWOULDBLOCK && tmp_error != WSAEINVAL &&
		    tmp_error != WSAEINPROGRESS) {
			goto out;
		}
#else
		if (errno != EINPROGRESS) {
			goto out;
		}
#endif
	}

	/* everything is fine */
	res = 0;

out:
#ifdef _EVENT_HAVE_GETADDRINFO
	freeaddrinfo(ai);
#else
	fake_freeaddrinfo(ai);
#endif

	return (res);
}
