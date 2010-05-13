/*
 * Copyright (C) 2010 Julien BLACHE <jb@jblache.org>
 * Based on evhttp from libevent 1.4.x
 *
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * This header file contains definitions for dealing with RTSP requests
 * that are internal to libevent.  As user of the library, you should not
 * need to know about these.
 */

#ifndef _RTSP_H_
#define _RTSP_H_

#define RTSP_CONNECT_TIMEOUT	45
#define RTSP_WRITE_TIMEOUT	50
#define RTSP_READ_TIMEOUT	50

#define RTSP_PREFIX		"rtsp://"

enum message_read_status {
	ALL_DATA_READ = 1,
	MORE_DATA_EXPECTED = 0,
	DATA_CORRUPTED = -1,
	REQUEST_CANCELED = -2
};

enum evrtsp_connection_error {
	EVCON_RTSP_TIMEOUT,
	EVCON_RTSP_EOF,
	EVCON_RTSP_INVALID_HEADER
};

struct evbuffer;
struct addrinfo;
struct evrtsp_request;

/* A stupid connection object - maybe make this a bufferevent later */

enum evrtsp_connection_state {
	EVCON_DISCONNECTED,	/**< not currently connected not trying either*/
	EVCON_CONNECTING,	/**< tries to currently connect */
	EVCON_IDLE,		/**< connection is established */
	EVCON_READING_FIRSTLINE,/**< reading Request-Line (incoming conn) or
				 **< Status-Line (outgoing conn) */
	EVCON_READING_HEADERS,	/**< reading request/response headers */
	EVCON_READING_BODY,	/**< reading request/response body */
	EVCON_READING_TRAILER,	/**< reading request/response chunked trailer */
	EVCON_WRITING		/**< writing request/response headers/body */
};

struct event_base;

struct evrtsp_connection {
	int fd;
	struct event ev;
	struct event close_ev;
	struct evbuffer *input_buffer;
	struct evbuffer *output_buffer;
	
	char *bind_address;		/* address to use for binding the src */
	u_short bind_port;		/* local port for binding the src */

	char *address;			/* address to connect to */
	int family;
	u_short port;

	int flags;
#define EVRTSP_CON_CLOSEDETECT  0x0004  /* detecting if persistent close */

	int timeout;			/* timeout in seconds for events */
	
	enum evrtsp_connection_state state;
	int cseq;

	TAILQ_HEAD(evcon_requestq, evrtsp_request) requests;
	
	void (*cb)(struct evrtsp_connection *, void *);
	void *cb_arg;
	
	void (*closecb)(struct evrtsp_connection *, void *);
	void *closecb_arg;

	struct event_base *base;
};

/* resets the connection; can be reused for more requests */
void evrtsp_connection_reset(struct evrtsp_connection *);

/* connects if necessary */
int evrtsp_connection_connect(struct evrtsp_connection *);

/* notifies the current request that it failed; resets connection */
void evrtsp_connection_fail(struct evrtsp_connection *,
    enum evrtsp_connection_error error);

int evrtsp_hostportfile(char *, char **, u_short *, char **);

int evrtsp_parse_firstline(struct evrtsp_request *, struct evbuffer*);
int evrtsp_parse_headers(struct evrtsp_request *, struct evbuffer*);

void evrtsp_start_read(struct evrtsp_connection *);
void evrtsp_make_header(struct evrtsp_connection *, struct evrtsp_request *);

void evrtsp_write_buffer(struct evrtsp_connection *,
    void (*)(struct evrtsp_connection *, void *), void *);

#endif /* _RTSP_H */
