/*
 * $Id$
 *
 * This is an implementation of rendezvous using the Bonjour (tm)
 * for windows dns_sd.h implementation
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>

#include "dns_sd.h"
#include "err.h"

/* Globals */
pthread_t rend_tid;
static volatile int rend_stop_flag = 0;
static volatile int rend_timeout = 10; /* select timeout */
static DNSServiceRef rend_client  = NULL;
static DNSServiceRef rend_client2 = NULL;
static volatile int rend_count=0;

/* Forwards */
void *rend_mainthread(void *arg);
void DNSSD_API rend_reg_reply(DNSServiceRef client, const DNSServiceFlags flags, 
    DNSServiceErrorType errorCode, const char *name, 
    const char *regtype, const char *domain, void *context); 

/* Typedefs */
typedef union { unsigned char b[2]; unsigned short NotAnInteger; } Opaque16;


/**
 * initialize the rendezvous interface.
 *
 * @param user user to drop privs to (ignored)
 * @returns 0 on success, -1 with errno set otherwise
 */
int rend_init(char *user) {
    /* we'll throw off a handler thread when we register a name */
    return 0;
}

/**
 * main bonjourvous thread
 *
 * @param arg unused
 */
void *rend_mainthread(void *arg) {
    /* this is pretty much right out of the old mdns stuff */
    int dns_sd_fd  = rend_client  ? DNSServiceRefSockFD(rend_client) : -1;
    int dns_sd_fd2 = rend_client2 ? DNSServiceRefSockFD(rend_client2) : -1;
    int nfds = dns_sd_fd + 1;
    fd_set readfds;
    struct timeval tv;
    int result;
    DNSServiceErrorType err = kDNSServiceErr_NoError;

    if (dns_sd_fd2 > dns_sd_fd) nfds = dns_sd_fd2 + 1;

    while (!rend_stop_flag) {
        FD_ZERO(&readfds);

        if (rend_client) FD_SET(dns_sd_fd, &readfds);
        if (rend_client2) FD_SET(dns_sd_fd2, &readfds);

        tv.tv_sec = rend_timeout;
        tv.tv_usec = 0;

        result = select(nfds, &readfds, (fd_set*)NULL, (fd_set*)NULL, &tv);
        if (result > 0) {
            if (rend_client  && FD_ISSET(dns_sd_fd, &readfds)) {
                err = DNSServiceProcessResult(rend_client);
            } else if (rend_client2 && FD_ISSET(dns_sd_fd2, &readfds)) {
                err = DNSServiceProcessResult(rend_client2);
            }
            if (err) { 
                DPRINTF(E_LOG,L_REND,"DNSServiceProcessResult returned %d\n", err); 
                rend_stop_flag = 1; 
            }
        } else if (result == 0) {
            DPRINTF(E_DBG,L_REND,"rendezvous: tick!\n");
             
//            myTimerCallBack();
        } else {
            DPRINTF(E_LOG,L_REND,"select() returned %d errno %d %s\n", result, errno, strerror(errno));
            if (errno != EINTR) rend_stop_flag = 1;
        }
    }
    if(rend_client) DNSServiceRefDeallocate(rend_client);
    if(rend_client2) DNSServiceRefDeallocate(rend_client2);

    rend_client = NULL;
    rend_client2 = NULL;

    return NULL;
}


/**
 * check to see if rendezvous is running.
 *
 * @returns TRUE if running, FALSE otherwise
 */
int rend_running(void) {
    return TRUE;
}

/**
 * stop rendezvous if it is running.  There should really be a way
 * to start it, would't one think?
 *
 * @returns TRUE if stopped, FALSE otherwise
 */
int rend_stop(void) {
    return TRUE;
}


/**
 * register a rendezvous name
 *
 * @param name long name to register (mt-daapd)
 * @param type type to register (_daap._tcp)
 * @param port port to register (3689)
 * @param iface interface to register with (ignored)
 * @returns TRUE if registered, FALSE otherwise
 */
int rend_register(char *name, char *type, int port, char *iface, char *txt) {
    int err;
    uint16_t port_netorder = htons((unsigned short)port);

    DPRINTF(E_INF,L_REND,"Registering %s as type (%s) on port %d\n",
            name, type, port);

    DNSServiceRegister(&rend_client,0,kDNSServiceInterfaceIndexAny,name,type,"local",NULL,
        htons((unsigned short)port),strlen(txt),txt,rend_reg_reply, NULL);
    
    /* throw off a new thread work this */
    if(!rend_count) {
        if((err=pthread_create(&rend_tid,NULL,rend_mainthread,NULL))) {
            DPRINTF(E_LOG,L_REND,"Could not spawn thread: %s\n",strerror(err));
            errno=err;
            return -1;
        }
    }

    rend_count++;
    return 0;
}

void DNSSD_API rend_reg_reply(DNSServiceRef client, const DNSServiceFlags flags, 
    DNSServiceErrorType errorCode, const char *name, 
    const char *regtype, const char *domain, void *context) 
{
    DPRINTF(E_INF,L_REND,"Got a reply for %s.%s%s: ", name, regtype, domain);
    switch (errorCode) {
    case kDNSServiceErr_NoError:
        DPRINTF(E_INF,L_REND,"Name now registered and active\n"); 
        break;
    case kDNSServiceErr_NameConflict: 
        DPRINTF(E_FATAL,L_REND,"Rendezvous name in use, aborting...\n"); 
        break;
    default:                         
        DPRINTF(E_FATAL,L_REND,"Error %d\n", errorCode);
        return;
    }
}

/**
 * unregister a name
 *
 * unimplemented
 */
int rend_unregister(char *name, char *type, int port) {
    return -1;
}
