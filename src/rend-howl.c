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

#include <errno.h>
#include <stdio.h>
#include <pwd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <rendezvous/rendezvous.h>
#include <salt/log.h>


#include "err.h"

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

    DPRINTF(ERR_DEBUG,"Publish reply: %s\n",status_text[status]);
    return SW_OKAY;
}


/*
 * public interface
 */
int rend_init(pid_t *pid, char *name, int port, char *user) {
    sw_rendezvous rendezvous;
    sw_result result;
    sw_rendezvous_publish_id daap_id;
    sw_rendezvous_publish_id http_id;
    struct passwd *pw=NULL;

    /* drop privs */
    if(getuid() == (uid_t)0) {
	pw=getpwnam(user);
	if(pw) {
	    if(initgroups(user,pw->pw_gid) != 0 || 
	       setgid(pw->pw_gid) != 0 ||
	       setuid(pw->pw_uid) != 0) {
		fprintf(stderr,"Couldn't change to %s, gid=%d, uid=%d\n",
			user,pw->pw_gid, pw->pw_uid);
		exit(EXIT_FAILURE);
	    }
	} else {
	    fprintf(stderr,"Couldn't lookup user %s\n",user);
	    exit(EXIT_FAILURE);
	}
    }

    
    if(sw_rendezvous_init(&rendezvous) != SW_OKAY) {
	DPRINTF(ERR_WARN,"Error initializing rendezvous\n");
	errno=EINVAL;
	return -1;
    }

    if((result=sw_rendezvous_publish(rendezvous,name,"_daap._tcp",NULL,NULL,port,NULL,NULL,
				     rend_howl_reply,NULL,&daap_id)) != SW_OKAY) {
	DPRINTF(ERR_WARN,"Error registering DAAP server via mDNS: %d\n",result);
	return -1;
    }

    if((result=sw_rendezvous_publish(rendezvous,name,"_http._tcp",NULL,NULL,port,NULL,NULL,
				     rend_howl_reply,NULL,&http_id)) != SW_OKAY) {
	DPRINTF(ERR_WARN,"Error registering HTTP server via mDNS: %d\n",result);
	return -1;
    }
    
    *pid=fork();
    if(*pid) {
	return 0;
    }

    DPRINTF(ERR_DEBUG,"Registered rendezvous services\n");
    sw_rendezvous_run(rendezvous);
    return 0;
}
