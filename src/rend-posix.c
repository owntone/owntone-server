/*
 * $Id$
 *
 * Do the zeroconf/mdns/rendezvous (tm) thing.  This is a hacked version
 * of Apple's Responder.c from the Rendezvous (tm) POSIX implementation
 */

/*
 * Copyright (c) 2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.2 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
  File:       responder.c

  Contains:   Code to implement an mDNS responder on the Posix platform.

  Written by: Quinn

  Copyright:  Copyright (c) 2002 by Apple Computer, Inc., All Rights Reserved.

  Disclaimer: IMPORTANT:  This Apple software is supplied to you by Apple Computer, Inc.
  ("Apple") in consideration of your agreement to the following terms, and your
  use, installation, modification or redistribution of this Apple software
  constitutes acceptance of these terms.  If you do not agree with these terms,
  please do not use, install, modify or redistribute this Apple software.

  In consideration of your agreement to abide by the following terms, and subject
  to these terms, Apple grants you a personal, non-exclusive license, under Apple's
  copyrights in this original Apple software (the "Apple Software"), to use,
  reproduce, modify and redistribute the Apple Software, with or without
  modifications, in source and/or binary forms; provided that if you redistribute
  the Apple Software in its entirety and without modifications, you must retain
  this notice and the following text and disclaimers in all such redistributions of
  the Apple Software.  Neither the name, trademarks, service marks or logos of
  Apple Computer, Inc. may be used to endorse or promote products derived from the
  Apple Software without specific prior written permission from Apple.  Except as
  expressly stated in this notice, no other rights or licenses, express or implied,
  are granted by Apple herein, including but not limited to any patent rights that
  may be infringed by your derivative works or by other works in which the Apple
  Software may be incorporated.

  The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO
  WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED
  WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN
  COMBINATION WITH YOUR PRODUCTS.

  IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION
  OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT
  (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN
  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Change History (most recent first):

  $Log$
  Revision 1.4  2003/11/20 21:58:22  ron
  More diag logging, move WS_PRIVATE into the WS_CONNINFO

  Revision 1.3  2003/11/17 16:40:09  ron
  add support for named db

  Revision 1.2  2003/11/14 04:54:55  ron
  Use port 53

  Revision 1.1  2003/10/30 22:41:56  ron
  Initial checkin

  Revision 1.3  2002/09/21 20:44:53  zarzycki
  Added APSL info

  Revision 1.2  2002/09/19 04:20:44  cheshire
  Remove high-ascii characters that confuse some systems

  Revision 1.1  2002/09/17 06:24:35  cheshire
  First checkin

*/

#include "mdns/mDNSClientAPI.h"// Defines the interface to the client layer above
#include "mdns/mDNSPosix.h"    // Defines the specific types needed to run mDNS on this platform

#include <assert.h>
#include <stdio.h>			// For printf()
#include <stdlib.h>			// For exit() etc.
#include <string.h>			// For strlen() etc.
#include <unistd.h>			// For select()
#include <errno.h>			// For errno, EINTR
#include <signal.h>
#include <fcntl.h>

#include "err.h"

#pragma mark ***** Globals

static mDNS mDNSStorage;       // mDNS core uses this to store its globals
static mDNS_PlatformSupport PlatformStorage;  // Stores this platform's globals

#pragma mark ***** Signals

static volatile mDNSBool gStopNow;

/* modified signal handling code - rep 21 Oct 2k3 */

// o SIGINT  causes an orderly shutdown of the program.
// o SIGQUIT causes a somewhat orderly shutdown (direct but dangerous)
//
// There are fatal race conditions in our signal handling, but there's not much 
// we can do about them while remaining within the Posix space.  Specifically, 
// if a signal arrives after we test the globals its sets but before we call 
// select, the signal will be dropped.  The user will have to send the signal 
// again.  Unfortunately, Posix does not have a "sigselect" to atomically 
// modify the signal mask and start a select.

static void HandleSigInt(int sigraised)
// A handler for SIGINT that causes us to break out of the 
// main event loop when the user types ^C.  This has the 
// effect of quitting the program.
{
    assert(sigraised == SIGINT);

    DPRINTF(ERR_INFO,"SIGINT\n");
    gStopNow = mDNStrue;
}

static void HandleSigQuit(int sigraised)
// If we get a SIGQUIT the user is desperate and we 
// just call mDNS_Close directly.  This is definitely 
// not safe (because it could reenter mDNS), but 
// we presume that the user has already tried the safe 
// alternatives.
{
    assert(sigraised == SIGQUIT);

    DPRINTF(ERR_INFO,"SIGQUIT\n");

    mDNS_Close(&mDNSStorage);
    exit(0);
}

#pragma mark ***** Parameter Checking

static mDNSBool CheckThatRichTextHostNameIsUsable(const char *richTextHostName)
// Checks that richTextHostName is a reasonable host name 
// label and, if it isn't and printExplanation is true, prints 
// an explanation of why not.
{
    mDNSBool    result;
    domainlabel richLabel;
    domainlabel poorLabel;
    
    result = mDNStrue;
    if (result && strlen(richTextHostName) > 63) {
        result = mDNSfalse;
    }
    if (result && richTextHostName[0] == 0) {
        result = mDNSfalse;
    }
    if (result) {
        ConvertCStringToDomainLabel(richTextHostName, &richLabel);
        ConvertUTF8PstringToRFC1034HostLabel(richLabel.c, &poorLabel);
        if (poorLabel.c[0] == 0) {
            result = mDNSfalse;
        }
    }
    return result;
}

static mDNSBool CheckThatServiceTypeIsUsable(const char *serviceType)
// Checks that serviceType is a reasonable service type 
// label and, if it isn't and printExplanation is true, prints 
// an explanation of why not.
{
    mDNSBool result;
    
    result = mDNStrue;
    if (result && strlen(serviceType) > 63) {
        result = mDNSfalse;
    }
    if (result && serviceType[0] == 0) {
        result = mDNSfalse;
    }
    return result;
}

static mDNSBool CheckThatServiceTextIsUsable(const char *serviceText,
                                             mDNSu8 *pStringList, mDNSu16 *pStringListLen)
// Checks that serviceText is a reasonable service text record 
// and, if it isn't and printExplanation is true, prints 
// an explanation of why not.  Also parse the text into 
// the packed PString buffer denoted by pStringList and 
// return the length of that buffer in *pStringListLen.
// Note that this routine assumes that the buffer is 
// sizeof(RDataBody) bytes long.
{
    mDNSBool result;
    size_t   serviceTextLen;
    
    // Note that parsing a C string into a PString list always 
    // expands the data by one character, so the following 
    // compare is ">=", not ">".  Here's the logic:
    //
    // #1 For a string with not ^A's, the PString length is one 
    //    greater than the C string length because we add a length 
    //    byte.
    // #2 For every regular (not ^A) character you add to the C 
    //    string, you add a regular character to the PString list.
    //    This does not affect the equivalence stated in #1.
    // #3 For every ^A you add to the C string, you add a length 
    //    byte to the PString list but you also eliminate the ^A, 
    //    which again does not affect the equivalence stated in #1.
    
    result = mDNStrue;
    serviceTextLen = strlen(serviceText);
    if (result && strlen(serviceText) >= sizeof(RDataBody)) {
        result = mDNSfalse;
    }
    
    // Now break the string up into PStrings delimited by ^A.
    // We know the data will fit so we can ignore buffer overrun concerns. 
    // However, we still have to treat runs long than 255 characters as
    // an error.
    
    if (result) {
        int         lastPStringOffset;
        int         i;
        int         thisPStringLen;
        
        // This algorithm is a little tricky.  We start by copying 
        // the string directly into the output buffer, shifted up by 
        // one byte.  We then fill in the first byte with a ^A. 
        // We then walk backwards through the buffer and, for each 
        // ^A that we find, we replace it with the difference between 
        // its offset and the offset of the last ^A that we found
        // (ie lastPStringOffset).
        
        memcpy(&pStringList[1], serviceText, serviceTextLen);
        pStringList[0] = 1;
        lastPStringOffset = serviceTextLen + 1;
        for (i = serviceTextLen; i >= 0; i--) {
            if ( pStringList[i] == 1 ) {
                thisPStringLen = (lastPStringOffset - i - 1);
                assert(thisPStringLen >= 0);
                if (thisPStringLen > 255) {
                    result = mDNSfalse;
                    break;
                } else {
                    pStringList[i]    = thisPStringLen;
                    lastPStringOffset = i;
                }
            }
        }
        
        *pStringListLen = serviceTextLen + 1;
    }

    return result;
}

static mDNSBool CheckThatPortNumberIsUsable(long portNumber)
// Checks that portNumber is a reasonable port number
// and, if it isn't and printExplanation is true, prints 
// an explanation of why not.
{
    mDNSBool result;
    
    result = mDNStrue;
    if (result && (portNumber <= 0 || portNumber > 65535)) {
        result = mDNSfalse;
    }
    return result;
}

#pragma mark ***** Command Line Arguments

/* get rid of pidfile handling - rep - 21 Oct 2k3 */
static const char kDefaultServiceType[] = "_http._tcp.";
enum {
    kDefaultPortNumber = 80
};

static   mDNSBool  gAvoidPort53      = mDNStrue;
static const char *gRichTextHostName = "";
static const char *gServiceType      = kDefaultServiceType;
static mDNSu8      gServiceText[sizeof(RDataBody)];
static mDNSu16     gServiceTextLen   = 0;
static        int  gPortNumber       = kDefaultPortNumber;

/*
static void ParseArguments(int argc, char **argv)
// Parses our command line arguments into the global variables 
// listed above.
{
    int ch;
    
    // Parse command line options using getopt.
    
    do {
        ch = getopt(argc, argv, "v:rn:x:t:p:f:dP");
        if (ch != -1) {
            switch (ch) {
		break;
	    case 'r':
		gAvoidPort53 = mDNSfalse;
		break;
	    case 'n':
		gRichTextHostName = optarg;
		if ( ! CheckThatRichTextHostNameIsUsable(gRichTextHostName) ) {
		    exit(1);
		}
		break;
	    case 't':
		gServiceType = optarg;
		if ( ! CheckThatServiceTypeIsUsable(gServiceType) ) {
		    exit(1);
		}
		break;
	    case 'x':
		if ( ! CheckThatServiceTextIsUsable(optarg, gServiceText, &gServiceTextLen) ) {
		    exit(1);
		}
		break;
	    case 'p':
		gPortNumber = atol(optarg);
		if ( ! CheckThatPortNumberIsUsable(gPortNumber) ) {
		    exit(1);
		}
		break;
	    case '?':
	    default:
		PrintUsage(argv);
		exit(1);
		break;
            }
        }
    } while (ch != -1);

    // Check for any left over command line arguments.
    
    if (optind != argc) {
        fprintf(stderr, "%s: Unexpected argument '%s'\n", gProgramName, argv[optind]);
        exit(1);
    }
    
    // Check for inconsistency between the arguments.
    
    if ( (gRichTextHostName[0] == 0) && (gServiceFile[0] == 0) ) {
        fprintf(stderr, "%s: You must specify a service to register (-n) or a service file (-f).\n", gProgramName);
        exit(1);
    }
}
*/

#pragma mark ***** Registration

typedef struct PosixService PosixService;

struct PosixService {
    ServiceRecordSet coreServ;
    PosixService *next;
    int serviceID;
};

static PosixService *gServiceList = NULL;

static void RegistrationCallback(mDNS *const m, ServiceRecordSet *const thisRegistration, mStatus status)
// mDNS core calls this routine to tell us about the status of 
// our registration.  The appropriate action to take depends 
// entirely on the value of status.
{
    switch (status) {

    case mStatus_NoError:      
	DPRINTF(ERR_DEBUG,"Callback: %##s Name Registered",
		thisRegistration->RR_SRV.name.c); 
	// Do nothing; our name was successfully registered.  We may 
	// get more call backs in the future.
	break;

    case mStatus_NameConflict: 
	DPRINTF(ERR_WARN,"Callback: %##s Name Conflict",
		thisRegistration->RR_SRV.name.c); 

	// In the event of a conflict, this sample RegistrationCallback 
	// just calls mDNS_RenameAndReregisterService to automatically 
	// pick a new unique name for the service. For a device such as a 
	// printer, this may be appropriate.  For a device with a user 
	// interface, and a screen, and a keyboard, the appropriate response 
	// may be to prompt the user and ask them to choose a new name for 
	// the service.
	//
	// Also, what do we do if mDNS_RenameAndReregisterService returns an 
	// error.  Right now I have no place to send that error to.
            
	status = mDNS_RenameAndReregisterService(m, thisRegistration);
	assert(status == mStatus_NoError);
	break;

    case mStatus_MemFree:      
	DPRINTF(ERR_WARN,"Callback: %##s Memory Free",
		thisRegistration->RR_SRV.name.c); 
            
	// When debugging is enabled, make sure that thisRegistration 
	// is not on our gServiceList.
            
#if defined(DEBUG)
	{
	    PosixService *cursor;
                    
	    cursor = gServiceList;
	    while (cursor != NULL) {
		assert(&cursor->coreServ != thisRegistration);
		cursor = cursor->next;
	    }
	}
#endif
	free(thisRegistration);
	break;

    default:                   
	DPRINTF(ERR_WARN,"Callback: %##s Unknown Status %d", 
		thisRegistration->RR_SRV.name.c, status); 
	break;
    }
}

static int gServiceID = 0;

static mStatus RegisterOneService(const char *  richTextHostName, 
                                  const char *  serviceType, 
                                  const mDNSu8  text[],
                                  mDNSu16       textLen,
                                  long          portNumber)
{
    mStatus             status;
    PosixService *      thisServ;
    mDNSOpaque16        port;
    domainlabel         name;
    domainname          type;
    domainname          domain;
    
    status = mStatus_NoError;
    thisServ = (PosixService *) malloc(sizeof(*thisServ));
    if (thisServ == NULL) {
        status = mStatus_NoMemoryErr;
    }
    if (status == mStatus_NoError) {
        ConvertCStringToDomainLabel(richTextHostName,  &name);
        ConvertCStringToDomainName(serviceType, &type);
        ConvertCStringToDomainName("local.", &domain);
        port.b[0] = (portNumber >> 8) & 0x0FF;
        port.b[1] = (portNumber >> 0) & 0x0FF;;
        status = mDNS_RegisterService(&mDNSStorage, &thisServ->coreServ,
				      &name, &type, &domain,
				      NULL,
				      port, 
				      text, textLen,
				      RegistrationCallback, thisServ);
    }
    if (status == mStatus_NoError) {
        thisServ->serviceID = gServiceID;
        gServiceID += 1;

        thisServ->next = gServiceList;
        gServiceList = thisServ;

	DPRINTF(ERR_DEBUG,
		"Registered service %d, name '%s', type '%s', port %ld\n", 
		thisServ->serviceID, 
		richTextHostName,
		serviceType,
		portNumber);
    } else {
        if (thisServ != NULL) {
            free(thisServ);
        }
    }
    return status;
}

static void DeregisterOurServices(void)
{
    PosixService *thisServ;
    int thisServID;
    
    while (gServiceList != NULL) {
        thisServ = gServiceList;
        gServiceList = thisServ->next;

        thisServID = thisServ->serviceID;
        
        mDNS_DeregisterService(&mDNSStorage, &thisServ->coreServ);

	DPRINTF(ERR_DEBUG,"Deregistered service %d\n",
		thisServ->serviceID);
    }
}

#pragma mark **** Main

int rend_init(pid_t *pid,char *name, int port) {
    mStatus status;
    mDNSBool result;

    status = mDNS_Init(&mDNSStorage, &PlatformStorage,
		       mDNS_Init_NoCache, mDNS_Init_ZeroCacheSize,
		       mDNS_Init_AdvertiseLocalAddresses,
		       mDNS_Init_NoInitCallback, mDNS_Init_NoInitCallbackContext);

    if (status != mStatus_NoError) {
	DPRINTF(ERR_FATAL,"mDNS Error %d\n",status);
	return(-1);
    }

    *pid=fork();
    if(*pid) {
	return 0;
    }

    DPRINTF(ERR_DEBUG,"Registering tcp service\n");
    RegisterOneService(name,"_http._tcp",NULL,0,port);
    RegisterOneService(name,"_daap._tcp",NULL,0,port);

    signal(SIGINT,  HandleSigInt);      // SIGINT is what you get for a Ctrl-C
    signal(SIGQUIT, HandleSigQuit);     // SIGQUIT is what you get for a Ctrl-\ (indeed)
    
    while (!gStopNow) {
	int nfds = 0;
	fd_set readfds;
	struct timeval timeout;
	int result;
	
	// 1. Set up the fd_set as usual here.
	// This example client has no file descriptors of its own,
	// but a real application would call FD_SET to add them to the set here
	FD_ZERO(&readfds);
	
	// 2. Set up the timeout.
	// This example client has no other work it needs to be doing,
	// so we set an effectively infinite timeout
	timeout.tv_sec = 0x3FFFFFFF;
	timeout.tv_usec = 0;
	
	// 3. Give the mDNSPosix layer a chance to add its information to the fd_set and timeout
	mDNSPosixGetFDSet(&mDNSStorage, &nfds, &readfds, &timeout);
	
	// 4. Call select as normal
	DPRINTF(ERR_DEBUG,"select(%d, %d.%06d)\n", nfds, 
		timeout.tv_sec, timeout.tv_usec);
	
	result = select(nfds, &readfds, NULL, NULL, &timeout);
	
	if (result < 0) {
	    DPRINTF(ERR_WARN,"select() returned %d errno %d\n", result, errno);
	    if (errno != EINTR) gStopNow = mDNStrue;
	} else {
	    // 5. Call mDNSPosixProcessFDSet to let the mDNSPosix layer do its work
	    mDNSPosixProcessFDSet(&mDNSStorage, result, &readfds);
	    
	    // 6. This example client has no other work it needs to be doing,
	    // but a real client would do its work here
	    // ... (do work) ...
	}
    }
    
    DPRINTF(ERR_DEBUG,"Exiting");
    
    DeregisterOurServices();
    mDNS_Close(&mDNSStorage);
    
    if (status == mStatus_NoError) {
        result = 0;
    } else {
        result = 2;
    }
    DPRINTF(ERR_DEBUG, "Finished with status %ld, result %d\n", 
	    status, result);

    return result;
}

