/*
 * $Id$
 *
 */

#include <libc.h>
#include <arpa/nameser.h>
#include <CoreFoundation/CoreFoundation.h>
#include <DNSServiceDiscovery/DNSServiceDiscovery.h>

#include "err.h"

CFRunLoopRef rend_runloop;


static void rend_stoprunloop(void) {
    CFRunLoopStop(rend_runloop);
}

static void rend_sigint(int sigraised) {
    DPRINTF(ERR_INFO,"SIGINT\n");
    rend_stoprunloop();
}

static void rend_handler(CFMachPortRef port, void *msg, CFIndex size, void *info) {
    DNSServiceDiscovery_handleReply(msg);
}

static int rend_addtorunloop(dns_service_discovery_ref client) {
    mach_port_t port=DNSServiceDiscoveryMachPort(client);

    if(!port)
	return -1;
    else {
	CFMachPortContext context = { 0, 0, NULL, NULL, NULL };
	Boolean shouldFreeInfo;
	CFMachPortRef cfMachPort=CFMachPortCreateWithPort(kCFAllocatorDefault, 
							  port, rend_handler, 
							  &context, &shouldFreeInfo);

	CFRunLoopSourceRef rls=CFMachPortCreateRunLoopSource(NULL,cfMachPort,0);
	CFRunLoopAddSource(CFRunLoopGetCurrent(),
			   rls,kCFRunLoopDefaultMode);
	CFRelease(rls);
	return 0;
    }
}

static void rend_reply(DNSServiceRegistrationReplyErrorType errorCode, void *context) {
    switch(errorCode) {
    case kDNSServiceDiscoveryNoError:
	DPRINTF(ERR_DEBUG,"Registered successfully\n");
	break;
    case kDNSServiceDiscoveryNameConflict:
	DPRINTF(ERR_WARN,"Error - name in use\n");
	break;
    default:
	DPRINTF(ERR_WARN,"Error %d\n",errorCode);
	break;
    }
}

/*
 * public interface
 */
int rend_init(pid_t *pid, char *name, int port) {
    dns_service_discovery_ref daap_ref=NULL;
    dns_service_discovery_ref http_ref=NULL;
    unsigned short usPort=port;

    *pid=fork();
    if(*pid) {
	return 0;
    }

    signal(SIGINT,  rend_sigint);      // SIGINT is what you get for a Ctrl-C


    DPRINTF(ERR_DEBUG,"Registering services\n");

    daap_ref=DNSServiceRegistrationCreate(name,"_daap._tcp","",usPort,"",rend_reply,nil);
    http_ref=DNSServiceRegistrationCreate(name,"_http._tcp","",port,"",rend_reply,nil);

    if(rend_addtorunloop(daap_ref)|| rend_addtorunloop(http_ref)) {
	DPRINTF(ERR_WARN,"Add to runloop failed\n");
	return -1;
    }

    rend_runloop = CFRunLoopGetCurrent();


    DPRINTF(ERR_DEBUG,"Registered rendezvous services\n");

    CFRunLoopRun();

    DPRINTF(ERR_DEBUG,"Exiting runloop\n");

    DNSServiceDiscoveryDeallocate(daap_ref);
    DNSServiceDiscoveryDeallocate(http_ref);

    exit(0);
}
