#ifndef _EVHTTP_COMPAT_H_
#define _EVHTTP_COMPAT_H_

#include "evhttp.h"

/* This file should only be included if using libevent 1
 *
 * The following adds libevent 2 evhttp functions to libevent 1, so we avoid
 * the need of having many HAVE_LIBEVENT2 conditions inside the code
 */

#define evhttp_request_get_response_code(x) x->response_code

#define evhttp_request_get_input_headers(x) x->input_headers
#define evhttp_request_get_output_headers(x) x->output_headers

#define evhttp_request_get_input_buffer(x) x->input_buffer
#define evhttp_request_get_output_buffer(x) x->output_buffer

#define evhttp_request_get_host(x) x->remote_host

#define evhttp_request_get_uri evhttp_request_uri

struct evhttp_connection *
evhttp_connection_base_new(struct event_base *base, void *ignore, const char *address, unsigned short port);

void
evhttp_request_set_header_cb(struct evhttp_request *req, int (*cb)(struct evhttp_request *, void *));

#endif /* _EVHTTP_COMPAT_H_ */
