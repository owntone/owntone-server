/*
 * $Id: os.h 1479 2007-01-09 18:12:39Z rpedde $
 * Abstract os interface for non-unix platforms
 *
 * Copyright (C) 2006 Ron Pedde (rpedde@users.sourceforge.net)
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


#ifndef _WSPRIVATE_H_
#define _WSPRIVATE_H_

extern int ws_socket_write(WS_CONNINFO *pwsc, unsigned char *buffer, int len);
extern int ws_socket_read(WS_CONNINFO *pwsc, unsigned char *buffer, int len);
extern void ws_socket_shutdown(WS_CONNINFO *pwsc);

#endif /* _WSPRIVATE_H_ */
