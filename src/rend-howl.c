/*
 * $Id$
 *
 */

#include <errno.h>
#include <stdio.h>
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
int rend_init(pid_t *pid, char *name, int port) {
    sw_rendezvous rendezvous;
    sw_result result;
    sw_rendezvous_publish_id daap_id;
    sw_rendezvous_publish_id http_id;
    
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
