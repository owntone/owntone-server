/*
 * $Id$
 * Rendezvous for SwampWolf's Howl (http://www.swampwolf.com)
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <rendezvous/rendezvous.h>
#include <salt/log.h>
#include <pthread.h>

#include "err.h"
#include "rend-unix.h"

pthread_t rend_tid;
sw_rendezvous rend_handle;

/* Forwards */
void *rend_pipe_monitor(void* arg);
void rend_callback(void);

/*
 * rend_howl_reply
 *
 * Callback function for mDNS stuff
 */
static sw_result rend_howl_reply(sw_rendezvous_publish_handler handler,
				 sw_rendezvous rendezvous,
				 sw_rendezvous_publish_status status,
				 sw_rendezvous_publish_id id,
				 sw_opaque extra) {
    static sw_string status_text[] = {
	"started",
	"stopped",
	"name collision",
	"invalid"
    };

    DPRINTF(E_DBG,L_REND,"Publish reply: %s\n",status_text[status]);
    return SW_OKAY;
}


/*
 * rend_private_init
 *
 * Initialize howl and start runloop
 */
int rend_private_init(char *user) {
    sw_result result;
    
    DPRINTF(E_DBG,L_REND,"Starting rendezvous services\n");
    signal(SIGHUP,  SIG_IGN);           // SIGHUP might happen from a request to reload the daap server

    if(sw_rendezvous_init(&rend_handle) != SW_OKAY) {
	DPRINTF(E_WARN,L_REND,"Error initializing rendezvous\n");
	errno=EINVAL;
	return -1;
    }

    if(drop_privs(user)) 
	return -1;

    DPRINTF(E_DBG,L_REND,"Starting polling thread\n");
    
    if(pthread_create(&rend_tid,NULL,rend_pipe_monitor,NULL)) {
	DPRINTF(E_FATAL,L_REND,"Could not start thread.  Terminating\n");
	/* should kill parent, too */
	exit(EXIT_FAILURE);
    }

    DPRINTF(E_DBG,L_REND,"Entering runloop\n");

    sw_rendezvous_run(rend_handle);

    DPRINTF(E_DBG,L_REND,"Exiting runloop\n");

    return 0;
}

/*
 * rend_pipe_monitor
 */
void *rend_pipe_monitor(void* arg) {
    fd_set rset;
    struct timeval tv;
    int result;

    while(1) {
	DPRINTF(E_DBG,L_REND,"Waiting for data\n");
	FD_ZERO(&rset);
	FD_SET(rend_pipe_to[RD_SIDE],&rset);

	/* sit in a select spin until there is data on the to fd */
	while(((result=select(rend_pipe_to[RD_SIDE] + 1,&rset,NULL,NULL,NULL)) != -1) &&
	    errno != EINTR) {
	    if(FD_ISSET(rend_pipe_to[RD_SIDE],&rset)) {
		DPRINTF(E_DBG,L_REND,"Received a message from daap server\n");
		rend_callback();
	    }
	}

	DPRINTF(E_DBG,L_REND,"Select error!\n");
	/* should really bail here */
    }
}


/*
 * rend_callback
 *
 * This gets called from the main thread when there is a 
 * message waiting to be processed.
 */
void rend_callback(void) {
    REND_MESSAGE msg;
    sw_rendezvous_publish_id rend_id;
    sw_result result;

    /* here, we've seen the message, now we have to process it */

    if(rend_read_message(&msg) != sizeof(msg)) {
	DPRINTF(E_FATAL,L_REND,"Error reading rendezvous message\n");
	exit(EXIT_FAILURE);
    }

    switch(msg.cmd) {
    case REND_MSG_TYPE_REGISTER:
	DPRINTF(E_DBG,L_REND,"Registering %s.%s (%d)\n",msg.type,msg.name,msg.port);
	if((result=sw_rendezvous_publish(rend_handle,msg.name,msg.type,NULL,NULL,msg.port,NULL,0,
					 NULL,rend_howl_reply,NULL,&rend_id)) != SW_OKAY) {
	    DPRINTF(E_WARN,L_REND,"Error registering name\n");
	    rend_send_response(-1);
	} else {
	    rend_send_response(0); /* success */
	}
	break;
    case REND_MSG_TYPE_UNREGISTER:
	DPRINTF(E_WARN,L_REND,"Unsupported function: UNREGISTER\n");
	rend_send_response(-1); /* error */
	break;
    case REND_MSG_TYPE_STOP:
	DPRINTF(E_DBG,L_REND,"Stopping mDNS\n");
	rend_send_response(0);
	//sw_rendezvous_stop_publish(rend_handle);
	sw_rendezvous_fina(rend_handle);
	break;
    case REND_MSG_TYPE_STATUS:
	DPRINTF(E_DBG,L_REND,"Status inquiry -- returning 0\n");
	rend_send_response(0); /* success */
	break;
    default:
	break;
    }
}
