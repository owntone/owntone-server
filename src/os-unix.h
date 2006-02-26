/*
 * $Id$
 * Copyright (C) 2006 Ron Pedde (rpedde@users.sourceforge.net)
 *
 */

#ifndef _OS_UNIX_H_
#define _OS_UNIX_H_

/* unix-specific functions */
extern int os_drop_privs(char *user);
void os_set_pidfile(char *file);

#endif

