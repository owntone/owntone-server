/*
 * $Id$
 * Build daap structs for replies
 *
 * Copyright (C) 2003 Ron Pedde (ron@pedde.com)
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
#ifndef _DAAP_H_
#define _DAAP_H_

#include "daap-proto.h"

DAAP_BLOCK *daap_response_server_info(void);
DAAP_BLOCK *daap_response_content_codes(void);
DAAP_BLOCK *daap_response_login(void);
DAAP_BLOCK *daap_response_update(int clientver);
DAAP_BLOCK *daap_response_databases(char *path);

#endif /* _DAAP_H_ */

