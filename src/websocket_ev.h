
#ifndef SRC_WEBSOCKET_EV_H_
#define SRC_WEBSOCKET_EV_H_

#include <event2/http.h>

int
websocketev_init(struct evhttp *evhttp);

void
websocketev_deinit(void);

#endif /* SRC_WEBSOCKET_EV_H_ */
