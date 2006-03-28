/*
 * $Id$
 */

#ifndef _XMLRPC_H_
#define _XMLRPC_H_

#include "webserver.h"

#define XML_FLAG_NONE     0
#define XML_FLAG_JSON     1
#define XML_FLAG_READABLE 2


struct tag_xmlstruct;

extern void xml_handle(WS_CONNINFO *pwsc);
extern struct tag_xmlstruct *xml_init(WS_CONNINFO *pwsc, int emit_header, 
                                      int flags);
extern void xml_push(struct tag_xmlstruct *pxml, char *term);
extern void xml_pop(struct tag_xmlstruct *pxml);
extern void xml_output(struct tag_xmlstruct *pxml, char *section, char *fmt, ...);
extern void xml_deinit(struct tag_xmlstruct *pxml);

#endif /* _XMLRPC_H_ */
