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
//static volatile int rend_timeout = 100000000; /* select timeout */
static volatile int rend_timeout=2;
static volatile int rend_count=0;
static pthread_mutex_t rend_mutex=PTHREAD_MUTEX_INITIALIZER; 
typedef struct tag_rend_entry {
	DNSServiceRef client;
	struct tag_rend_entry *next;
} REND_ENTRY;
static REND_ENTRY rend_clients = { NULL, NULL };

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

/* FIXME */
void rend_lock(void) {
    if(pthread_mutex_lock(&rend_mutex))
        DPRINTF(E_FATAL,L_MISC,"Could not lock mutex\n");
}

void rend_unlock(void) {
    pthread_mutex_unlock(&rend_mutex);
}

/**
 * main bonjourvous thread
 *
 * @param arg unused
 */
void *rend_mainthread(void *arg) {
    /* this is pretty much right out of the old mdns stuff */
    int nfds;
    fd_set readfds;
    fd_set *which;

    struct timeval tv;
    int result;
    REND_ENTRY *current;
    DNSServiceErrorType err = kDNSServiceErr_NoError;

    while (!rend_stop_flag) {
        FD_ZERO(&readfds);

        rend_lock();
        nfds=1;
        current = rend_clients.next;
        while(current) {
            if(current->client) {
                if(DNSServiceRefSockFD(current->client) > nfds)
                    nfds = DNSServiceRefSockFD(current->client) + 1;

                FD_SET(DNSServiceRefSockFD(current->client),&readfds);
            }
            current = current->next;            
        }
        rend_unlock();

        tv.tv_sec = rend_timeout;
        tv.tv_usec = 0;

        result = select(nfds, &readfds, (fd_set*)NULL, (fd_set*)NULL, &tv);
        if (result > 0) {
            rend_lock();
            current = rend_clients.next;
            err=0;
            while(current) {
                if(current->client) {
                    if((!err) && (FD_ISSET(DNSServiceRefSockFD(current->client),&readfds))) {
                        err = DNSServiceProcessResult(current->client);
                    }
                }
                current = current->next;
            }
            rend_unlock();

            if (err) { 
                DPRINTF(E_LOG,L_REND,"DNSServiceProcessResult returned %d\n", err); 
                rend_stop_flag = 1; 
            }
        } else if (result == 0) {
            DPRINTF(E_SPAM,L_REND,"rendezvous: tick!\n");
             
//            myTimerCallBack();
        } else {
            DPRINTF(E_INF,L_REND,"select() returned %d errno %d %s\n", result, errno, strerror(errno));
            if (errno != EINTR) rend_stop_flag = 1;
        }
    }

    rend_lock();
    while(current) {
        if(current->client)
            DNSServiceRefDeallocate(current->client);
        current = current->next;            
    }
    rend_unlock();
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
    REND_ENTRY *pnew;
    uint16_t port_netorder = htons((unsigned short)port);

    pnew = (REND_ENTRY *)malloc(sizeof(REND_ENTRY));
    if(!pnew)
        return -1;

    rend_lock();
    pnew->client = NULL;
    pnew->next = rend_clients.next;
    rend_clients.next = pnew;
    rend_unlock();

    DPRINTF(E_INF,L_REND,"Registering %s as type (%s) on port %d\n",
            name, type, port);

    DNSServiceRegister(&pnew->client,0,kDNSServiceInterfaceIndexAny,name,type,"local",NULL,
        port_netorder,(uint16_t)strlen(txt),txt,rend_reg_reply, NULL);
    
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
    DPRINTF(E_INF,L_REND,"Got a reply for %s.%s%s\n", name, regtype, domain);
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
