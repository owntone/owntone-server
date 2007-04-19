/*
 * $Id: $
 *
 * Implementation of functions for UPnP discovery
 *
 * Copyright (C) 2007 Ron Pedde (ron@pedde.com)
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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "conf.h"
#include "daapd.h"
#include "err.h"
#include "os.h"
#include "util.h"
#include "upnp.h"

#define UPNP_MAX_PACKET 1500
#define UPNP_ADDR "239.255.255.250"
#define UPNP_PORT 1900

typedef struct upnp_packetinfo_t {
    char *group_id;
    char *base_packet;
    char *location;
    char *usn;
    char *nt;
    char *body;
    struct upnp_packetinfo_t *next;
} UPNP_PACKETINFO;

/* Globals */
UPNP_PACKETINFO upnp_packetlist;
OS_SOCKETTYPE upnp_socket;

/* Forwards */
int upnp_strcat(char *what, char *where, int bytes_left);
void upnp_build_packet(char *packet, int len, UPNP_PACKETINFO *pi);
void upnp_broadcast(void);

/**
 * add a upnp packet too the root list.
 */
void upnp_add_packet(char *group_id, char *location,
                     char *usn, char *nt, char *body) {
    UPNP_PACKETINFO *pnew;

    pnew = (UPNP_PACKETINFO *)malloc(sizeof(UPNP_PACKETINFO));
    if(!pnew)
        DPRINTF(E_FATAL,L_MISC,"Malloc error\n");

    memset(pnew,0,sizeof(UPNP_PACKETINFO));
    if(group_id)
        pnew->group_id = strdup(group_id);
    if(location)
        pnew->location = strdup(location);
    if(usn)
        pnew->usn = strdup(usn);
    if(nt)
        pnew->nt = strdup(nt);
    if(body)
        pnew->body = strdup(body);

    util_mutex_lock(l_upnp);
    pnew->next = upnp_packetlist.next;
    upnp_packetlist.next = pnew;
    util_mutex_unlock(l_upnp);
}

int upnp_strcat(char *what, char *where, int bytes_left) {
    if(!what)
        return bytes_left;

    if(strlen(what) < bytes_left) {
        strcat(where,what);
        return bytes_left - strlen(what);
    }

    return bytes_left;
}


void upnp_build_packet(char *packet, int len, UPNP_PACKETINFO *pi) {
    char buffer[256];
    char hostname[256];
    *packet = '\0';
    int port;

    port = conf_get_int("general","port",0);

    len = upnp_strcat("NOTIFY * HTTP/1.1\r\n",packet,len);
    if(pi->location) {
        gethostname(hostname,sizeof(hostname));
        snprintf(buffer,sizeof(buffer),"LOCATION: http://%s:%d%s\r\n",
                 hostname,port,pi->location);

        len=upnp_strcat(buffer,packet,len);
    }
    len=upnp_strcat("HOST: 239.255.255.250:1900\r\n",packet,len);
    len=upnp_strcat("SERVER: POSIX, UPnP/1.0, Firefly/" VERSION "\r\n",
                     packet,len);
    len=upnp_strcat("NTS: ssdp:alive\r\n",packet,len);

    /* USN */
    len=upnp_strcat("USN:uuid:",packet,len);
    len=upnp_strcat(UPNP_UUID,packet,len);
    if(pi->usn) {
        snprintf(buffer,sizeof(buffer),"::%s",pi->usn);
        len=upnp_strcat(buffer,packet,len);
    }
    len=upnp_strcat("\r\n",packet,len);

    len=upnp_strcat("CACHE-CONTROL: max-age=1800\r\n",packet,len);
    if(pi->nt) {
        snprintf(buffer,sizeof(buffer),"NT:%s\r\n",pi->nt);
    } else {
        snprintf(buffer,sizeof(buffer),"NT:uuid:%s\r\n", UPNP_UUID);
    }
    len=upnp_strcat(buffer,packet,len);

    if(pi->body) {
        snprintf(buffer,sizeof(buffer),"Content-Length: %d\r\n\r\n",
                (int)strlen(pi->body));
        len=upnp_strcat(buffer,packet,len);
        len=upnp_strcat(pi->body,packet,len);
    } else {
        len=upnp_strcat("Content-Length: 0\r\n\r\n",packet,len);
    }
}

/**
 * broadcast out the upnp packets
 */
void upnp_broadcast(void) {
    UPNP_PACKETINFO *pi;
    struct sockaddr_in sin;
    char packet[UPNP_MAX_PACKET];
    int pass;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(UPNP_PORT);
    sin.sin_addr.s_addr = inet_addr(UPNP_ADDR);

    util_mutex_lock(l_upnp);

    for(pass=0; pass < 2; pass++) {
        pi=upnp_packetlist.next;
        while(pi) {
            upnp_build_packet(packet, UPNP_MAX_PACKET, pi);
            sendto(upnp_socket,packet,strlen(packet),0,
                   (struct sockaddr *)&sin, sizeof(sin));
            pi = pi->next;
        }
    }

    util_mutex_unlock(l_upnp);
}


int upnp_tick(void) {
    static time_t last_broadcast = 0;

    if((time(NULL) - last_broadcast) > 60) {
        upnp_broadcast();
        last_broadcast = time(NULL);
    }

    return TRUE;
}


/**
 * start UPnP broadcaster.  We'll want to send at least a rootdevice
 * announcement with a location pointer to the admin page
 */
int upnp_init(void) {
    int ttl = 3;
    int reuse=1;
    int result;
    struct sockaddr_in addr;
    struct ip_mreq mreq;

    memset(&upnp_packetlist,0,sizeof(upnp_packetlist));
    upnp_add_packet("base","/upnp-basic.xml",NULL,NULL,NULL);
    upnp_add_packet("base","/upnp-basic.xml",
                    "urn:schemas-upnp-org:device:MediaServer:1",
                    "urn:schemas-upnp-org:device:MediaServer:1",NULL);
    upnp_add_packet("base","/upnp-basic.xml",
                    "urn:schemas-upnp-org:service:AVTransport:1",
                    "urn:schemas-upnp-org:service:AVTransport:1",NULL);
    upnp_add_packet("base","/upnp-basic.xml",
                    "urn:schemas-upnp-org:service:ContentDirectory:1",
                    "urn:schemas-upnp-org:service:ContentDirectory:1",NULL);
    upnp_add_packet("basic","/upnp-basic.xml",
                    "urn:schemas-upnp-org:service:ConnectionManager:1",
                    "urn:schemas-upnp-org:service:ConnectionManager:1",NULL);
    upnp_add_packet("base","/upnp-basic.xml","upnp:rootdevice","upnp:rootdevice",NULL);

    upnp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    result = setsockopt(upnp_socket, IPPROTO_IP, IP_MULTICAST_TTL,
                        &ttl, sizeof(ttl));
    if(result == -1) {
        close(upnp_socket);
        DPRINTF(E_LOG,L_MISC,"Error setting IP_MULTICAST_TTL\n");
        return FALSE;
    }

    result = setsockopt(upnp_socket,SOL_SOCKET,SO_REUSEADDR,
                        &reuse,sizeof(reuse));
    if(result == -1) {
        close(upnp_socket);
        DPRINTF(E_LOG,L_MISC,"Error setting SO_REUSEADDR\n");
        return FALSE;
    }


    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=htonl(INADDR_ANY);
    addr.sin_port=htons(UPNP_PORT);

    if(bind(upnp_socket,(struct sockaddr *)&addr,sizeof(addr)) < 0) {
        DPRINTF(E_LOG,L_MISC,"Error binding to upnp port\n");
        close(upnp_socket);
        return FALSE;
    }

    mreq.imr_multiaddr.s_addr=inet_addr(UPNP_ADDR);
    mreq.imr_interface.s_addr=htonl(INADDR_ANY);
    if(setsockopt(upnp_socket,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq)) < 0) {
        DPRINTF(E_LOG,L_MISC,"Error joining UPnP multicast group\n");
        close(upnp_socket);
        return FALSE;
    }

    return TRUE;
}

/**
 * turn off any upnp services.  Should really de-register
 * all registered devices, etc.
 */
int upnp_deinit(void) {
    close(upnp_socket);
    return TRUE;
}



