/*
 * $Id$
 * Driver for multi-threaded daap server
 *
 * Copyright (C) 2003 Ron Pedde (ron@corbey.com)
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

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "err.h"
#include "webserver.h"

// 3689

int main(int argc, char *argv[]) {
    int status;
    char *confdir=NULL;
    WSCONFIG ws_config;
    WSHANDLE server;

    printf("mt-daapd: version $Revision$\n\n");
    
    ws_config.web_root="/Users/ron/Documents/rfc";
    ws_config.port=80;

    err_debuglevel=9;
    server=ws_start(&ws_config);
    if(!server) {
	perror("ws_start");
	return EXIT_FAILURE;
    }

    while(1) {
	sleep(20);
    }

    return EXIT_SUCCESS;
}

