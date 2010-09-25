/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
 * Based on evhttp from libevent 1.4.x
 *
 * Copyright (c) 2000-2004 Niels Provos <provos@citi.umich.edu>
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
#ifndef _EVRTSP_H_
#define _EVRTSP_H_

#include <event.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

/* Response codes */
#define RTSP_OK			200
#define RTSP_UNAUTHORIZED       401

struct evrtsp_connection;

/*
 * Interfaces for making requests
 */
enum evrtsp_cmd_type {
  EVRTSP_REQ_ANNOUNCE,
  EVRTSP_REQ_OPTIONS,
  EVRTSP_REQ_SETUP,
  EVRTSP_REQ_RECORD,
  EVRTSP_REQ_PAUSE,
  EVRTSP_REQ_GET_PARAMETER,
  EVRTSP_REQ_SET_PARAMETER,
  EVRTSP_REQ_FLUSH,
  EVRTSP_REQ_TEARDOWN,
};

enum evrtsp_request_kind { EVRTSP_REQUEST, EVRTSP_RESPONSE };

struct evrtsp_request {
#if defined(TAILQ_ENTRY)
	TAILQ_ENTRY(evrtsp_request) next;
#else
struct {
	struct evrtsp_request *tqe_next;
	struct evrtsp_request **tqe_prev;
}       next;
#endif

	/* the connection object that this request belongs to */
	struct evrtsp_connection *evcon;
	int flags;
#define EVRTSP_REQ_OWN_CONNECTION	0x0001

	struct evkeyvalq *input_headers;
	struct evkeyvalq *output_headers;

	enum evrtsp_request_kind kind;
	enum evrtsp_cmd_type type;

	char *uri;			/* uri after RTSP request was parsed */

	char major;			/* RTSP Major number */
	char minor;			/* RTSP Minor number */

	int response_code;		/* RTSP Response code */
	char *response_code_line;	/* Readable response */

	struct evbuffer *input_buffer;	/* read data */
	ev_int64_t ntoread;

	struct evbuffer *output_buffer;	/* outgoing post or data */

	/* Callback */
	void (*cb)(struct evrtsp_request *, void *);
	void *cb_arg;
};

/**
 * Creates a new request object that needs to be filled in with the request
 * parameters.  The callback is executed when the request completed or an
 * error occurred.
 */
struct evrtsp_request *evrtsp_request_new(
	void (*cb)(struct evrtsp_request *, void *), void *arg);

/** Frees the request object and removes associated events. */
void evrtsp_request_free(struct evrtsp_request *req);

/**
 * A connection object that can be used to for making RTSP requests.  The
 * connection object tries to establish the connection when it is given an
 * rtsp request object.
 */
struct evrtsp_connection *evrtsp_connection_new(
	const char *address, unsigned short port);

/** Frees an rtsp connection */
void evrtsp_connection_free(struct evrtsp_connection *evcon);

/** Set a callback for connection close. */
void evrtsp_connection_set_closecb(struct evrtsp_connection *evcon,
    void (*)(struct evrtsp_connection *, void *), void *);

/**
 * Associates an event base with the connection - can only be called
 * on a freshly created connection object that has not been used yet.
 */
void evrtsp_connection_set_base(struct evrtsp_connection *evcon,
    struct event_base *base);

/** Get the remote address and port associated with this connection. */
void evrtsp_connection_get_peer(struct evrtsp_connection *evcon,
    char **address, u_short *port);

/** Get the local address and port associated with this connection. */
void
evrtsp_connection_get_local_address(struct evrtsp_connection *evcon,
    char **address, u_short *port);

/** The connection gets ownership of the request */
int evrtsp_make_request(struct evrtsp_connection *evcon,
    struct evrtsp_request *req,
    enum evrtsp_cmd_type type, const char *uri);

const char *evrtsp_request_uri(struct evrtsp_request *req);

/* Interfaces for dealing with headers */

const char *evrtsp_find_header(const struct evkeyvalq *, const char *);
int evrtsp_remove_header(struct evkeyvalq *, const char *);
int evrtsp_add_header(struct evkeyvalq *, const char *, const char *);
void evrtsp_clear_headers(struct evkeyvalq *);

const char *evrtsp_method(enum evrtsp_cmd_type type);

#ifdef __cplusplus
}
#endif

#endif /* !_EVRTSP_H_ */
