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
    File:       mDNSClientAPI.h

    Contains:   Programming interface to the mDNS core.

    Written by: Stuart Cheshire

    Version:    mDNS Core, September 2002

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
Revision 1.1  2003/10/23 21:43:01  ron
Add Apple mDNS reponder

Revision 1.25  2002/09/21 20:44:49  zarzycki
Added APSL info

Revision 1.24  2002/09/19 23:47:35  cheshire
Added mDNS_RegisterNoSuchService() function for assertion of non-existance
of a particular named service

Revision 1.23  2002/09/19 21:25:34  cheshire
mDNS_sprintf() doesn't need to be in a separate file

Revision 1.22  2002/09/19 04:20:43  cheshire
Remove high-ascii characters that confuse some systems

Revision 1.21  2002/09/17 01:06:35  cheshire
Change mDNS_AdvertiseLocalAddresses to be a parameter to mDNS_Init()

Revision 1.20  2002/09/16 18:41:41  cheshire
Merge in license terms from Quinn's copy, in preparation for Darwin release

*/

#ifndef __mDNSClientAPI_h
#define __mDNSClientAPI_h

#include <stdarg.h>		// stdarg.h is required for for va_list support for the mDNS_vsprintf declaration
#include "mDNSDebug.h"

#ifdef	__cplusplus
	extern "C" {
#endif

// ***************************************************************************
// Function scope indicators

// If you see "mDNSlocal" before a function name, it means the function is not callable outside this file
#define mDNSlocal static
// If you see "mDNSexport" before a symbol, it means the symbol is exported for use by clients
#define mDNSexport

// ***************************************************************************
#if 0
#pragma mark - DNS Resource Record class and type constants
#endif

typedef enum						// From RFC 1035
	{
	kDNSClass_IN          = 1,		// Internet
	kDNSClass_CS          = 2,		// CSNET
	kDNSClass_CH          = 3,		// CHAOS
	kDNSClass_HS          = 4,		// Hesiod
	kDNSClass_NONE        = 254,	// Used in DNS UPDATE [RFC 2136]
	kDNSQClass_ANY        = 255,	// Not a DNS class, but a DNS query class, meaning "all classes"
	kDNSQClass_Mask       = 0x7FFF,	// Multicast DNS uses the bottom 15 bits to identify the record class...
	kDNSClass_UniqueRRSet = 0x8000	// ... and the top bit indicates that all other cached records are now invalid
	} DNS_ClassValues;

typedef enum				// From RFC 1035
	{
	kDNSType_A = 1,			//  1 Address
	kDNSType_NS,			//  2 Name Server
	kDNSType_MD,			//  3 Mail Destination
	kDNSType_MF,			//  4 Mail Forwarder
	kDNSType_CNAME,			//  5 Canonical Name
	kDNSType_SOA,			//  6 Start of Authority
	kDNSType_MB,			//  7 Mailbox
	kDNSType_MG,			//  8 Mail Group
	kDNSType_MR,			//  9 Mail Rename
	kDNSType_NULL,			// 10 NULL RR
	kDNSType_WKS,			// 11 Well-known-service
	kDNSType_PTR,			// 12 Domain name pointer
	kDNSType_HINFO,			// 13 Host information
	kDNSType_MINFO,			// 14 Mailbox information
	kDNSType_MX,			// 15 Mail Exchanger
	kDNSType_TXT,			// 16 Arbitrary text string
	
	kDNSType_SRV = 33,		// 33 Service record

	kDNSQType_ANY = 255		// Not a DNS type, but a DNS query type, meaning "all types"
	} DNS_TypeValues;

// ***************************************************************************
#if 0
#pragma mark - Simple types
#endif

// mDNS defines its own names for these common types to simplify portability across
// multiple platforms that may each have their own (different) names for these types.
typedef unsigned char  mDNSBool;
typedef   signed char  mDNSs8;
typedef unsigned char  mDNSu8;
typedef   signed short mDNSs16;
typedef unsigned short mDNSu16;
typedef   signed long  mDNSs32;
typedef unsigned long  mDNSu32;

// These types are for opaque two- and four-byte identifiers.
// The "NotAnInteger" fields of the unions allow the value to be conveniently passed around in a register
// for the sake of efficiency, but don't forget -- just because it is in a register doesn't mean it is an
// integer. Operations like add, multiply, increment, decrement, etc., are undefined for opaque identifiers.
typedef union { mDNSu8 b[2]; mDNSu16 NotAnInteger; } mDNSOpaque16;
typedef union { mDNSu8 b[4]; mDNSu32 NotAnInteger; } mDNSOpaque32;

typedef mDNSOpaque16 mDNSIPPort;		// An IP port is a two-byte opaque identifier (not an integer)
typedef mDNSOpaque32 mDNSIPAddr;		// An IP address is a four-byte opaque identifier (not an integer)

enum { mDNSfalse = 0, mDNStrue = 1 };

#define mDNSNULL 0L

enum
	{
	mStatus_Waiting           = 1,
	mStatus_NoError           = 0,

	// mDNS Error codes are in the range FFFE FF00 (-65792) to FFFE FFFF (-65537)
	mStatus_UnknownErr        = -65537,		// 0xFFFE FFFF
	mStatus_NoSuchNameErr     = -65538,
	mStatus_NoMemoryErr       = -65539,
	mStatus_BadParamErr       = -65540,
	mStatus_BadReferenceErr   = -65541,
	mStatus_BadStateErr       = -65542,
	mStatus_BadFlagsErr       = -65543,
	mStatus_UnsupportedErr    = -65544,
	mStatus_NotInitializedErr = -65545,
	mStatus_NoCache           = -65546,
	mStatus_AlreadyRegistered = -65547,
	mStatus_NameConflict      = -65548,
	mStatus_Invalid           = -65549,
	
	mStatus_MemFree           = -65792		// 0xFFFE FF00
	};

typedef mDNSs32 mStatus;

#define MAX_DOMAIN_LABEL 63
typedef struct { mDNSu8 c[ 64]; } domainlabel;		// One label: length byte and up to 63 characters
#define MAX_DOMAIN_NAME 255
typedef struct { mDNSu8 c[256]; } domainname;		// Up to 255 bytes of length-prefixed domainlabels
typedef struct { mDNSu8 c[256]; } UTF8str255;		// Null-terminated C string

// ***************************************************************************
#if 0
#pragma mark - Resource Record structures
#endif

// Shared Resource Records do not have to be unique
// -- Shared Resource Records are used for NIAS service PTRs
// -- It is okay for several hosts to have RRs with the same name but different RDATA
// -- We use a random delay on replies to reduce collisions when all the hosts reply to the same query
// -- These RRs typically have moderately high TTLs (e.g. one hour)
// -- These records are announced on startup and topology changes for the benefit of passive listeners

// Unique Resource Records should be unique among hosts within any given mDNS scope
// -- The majority of Resource Records are of this type
// -- If two entities on the network have RRs with the same name but different RDATA, this is a conflict
// -- Replies may be sent immediately, because only one host should be replying to any particular query
// -- These RRs typically have low TTLs (e.g. ten seconds)
// -- On startup and after topology changes, a host issues queries to verify uniqueness

// Known Unique Resource Records are treated like Unique Resource Records, except that mDNS does
// not have to verify their uniqueness because this is already known by other means (e.g. the RR name
// is derived from the host's IP or Ethernet address, which is already known to be a unique identifier).

enum
	{
	kDNSRecordTypeUnregistered     = 0x00,	// Not currently in any list
	kDNSRecordTypeDeregistering    = 0x01,	// Shared record about to announce its departure and leave the list

	kDNSRecordTypeUnique           = 0x08,	// Will become a kDNSRecordTypeVerified when probing is complete

	kDNSRecordTypePacketAnswer     = 0x10,	// Received in the Answer Section of a DNS Response
	kDNSRecordTypePacketAdditional = 0x11,	// Received in the Additional Section of a DNS Response
	kDNSRecordTypePacketUniqueAns  = 0x18,	// Received in the Answer Section of a DNS Response with kDNSQClass_CacheFlushBit set
	kDNSRecordTypePacketUniqueAdd  = 0x19,	// Received in the Additional Section of a DNS Response with kDNSQClass_CacheFlushBit set

	kDNSRecordTypeShared           = 0x20,	// Shared means record name does not have to be unique -- so use random delay on replies
	kDNSRecordTypeVerified         = 0x28,	// Unique means mDNS should check that name is unique (and then send immediate replies)
	kDNSRecordTypeKnownUnique      = 0x29,	// Known Unique means mDNS can assume name is unique without checking
	
	kDNSRecordTypeUniqueMask       = 0x08,	// Test for records that are supposed to not be shared with other hosts
	kDNSRecordTypeRegisteredMask   = 0xF8,	// Test for records that have not had mDNS_Deregister called on them yet
	kDNSRecordTypeActiveMask       = 0xF0	// Test for all records that have finished their probing and are now active
	};

enum
	{
	kDNSSendPriorityNone       = 0,		// Don't need to send this record right now
	kDNSSendPriorityAdditional = 1,		// Send this record as an additional, if we have space in the packet
	kDNSSendPriorityAnswer     = 2		// Need to send this record as an answer
	};

typedef struct { mDNSu16 priority; mDNSu16 weight; mDNSIPPort port; domainname target; } rdataSRV;

typedef union
	{
	mDNSu8     data[512];	// Generic untyped data (temporarily set 512 for the benefit of iChat)
	mDNSIPAddr ip;			// For 'A' record
	domainname name;		// For PTR and CNAME records
	UTF8str255 txt;			// For TXT record
	rdataSRV   srv;			// For SRV record
	} RDataBody;

typedef struct
	{
	mDNSu16    MaxRDLength;	// Amount of storage allocated for rdata (usually sizeof(RDataBody))
	mDNSu16    RDLength;	// Size of the rdata currently stored here
	RDataBody  u;
	} RData;

typedef struct ResourceRecord_struct ResourceRecord;
typedef struct ResourceRecord_struct *ResourceRecordPtr;

typedef struct mDNS_struct mDNS;
typedef struct mDNS_PlatformSupport_struct mDNS_PlatformSupport;

typedef void mDNSRecordCallback(mDNS *const m, ResourceRecord *const rr, mStatus result);
typedef void mDNSRecordUpdateCallback(mDNS *const m, ResourceRecord *const rr, RData *OldRData);

// Fields labelled "AR:" apply to our authoritative records
// Fields labelled "CR:" apply to cache records
// Fields labelled "--:" apply to both
// (May want to make this a union later, but not now, because using the
// same storage for two different purposes always makes debugging harder.)
struct ResourceRecord_struct
	{
	ResourceRecord     *next;			// --: Next in list
	
	// Field Group 1: Persistent metadata for Authoritative Records
	ResourceRecord     *Additional1;	// AR: Recommended additional record to include in response
	ResourceRecord     *Additional2;	// AR: Another additional
	ResourceRecord     *DependentOn;	// AR: This record depends on another for its uniqueness checking
	ResourceRecord     *RRSet;			// AR: This unique record is part of an RRSet
	mDNSRecordCallback *Callback;		// AR: Callback function to call for state changes
	void               *Context;		// AR: Context parameter for the callback function
	mDNSu8              RecordType;		// --: See enum above
	mDNSu8              HostTarget;		// AR: Set if the target of this record (PTR, CNAME, SRV, etc.) is our host name

	// Field Group 2: Transient state for Authoritative Records
	mDNSu8          Acknowledged;		// AR: Set if we've given the success callback to the client
	mDNSu8          ProbeCount;			// AR: Number of probes remaining before this record is valid (kDNSRecordTypeUnique)
	mDNSu8          AnnounceCount;		// AR: Number of announcements remaining (kDNSRecordTypeShared)
	mDNSu8          IncludeInProbe;		// AR: Set if this RR is being put into a probe right now
	mDNSu8          SendPriority;		// AR: See enum above
	mDNSIPAddr      Requester;			// AR: Used for inter-packet duplicate suppression
										//     If set, give the IP address of the last host that sent a truncated query for this record
										//     If set to all-ones, more than one host sent such a request in the last few milliseconds
	ResourceRecord *NextResponse;		// AR: Link to the next element in the chain of responses to generate
	const mDNSu8   *NR_AnswerTo;		// AR: Set if this record was selected by virtue of being a direct answer to a question
	ResourceRecord *NR_AdditionalTo;	// AR: Set if this record was selected by virtue of being additional to another
	mDNSs32         LastSendTime;		// AR: In platform time units
	mDNSs32         NextSendTime;		// AR: In platform time units
	mDNSs32         NextSendInterval;	// AR: In platform time units
	RData          *NewRData;			// AR: Set if we are updating this record with new rdata
	mDNSRecordUpdateCallback *UpdateCallback;

	// Field Group 3: Transient state for Cache Records
	ResourceRecord *NextDupSuppress;	// CR: Link to the next element in the chain of duplicate suppression answers to send
	mDNSs32         TimeRcvd;			// CR: In platform time units
	mDNSs32         LastUsed;			// CR: In platform time units
	mDNSu32         UseCount;			// CR: Number of times this RR has been used to answer a question
	mDNSu32         UnansweredQueries;	// CR: Number of times we've issued a query for this record without getting an answer
	mDNSBool        Active;				// CR: Set if there is currently a question referencing this answer
	mDNSBool        NewData;			// CR: Set if this is a record we just received

	// Field Group 4: The actual information pertaining to this resource record
	mDNSIPAddr      InterfaceAddr;		// --: Set if this RR is specific to one interface (e.g. a linklocal address)
										// For records received off the wire, InterfaceAddr is *always* set to the receiving interface
										// For our authoritative records, InterfaceAddr is usually zero,
										// except those few records that are interface-specific (e.g. linklocal address records)
	domainname      name;				// --: All the rest are used both in our authoritative records and in cache records
	mDNSu16         rrtype;
	mDNSu16         rrclass;
	mDNSu32         rroriginalttl;		// In seconds.
	mDNSu32         rrremainingttl;		// In seconds. Always set to correct value before calling question callback.
	mDNSu16         rdestimate;			// Upper bound on size of rdata after name compression
	RData           *rdata;				// Pointer to storage for this rdata
	RData           rdatastorage;		// Normally the storage is right here, except for oversized records
	};

typedef struct NetworkInterfaceInfo_struct NetworkInterfaceInfo;

struct NetworkInterfaceInfo_struct
	{
	NetworkInterfaceInfo *next;
	mDNSIPAddr     ip;
	mDNSBool       Advertise;		// Set Advertise to false if you are only searching on this interface
	// Standard ResourceRecords that every Responder host should have (one per active IP address)
	ResourceRecord RR_A1;			// 'A' (address) record for our ".local" name
	ResourceRecord RR_A2;			// 'A' record for our ".local.arpa" name
	ResourceRecord RR_PTR;			// PTR (reverse lookup) record
	};

typedef struct ExtraResourceRecord_struct ExtraResourceRecord;
struct ExtraResourceRecord_struct
	{
	ExtraResourceRecord *next;
	ResourceRecord r;
	// Note: Add any additional fields *before* the ResourceRecord in this structure, not at the end.
	// In some cases clients can allocate larger chunks of memory and set r->rdata->MaxRDLength to indicate
	// that this extra memory is available, which would result in any fields after the ResourceRecord getting smashed
	};

typedef struct ServiceRecordSet_struct ServiceRecordSet;
typedef void mDNSServiceCallback(mDNS *const m, ServiceRecordSet *const sr, mStatus result);
struct ServiceRecordSet_struct
	{
	mDNSServiceCallback *Callback;
	void                *Context;
	ExtraResourceRecord *Extras;	// Optional list of extra ResourceRecords attached to this service registration
	mDNSBool             Conflict;	// Set if this record set was forcibly deregistered because of a conflict
	domainname           Host;		// Set if this service record does not use the standard target host name
	ResourceRecord       RR_PTR;	// e.g. _printer._tcp.local.      PTR Name._printer._tcp.local.
	ResourceRecord       RR_SRV;	// e.g. Name._printer._tcp.local. SRV 0 0 port target
	ResourceRecord       RR_TXT;	// e.g. Name._printer._tcp.local. TXT PrintQueueName
	// Don't add any fields after ResourceRecord RR_TXT.
	// This is where the implicit extra space goes if we allocate a ServiceRecordSet containing an oversized RR_TXT record
	};

// ***************************************************************************
#if 0
#pragma mark - Question structures
#endif

typedef struct DNSQuestion_struct DNSQuestion;
typedef void mDNSQuestionCallback(mDNS *const m, DNSQuestion *question, const ResourceRecord *const answer);
struct DNSQuestion_struct
	{
	DNSQuestion          *next;
	mDNSs32               NextQTime;		// In platform time units
	mDNSs32               ThisQInterval;	// In platform time units (zero for questions not in list)
											// ThisQInterval will be non-zero for an active question;
											// Zero for a cancelled or inactive question
	mDNSs32               NextQInterval;
	DNSQuestion          *DuplicateOf;
	mDNSIPAddr            InterfaceAddr;	// Non-zero if you want to issue link-local queries only on a single specific IP interface
	domainname            name;
	mDNSu16               rrtype;
	mDNSu16               rrclass;
	mDNSQuestionCallback *Callback;
	void                 *Context;
	};

typedef struct
	{
	domainname name;
	mDNSIPAddr InterfaceAddr;		// Local (source) IP Interface (needed for scoped addresses such as link-local)
	mDNSIPAddr ip;					// Remote (destination) IP address where this service can be accessed
	mDNSIPPort port;				// Port where this service can be accessed
	mDNSu16    TXTlen;
	mDNSu8     TXTinfo[2048];		// Additional demultiplexing information (e.g. LPR queue name)
	} ServiceInfo;

typedef struct ServiceInfoQuery_struct ServiceInfoQuery;
typedef void ServiceInfoQueryCallback(mDNS *const m, ServiceInfoQuery *query);
struct ServiceInfoQuery_struct
	{
	DNSQuestion               qSRV;
	DNSQuestion               qTXT;
	DNSQuestion               qADD;
	mDNSu8                    GotSRV;
	mDNSu8                    GotTXT;
	mDNSu8                    GotADD;
	ServiceInfo              *info;
	ServiceInfoQueryCallback *Callback;
	void                     *Context;
	};

// ***************************************************************************
#if 0
#pragma mark - Main mDNS object, used to hold all the mDNS state
#endif

typedef void mDNSCallback(mDNS *const m, mStatus result);

struct mDNS_struct
	{
	mDNS_PlatformSupport *p;		// Pointer to platform-specific data of indeterminite size
	mDNSBool AdvertiseLocalAddresses;
	mStatus mDNSPlatformStatus;
	mDNSCallback *Callback;
	void         *Context;

	mDNSu32 mDNS_busy;				// For debugging: To catch and report locking failures

	mDNSu8 lock_rrcache;			// For debugging: Set at times when these lists may not be modified
	mDNSu8 lock_Questions;
	mDNSu8 lock_Records;
	mDNSu8 padding;

	// These fields only required for mDNS Searcher...
	DNSQuestion *ActiveQuestions;	// List of all active questions
	DNSQuestion *NewQuestions;		// Fresh questions not yet answered from cache
	DNSQuestion *CurrentQuestion;	// Next question about to be examined in AnswerLocalQuestions()
	mDNSu32 rrcache_size;
	mDNSu32 rrcache_used;
	mDNSu32 rrcache_report;
	ResourceRecord *rrcache_free;
	ResourceRecord *rrcache;

	// Fields below only required for mDNS Responder...
	domainlabel nicelabel;			// Rich text label encoded using canonically precomposed UTF-8
	domainlabel hostlabel;			// Conforms to RFC 1034 "letter-digit-hyphen" ARPANET host name rules
	domainname  hostname1;			// Primary Host Name, e.g. "Foo.local."
	domainname  hostname2;			// Secondary Host Name, e.g. "Foo.local.arpa."
	ResourceRecord *ResourceRecords;
	ResourceRecord *CurrentRecord;	// Next ResourceRecord about to be examined
	NetworkInterfaceInfo *HostInterfaces;
	mDNSs32 SuppressSending;
	mDNSs32 SuppressProbes;
	mDNSBool SleepState;
	mDNSBool NetChanged;
	};

// ***************************************************************************
#if 0
#pragma mark - Useful Static Constants
#endif

extern const ResourceRecord zeroRR;
extern const mDNSIPPort zeroIPPort;
extern const mDNSIPAddr zeroIPAddr;
extern const mDNSIPAddr onesIPAddr;

extern const mDNSIPPort UnicastDNSPort;
extern const mDNSIPPort MulticastDNSPort;
extern const mDNSIPAddr AllDNSLinkGroup;
extern const mDNSIPAddr AllDNSAdminGroup;

// ***************************************************************************
#if 0
#pragma mark - Main Client Functions
#endif

// Every client should call mDNS_Init, passing in storage for the mDNS object, mDNS_PlatformSupport object, and rrcache.
// The rrcachesize parameter is the size of (i.e. number of entries in) the rrcache array passed in.
// When mDNS has finished setting up the initComplete callback is called
// A client can also spin and poll the mDNSPlatformStatus field to see when it changes from mStatus_Waiting to mStatus_NoError
//
// Call mDNS_Close to tidy up before exiting
//
// Call mDNS_Register with a completed ResourceRecord object to register a resource record
// If the resource record type is kDNSRecordTypeUnique (or kDNSknownunique) then if a conflicting resource record is discovered,
// the resource record's mDNSRecordCallback will be called with error code mStatus_NameConflict. The callback should deregister
// the record, and may then try registering the record again after picking a new name (e.g. by automatically appending a number).
//
// Call mDNS_StartQuery to initiate a query. mDNS will proceed to issue Multicast DNS query packets, and any time a reply
// is received containing a record which matches the question, the DNSQuestion's mDNSAnswerCallback function will be called
// Call mDNS_StopQuery when no more answers are required
//
// The mDNS routines are intentionally not thread-safe -- adding locking operations would add overhead that may not
// be necessary or appropriate on every platform. Instead, code in a pre-emptive environment calling any mDNS routine
// (except mDNS_Init and mDNS_Close) is responsible for doing the necessary synchronization to ensure that mDNS code is
// not re-entered. This includes both client software above mDNS, and the platform support code below. For example, if
// the support code on a particular platform implements timer callbacks at interrupt time, then clients on that platform
// need to disable interrupts or do similar concurrency control to ensure that the mDNS code is not entered by an
// interrupt-time timer callback while in the middle of processing a client call.

extern mStatus mDNS_Init      (mDNS *const m, mDNS_PlatformSupport *const p,
								ResourceRecord *rrcachestorage, mDNSu32 rrcachesize,
								mDNSBool AdvertiseLocalAddresses,
								mDNSCallback *Callback, void *Context);
#define mDNS_Init_NoCache                     mDNSNULL
#define mDNS_Init_ZeroCacheSize               0
#define mDNS_Init_AdvertiseLocalAddresses     mDNStrue
#define mDNS_Init_DontAdvertiseLocalAddresses mDNSfalse
#define mDNS_Init_NoInitCallback              mDNSNULL
#define mDNS_Init_NoInitCallbackContext       mDNSNULL
extern void    mDNS_Close     (mDNS *const m);
extern mStatus mDNS_Register  (mDNS *const m, ResourceRecord *const rr);
extern mStatus mDNS_Update    (mDNS *const m, ResourceRecord *const rr, mDNSu32 newttl,
								RData *const newrdata, mDNSRecordUpdateCallback *Callback);
extern void    mDNS_Deregister(mDNS *const m, ResourceRecord *const rr);
extern mStatus mDNS_StartQuery(mDNS *const m, DNSQuestion *const question);
extern void    mDNS_StopQuery (mDNS *const m, DNSQuestion *const question);

// ***************************************************************************
#if 0
#pragma mark - General utility and helper functions
#endif

// mDNS_RegisterHostSet is a single call to register the standard resource records associated with every host.
// mDNS_RegisterService is a single call to register the set of resource records associated with a given named service.
//
// mDNS_StartResolveService is single call which is equivalent to multiple calls to mDNS_StartQuery,
// to find the IP address, port number, and demultiplexing information for a given named service.
// As with mDNS_StartQuery, it executes asynchronously, and calls the ServiceInfoQueryCallback when the answer is
// found. After the service is resolved, the client should call mDNS_StopResolveService to complete the transaction.
// The client can also call mDNS_StopResolveService at any time to abort the transaction.
//
// mDNS_GetBrowseDomains is a special case of the mDNS_StartQuery call, where the resulting answers
// are a list of PTR records indicating (in the rdata) domains that are recommended for browsing.
// After getting the list of domains to browse, call mDNS_StopQuery to end the search.
// mDNS_GetDefaultBrowseDomain returns the name of the domain that should be highlighted by default.
//
// mDNS_GetRegistrationDomains and mDNS_GetDefaultRegistrationDomain are the equivalent calls to get the list
// of one or more domains that should be offered to the user as choices for where they may register their service,
// and the default domain in which to register in the case where the user has made no selection.

extern void    mDNS_SetupResourceRecord(ResourceRecord *rr, RData *RDataStorage, mDNSIPAddr InterfaceAddr,
               mDNSu16 rrtype, mDNSu32 ttl, mDNSu8 RecordType, mDNSRecordCallback Callback, void *Context);

extern void    mDNS_GenerateFQDN(mDNS *const m);
extern mStatus mDNS_RegisterInterface  (mDNS *const m, NetworkInterfaceInfo *set);
extern void    mDNS_DeregisterInterface(mDNS *const m, NetworkInterfaceInfo *set);

extern mStatus mDNS_RegisterService  (mDNS *const m, ServiceRecordSet *sr,
               const domainlabel *const name, const domainname *const type, const domainname *const domain,
               const domainname *const host, mDNSIPPort port, const mDNSu8 txtinfo[], mDNSu16 txtlen,
               mDNSServiceCallback Callback, void *Context);
extern mStatus mDNS_AddRecordToService(mDNS *const m, ServiceRecordSet *sr, ExtraResourceRecord *extra, RData *rdata, mDNSu32 ttl);
extern mStatus mDNS_RemoveRecordFromService(mDNS *const m, ServiceRecordSet *sr, ExtraResourceRecord *extra);
extern mStatus mDNS_RenameAndReregisterService(mDNS *const m, ServiceRecordSet *const sr);
extern void    mDNS_DeregisterService(mDNS *const m, ServiceRecordSet *sr);

extern mStatus mDNS_RegisterNoSuchService(mDNS *const m, ResourceRecord *const rr,
				const domainlabel *const name, const domainname *const type, const domainname *const domain,
				mDNSRecordCallback Callback, void *Context);
#define        mDNS_DeregisterNoSuchService mDNS_Deregister

extern mStatus mDNS_StartBrowse(mDNS *const m, DNSQuestion *const question,
				const domainname *const srv, const domainname *const domain,
				const mDNSIPAddr InterfaceAddr, mDNSQuestionCallback *Callback, void *Context);
#define        mDNS_StopBrowse mDNS_StopQuery

extern mStatus mDNS_StartResolveService(mDNS *const m, ServiceInfoQuery *query, ServiceInfo *info, ServiceInfoQueryCallback *Callback, void *Context);
extern void    mDNS_StopResolveService (mDNS *const m, ServiceInfoQuery *query);

typedef enum
	{
	mDNS_DomainTypeBrowse              = 0,
	mDNS_DomainTypeBrowseDefault       = 1,
	mDNS_DomainTypeRegistration        = 2,
	mDNS_DomainTypeRegistrationDefault = 3
	} mDNS_DomainType;

extern mStatus mDNS_GetDomains(mDNS *const m, DNSQuestion *const question, mDNSu8 DomainType, const mDNSIPAddr InterfaceAddr, mDNSQuestionCallback *Callback, void *Context);
#define        mDNS_StopGetDomains mDNS_StopQuery
extern mStatus mDNS_AdvertiseDomains(mDNS *const m, ResourceRecord *rr, mDNSu8 DomainType, const mDNSIPAddr InterfaceAddr, char *domname);
#define        mDNS_StopAdvertiseDomains mDNS_Deregister

// ***************************************************************************
#if 0
#pragma mark - DNS name utility functions
#endif

// In order to expose the full capabilities of the DNS protocol (which allows any arbitrary eight-bit values
// in domain name labels, including unlikely characters like ascii nulls and even dots) all the mDNS APIs
// work with DNS's native length-prefixed strings. For convenience in C, the following utility functions
// are provided for converting between C's null-terminated strings and DNS's length-prefixed strings.

extern mDNSBool SameDomainName(const domainname *const d1, const domainname *const d2);

extern mDNSu16 DomainNameLength(const domainname *const name);
extern void AppendDomainLabelToName(domainname *const name, const domainlabel *const label);
extern void AppendStringLabelToName(domainname *const name, const char *cstr);
extern void AppendDomainNameToName(domainname *const name, const domainname *const append);
extern void AppendStringNameToName(domainname *const name, const char *cstr);

extern void   ConvertCStringToDomainLabel(const char *src, domainlabel *label);
extern mDNSu8 *ConvertCStringToDomainName(const char *const cstr, domainname *name);

extern char *ConvertDomainLabelToCString_withescape(const domainlabel *const name, char *cstr, char esc);
#define      ConvertDomainLabelToCString_unescaped(D,C) ConvertDomainLabelToCString_withescape((D), (C), 0)
#define      ConvertDomainLabelToCString(D,C)           ConvertDomainLabelToCString_withescape((D), (C), '\\')

extern char *ConvertDomainNameToCString_withescape(const domainname *const name, char *cstr, char esc);
#define      ConvertDomainNameToCString_unescaped(D,C) ConvertDomainNameToCString_withescape((D), (C), 0)
#define      ConvertDomainNameToCString(D,C)           ConvertDomainNameToCString_withescape((D), (C), '\\')
extern void  ConvertUTF8PstringToRFC1034HostLabel(const mDNSu8 UTF8Name[], domainlabel *const hostlabel);

extern mDNSu8    *ConstructServiceName(domainname *const fqdn, const domainlabel *const name, const domainname *const type, const domainname *const domain);
extern mDNSBool DeconstructServiceName(const domainname *const fqdn, domainlabel *const name, domainname *const type, domainname *const domain);

// ***************************************************************************
#if 0
#pragma mark - Other utility functions
#endif

extern int mDNS_sprintf(char *sbuffer, const char *fmt, ...);
extern int mDNS_vsprintf(char *sbuffer, const char *fmt, va_list arg);
extern void IncrementLabelSuffix(domainlabel *name, mDNSBool RichText);

#ifdef	__cplusplus
	}
#endif

#endif
