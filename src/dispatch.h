/*
 * $Id$
 */

#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "db-generic.h"

extern void daap_handler(WS_CONNINFO *pwsc);
extern int daap_auth(char *hostname, char *username, char *password);
extern void dispatch_stream_id(WS_CONNINFO *pwsc, DBQUERYINFO *pqi, char *id);

#endif
