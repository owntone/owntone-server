/*
 * Copyright (c) 2002-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@

    Change History (most recent first):

$Log$
Revision 1.2  2004/03/02 00:03:37  rpedde
Merge new rendezvous code

Revision 1.16  2003/08/14 02:19:55  cheshire
<rdar://problem/3375491> Split generic ResourceRecord type into two separate types: AuthRecord and CacheRecord

Revision 1.15  2003/08/12 19:56:26  cheshire
Update to APSL 2.0

Revision 1.14  2003/08/06 18:20:51  cheshire
Makefile cleanup

Revision 1.13  2003/07/23 00:00:04  cheshire
Add comments

Revision 1.12  2003/07/15 01:55:16  cheshire
<rdar://problem/3315777> Need to implement service registration with subtypes

Revision 1.11  2003/07/14 18:11:54  cheshire
Fix stricter compiler warnings

Revision 1.10  2003/07/10 20:27:31  cheshire
<rdar://problem/3318717> mDNSResponder Posix version is missing a 'b' in the getopt option string

Revision 1.9  2003/07/02 21:19:59  cheshire
<rdar://problem/3313413> Update copyright notices, etc., in source code comments

Revision 1.8  2003/06/18 05:48:41  cheshire
Fix warnings

Revision 1.7  2003/05/06 00:00:50  cheshire
<rdar://problem/3248914> Rationalize naming of domainname manipulation functions

Revision 1.6  2003/03/08 00:35:56  cheshire
Switched to using new "mDNS_Execute" model (see "mDNSCore/Implementer Notes.txt")

Revision 1.5  2003/02/20 06:48:36  cheshire
Bug #: 3169535 Xserve RAID needs to do interface-specific registrations
Reviewed by: Josh Graessley, Bob Bradley

Revision 1.4  2003/01/28 03:07:46  cheshire
Add extra parameter to mDNS_RenameAndReregisterService(),
and add support for specifying a domain other than dot-local.

Revision 1.3  2002/09/21 20:44:53  zarzycki
Added APSL info

Revision 1.2  2002/09/19 04:20:44  cheshire
Remove high-ascii characters that confuse some systems

Revision 1.1  2002/09/17 06:24:35  cheshire
First checkin

*/

#include "mDNSClientAPI.h"// Defines the interface to the client layer above
#include "mDNSPosix.h"    // Defines the specific types needed to run mDNS on this platform

#include <assert.h>
#include <stdio.h>			// For printf()
#include <stdlib.h>			// For exit() etc.
#include <string.h>			// For strlen() etc.
#include <unistd.h>			// For select()
#include <errno.h>			// For errno, EINTR
#include <signal.h>
#include <fcntl.h>

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark ***** Globals
#endif

static mDNS mDNSStorage;       // mDNS core uses this to store its globals
static mDNS_PlatformSupport PlatformStorage;  // Stores this platform's globals

static const char *gProgramName = "mDNSResponderPosix";

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark ***** Signals
#endif

static volatile mDNSBool gReceivedSigUsr1;
static volatile mDNSBool gReceivedSigHup;
static volatile mDNSBool gStopNow;

// We support 4 signals.
//
// o SIGUSR1 toggles verbose mode on and off in debug builds
// o SIGHUP  triggers the program to re-read its preferences.
// o SIGINT  causes an orderly shutdown of the program.
// o SIGQUIT causes a somewhat orderly shutdown (direct but dangerous)
// o SIGKILL kills us dead (easy to implement :-)
//
// There are fatal race conditions in our signal handling, but there's not much 
// we can do about them while remaining within the Posix space.  Specifically, 
// if a signal arrives after we test the globals its sets but before we call 
// select, the signal will be dropped.  The user will have to send the signal 
// again.  Unfortunately, Posix does not have a "sigselect" to atomically 
// modify the signal mask and start a select.

static void HandleSigUsr1(int sigraised)
    // If we get a SIGUSR1 we toggle the state of the 
    // verbose mode.
{
    assert(sigraised == SIGUSR1);
    gReceivedSigUsr1 = mDNStrue;
}

static void HandleSigHup(int sigraised)
    // A handler for SIGHUP that causes us to break out of the 
    // main event loop when the user kill 1's us.  This has the 
    // effect of triggered the main loop to deregister the 
    // current services and re-read the preferences.
{
    assert(sigraised == SIGHUP);
	gReceivedSigHup = mDNStrue;
}

static void HandleSigInt(int sigraised)
    // A handler for SIGINT that causes us to break out of the 
    // main event loop when the user types ^C.  This has the 
    // effect of quitting the program.
{
    assert(sigraised == SIGINT);
    
    if (gMDNSPlatformPosixVerboseLevel > 0) {
        fprintf(stderr, "\nSIGINT\n");
    }
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

    if (gMDNSPlatformPosixVerboseLevel > 0) {
        fprintf(stderr, "\nSIGQUIT\n");
    }
    mDNS_Close(&mDNSStorage);
    exit(0);
}

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark ***** Parameter Checking
#endif

static mDNSBool CheckThatRichTextHostNameIsUsable(const char *richTextHostName, mDNSBool printExplanation)
    // Checks that richTextHostName is a reasonable host name 
    // label and, if it isn't and printExplanation is true, prints 
    // an explanation of why not.
{
    mDNSBool    result;
    domainlabel richLabel;
    domainlabel poorLabel;
    
    result = mDNStrue;
    if (result && strlen(richTextHostName) > 63) {
        if (printExplanation) {
            fprintf(stderr, 
                    "%s: Host name is too long (must be 63 characters or less)\n", 
                    gProgramName);
        }
        result = mDNSfalse;
    }
    if (result && richTextHostName[0] == 0) {
        if (printExplanation) {
            fprintf(stderr, "%s: Host name can't be empty\n", gProgramName);
        }
        result = mDNSfalse;
    }
    if (result) {
        MakeDomainLabelFromLiteralString(&richLabel, richTextHostName);
        ConvertUTF8PstringToRFC1034HostLabel(richLabel.c, &poorLabel);
        if (poorLabel.c[0] == 0) {
            if (printExplanation) {
                fprintf(stderr, 
                        "%s: Host name doesn't produce a usable RFC-1034 name\n", 
                        gProgramName);
            }
            result = mDNSfalse;
        }
    }
    return result;
}

static mDNSBool CheckThatServiceTypeIsUsable(const char *serviceType, mDNSBool printExplanation)
    // Checks that serviceType is a reasonable service type 
    // label and, if it isn't and printExplanation is true, prints 
    // an explanation of why not.
{
    mDNSBool result;
    
    result = mDNStrue;
    if (result && strlen(serviceType) > 63) {
        if (printExplanation) {
            fprintf(stderr, 
                    "%s: Service type is too long (must be 63 characters or less)\n", 
                    gProgramName);
        }
        result = mDNSfalse;
    }
    if (result && serviceType[0] == 0) {
        if (printExplanation) {
            fprintf(stderr, 
                    "%s: Service type can't be empty\n", 
                    gProgramName);
        }
        result = mDNSfalse;
    }
    return result;
}

static mDNSBool CheckThatServiceTextIsUsable(const char *serviceText, mDNSBool printExplanation,
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
        if (printExplanation) {
            fprintf(stderr, 
                    "%s: Service text record is too long (must be less than %d characters)\n", 
                    gProgramName,
                    (int) sizeof(RDataBody) );
        }
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
                    if (printExplanation) {
                        fprintf(stderr, 
                                "%s: Each component of the service text record must be 255 characters or less\n", 
                                gProgramName);
                    }
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

static mDNSBool CheckThatPortNumberIsUsable(long portNumber, mDNSBool printExplanation)
    // Checks that portNumber is a reasonable port number
    // and, if it isn't and printExplanation is true, prints 
    // an explanation of why not.
{
    mDNSBool result;
    
    result = mDNStrue;
    if (result && (portNumber <= 0 || portNumber > 65535)) {
        if (printExplanation) {
            fprintf(stderr, 
                    "%s: Port number specified by -p must be in range 1..65535\n", 
                    gProgramName);
        }
        result = mDNSfalse;
    }
    return result;
}

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark ***** Command Line Arguments
#endif

static const char kDefaultPIDFile[]     = "/var/run/mDNSResponder.pid";
static const char kDefaultServiceType[] = "_afpovertcp._tcp.";
static const char kDefaultServiceDomain[] = "local.";
enum {
    kDefaultPortNumber = 548
};

static void PrintUsage()
{
    fprintf(stderr, 
            "Usage: %s [-v level ] [-r] [-n name] [-t type] [-d domain] [-x TXT] [-p port] [-f file] [-b] [-P pidfile]\n", 
            gProgramName);
    fprintf(stderr, "          -v verbose mode, level is a number from 0 to 2\n");
    fprintf(stderr, "             0 = no debugging info (default)\n");
    fprintf(stderr, "             1 = standard debugging info\n");
    fprintf(stderr, "             2 = intense debugging info\n");
    fprintf(stderr, "             can be cycled kill -USR1\n");
    fprintf(stderr, "          -r also bind to port 53 (port 5353 is always bound)\n");
    fprintf(stderr, "          -n uses 'name' as the host name (default is none)\n");
    fprintf(stderr, "          -t uses 'type' as the service type (default is '%s')\n", kDefaultServiceType);
    fprintf(stderr, "          -d uses 'domain' as the service domain (default is '%s')\n", kDefaultServiceDomain);
    fprintf(stderr, "          -x uses 'TXT' as the service TXT record (default is empty)\n");
    fprintf(stderr, "          -p uses 'port' as the port number (default is '%d')\n",  kDefaultPortNumber);
    fprintf(stderr, "          -f reads a service list from 'file'\n");
    fprintf(stderr, "          -b forces daemon (background) mode\n");
    fprintf(stderr, "          -P uses 'pidfile' as the PID file\n");
    fprintf(stderr, "             (default is '%s')\n",  kDefaultPIDFile);
    fprintf(stderr, "             only meaningful if -b also specified\n");
}

static   mDNSBool  gAvoidPort53      = mDNStrue;
static const char *gRichTextHostName = "";
static const char *gServiceType      = kDefaultServiceType;
static const char *gServiceDomain    = kDefaultServiceDomain;
static mDNSu8      gServiceText[sizeof(RDataBody)];
static mDNSu16     gServiceTextLen   = 0;
static        int  gPortNumber       = kDefaultPortNumber;
static const char *gServiceFile      = "";
static   mDNSBool  gDaemon           = mDNSfalse;
static const char *gPIDFile          = kDefaultPIDFile;

static void ParseArguments(int argc, char **argv)
    // Parses our command line arguments into the global variables 
    // listed above.
{
    int ch;
    
    // Set gProgramName to the last path component of argv[0]
    
    gProgramName = strrchr(argv[0], '/');
    if (gProgramName == NULL) {
        gProgramName = argv[0];
    } else {
        gProgramName += 1;
    }
    
    // Parse command line options using getopt.
    
    do {
        ch = getopt(argc, argv, "v:rn:t:d:x:p:f:dPb");
        if (ch != -1) {
            switch (ch) {
                case 'v':
                    gMDNSPlatformPosixVerboseLevel = atoi(optarg);
                    if (gMDNSPlatformPosixVerboseLevel < 0 || gMDNSPlatformPosixVerboseLevel > 2) {
                        fprintf(stderr, 
                                "%s: Verbose mode must be in the range 0..2\n", 
                                gProgramName);
                        exit(1);
                    }
                    break;
                case 'r':
                    gAvoidPort53 = mDNSfalse;
                    break;
                case 'n':
                    gRichTextHostName = optarg;
                    if ( ! CheckThatRichTextHostNameIsUsable(gRichTextHostName, mDNStrue) ) {
                        exit(1);
                    }
                    break;
                case 't':
                    gServiceType = optarg;
                    if ( ! CheckThatServiceTypeIsUsable(gServiceType, mDNStrue) ) {
                        exit(1);
                    }
                    break;
                case 'd':
                    gServiceDomain = optarg;
                    break;
                case 'x':
                    if ( ! CheckThatServiceTextIsUsable(optarg, mDNStrue, gServiceText, &gServiceTextLen) ) {
                        exit(1);
                    }
                    break;
                case 'p':
                    gPortNumber = atol(optarg);
                    if ( ! CheckThatPortNumberIsUsable(gPortNumber, mDNStrue) ) {
                        exit(1);
                    }
                    break;
                case 'f':
                    gServiceFile = optarg;
                    break;
                case 'b':
                    gDaemon = mDNStrue;
                    break;
                case 'P':
                    gPIDFile = optarg;
                    break;
                case '?':
                default:
                    PrintUsage();
                    exit(1);
                    break;
            }
        }
    } while (ch != -1);

    // Check for any left over command line arguments.
    
    if (optind != argc) {
	    PrintUsage();
        fprintf(stderr, "%s: Unexpected argument '%s'\n", gProgramName, argv[optind]);
        exit(1);
    }
    
    // Check for inconsistency between the arguments.
    
    if ( (gRichTextHostName[0] == 0) && (gServiceFile[0] == 0) ) {
    	PrintUsage();
        fprintf(stderr, "%s: You must specify a service to register (-n) or a service file (-f).\n", gProgramName);
        exit(1);
    }
}

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark ***** Registration
#endif

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
            debugf("Callback: %##s Name Registered",   thisRegistration->RR_SRV.resrec.name.c); 
            // Do nothing; our name was successfully registered.  We may 
            // get more call backs in the future.
            break;

        case mStatus_NameConflict: 
            debugf("Callback: %##s Name Conflict",     thisRegistration->RR_SRV.resrec.name.c); 

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
            
            status = mDNS_RenameAndReregisterService(m, thisRegistration, mDNSNULL);
            assert(status == mStatus_NoError);
            break;

        case mStatus_MemFree:      
            debugf("Callback: %##s Memory Free",       thisRegistration->RR_SRV.resrec.name.c); 
            
            // When debugging is enabled, make sure that thisRegistration 
            // is not on our gServiceList.
            
            #if !defined(NDEBUG)
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
            debugf("Callback: %##s Unknown Status %d", thisRegistration->RR_SRV.resrec.name.c, status); 
            break;
    }
}

static int gServiceID = 0;

static mStatus RegisterOneService(const char *  richTextHostName, 
                                  const char *  serviceType, 
                                  const char *  serviceDomain, 
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
        MakeDomainLabelFromLiteralString(&name,  richTextHostName);
        MakeDomainNameFromDNSNameString(&type, serviceType);
        MakeDomainNameFromDNSNameString(&domain, serviceDomain);
        port.b[0] = (portNumber >> 8) & 0x0FF;
        port.b[1] = (portNumber >> 0) & 0x0FF;;
        status = mDNS_RegisterService(&mDNSStorage, &thisServ->coreServ,
                &name, &type, &domain,				// Name, type, domain
                NULL, port, 						// Host and port
                text, textLen,						// TXT data, length
                NULL, 0,							// Subtypes
                mDNSInterface_Any,					// Interace ID
                RegistrationCallback, thisServ);	// Callback and context
    }
    if (status == mStatus_NoError) {
        thisServ->serviceID = gServiceID;
        gServiceID += 1;

        thisServ->next = gServiceList;
        gServiceList = thisServ;

        if (gMDNSPlatformPosixVerboseLevel > 0) {
            fprintf(stderr, 
                    "%s: Registered service %d, name '%s', type '%s', port %ld\n", 
                    gProgramName, 
                    thisServ->serviceID, 
                    richTextHostName,
                    serviceType,
                    portNumber);
        }
    } else {
        if (thisServ != NULL) {
            free(thisServ);
        }
    }
    return status;
}

static mDNSBool ReadALine(char *buf, size_t bufSize, FILE *fp)
{
    mDNSBool good;
    size_t len;
    
    good = (fgets(buf, bufSize, fp) != NULL);
    if (good) {
        len = strlen(buf);
        good = (len > 0 && buf[len - 1] == '\n');
    }
    if (good) {
        buf[len - 1] = 0;
    }
    return good;
}

static mStatus RegisterServicesInFile(const char *filePath)
{
    mStatus     status;
    FILE *      fp;
    int         junk;
    mDNSBool    good;
    int         ch;
    char name[256];
    char type[256];
    const char *dom = kDefaultServiceDomain;
    char rawText[1024];
    mDNSu8  text[sizeof(RDataBody)];
    mDNSu16 textLen;
    char port[256];
    
    status = mStatus_NoError;
    fp = fopen(filePath, "r");
    if (fp == NULL) {
        status = mStatus_UnknownErr;
    }
    if (status == mStatus_NoError) {
        good = mDNStrue;
        do {
            // Skip over any blank lines.
            do {
                ch = fgetc(fp);
            } while ( ch == '\n' || ch == '\r' );
            if (ch != EOF) {
                good = (ungetc(ch, fp) == ch);
            }
            
            // Read three lines, check them for validity, and register the service.
            if ( good && ! feof(fp) ) {
                good = ReadALine(name, sizeof(name), fp);               
                if (good) {
                    good = ReadALine(type, sizeof(type), fp);
                }
                if (good) {
                	char *p = type;
                	while (*p && *p != ' ') p++;
                	if (*p) {
                		*p = 0;
                		dom = p+1;
                	}
                }
                if (good) {
                    good = ReadALine(rawText, sizeof(rawText), fp);
                }
                if (good) {
                    good = ReadALine(port, sizeof(port), fp);
                }
                if (good) {
                    good =     CheckThatRichTextHostNameIsUsable(name, mDNSfalse)
                            && CheckThatServiceTypeIsUsable(type, mDNSfalse)
                            && CheckThatServiceTextIsUsable(rawText, mDNSfalse, text, &textLen)
                            && CheckThatPortNumberIsUsable(atol(port), mDNSfalse);
                }
                if (good) {
                    status = RegisterOneService(name, type, dom, text, textLen, atol(port));
                    if (status != mStatus_NoError) {
                        fprintf(stderr, 
                                "%s: Failed to register service, name = %s, type = %s, port = %s\n", 
                                gProgramName,
                                name,
                                type,
                                port);
                        status = mStatus_NoError;       // keep reading
                    }
                }
            }
        } while (good && !feof(fp));

        if ( ! good ) {
            fprintf(stderr, "%s: Error reading service file %s\n", gProgramName, gServiceFile);
        }
    }
    
    if (fp != NULL) {
        junk = fclose(fp);
        assert(junk == 0);
    }
    
    return status;
}

static mStatus RegisterOurServices(void)
{
    mStatus status;
    
    status = mStatus_NoError;
    if (gRichTextHostName[0] != 0) {
        status = RegisterOneService(gRichTextHostName, 
                                    gServiceType, 
                                    gServiceDomain, 
                                    gServiceText, gServiceTextLen, 
                                    gPortNumber);
    }
    if (status == mStatus_NoError && gServiceFile[0] != 0) {
        status = RegisterServicesInFile(gServiceFile);
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

        if (gMDNSPlatformPosixVerboseLevel > 0) {
            fprintf(stderr, 
                    "%s: Deregistered service %d\n",
                    gProgramName, 
                    thisServ->serviceID);
        }
    }
}

#if COMPILER_LIKES_PRAGMA_MARK
#pragma mark **** Main
#endif

#ifdef NOT_HAVE_DAEMON

    // The version of Solaris that I tested on didn't have the daemon 
    // call.  This implementation was basically stolen from the 
    // Mac OS X standard C library.
    
    static int daemon(int nochdir, int noclose)
    {
        int fd;
    
        switch (fork()) {
        case -1:
            return (-1);
        case 0:
            break;
        default:
            _exit(0);
        }
    
        if (setsid() == -1)
            return (-1);
    
        if (!nochdir)
            (void)chdir("/");
    
        if (!noclose && (fd = _open("/dev/null", O_RDWR, 0)) != -1) {
            (void)dup2(fd, STDIN_FILENO);
            (void)dup2(fd, STDOUT_FILENO);
            (void)dup2(fd, STDERR_FILENO);
            if (fd > 2)
                (void)_close(fd);
        }
        return (0);
    }

#endif /* NOT_HAVE_DAEMON */

int main(int argc, char **argv)
{
    mStatus status;
    int     result;

    // Parse our command line arguments.  This won't come back if there's an error.
    
    ParseArguments(argc, argv);

    // If we're told to run as a daemon, then do that straight away.
    // Note that we don't treat the inability to create our PID 
    // file as an error.  Also note that we assign getpid to a long 
    // because printf has no format specified for pid_t.
    
    if (gDaemon) {
        if (gMDNSPlatformPosixVerboseLevel > 0) {
            fprintf(stderr, "%s: Starting in daemon mode\n", gProgramName);
        }
        daemon(0,0);
        {
            FILE *fp;
            int  junk;
            
            fp = fopen(gPIDFile, "w");
            if (fp != NULL) {
                fprintf(fp, "%ld\n", (long) getpid());
                junk = fclose(fp);
                assert(junk == 0);
            }
        }
    } else {
        if (gMDNSPlatformPosixVerboseLevel > 0) {
            fprintf(stderr, "%s: Starting in foreground mode, PID %ld\n", gProgramName, (long) getpid());
        }
    }

    status = mDNS_Init(&mDNSStorage, &PlatformStorage,
    	mDNS_Init_NoCache, mDNS_Init_ZeroCacheSize,
    	mDNS_Init_AdvertiseLocalAddresses,
    	mDNS_Init_NoInitCallback, mDNS_Init_NoInitCallbackContext);
    if (status != mStatus_NoError) return(2);

	status = RegisterOurServices();
    if (status != mStatus_NoError) return(2);
    
    signal(SIGHUP,  HandleSigHup);      // SIGHUP has to be sent by kill -HUP <pid>
    signal(SIGINT,  HandleSigInt);      // SIGINT is what you get for a Ctrl-C
    signal(SIGQUIT, HandleSigQuit);     // SIGQUIT is what you get for a Ctrl-\ (indeed)
    signal(SIGUSR1, HandleSigUsr1);     // SIGUSR1 has to be sent by kill -USR1 <pid>

	while (!gStopNow)
		{
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
		verbosedebugf("select(%d, %d.%06d)", nfds, timeout.tv_sec, timeout.tv_usec);
		result = select(nfds, &readfds, NULL, NULL, &timeout);
		
		if (result < 0)
			{
			verbosedebugf("select() returned %d errno %d", result, errno);
			if (errno != EINTR) gStopNow = mDNStrue;
			else
				{
				if (gReceivedSigUsr1)
					{
					gReceivedSigUsr1 = mDNSfalse;
					gMDNSPlatformPosixVerboseLevel += 1;
					if (gMDNSPlatformPosixVerboseLevel > 2)
						gMDNSPlatformPosixVerboseLevel = 0;
					if ( gMDNSPlatformPosixVerboseLevel > 0 )
						fprintf(stderr, "\nVerbose level %d\n", gMDNSPlatformPosixVerboseLevel);
					}
				if (gReceivedSigHup)
					{
					if (gMDNSPlatformPosixVerboseLevel > 0)
						fprintf(stderr, "\nSIGHUP\n");
					gReceivedSigHup = mDNSfalse;
					DeregisterOurServices();
					status = mDNSPlatformPosixRefreshInterfaceList(&mDNSStorage);
					if (status != mStatus_NoError) break;
					status = RegisterOurServices();
					if (status != mStatus_NoError) break;
					}
				}
			}
		else
			{
			// 5. Call mDNSPosixProcessFDSet to let the mDNSPosix layer do its work
			mDNSPosixProcessFDSet(&mDNSStorage, &readfds);
			
			// 6. This example client has no other work it needs to be doing,
			// but a real client would do its work here
			// ... (do work) ...
			}
		}

	debugf("Exiting");
    
	DeregisterOurServices();
	mDNS_Close(&mDNSStorage);

    if (status == mStatus_NoError) {
        result = 0;
    } else {
        result = 2;
    }
    if ( (result != 0) || (gMDNSPlatformPosixVerboseLevel > 0) ) {
        fprintf(stderr, "%s: Finished with status %ld, result %d\n", gProgramName, status, result);
    }
    
    return result;
}
