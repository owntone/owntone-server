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
    File:       mDNS.c

    Contains:   Implementation of the mDNS core itself.

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

Revision 1.63  2002/09/21 20:44:49  zarzycki
Added APSL info

Revision 1.62  2002/09/20 03:25:37  cheshire
Fix some compiler warnings

Revision 1.61  2002/09/20 01:05:24  cheshire
Don't kill the Extras list in mDNS_DeregisterService()

Revision 1.60  2002/09/19 23:47:35  cheshire
Added mDNS_RegisterNoSuchService() function for assertion of non-existance
of a particular named service

Revision 1.59  2002/09/19 21:25:34  cheshire
mDNS_sprintf() doesn't need to be in a separate file

Revision 1.58  2002/09/19 04:20:43  cheshire
Remove high-ascii characters that confuse some systems

Revision 1.57  2002/09/17 01:07:08  cheshire
Change mDNS_AdvertiseLocalAddresses to be a parameter to mDNS_Init()

Revision 1.56  2002/09/16 19:44:17  cheshire
Merge in license terms from Quinn's copy, in preparation for Darwin release

*/

// ***************************************************************************
// mDNS.c
// This file defines all of mDNS, including
// mDNS Service Discovery, mDNS Responder, and mDNS Searcher.
//
// This code is completely 100% portable C. It does not depend on any external header files
// from outside the mDNS project -- all the types it expects to find are defined right here.
//
// The previous point is very important: This file does not depend on any external
// header files. It should complile on *any* platform that has a C compiler, without
// making *any* assumptions about availability of so-called "standard" C functions,
// routines, or types (which may or may not be present on any given platform).
// ***************************************************************************

/*
 * Formatting notes:
 * This code follows the "Whitesmiths style" C indentation rules. Plenty of discussion
 * on C indentation can be found on the web, such as <http://www.kafejo.com/komp/1tbs.htm>,
 * but for the sake of brevity here I will say just this: Curly braces are not syntactially
 * part of an "if" statement; they are the beginning and ending markers of a compound statement;
 * therefore common sense dictates that if they are part of a compound statement then they
 * should be indented to the same level as everything else in that compound statement.
 * Indenting curly braces at the same level as the "if" implies that curly braces are
 * part of the "if", which is false. (This is as misleading as people who write "char* x,y;"
 * thinking that variables x and y are both of type "char*" -- and anyone who doesn't
 * understand why variable y is not of type "char*" just proves the point that poor code
 * layout leads people to unfortunate misunderstandings about how the C language really works.)
 */

#include "mDNSClientAPI.h"				// Defines the interface provided to the client layer above
#include "mDNSPlatformFunctions.h"		// Defines the interface required of the supporting layer below

#if(defined(_MSC_VER))
	// Disable warnings about Microsoft Visual Studio/C++ not understanding "pragma unused"
    #pragma warning( disable:4068 )
#endif

// ***************************************************************************
#if 0
#pragma mark - DNS Protocol Constants
#endif

typedef enum
	{
	kDNSFlag0_QR_Mask     = 0x80,		// Query or response?
	kDNSFlag0_QR_Query    = 0x00,
	kDNSFlag0_QR_Response = 0x80,
	
	kDNSFlag0_OP_Mask     = 0x78,		// Operation type
	kDNSFlag0_OP_StdQuery = 0x00,
	kDNSFlag0_OP_Iquery   = 0x08,
	kDNSFlag0_OP_Status   = 0x10,
	kDNSFlag0_OP_Unused3  = 0x18,
	kDNSFlag0_OP_Notify   = 0x20,
	kDNSFlag0_OP_Update   = 0x28,
	
	kDNSFlag0_QROP_Mask   = kDNSFlag0_QR_Mask | kDNSFlag0_OP_Mask,
	
	kDNSFlag0_AA          = 0x04,		// Authoritative Answer?
	kDNSFlag0_TC          = 0x02,		// Truncated?
	kDNSFlag0_RD          = 0x01,		// Recursion Desired?
	kDNSFlag1_RA          = 0x80,		// Recursion Available?
	
	kDNSFlag1_Zero        = 0x40,		// Reserved; must be zero
	kDNSFlag1_AD          = 0x20,		// Authentic Data [RFC 2535]
	kDNSFlag1_CD          = 0x10,		// Checking Disabled [RFC 2535]

	kDNSFlag1_RC          = 0x0F,		// Response code
	kDNSFlag1_RC_NoErr    = 0x00,
	kDNSFlag1_RC_FmtErr   = 0x01,
	kDNSFlag1_RC_SrvErr   = 0x02,
	kDNSFlag1_RC_NXDomain = 0x03,
	kDNSFlag1_RC_NotImpl  = 0x04,
	kDNSFlag1_RC_Refused  = 0x05,
	kDNSFlag1_RC_YXDomain = 0x06,
	kDNSFlag1_RC_YXRRSet  = 0x07,
	kDNSFlag1_RC_NXRRSet  = 0x08,
	kDNSFlag1_RC_NotAuth  = 0x09,
	kDNSFlag1_RC_NotZone  = 0x0A
	} DNS_Flags;

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Program Constants
#endif

mDNSexport const ResourceRecord zeroRR;
mDNSexport const mDNSIPPort zeroIPPort = { { 0 } };
mDNSexport const mDNSIPAddr zeroIPAddr = { { 0 } };
mDNSexport const mDNSIPAddr onesIPAddr = { { 255, 255, 255, 255 } };

#define UnicastDNSPortAsNumber 53
#define MulticastDNSPortAsNumber 5353
mDNSexport const mDNSIPPort UnicastDNSPort     = { { UnicastDNSPortAsNumber   >> 8, UnicastDNSPortAsNumber   & 0xFF } };
mDNSexport const mDNSIPPort MulticastDNSPort   = { { MulticastDNSPortAsNumber >> 8, MulticastDNSPortAsNumber & 0xFF } };
mDNSexport const mDNSIPAddr AllDNSLinkGroup  = { { 224,   0,   0, 251 } };
mDNSexport const mDNSIPAddr AllDNSAdminGroup = { { 239, 255, 255, 251 } };

static const mDNSOpaque16 zeroID = { { 0, 0 } };
static const mDNSOpaque16 QueryFlags    = { { kDNSFlag0_QR_Query    | kDNSFlag0_OP_StdQuery,                0 } };
static const mDNSOpaque16 ResponseFlags = { { kDNSFlag0_QR_Response | kDNSFlag0_OP_StdQuery | kDNSFlag0_AA, 0 } };
#define zeroDomainNamePtr ((domainname*)"")

static const char *const mDNS_DomainTypeNames[] =
	{
	"_browse._mdns._udp.local.",
	"_default._browse._mdns._udp.local.",
	"_register._mdns._udp.local.",
	"_default._register._mdns._udp.local."
	};

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Specialized mDNS version of vsprintf
#endif

static const struct mDNSsprintf_format
	{
	unsigned 		leftJustify : 1;
	unsigned 		forceSign : 1;
	unsigned 		zeroPad : 1;
	unsigned 		havePrecision : 1;
	unsigned 		hSize : 1;
	unsigned 		lSize : 1;
	char	 		altForm;
	char			sign;		// +, - or space
	int				fieldWidth;
	int				precision;
	} mDNSsprintf_format_default;

#define mDNS_vsprintf_BUFLEN			512

mDNSexport int mDNS_vsprintf(char *sbuffer, const char *fmt, va_list arg)
	{
	int c, nwritten = 0;

	for (c = *fmt; c; c = *++fmt)
		{
		int i=0, j;
		char buf[mDNS_vsprintf_BUFLEN], *digits;
		char *s = &buf[mDNS_vsprintf_BUFLEN];
		struct mDNSsprintf_format F;
		if (c != '%') goto copy1;
		F = mDNSsprintf_format_default;

		for (;;)	//  decode flags
			{
			c = *++fmt;
			if      (c == '-')	F.leftJustify = 1;
			else if (c == '+')	F.forceSign = 1;
			else if (c == ' ')	F.sign = ' ';
			else if (c == '#')	F.altForm++;
			else if (c == '0')	F.zeroPad = 1;
			else break;
			}

		if (c == '*')	//  decode field width
			{
			if ((F.fieldWidth = va_arg(arg, int)) < 0)
				{
				F.leftJustify = 1;
				F.fieldWidth = -F.fieldWidth;
				}
			c = *++fmt;
			}
		else
			{
			for (; c >= '0' && c <= '9'; c = *++fmt)
				F.fieldWidth = (10 * F.fieldWidth) + (c - '0');
			}

		if (c == '.')	//  decode precision
			{
			if ((c = *++fmt) == '*')
				{ F.precision = va_arg(arg, int); c = *++fmt; }
			else for (; c >= '0' && c <= '9'; c = *++fmt)
					F.precision = (10 * F.precision) + (c - '0');
			if (F.precision >= 0) F.havePrecision = 1;
			}

		if (F.leftJustify) F.zeroPad = 0;

conv:	switch (c)	//  perform appropriate conversion
			{
			unsigned long n;
			case 'h' :	F.hSize = 1; c = *++fmt; goto conv;
			case 'l' :	// fall through
			case 'L' :	F.lSize = 1; c = *++fmt; goto conv;
			case 'd' :
			case 'i' :	if (F.lSize) n = (unsigned long)va_arg(arg, long);
						else n = (unsigned long)va_arg(arg, int);
						if (F.hSize) n = (short) n;
						if ((long) n < 0) { n = (unsigned long)-(long)n; F.sign = '-'; }
						else if (F.forceSign) F.sign = '+';
						goto decimal;
			case 'u' :	if (F.lSize) n = va_arg(arg, unsigned long);
						else n = va_arg(arg, unsigned int);
						if (F.hSize) n = (unsigned short) n;
						F.sign = 0;
						goto decimal;
			decimal:	if (!F.havePrecision)
							{
							if (F.zeroPad)
								{
								F.precision = F.fieldWidth;
								if (F.sign) --F.precision;
								}
							if (F.precision < 1) F.precision = 1;
							}
						for (i = 0; n; n /= 10, i++) *--s = (char)(n % 10 + '0');
						for (; i < F.precision; i++) *--s = '0';
						if (F.sign) { *--s = F.sign; i++; }
						break;

			case 'o' :	if (F.lSize) n = va_arg(arg, unsigned long);
						else n = va_arg(arg, unsigned int);
						if (F.hSize) n = (unsigned short) n;
						if (!F.havePrecision)
							{
							if (F.zeroPad) F.precision = F.fieldWidth;
							if (F.precision < 1) F.precision = 1;
							}
						for (i = 0; n; n /= 8, i++) *--s = (char)(n % 8 + '0');
						if (F.altForm && i && *s != '0') { *--s = '0'; i++; }
						for (; i < F.precision; i++) *--s = '0';
						break;

			case 'a' :	{
						unsigned char *a = va_arg(arg, unsigned char *);
						unsigned short *w = (unsigned short *)a;
						s = buf;
						switch (F.precision)
							{
							case  4: i = mDNS_sprintf(s, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]); break;
							case  6: i = mDNS_sprintf(s, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]); break;
							case 16: i = mDNS_sprintf(s, "%04X:%04X:%04X:%04X:%04X:%04X:%04X:%04X",
												w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]); break;
							default: i = mDNS_sprintf(s, "%s", "ERROR: Must specify address size "
												"(i.e. %.4a=IPv4, %.6a=Ethernet, %.16a=IPv6) >>"); break;
							}
						}
						break;

			case 'p' :	F.havePrecision = F.lSize = 1;
						F.precision = 8;
			case 'X' :	digits = "0123456789ABCDEF";
						goto hexadecimal;
			case 'x' :	digits = "0123456789abcdef";
			hexadecimal:if (F.lSize) n = va_arg(arg, unsigned long);
						else n = va_arg(arg, unsigned int);
						if (F.hSize) n = (unsigned short) n;
						if (!F.havePrecision)
							{
							if (F.zeroPad)
								{
								F.precision = F.fieldWidth;
								if (F.altForm) F.precision -= 2;
								}
							if (F.precision < 1) F.precision = 1;
							}
						for (i = 0; n; n /= 16, i++) *--s = digits[n % 16];
						for (; i < F.precision; i++) *--s = '0';
						if (F.altForm) { *--s = (char)c; *--s = '0'; i += 2; }
						break;

			case 'c' :	*--s = (char)va_arg(arg, int); i = 1; break;

			case 's' :	s = va_arg(arg, char *);
						switch (F.altForm)
							{
							case 0: { char *a=s; i=0; while(*a++) i++; break; }	// C string
							case 1: i = (unsigned char) *s++; break;	// Pascal string
							case 2: {									// DNS label-sequence name
									unsigned char *a = (unsigned char *)s;
									s = buf;
									if (*a == 0) *s++ = '.';	// Special case for root DNS name
									while (*a && s + *a + 1 < &buf[mDNS_vsprintf_BUFLEN])
										{
										s += mDNS_sprintf(s, "%#s.", a);
										a += 1 + *a;
										}
									i = (int)(s - buf);
									s = buf;
									break;
									}
							}
						if (F.havePrecision && i > F.precision) i = F.precision;
						break;

			case 'n' :	s = va_arg(arg, char *);
						if      (F.hSize) * (short *) s = (short)nwritten;
						else if (F.lSize) * (long  *) s = (long)nwritten;
						else              * (int   *) s = (int)nwritten;
						continue;

				//  oops - unknown conversion, abort

			case 'M': case 'N': case 'O': case 'P': case 'Q':
			case 'R': case 'S': case 'T': case 'U': case 'V':
			// (extra cases force this to be an indexed switch)
			default: goto done;

			case '%' :
			copy1    :	*sbuffer++ = (char)c; ++nwritten; continue;
			}

			//  pad on the left

		if (i < F.fieldWidth && !F.leftJustify)
			do { *sbuffer++ = ' '; ++nwritten; } while (i < --F.fieldWidth);

			//  write the converted result

		for (j=0; j<i; j++) *sbuffer++ = *s++;
		nwritten += i;

			//  pad on the right

		for (; i < F.fieldWidth; i++)
			{ *sbuffer++ = ' '; ++nwritten; }
		}

done: return(nwritten);
	}

int mDNS_sprintf(char *sbuffer, const char *fmt, ...)
	{
	int	length;
	
    va_list ptr;
	va_start(ptr,fmt);
	length = mDNS_vsprintf(sbuffer, fmt, ptr);
	sbuffer[length] = 0;
	va_end(ptr);
	
	return length;
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - General Utility Functions
#endif

// We define DNSTypeName this way because if we declare it as a static (local) function
// then some compilers give "static function defined but not used" warnings in non-debug builds.
// Conditionalizing it with "#if MDNS_DEBUGMSGS" so that it is not present in non-debug builds
// fixes the warning on those compilers, but doesn't work on other compilers that don't strip
// unreachable code, which then give "undefined function" errors.
extern char *DNSTypeName(mDNSu16 rrtype);
char *DNSTypeName(mDNSu16 rrtype)
	{
	switch (rrtype)
		{
		case kDNSType_A:	return("Address");
		case kDNSType_CNAME:return("CNAME");
		case kDNSType_PTR:	return("PTR");
		case kDNSType_TXT:  return("TXT");
		case kDNSType_SRV:	return("SRV");
		default:			{
							static char buffer[16];
							mDNS_sprintf(buffer, "(%d)", rrtype);
							return(buffer);
							}
		}
	}

mDNSlocal mDNSu32 mDNSRandom(mDNSu32 max)
	{
	static mDNSu32 seed = 1;
	mDNSu32 mask = 1;
	while (mask < max) mask = (mask << 1) | 1;
	do seed = seed * 21 + 1; while ((seed & mask) > max);
	return (seed & mask);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Domain Name Utility Functions
#endif

// Returns length of a domain name INCLUDING the byte for the final null label
// i.e. for the root label "." it returns one
// For the FQDN "com." it returns 5 (length, three data bytes, final zero)
// Legal results are 1 (just root label) to 255 (MAX_DOMAIN_NAME)
// If the given domainname is invalid, result is 256
mDNSexport mDNSu16 DomainNameLength(const domainname *const name)
	{
	const mDNSu8 *src = name->c;
	while (*src)
		{
		if (*src > MAX_DOMAIN_LABEL) return(MAX_DOMAIN_NAME+1);
		src += 1 + *src;
		if (src - name->c >= MAX_DOMAIN_NAME) return(MAX_DOMAIN_NAME+1);
		}
	return((mDNSu16)(src - name->c + 1));
	}

mDNSlocal mDNSBool SameDomainLabel(const mDNSu8 *a, const mDNSu8 *b)
	{
	int i;
	const int len = *a++;

	if (len > MAX_DOMAIN_LABEL)
		{ debugf("Malformed label (too long)"); return(mDNSfalse); }

	if (len != *b++) return(mDNSfalse);
	for (i=0; i<len; i++)
		{
		mDNSu8 ac = *a++;
		mDNSu8 bc = *b++;
		if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
		if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
		if (ac != bc) return(mDNSfalse);
		}
	return(mDNStrue);
	}

mDNSexport mDNSBool SameDomainName(const domainname *const d1, const domainname *const d2)
	{
	const mDNSu8 *      a   = d1->c;
	const mDNSu8 *      b   = d2->c;
	const mDNSu8 *const max = d1->c + MAX_DOMAIN_NAME;			// Maximum that's valid

	while (*a || *b)
		{
		if (a + 1 + *a >= max)
			{ debugf("Malformed domain name (more than 255 characters)"); return(mDNSfalse); }
		if (!SameDomainLabel(a, b)) return(mDNSfalse);
		a += 1 + *a;
		b += 1 + *b;
		}
	
	return(mDNStrue);
	}

// CompressedDomainNameLength returns the length of a domain name INCLUDING the byte
// for the final null label i.e. for the root label "." it returns one.
// E.g. for the FQDN "foo.com." it returns 9
// (length, three data bytes, length, three more data bytes, final zero).
// In the case where a parent domain name is provided, and the given name is a child
// of that parent, CompressedDomainNameLength returns the length of the prefix portion
// of the child name, plus TWO bytes for the compression pointer.
// E.g. for the name "foo.com." with parent "com.", it returns 6
// (length, three data bytes, two-byte compression pointer).
mDNSlocal mDNSu16 CompressedDomainNameLength(const domainname *const name, const domainname *parent)
	{
	const mDNSu8 *src = name->c;
	if (parent && parent->c[0] == 0) parent = mDNSNULL;
	while (*src)
		{
		if (*src > MAX_DOMAIN_LABEL) return(MAX_DOMAIN_NAME+1);
		if (parent && SameDomainName((domainname *)src, parent)) return((mDNSu16)(src - name->c + 2));
		src += 1 + *src;
		if (src - name->c >= MAX_DOMAIN_NAME) return(MAX_DOMAIN_NAME+1);
		}
	return((mDNSu16)(src - name->c + 1));
	}

mDNSexport void AppendDomainLabelToName(domainname *const name, const domainlabel *const label)
	{
	int i;
	mDNSu8 *ptr = name->c + DomainNameLength(name) - 1;
	const mDNSu8 *const lim = name->c + MAX_DOMAIN_NAME;
	if (ptr + 1 + label->c[0] + 1 >= lim) return;
	for (i=0; i<=label->c[0]; i++) *ptr++ = label->c[i];
	*ptr++ = 0;										// Put the null root label on the end
	}

// AppendStringLabelToName appends a single label to an existing (possibly empty) domainname.
// The C string contains the label as-is, with no escaping, etc.
// Any dots in the name are literal dots, not label separators
mDNSexport void AppendStringLabelToName(domainname *const name, const char *cstr)
	{
	mDNSu8 *lengthbyte;
	mDNSu8 *ptr = name->c + DomainNameLength(name) - 1;
	const mDNSu8 *lim = name->c + MAX_DOMAIN_NAME - 1;
	if (lim > ptr + MAX_DOMAIN_LABEL + 1)
		lim = ptr + MAX_DOMAIN_LABEL + 1;
	lengthbyte = ptr++;
	while (*cstr && ptr < lim) *ptr++ = (mDNSu8)*cstr++;
	*lengthbyte = (mDNSu8)(ptr - lengthbyte - 1);
	*ptr++ = 0;										// Put the null root label on the end
	}

mDNSexport void AppendDomainNameToName(domainname *const name, const domainname *const append)
	{
	int i;
	mDNSu8 *ptr = name->c + DomainNameLength(name) - 1;
	const mDNSu8 *src = append->c;
	const mDNSu8 *const lim = name->c + MAX_DOMAIN_NAME;
	while(src[0])
		{
		if (ptr + 1 + src[0] + 1 >= lim) return;
		for (i=0; i<=src[0]; i++) *ptr++ = src[i];
		*ptr = 0;	// Put the null root label on the end
		src += i;
		}
	}

// AppendStringNameToName appends zero or more labels to an existing (possibly empty) domainname.
// The C string contains the labels separated by dots, but otherwise as-is, with no escaping, etc.
mDNSexport void AppendStringNameToName(domainname *const name, const char *cstr)
	{
	mDNSu8 *ptr = name->c + DomainNameLength(name) - 1;			// Find end of current name
	const mDNSu8 *const lim = name->c + MAX_DOMAIN_NAME - 1;		// Find limit of how much we can add
	while (*cstr)
		{
		mDNSu8 *const lengthbyte = ptr++;
		const mDNSu8 *const lim2 = ptr + MAX_DOMAIN_LABEL;
		const mDNSu8 *const lim3 = (lim < lim2) ? lim : lim2;
		while (*cstr && *cstr != '.' && ptr < lim3) *ptr++ = (mDNSu8)*cstr++;
		*lengthbyte = (mDNSu8)(ptr - lengthbyte - 1);
		if (*cstr == '.') cstr++;
		}
	
	*ptr++ = 0;										// Put the null root label on the end
	}

//#define IsThreeDigit(X) (IsDigit((X)[1]) && IsDigit((X)[2]) && IsDigit((X)[3]))
//#define ValidEscape(X) (X)[0] == '\\' && ((X)[1] == '\\' || (X)[1] == '\\' || IsThreeDigit(X))

#define mdnsIsLetter(X) (((X) >= 'A' && (X) <= 'Z') || ((X) >= 'a' && (X) <= 'z'))
#define mdnsIsDigit(X) (((X) >= '0' && (X) <= '9'))
#define mdnsValidHostChar(X, notfirst, notlast) (mdnsIsLetter(X) || \
	((notfirst) && (mdnsIsDigit(X) || ((notlast) && (X) == '-'))) )

mDNSexport void ConvertCStringToDomainLabel(const char *src, domainlabel *label)
	{
	mDNSu8       *      ptr   = label->c + 1;					// Where we're putting it
	const mDNSu8 *const limit = ptr + MAX_DOMAIN_LABEL;			// The maximum we can put
	while (*src && ptr < limit)									// While we have characters in the label...
		{
		mDNSu8 c = (mDNSu8)*src++;								// Read the character
		if (c == '\\')											// If escape character, check next character
			{
			if (*src == '\\' || *src == '.')					// If a second escape, or a dot,
				c = (mDNSu8)*src++;								// just use the second character
			else if (mdnsIsDigit(src[0]) && mdnsIsDigit(src[1]) && mdnsIsDigit(src[2]))
				{												// else, if three decimal digits,
				int v0 = src[0] - '0';							// then interpret as three-digit decimal
				int v1 = src[1] - '0';
				int v2 = src[2] - '0';
				int val = v0 * 100 + v1 * 10 + v2;
				if (val <= 255) { c = (mDNSu8)val; src += 3; }	// If valid value, use it
				}
			}
		*ptr++ = c;												// Write the character
		}
	label->c[0] = (mDNSu8)(ptr - label->c - 1);
	}

mDNSexport mDNSu8 *ConvertCStringToDomainName(const char *const cstr, domainname *name)
	{
	const mDNSu8 *src         = (const mDNSu8 *)cstr;					// C string we're reading
	mDNSu8       *ptr         = name->c;								// Where we're putting it
	const mDNSu8 *const limit = ptr + MAX_DOMAIN_NAME;				// The maximum we can put

	while (*src && ptr < limit)										// While more characters, and space to put them...
		{
		mDNSu8 *lengthbyte = ptr++;									// Record where the length is going to go
		while (*src && *src != '.' && ptr < limit)					// While we have characters in the label...
			{
			mDNSu8 c = *src++;										// Read the character
			if (c == '\\')											// If escape character, check next character
				{
				if (*src == '\\' || *src == '.')					// If a second escape, or a dot,
					c = *src++;										// just use the second character
				else if (mdnsIsDigit(src[0]) && mdnsIsDigit(src[1]) && mdnsIsDigit(src[2]))
					{												// else, if three decimal digits,
					int v0 = src[0] - '0';						// then interpret as three-digit decimal
					int v1 = src[1] - '0';
					int v2 = src[2] - '0';
					int val = v0 * 100 + v1 * 10 + v2;
					if (val <= 255) { c = (mDNSu8)val; src += 3; }	// If valid value, use it
					}
				}
			*ptr++ = c;												// Write the character
			}
		if (*src) src++;											// Skip over the trailing dot (if present)
		if (ptr - lengthbyte - 1 > MAX_DOMAIN_LABEL) return(mDNSNULL);	// If illegal label, abort
		*lengthbyte = (mDNSu8)(ptr - lengthbyte - 1);
		}

	if (ptr < limit)												// If we didn't run out of space
		{
		*ptr++ = 0;													// Put the final root label
		return(ptr);												// and return
		}

	return(mDNSNULL);
	}

//#define convertCstringtodomainname(C,D)        convertCstringtodomainname_withescape((C), (D), -1)
//#define convertescapedCstringtodomainname(C,D) convertCstringtodomainname_withescape((C), (D), '\\')

mDNSexport char *ConvertDomainLabelToCString_withescape(const domainlabel *const label, char *ptr, char esc)
	{
	const mDNSu8 *      src = label->c;							// Domain label we're reading
	const mDNSu8        len = *src++;							// Read length of this (non-null) label
	const mDNSu8 *const end = src + len;							// Work out where the label ends
	if (len > MAX_DOMAIN_LABEL) return(mDNSNULL);					// If illegal label, abort
	while (src < end)											// While we have characters in the label
		{
		mDNSu8 c = *src++;
		if (esc)
			{
			if (c == '.')										// If character is a dot,
				*ptr++ = esc;									// Output escape character
			else if (c <= ' ')									// If non-printing ascii,
				{												// Output decimal escape sequence
				*ptr++ = esc;
				*ptr++ = (char) ('0' + (c / 100)     );
				*ptr++ = (char) ('0' + (c /  10) % 10);
				c      = (mDNSu8)('0' + (c      ) % 10);
				}
			}
		*ptr++ = (char)c;										// Copy the character
		}
	*ptr = 0;													// Null-terminate the string
	return(ptr);												// and return
	}

// Note, to guarantee that there will be no possible overrun, cstr must be at least 1005 bytes
// The longest legal domain name is 255 bytes, in the form of three 64-byte labels, one 62-byte label,
// and the null root label.
// If every label character has to be escaped as a four-byte escape sequence, the maximum textual
// ascii display of this is 63*4 + 63*4 + 63*4 + 61*4 = 1000 label characters,
// plus four dots and the null at the end of the C string = 1005
mDNSexport char *ConvertDomainNameToCString_withescape(const domainname *const name, char *ptr, char esc)
	{
	const mDNSu8 *src         = name->c;								// Domain name we're reading
	const mDNSu8 *const max   = name->c + MAX_DOMAIN_NAME;			// Maximum that's valid
	
	if (*src == 0) *ptr++ = '.';									// Special case: For root, just write a dot

	while (*src)													// While more characters in the domain name
		{
		if (src + 1 + *src >= max) return(mDNSNULL);
		ptr = ConvertDomainLabelToCString_withescape((const domainlabel *)src, ptr, esc);
		if (!ptr) return(mDNSNULL);
		src += 1 + *src;
		*ptr++ = '.';												// Write the dot after the label
		}

	*ptr++ = 0;														// Null-terminate the string
	return(ptr);													// and return
	}

// RFC 1034 rules:
// Host names must start with a letter, end with a letter or digit,
// and have as interior characters only letters, digits, and hyphen.

mDNSexport void ConvertUTF8PstringToRFC1034HostLabel(const mDNSu8 UTF8Name[], domainlabel *const hostlabel)
	{
	const mDNSu8 *      src  = &UTF8Name[1];
	const mDNSu8 *const end  = &UTF8Name[1] + UTF8Name[0];
	      mDNSu8 *      ptr  = &hostlabel->c[1];
	const mDNSu8 *const lim  = &hostlabel->c[1] + MAX_DOMAIN_LABEL;
	while (src < end)
		{
		// Delete apostrophes from source name
		if (src[0] == '\'') { src++; continue; }		// Standard straight single quote
		if (src + 2 < end && src[0] == 0xE2 && src[1] == 0x80 && src[2] == 0x99)
			{ src += 3; continue; }	// Unicode curly apostrophe
		if (ptr < lim)
			{
			if (mdnsValidHostChar(*src, (ptr > &hostlabel->c[1]), (src < end-1))) *ptr++ = *src;
			else if (ptr > &hostlabel->c[1] && ptr[-1] != '-') *ptr++ = '-';
			}
		src++;
		}
	while (ptr > &hostlabel->c[1] && ptr[-1] == '-') ptr--;	// Truncate trailing '-' marks
	hostlabel->c[0] = (mDNSu8)(ptr - &hostlabel->c[1]);
	}

mDNSexport mDNSu8 *ConstructServiceName(domainname *const fqdn,
	const domainlabel *const name, const domainname *const type, const domainname *const domain)
	{
	int i, len;
	mDNSu8 *dst = fqdn->c;
	mDNSu8 *max = fqdn->c + MAX_DOMAIN_NAME;
	const mDNSu8 *src;

	if (name)
		{
		src = name->c;									// Put the service name into the domain name
		len = *src;
		if (len >= 0x40) { debugf("ConstructServiceName: service name too long"); return(0); }
		for (i=0; i<=len; i++) *dst++ = *src++;
		}
	
	src = type->c;										// Put the service type into the domain name
	len = *src;
	if (len == 0 || len >= 0x40)  { debugf("ConstructServiceName: Invalid service name"); return(0); }
	if (dst + 1 + len + 1 >= max) { debugf("ConstructServiceName: service type too long"); return(0); }
	for (i=0; i<=len; i++) *dst++ = *src++;

	len = *src;
	if (len == 0 || len >= 0x40)  { debugf("ConstructServiceName: Invalid service name"); return(0); }
	if (dst + 1 + len + 1 >= max) { debugf("ConstructServiceName: service type too long"); return(0); }
	for (i=0; i<=len; i++) *dst++ = *src++;
	
	if (*src) { debugf("ConstructServiceName: Service type must have only two labels"); return(0); }

	src = domain->c;									// Put the service domain into the domain name
	while (*src)
		{
		len = *src;
		if (dst + 1 + len + 1 >= max)
			{ debugf("ConstructServiceName: service domain too long"); return(0); }
		for (i=0; i<=len; i++) *dst++ = *src++;
		}
	
	*dst++ = 0;		// Put the null root label on the end
	return(dst);
	}

mDNSexport mDNSBool DeconstructServiceName(const domainname *const fqdn,
	domainlabel *const name, domainname *const type, domainname *const domain)
	{
	int i, len;
	const mDNSu8 *src = fqdn->c;
	const mDNSu8 *max = fqdn->c + MAX_DOMAIN_NAME;
	mDNSu8 *dst;
	
	dst = name->c;										// Extract the service name from the domain name
	len = *src;
	if (len >= 0x40) { debugf("DeconstructServiceName: service name too long"); return(mDNSfalse); }
	for (i=0; i<=len; i++) *dst++ = *src++;
	
	dst = type->c;										// Extract the service type from the domain name
	len = *src;
	if (len >= 0x40) { debugf("DeconstructServiceName: service type too long"); return(mDNSfalse); }
	for (i=0; i<=len; i++) *dst++ = *src++;

	len = *src;
	if (len >= 0x40) { debugf("DeconstructServiceName: service type too long"); return(mDNSfalse); }
	for (i=0; i<=len; i++) *dst++ = *src++;
	*dst++ = 0;		// Put the null root label on the end of the service type

	dst = domain->c;									// Extract the service domain from the domain name
	while (*src)
		{
		len = *src;
		if (len >= 0x40)
			{ debugf("DeconstructServiceName: service domain label too long"); return(mDNSfalse); }
		if (src + 1 + len + 1 >= max)
			{ debugf("DeconstructServiceName: service domain too long"); return(mDNSfalse); }
		for (i=0; i<=len; i++) *dst++ = *src++;
		}
	*dst++ = 0;		// Put the null root label on the end
	
	return(mDNStrue);
	}

mDNSexport void IncrementLabelSuffix(domainlabel *name, mDNSBool RichText)
	{
	long val = 0, multiplier = 1, divisor = 1, digits = 1;

	// Get any existing numerical suffix off the name
	while (mdnsIsDigit(name->c[name->c[0]]))
		{ val += (name->c[name->c[0]] - '0') * multiplier; multiplier *= 10; name->c[0]--; }
	
	// If existing suffix, increment it, else start by renaming "Foo" as "Foo2"
	if (multiplier > 1 && val < 999999) val++; else val = 2;

	// Can only add spaces to rich text names, not RFC 1034 names
	if (RichText && name->c[name->c[0]] != ' ' && name->c[0] < MAX_DOMAIN_LABEL)
		name->c[++name->c[0]] = ' ';
	
	while (val >= divisor * 10)
		{ divisor *= 10; digits++; }

	if (name->c[0] > (mDNSu8)(MAX_DOMAIN_LABEL - digits))
		name->c[0] = (mDNSu8)(MAX_DOMAIN_LABEL - digits);
	
	while (divisor)
		{
		name->c[++name->c[0]] = (mDNSu8)('0' + val / divisor);
		val     %= divisor;
		divisor /= 10;
		}
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Resource Record Utility Functions
#endif

#define ResourceRecordIsValidAnswer(RR) ( ((RR)->             RecordType & kDNSRecordTypeActiveMask)  && \
		((RR)->Additional1 == mDNSNULL || ((RR)->Additional1->RecordType & kDNSRecordTypeActiveMask)) && \
		((RR)->Additional2 == mDNSNULL || ((RR)->Additional2->RecordType & kDNSRecordTypeActiveMask)) && \
		((RR)->DependentOn == mDNSNULL || ((RR)->DependentOn->RecordType & kDNSRecordTypeActiveMask))  )

#define ResourceRecordIsValidInterfaceAnswer(RR, I) \
	(ResourceRecordIsValidAnswer(RR) && \
	((RR)->InterfaceAddr.NotAnInteger == 0 || (RR)->InterfaceAddr.NotAnInteger == (I).NotAnInteger))

#define DefaultProbeCountForTypeUnique ((mDNSu8)3)

#define DefaultAnnounceCountForTypeShared ((mDNSu8)10)
#define DefaultAnnounceCountForTypeUnique ((mDNSu8)2)

#define DefaultAnnounceCountForRecordType(X)   ((X) == kDNSRecordTypeShared   ? DefaultAnnounceCountForTypeShared : \
												(X) == kDNSRecordTypeUnique   ? DefaultAnnounceCountForTypeUnique : \
												(X) == kDNSRecordTypeVerified ? DefaultAnnounceCountForTypeUnique : (mDNSu8)0)

#define DefaultSendIntervalForRecordType(X)    ((X) == kDNSRecordTypeShared   ? mDNSPlatformOneSecond   : \
												(X) == kDNSRecordTypeUnique   ? mDNSPlatformOneSecond/4 : \
												(X) == kDNSRecordTypeVerified ? mDNSPlatformOneSecond/4 : 0)

#define TimeToAnnounceThisRecord(RR,time) ((RR)->AnnounceCount && time - (RR)->NextSendTime >= 0)
#define TimeToSendThisRecord(RR,time) \
	((TimeToAnnounceThisRecord(RR,time) || (RR)->SendPriority) && ResourceRecordIsValidAnswer(RR))

mDNSlocal mDNSBool SameRData(const mDNSu16 rrtype, const RData *const r1, const RData *const r2)
	{
	if (r1->RDLength != r2->RDLength) return(mDNSfalse);
	switch(rrtype)
		{
		case kDNSType_CNAME:// Same as PTR
		case kDNSType_PTR:	return(SameDomainName(&r1->u.name, &r2->u.name));

		case kDNSType_SRV:	return( r1->u.srv.priority          == r2->u.srv.priority          &&
									r1->u.srv.weight            == r2->u.srv.weight            &&
									r1->u.srv.port.NotAnInteger == r2->u.srv.port.NotAnInteger &&
									SameDomainName(&r1->u.srv.target, &r2->u.srv.target));

		default:			return(mDNSPlatformMemSame(r1->u.data, r2->u.data, r1->RDLength));
		}
	}

mDNSlocal mDNSBool ResourceRecordAnswersQuestion(const ResourceRecord *const rr, const DNSQuestion *const q)
	{
	if (rr->InterfaceAddr.NotAnInteger &&
		q ->InterfaceAddr.NotAnInteger &&
		rr->InterfaceAddr.NotAnInteger != q->InterfaceAddr.NotAnInteger) return(mDNSfalse);

	// RR type CNAME matches any query type. QTYPE ANY matches any RR type. QCLASS ANY matches any RR class.
	if (rr->rrtype != kDNSType_CNAME && rr->rrtype  != q->rrtype  && q->rrtype  != kDNSQType_ANY ) return(mDNSfalse);
	if (                                rr->rrclass != q->rrclass && q->rrclass != kDNSQClass_ANY) return(mDNSfalse);
	return(SameDomainName(&rr->name, &q->name));
	}

// SameResourceRecordSignature returns true if two resources records have the same interface, name, type, and class.
// -- i.e. if they would both be given in response to the same question.
// (TTL and rdata may differ)
mDNSlocal mDNSBool SameResourceRecordSignature(const ResourceRecord *const r1, const ResourceRecord *const r2)
	{
	if (!r1) { debugf("SameResourceRecordSignature ERROR: r1 is NULL"); return(mDNSfalse); }
	if (!r2) { debugf("SameResourceRecordSignature ERROR: r2 is NULL"); return(mDNSfalse); }
	if (r1->InterfaceAddr.NotAnInteger &&
		r2->InterfaceAddr.NotAnInteger &&
		r1->InterfaceAddr.NotAnInteger != r2->InterfaceAddr.NotAnInteger) return(mDNSfalse);
	return (r1->rrtype == r2->rrtype && r1->rrclass == r2->rrclass && SameDomainName(&r1->name, &r2->name));
	}

// SameResourceRecordSignatureAnyInterface returns true if two resources records have the same name, type, and class.
// (InterfaceAddr, TTL and rdata may differ)
mDNSlocal mDNSBool SameResourceRecordSignatureAnyInterface(const ResourceRecord *const r1, const ResourceRecord *const r2)
	{
	if (!r1) { debugf("SameResourceRecordSignatureAnyInterface ERROR: r1 is NULL"); return(mDNSfalse); }
	if (!r2) { debugf("SameResourceRecordSignatureAnyInterface ERROR: r2 is NULL"); return(mDNSfalse); }
	return (r1->rrtype == r2->rrtype && r1->rrclass == r2->rrclass && SameDomainName(&r1->name, &r2->name));
	}

// IdenticalResourceRecord returns true if two resources records have
// the same interface, name, type, class, and identical rdata (TTL may differ)
mDNSlocal mDNSBool IdenticalResourceRecord(const ResourceRecord *const r1, const ResourceRecord *const r2)
	{
	if (!SameResourceRecordSignature(r1, r2)) return(mDNSfalse);
	return(SameRData(r1->rrtype, r1->rdata, r2->rdata));
	}

// IdenticalResourceRecordAnyInterface returns true if two resources records have
// the same name, type, class, and identical rdata (InterfaceAddr and TTL may differ)
mDNSlocal mDNSBool IdenticalResourceRecordAnyInterface(const ResourceRecord *const r1, const ResourceRecord *const r2)
	{
	if (!SameResourceRecordSignatureAnyInterface(r1, r2)) return(mDNSfalse);
	return(SameRData(r1->rrtype, r1->rdata, r2->rdata));
	}

// ResourceRecord *ds is the ResourceRecord from the duplicate suppression section of the query
// This is the information that the requester believes to be correct
// ResourceRecord *rr is the answer we are proposing to give, if not suppressed
// This is the information that we believe to be correct
mDNSlocal mDNSBool SuppressDuplicate(const ResourceRecord *const ds, const ResourceRecord *const rr)
	{
	// If RR signature is different, or data is different, then don't suppress
	if (!IdenticalResourceRecord(ds,rr)) return(mDNSfalse);
	
	// If the requester's indicated TTL is less than half the real TTL,
	// we need to give our answer before the requester's copy expires.
	// If the requester's indicated TTL is at least half the real TTL,
	// then we can suppress our answer this time.
	// If the requester's indicated TTL is greater than the TTL we believe,
	// then that's okay, and we don't need to do anything about it.
	// (If two responders on the network are offering the same information,
	// that's okay, and if they are offering the information with different TTLs,
	// the one offering the lower TTL should defer to the one offering the higher TTL.)
	return(ds->rroriginalttl >= rr->rroriginalttl / 2);
	}

mDNSlocal mDNSu16 GetRDLength(const ResourceRecord *const rr, mDNSBool estimate)
	{
	const domainname *const name = estimate ? &rr->name : mDNSNULL;
	switch (rr->rrtype)
		{
		case kDNSType_A:	return(sizeof(rr->rdata->u.ip)); break;
		case kDNSType_CNAME:// Same as PTR
		case kDNSType_PTR:	return(CompressedDomainNameLength(&rr->rdata->u.name, name));
		case kDNSType_TXT:  return(rr->rdata->RDLength); // TXT is not self-describing, so have to just trust rdlength
		case kDNSType_SRV:	return(mDNSu16)(6 + CompressedDomainNameLength(&rr->rdata->u.srv.target, name));
		default:			debugf("Warning! Don't know how to get length of resource type %d", rr->rrtype);
							return(rr->rdata->RDLength);
		}
	}

// rr is a ResourceRecord in our cache
// (kDNSRecordTypePacketAnswer/kDNSRecordTypePacketAdditional/kDNSRecordTypePacketUniqueAns/kDNSRecordTypePacketUniqueAdd)
mDNSlocal DNSQuestion *CacheRRActive(const mDNS *const m, ResourceRecord *rr)
	{
	DNSQuestion *q;
	for (q = m->ActiveQuestions; q; q=q->next)		// Scan our list of questions
		if (!q->DuplicateOf && ResourceRecordAnswersQuestion(rr, q))
			return(q);
	return(mDNSNULL);
	}

mDNSlocal void SetTargetToHostName(const mDNS *const m, ResourceRecord *const rr)
	{
	switch (rr->rrtype)
		{
		case kDNSType_CNAME:// Same as PTR
		case kDNSType_PTR:	rr->rdata->u.name       = m->hostname1; break;
		case kDNSType_SRV:	rr->rdata->u.srv.target = m->hostname1; break;
		default: debugf("SetTargetToHostName: Dont' know how to set the target of rrtype %d", rr->rrtype); break;
		}
	rr->rdata->RDLength = GetRDLength(rr, mDNSfalse);
	rr->rdestimate      = GetRDLength(rr, mDNStrue);
	
	// If we're in the middle of probing this record, we need to start again,
	// because changing its rdata may change the outcome of the tie-breaker.
	if (rr->RecordType == kDNSRecordTypeUnique) rr->ProbeCount = DefaultProbeCountForTypeUnique;
	}

mDNSlocal void UpdateHostNameTargets(const mDNS *const m)
	{
	ResourceRecord *rr;
	for (rr = m->ResourceRecords; rr; rr=rr->next)
		if (rr->HostTarget)
			SetTargetToHostName(m, rr);
	}

mDNSlocal mStatus mDNS_Register_internal(mDNS *const m, ResourceRecord *const rr, const mDNSs32 timenow)
	{
	ResourceRecord **p = &m->ResourceRecords;
	while (*p && *p != rr) p=&(*p)->next;
	if (*p)
		{
		debugf("Error! Tried to register a ResourceRecord that's already in the list");
		return(mStatus_AlreadyRegistered);
		}

	if (rr->DependentOn)
		{
		if (rr->RecordType == kDNSRecordTypeUnique)
			rr->RecordType =  kDNSRecordTypeVerified;
		else
			{
			debugf("mDNS_Register_internal: ERROR! %##s: rr->DependentOn && RecordType != kDNSRecordTypeUnique",
				rr->name.c);
			return(mStatus_Invalid);
			}
		if (rr->DependentOn->RecordType != kDNSRecordTypeUnique && rr->DependentOn->RecordType != kDNSRecordTypeVerified)
			{
			debugf("mDNS_Register_internal: ERROR! %##s: rr->DependentOn->RecordType bad type %X",
				rr->name.c, rr->DependentOn->RecordType);
			return(mStatus_Invalid);
			}
		}

	rr->next = mDNSNULL;

	// Field Group 1: Persistent metadata for Authoritative Records
//	rr->Additional1       = set to mDNSNULL in mDNS_SetupResourceRecord; may be overridden by client
//	rr->Additional2       = set to mDNSNULL in mDNS_SetupResourceRecord; may be overridden by client
//	rr->DependentOn       = set to mDNSNULL in mDNS_SetupResourceRecord; may be overridden by client
//	rr->RRSet             = set to mDNSNULL in mDNS_SetupResourceRecord; may be overridden by client
//	rr->Callback          = already set in mDNS_SetupResourceRecord
//	rr->Context           = already set in mDNS_SetupResourceRecord
//	rr->RecordType        = already set in mDNS_SetupResourceRecord
//	rr->HostTarget        = set to mDNSfalse in mDNS_SetupResourceRecord; may be overridden by client

	// Field Group 2: Transient state for Authoritative Records
	rr->Acknowledged      = mDNSfalse;
	rr->ProbeCount        = (rr->RecordType == kDNSRecordTypeUnique) ? DefaultProbeCountForTypeUnique : (mDNSu8)0;
	rr->AnnounceCount     = DefaultAnnounceCountForRecordType(rr->RecordType);
	rr->IncludeInProbe    = mDNSfalse;
	rr->SendPriority      = 0;
	rr->Requester         = zeroIPAddr;
	rr->NextResponse      = mDNSNULL;
	rr->NR_AnswerTo       = mDNSNULL;
	rr->NR_AdditionalTo   = mDNSNULL;
	rr->LastSendTime      = timenow - mDNSPlatformOneSecond;
	rr->NextSendTime      = timenow;
	if (rr->RecordType == kDNSRecordTypeUnique && m->SuppressProbes) rr->NextSendTime = m->SuppressProbes;
	rr->NextSendInterval  = DefaultSendIntervalForRecordType(rr->RecordType);
	rr->NewRData          = mDNSNULL;
	rr->UpdateCallback    = mDNSNULL;

	// Field Group 3: Transient state for Cache Records
	rr->NextDupSuppress   = mDNSNULL;	// Not strictly relevant for a local record
	rr->TimeRcvd          = 0;			// Not strictly relevant for a local record
	rr->LastUsed          = 0;			// Not strictly relevant for a local record
	rr->UseCount          = 0;			// Not strictly relevant for a local record
	rr->UnansweredQueries = 0;			// Not strictly relevant for a local record
	rr->Active            = mDNSfalse;	// Not strictly relevant for a local record
	rr->NewData           = mDNSfalse;	// Not strictly relevant for a local record

	// Field Group 4: The actual information pertaining to this resource record
//	rr->interface         = already set in mDNS_SetupResourceRecord
//	rr->name.c            = MUST be set by client
//	rr->rrtype            = already set in mDNS_SetupResourceRecord
//	rr->rrclass           = already set in mDNS_SetupResourceRecord
//	rr->rroriginalttl     = already set in mDNS_SetupResourceRecord
//	rr->rrremainingttl    = already set in mDNS_SetupResourceRecord

	if (rr->HostTarget)
		SetTargetToHostName(m, rr);	// This also sets rdlength and rdestimate for us
	else
		{
		rr->rdata->RDLength = GetRDLength(rr, mDNSfalse);
		rr->rdestimate      = GetRDLength(rr, mDNStrue);
		}
//	rr->rdata             = MUST be set by client

	*p = rr;
	return(mStatus_NoError);
	}

// mDNS_Dereg_normal is used for most calls to mDNS_Deregister_internal
// mDNS_Dereg_conflict is used to indicate that this record is being forcibly deregistered because of a conflict
// mDNS_Dereg_repeat is used when cleaning up, for records that may have already been forcibly deregistered
typedef enum { mDNS_Dereg_normal, mDNS_Dereg_conflict, mDNS_Dereg_repeat } mDNS_Dereg_type;

// NOTE: mDNS_Deregister_internal can call a user callback, which may change the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void mDNS_Deregister_internal(mDNS *const m, ResourceRecord *const rr, const mDNSs32 timenow, mDNS_Dereg_type drt)
	{
	mDNSu8 RecordType = rr->RecordType;
	// If this is a shared record and we've announced it at least once,
	// we need to retract that announcement before we delete the record
	if (RecordType == kDNSRecordTypeShared && rr->AnnounceCount < DefaultAnnounceCountForTypeShared)
		{
		debugf("mDNS_Deregister_internal: Sending deregister for %##s (%s)", rr->name.c, DNSTypeName(rr->rrtype));
		rr->RecordType     = kDNSRecordTypeDeregistering;
		rr->rroriginalttl  = 0;
		rr->rrremainingttl = 0;
		}
	else
		{
		// Find this record in our list of active records
		ResourceRecord **p = &m->ResourceRecords;
		while (*p && *p != rr) p=&(*p)->next;

		if (*p) *p = rr->next;
		else
			{
			// No need to give an error message if we already know this is a potentially repeated deregistration
			if (drt != mDNS_Dereg_repeat)
				debugf("mDNS_Deregister_internal: Record %##s (%s) not found in list", rr->name.c, DNSTypeName(rr->rrtype));
			return;
			}
		// If someone is about to look at this, bump the pointer forward
		if (m->CurrentRecord == rr) m->CurrentRecord = rr->next;
		rr->next = mDNSNULL;

		if      (RecordType == kDNSRecordTypeUnregistered)
			debugf("mDNS_Deregister_internal: Record %##s (%s) already marked kDNSRecordTypeUnregistered",
				rr->name.c, DNSTypeName(rr->rrtype));
		else if (RecordType == kDNSRecordTypeDeregistering)
			debugf("mDNS_Deregister_internal: Record %##s (%s) already marked kDNSRecordTypeDeregistering",
				rr->name.c, DNSTypeName(rr->rrtype));
		else
			{
			debugf("mDNS_Deregister_internal: Deleting record for %##s (%s)", rr->name.c, DNSTypeName(rr->rrtype));
			rr->RecordType = kDNSRecordTypeUnregistered;
			}

		if ((drt == mDNS_Dereg_conflict || drt == mDNS_Dereg_repeat) && RecordType == kDNSRecordTypeShared)
			debugf("mDNS_Deregister_internal: Cannot have a conflict on a shared record! %##s (%s)",
				rr->name.c, DNSTypeName(rr->rrtype));

		// If we have an update queued up which never executed, give the client a chance to free that memory
		if (rr->NewRData)
			{
			RData *n = rr->NewRData;
			rr->NewRData = mDNSNULL;	// Clear the NewRData pointer ...
			if (rr->UpdateCallback) rr->UpdateCallback(m, rr, n); // ...and let the client free this memory, if necessary
			}
		
		// CAUTION: MUST NOT do anything more with rr after calling rr->Callback(), because the client's callback function
		// is allowed to do anything, including starting/stopping queries, registering/deregistering records, etc.
		// In this case the likely client action to the mStatus_MemFree message is to free the memory,
		// so any attempt to touch rr after this is likely to lead to a crash.
		if (RecordType == kDNSRecordTypeShared && rr->Callback)
			rr->Callback(m, rr, mStatus_MemFree);
		else if (drt == mDNS_Dereg_conflict)
			{
			m->SuppressProbes = timenow + mDNSPlatformOneSecond;
			if (m->SuppressProbes == 0) m->SuppressProbes = 1;
			if (rr->Callback) rr->Callback(m, rr, mStatus_NameConflict);
			}
		}
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark -
#pragma mark - DNS Message Creation Functions
#endif

mDNSlocal void InitializeDNSMessage(DNSMessageHeader *h, mDNSOpaque16 id, mDNSOpaque16 flags)
	{
	h->id             = id;
	h->flags          = flags;
	h->numQuestions   = 0;
	h->numAnswers     = 0;
	h->numAuthorities = 0;
	h->numAdditionals = 0;
	}

mDNSlocal const mDNSu8 *FindCompressionPointer(const mDNSu8 *const base, const mDNSu8 *const end, const mDNSu8 *const domname)
	{
	const mDNSu8 *result = end - *domname - 1;

	if (*domname == 0) return(mDNSNULL);	// There's no point trying to match just the root label
	
	// This loop examines each possible starting position in packet, starting end of the packet and working backwards
	while (result >= base)
		{
		// If the length byte and first character of the label match, then check further to see
		// if this location in the packet will yield a useful name compression pointer.
		if (result[0] == domname[0] && result[1] == domname[1])
			{
			const mDNSu8 *name = domname;
			const mDNSu8 *targ = result;
			while (targ + *name < end)
				{
				// First see if this label matches
				int i;
				const mDNSu8 *pointertarget;
				for (i=0; i <= *name; i++) if (targ[i] != name[i]) break;
				if (i <= *name) break;							// If label did not match, bail out
				targ += 1 + *name;								// Else, did match, so advance target pointer
				name += 1 + *name;								// and proceed to check next label
				if (*name == 0 && *targ == 0) return(result);	// If no more labels, we found a match!
				if (*name == 0) break;							// If no more labels to match, we failed, so bail out

				// The label matched, so now follow the pointer (if appropriate) and then see if the next label matches
				if (targ[0] < 0x40) continue;					// If length value, continue to check next label
				if (targ[0] < 0xC0) break;						// If 40-BF, not valid
				if (targ+1 >= end) break;						// Second byte not present!
				pointertarget = base + (((mDNSu16)(targ[0] & 0x3F)) << 8) + targ[1];
				if (targ < pointertarget) break;				// Pointertarget must point *backwards* in the packet
				if (pointertarget[0] >= 0x40) break;			// Pointertarget must point to a valid length byte
				targ = pointertarget;
				}
			}
		result--;	// We failed to match at this search position, so back up the tentative result pointer and try again
		}
	return(mDNSNULL);
	}

// Put a string of dot-separated labels as length-prefixed labels
// domainname is a fully-qualified name (i.e. assumed to be ending in a dot, even if it doesn't)
// msg points to the message we're building (pass mDNSNULL if we don't want to use compression pointers)
// end points to the end of the message so far
// ptr points to where we want to put the name
// limit points to one byte past the end of the buffer that we must not overrun
// domainname is the name to put
mDNSlocal mDNSu8 *putDomainNameAsLabels(const DNSMessage *const msg,
	mDNSu8 *ptr, const mDNSu8 *const limit, const domainname *const name)
	{
	const mDNSu8 *const base        = (const mDNSu8 *const)msg;
	const mDNSu8 *      np          = name->c;
	const mDNSu8 *const max         = name->c + MAX_DOMAIN_NAME;	// Maximum that's valid
	const mDNSu8 *      pointer     = mDNSNULL;
	const mDNSu8 *const searchlimit = ptr;

	while (*np && ptr < limit-1)		// While we've got characters in the name, and space to write them in the message...
		{
		if (np + 1 + *np >= max)
			{ debugf("Malformed domain name (more than 255 characters)"); return(mDNSNULL); }

		if (base) pointer = FindCompressionPointer(base, searchlimit, np);
		if (pointer)					// Use a compression pointer if we can
			{
			mDNSu16 offset = (mDNSu16)(pointer - base);
			*ptr++ = (mDNSu8)(0xC0 | (offset >> 8));
			*ptr++ = (mDNSu8)(        offset      );
			return(ptr);
			}
		else							// Else copy one label and try again
			{
			int i;
			mDNSu8 len = *np++;
			if (ptr + 1 + len >= limit) return(mDNSNULL);
			*ptr++ = len;
			for (i=0; i<len; i++) *ptr++ = *np++;
			}
		}

	if (ptr < limit)												// If we didn't run out of space
		{
		*ptr++ = 0;													// Put the final root label
		return(ptr);												// and return
		}

	return(mDNSNULL);
	}

mDNSlocal mDNSu8 *putRData(const DNSMessage *const msg, mDNSu8 *ptr, const mDNSu8 *const limit,
	const mDNSu16 rrtype, const RData *const rdata)
	{
	switch (rrtype)
		{
		case kDNSType_A:	if (rdata->RDLength != 4)
								{
								debugf("putRData: Illegal length %d for kDNSType_A", rdata->RDLength);
								return(mDNSNULL);
								}
							if (ptr + 4 > limit) return(mDNSNULL);
							*ptr++ = rdata->u.ip.b[0];
							*ptr++ = rdata->u.ip.b[1];
							*ptr++ = rdata->u.ip.b[2];
							*ptr++ = rdata->u.ip.b[3];
							return(ptr);

		case kDNSType_CNAME:// Same as PTR
		case kDNSType_PTR:	return(putDomainNameAsLabels(msg, ptr, limit, &rdata->u.name));

		case kDNSType_TXT:  if (ptr + rdata->RDLength > limit) return(mDNSNULL);
							mDNSPlatformMemCopy(rdata->u.data, ptr, rdata->RDLength);
							return(ptr + rdata->RDLength);

		case kDNSType_SRV:	if (ptr + 6 > limit) return(mDNSNULL);
							*ptr++ = (mDNSu8)(rdata->u.srv.priority >> 8);
							*ptr++ = (mDNSu8)(rdata->u.srv.priority     );
							*ptr++ = (mDNSu8)(rdata->u.srv.weight   >> 8);
							*ptr++ = (mDNSu8)(rdata->u.srv.weight       );
							*ptr++ = rdata->u.srv.port.b[0];
							*ptr++ = rdata->u.srv.port.b[1];
							return(putDomainNameAsLabels(msg, ptr, limit, &rdata->u.srv.target));

		default:			if (ptr + rdata->RDLength > limit) return(mDNSNULL);
							debugf("putRData: Warning! Writing resource type %d as raw data", rrtype);
							mDNSPlatformMemCopy(rdata->u.data, ptr, rdata->RDLength);
							return(ptr + rdata->RDLength);
		}
	}

// Put a domain name, type, class, ttl, length, and type-specific data
// domainname is a fully-qualified name
// Only pass the "m" and "timenow" parameters in cases where the LastSendTime is to be updated,
// and the kDNSClass_UniqueRRSet bit set
mDNSlocal mDNSu8 *putResourceRecord(DNSMessage *const msg, mDNSu8 *ptr,
	mDNSu16 *count, ResourceRecord *rr, mDNS *const m, const mDNSs32 timenow)
	{
	mDNSu8 *endofrdata;
	mDNSu16 actualLength;
	const mDNSu8 *limit = msg->data + AbsoluteMaxDNSMessageData;
	
	// If we have a single large record to put in the packet, then we allow the packet to be up to 9K bytes,
	// but in the normal case we try to keep the packets below 1500 to avoid IP fragmentation on standard Ethernet
	if (msg->h.numAnswers || msg->h.numAuthorities || msg->h.numAdditionals)
		limit = msg->data + NormalMaxDNSMessageData;

	if (rr->RecordType == kDNSRecordTypeUnregistered)
		{
		debugf("putResourceRecord ERROR! Attempt to put kDNSRecordTypeUnregistered");
		return(ptr);
		}

	ptr = putDomainNameAsLabels(msg, ptr, limit, &rr->name);
	if (!ptr || ptr + 10 >= limit) return(mDNSNULL);	// If we're out-of-space, return mDNSNULL
	ptr[0] = (mDNSu8)(rr->rrtype  >> 8);
	ptr[1] = (mDNSu8)(rr->rrtype      );
	ptr[2] = (mDNSu8)(rr->rrclass >> 8);
	ptr[3] = (mDNSu8)(rr->rrclass     );
	ptr[4] = (mDNSu8)(rr->rrremainingttl >> 24);
	ptr[5] = (mDNSu8)(rr->rrremainingttl >> 16);
	ptr[6] = (mDNSu8)(rr->rrremainingttl >>  8);
	ptr[7] = (mDNSu8)(rr->rrremainingttl      );
	endofrdata = putRData(msg, ptr+10, limit, rr->rrtype, rr->rdata);
	if (!endofrdata) { debugf("Ran out of space in putResourceRecord!"); return(mDNSNULL); }

	// Go back and fill in the actual number of data bytes we wrote
	// (actualLength can be less than rdlength when domain name compression is used)
	actualLength = (mDNSu16)(endofrdata - ptr - 10);
	ptr[8] = (mDNSu8)(actualLength >> 8);
	ptr[9] = (mDNSu8)(actualLength     );

	if (m)														// If the 'm' parameter was passed in...
		{
		rr->LastSendTime = timenow;								// ... then update LastSendTime
		if (rr->RecordType & kDNSRecordTypeUniqueMask)			// If it is supposed to be unique
			{
			const ResourceRecord *a = mDNSNULL;
			// If we find a member of the same RRSet (same name/type/class)
			// that hasn't been updated within the last quarter second, don't set the bit
			for (a = m->ResourceRecords; a; a=a->next)
				if (SameResourceRecordSignatureAnyInterface(rr, a))
					if (timenow - a->LastSendTime > mDNSPlatformOneSecond/4)
						break;
			if (a == mDNSNULL)
				ptr[2] |= kDNSClass_UniqueRRSet >> 8;
			}
		}

	(*count)++;
	return(endofrdata);
	}

#if 0
mDNSlocal mDNSu8 *putEmptyResourceRecord(DNSMessage *const msg, mDNSu8 *ptr, const mDNSu8 *const limit,
	mDNSu16 *count, const ResourceRecord *rr)
	{
	ptr = putDomainNameAsLabels(msg, ptr, limit, &rr->name);
	if (!ptr || ptr + 10 > limit) return(mDNSNULL);		// If we're out-of-space, return mDNSNULL
	ptr[0] = (mDNSu8)(rr->rrtype  >> 8);				// Put type
	ptr[1] = (mDNSu8)(rr->rrtype      );
	ptr[2] = (mDNSu8)(rr->rrclass >> 8);				// Put class
	ptr[3] = (mDNSu8)(rr->rrclass     );
	ptr[4] = ptr[5] = ptr[6] = ptr[7] = 0;				// TTL is zero
	ptr[8] = ptr[9] = 0;								// RDATA length is zero
	(*count)++;
	return(ptr + 10);
	}
#endif

mDNSlocal mDNSu8 *putQuestion(DNSMessage *const msg, mDNSu8 *ptr, const mDNSu8 *const limit,
							const domainname *const name, mDNSu16 rrtype, mDNSu16 rrclass)
	{
	ptr = putDomainNameAsLabels(msg, ptr, limit, name);
	if (!ptr || ptr+4 >= limit) return(mDNSNULL);			// If we're out-of-space, return mDNSNULL
	ptr[0] = (mDNSu8)(rrtype  >> 8);
	ptr[1] = (mDNSu8)(rrtype      );
	ptr[2] = (mDNSu8)(rrclass >> 8);
	ptr[3] = (mDNSu8)(rrclass     );
	msg->h.numQuestions++;
	return(ptr+4);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - DNS Message Parsing Functions
#endif

mDNSlocal const mDNSu8 *skipDomainName(const DNSMessage *const msg, const mDNSu8 *ptr, const mDNSu8 *const end)
	{
	mDNSu16 total = 0;

	if (ptr < (mDNSu8*)msg || ptr >= end)
		{ debugf("skipDomainName: Illegal ptr not within packet boundaries"); return(mDNSNULL); }

	while (1)						// Read sequence of labels
		{
		const mDNSu8 len = *ptr++;	// Read length of this label
		if (len == 0) return(ptr);	// If length is zero, that means this name is complete
		switch (len & 0xC0)
			{
			case 0x00:	if (ptr + len >= end)					// Remember: expect at least one more byte for the root label
							{ debugf("skipDomainName: Malformed domain name (overruns packet end)"); return(mDNSNULL); }
						if (total + 1 + len >= MAX_DOMAIN_NAME)	// Remember: expect at least one more byte for the root label
							{ debugf("skipDomainName: Malformed domain name (more than 255 characters)"); return(mDNSNULL); }
						ptr += len;
						total += 1 + len;
						break;

			case 0x40:	debugf("skipDomainName: Extended EDNS0 label types 0x%X not supported", len); return(mDNSNULL);
			case 0x80:	debugf("skipDomainName: Illegal label length 0x%X", len); return(mDNSNULL);
			case 0xC0:	return(ptr+1);
			}
		}
	}

// Routine to fetch an FQDN from the DNS message, following compression pointers if necessary.
mDNSlocal const mDNSu8 *getDomainName(const DNSMessage *const msg, const mDNSu8 *ptr, const mDNSu8 *const end,
	domainname *const name)
	{
	const mDNSu8 *nextbyte = mDNSNULL;					// Record where we got to before we started following pointers
	mDNSu8       *np = name->c;							// Name pointer
	const mDNSu8 *const limit = np + MAX_DOMAIN_NAME;	// Limit so we don't overrun buffer

	if (ptr < (mDNSu8*)msg || ptr >= end)
		{ debugf("getDomainName: Illegal ptr not within packet boundaries"); return(mDNSNULL); }

	*np = 0;						// Tentatively place the root label here (may be overwritten if we have more labels)

	while (1)						// Read sequence of labels
		{
		const mDNSu8 len = *ptr++;	// Read length of this label
		if (len == 0) break;		// If length is zero, that means this name is complete
		switch (len & 0xC0)
			{
			int i;
			mDNSu16 offset;

			case 0x00:	if (ptr + len >= end)		// Remember: expect at least one more byte for the root label
							{ debugf("getDomainName: Malformed domain name (overruns packet end)"); return(mDNSNULL); }
						if (np + 1 + len >= limit)	// Remember: expect at least one more byte for the root label
							{ debugf("getDomainName: Malformed domain name (more than 255 characters)"); return(mDNSNULL); }
						*np++ = len;
						for (i=0; i<len; i++) *np++ = *ptr++;
						*np = 0;	// Tentatively place the root label here (may be overwritten if we have more labels)
						break;

			case 0x40:	debugf("getDomainName: Extended EDNS0 label types 0x%X not supported in name %##s", len, name->c);
						return(mDNSNULL);

			case 0x80:	debugf("getDomainName: Illegal label length 0x%X in domain name %##s", len, name->c); return(mDNSNULL);

			case 0xC0:	offset = (mDNSu16)((((mDNSu16)(len & 0x3F)) << 8) | *ptr++);
						if (!nextbyte) nextbyte = ptr;	// Record where we got to before we started following pointers
						ptr = (mDNSu8 *)msg + offset;
						if (ptr < (mDNSu8*)msg || ptr >= end)
							{ debugf("getDomainName: Illegal compression pointer not within packet boundaries"); return(mDNSNULL); }
						if (*ptr & 0xC0)
							{ debugf("getDomainName: Compression pointer must point to real label"); return(mDNSNULL); }
						break;
			}
		}
	
	if (nextbyte) return(nextbyte);
	else return(ptr);
	}

mDNSlocal const mDNSu8 *skipResourceRecord(const DNSMessage *msg, const mDNSu8 *ptr, const mDNSu8 *end)
	{
	mDNSu16 pktrdlength;

	ptr = skipDomainName(msg, ptr, end);
	if (!ptr) { debugf("skipResourceRecord: Malformed RR name"); return(mDNSNULL); }
	
	if (ptr + 10 > end) { debugf("skipResourceRecord: Malformed RR -- no type/class/ttl/len!"); return(mDNSNULL); }
	pktrdlength = (mDNSu16)((mDNSu16)ptr[8] <<  8 | ptr[9]);
	ptr += 10;
	if (ptr + pktrdlength > end) { debugf("skipResourceRecord: RDATA exceeds end of packet"); return(mDNSNULL); }

	return(ptr + pktrdlength);
	}

mDNSlocal const mDNSu8 *getResourceRecord(const DNSMessage *msg, const mDNSu8 *ptr, const mDNSu8 *end,
	const mDNSIPAddr InterfaceAddr, const mDNSs32 timenow, mDNSu8 RecordType, ResourceRecord *rr, RData *RDataStorage)
	{
	mDNSu16 pktrdlength;

	rr->next              = mDNSNULL;
	
	// Field Group 1: Persistent metadata for Authoritative Records
	rr->Additional1       = mDNSNULL;
	rr->Additional2       = mDNSNULL;
	rr->DependentOn       = mDNSNULL;
	rr->RRSet             = mDNSNULL;
	rr->Callback          = mDNSNULL;
	rr->Context           = mDNSNULL;
	rr->RecordType        = RecordType;
	rr->HostTarget        = mDNSfalse;

	// Field Group 2: Transient state for Authoritative Records
	rr->Acknowledged      = mDNSfalse;
	rr->ProbeCount        = 0;
	rr->AnnounceCount     = 0;
	rr->IncludeInProbe    = mDNSfalse;
	rr->SendPriority      = 0;
	rr->Requester         = zeroIPAddr;
	rr->NextResponse      = mDNSNULL;
	rr->NR_AnswerTo       = mDNSNULL;
	rr->NR_AdditionalTo   = mDNSNULL;
	rr->LastSendTime      = 0;
	rr->NextSendTime      = 0;
	rr->NextSendInterval  = 0;
	rr->NewRData          = mDNSNULL;
	rr->UpdateCallback    = mDNSNULL;

	// Field Group 3: Transient state for Cache Records
	rr->NextDupSuppress   = mDNSNULL;
	rr->TimeRcvd          = timenow;
	rr->LastUsed          = timenow;
	rr->UseCount          = 0;
	rr->UnansweredQueries = 0;
	rr->Active            = mDNSfalse;
	rr->NewData           = mDNStrue;

	// Field Group 4: The actual information pertaining to this resource record
	rr->InterfaceAddr     = InterfaceAddr;
	ptr = getDomainName(msg, ptr, end, &rr->name);
	if (!ptr) { debugf("getResourceRecord: Malformed RR name"); return(mDNSNULL); }

	if (ptr + 10 > end) { debugf("getResourceRecord: Malformed RR -- no type/class/ttl/len!"); return(mDNSNULL); }
	
	rr->rrtype            = (mDNSu16) ((mDNSu16)ptr[0] <<  8 | ptr[1]);
	rr->rrclass           = (mDNSu16)(((mDNSu16)ptr[2] <<  8 | ptr[3]) & kDNSQClass_Mask);
	rr->rroriginalttl     = (mDNSu32) ((mDNSu32)ptr[4] << 24 | (mDNSu32)ptr[5] << 16 | (mDNSu32)ptr[6] << 8 | ptr[7]);
	if (rr->rroriginalttl > 0x70000000UL / mDNSPlatformOneSecond)
		rr->rroriginalttl = 0x70000000UL / mDNSPlatformOneSecond;
	rr->rrremainingttl    = 0;
	pktrdlength           = (mDNSu16)((mDNSu16)ptr[8] <<  8 | ptr[9]);
	if (ptr[2] & (kDNSClass_UniqueRRSet >> 8))
		rr->RecordType |= kDNSRecordTypeUniqueMask;
	ptr += 10;
	if (ptr + pktrdlength > end) { debugf("getResourceRecord: RDATA exceeds end of packet"); return(mDNSNULL); }

	if (RDataStorage)
		rr->rdata = RDataStorage;
	else
		{
		rr->rdata = &rr->rdatastorage;
		rr->rdata->MaxRDLength = sizeof(RDataBody);
		}

	switch (rr->rrtype)
		{
		case kDNSType_A:	rr->rdata->u.ip.b[0] = ptr[0];
							rr->rdata->u.ip.b[1] = ptr[1];
							rr->rdata->u.ip.b[2] = ptr[2];
							rr->rdata->u.ip.b[3] = ptr[3];
							break;

		case kDNSType_CNAME:// CNAME is same as PTR
		case kDNSType_PTR:	if (!getDomainName(msg, ptr, end, &rr->rdata->u.name))
								{ debugf("getResourceRecord: Malformed CNAME/PTR RDATA name"); return(mDNSNULL); }
							//debugf("%##s PTR %##s rdlen %d", rr->name.c, rr->rdata->u.name.c, pktrdlength);
							break;

		case kDNSType_TXT:  if (pktrdlength > rr->rdata->MaxRDLength)
								{
								debugf("getResourceRecord: TXT rdata size (%d) exceeds storage (%d)",
									pktrdlength, rr->rdata->MaxRDLength);
								return(mDNSNULL);
								}
							rr->rdata->RDLength = pktrdlength;
							mDNSPlatformMemCopy(ptr, rr->rdata->u.data, pktrdlength);
							break;

		case kDNSType_SRV:	rr->rdata->u.srv.priority = (mDNSu16)((mDNSu16)ptr[0] <<  8 | ptr[1]);
							rr->rdata->u.srv.weight   = (mDNSu16)((mDNSu16)ptr[2] <<  8 | ptr[3]);
							rr->rdata->u.srv.port.b[0] = ptr[4];
							rr->rdata->u.srv.port.b[1] = ptr[5];
							if (!getDomainName(msg, ptr+6, end, &rr->rdata->u.srv.target))
								{ debugf("getResourceRecord: Malformed SRV RDATA name"); return(mDNSNULL); }
							//debugf("%##s SRV %##s rdlen %d", rr->name.c, rr->rdata->u.srv.target.c, pktrdlength);
							break;

		default:			if (pktrdlength > rr->rdata->MaxRDLength)
								{
								debugf("getResourceRecord: rdata %d size (%d) exceeds storage (%d)",
									rr->rrtype, pktrdlength, rr->rdata->MaxRDLength);
								return(mDNSNULL);
								}
							debugf("getResourceRecord: Warning! Reading resource type %d as opaque data", rr->rrtype);
							// Note: Just because we don't understand the record type, that doesn't
							// mean we fail. The DNS protocol specifies rdlength, so we can
							// safely skip over unknown records and ignore them.
							// We also grab a binary copy of the rdata anyway, since the caller
							// might know how to interpret it even if we don't.
							rr->rdata->RDLength = pktrdlength;
							mDNSPlatformMemCopy(ptr, rr->rdata->u.data, pktrdlength);
							break;
		}

	rr->rdata->RDLength   = GetRDLength(rr, mDNSfalse);
	rr->rdestimate        = GetRDLength(rr, mDNStrue);
	return(ptr + pktrdlength);
	}

mDNSlocal const mDNSu8 *skipQuestion(const DNSMessage *msg, const mDNSu8 *ptr, const mDNSu8 *end)
	{
	ptr = skipDomainName(msg, ptr, end);
	if (!ptr) { debugf("skipQuestion: Malformed domain name in DNS question section"); return(mDNSNULL); }
	if (ptr+4 > end) { debugf("skipQuestion: Malformed DNS question section -- no query type and class!"); return(mDNSNULL); }
	return(ptr+4);
	}

mDNSlocal const mDNSu8 *getQuestion(const DNSMessage *msg, const mDNSu8 *ptr, const mDNSu8 *end, const mDNSIPAddr InterfaceAddr,
	DNSQuestion *question)
	{
	question->InterfaceAddr = InterfaceAddr;
	ptr = getDomainName(msg, ptr, end, &question->name);
	if (!ptr) { debugf("Malformed domain name in DNS question section"); return(mDNSNULL); }
	if (ptr+4 > end) { debugf("Malformed DNS question section -- no query type and class!"); return(mDNSNULL); }
	
	question->rrtype  = (mDNSu16)((mDNSu16)ptr[0] << 8 | ptr[1]);			// Get type
	question->rrclass = (mDNSu16)((mDNSu16)ptr[2] << 8 | ptr[3]);			// and class
	return(ptr+4);
	}

mDNSlocal const mDNSu8 *LocateAnswers(const DNSMessage *const msg, const mDNSu8 *const end)
	{
	int i;
	const mDNSu8 *ptr = msg->data;
	for (i = 0; i < msg->h.numQuestions && ptr; i++) ptr = skipQuestion(msg, ptr, end);
	return(ptr);
	}

mDNSlocal const mDNSu8 *LocateAuthorities(const DNSMessage *const msg, const mDNSu8 *const end)
	{
	int i;
	const mDNSu8 *ptr = LocateAnswers(msg, end);
	for (i = 0; i < msg->h.numAnswers && ptr; i++) ptr = skipResourceRecord(msg, ptr, end);
	return(ptr);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark -
#pragma mark - Packet Sending Functions
#endif

mDNSlocal mStatus mDNSSendDNSMessage(const mDNS *const m, DNSMessage *const msg, const mDNSu8 *const end,
	mDNSIPAddr src, mDNSIPPort srcport, mDNSIPAddr dst, mDNSIPPort dstport)
	{
	mStatus status;
	mDNSu16 numQuestions   = msg->h.numQuestions;
	mDNSu16 numAnswers     = msg->h.numAnswers;
	mDNSu16 numAuthorities = msg->h.numAuthorities;
	mDNSu16 numAdditionals = msg->h.numAdditionals;
	
	// Put all the integer values in IETF byte-order (MSB first, LSB second)
	mDNSu8 *ptr = (mDNSu8 *)&msg->h.numQuestions;
	*ptr++ = (mDNSu8)(numQuestions   >> 8);
	*ptr++ = (mDNSu8)(numQuestions       );
	*ptr++ = (mDNSu8)(numAnswers     >> 8);
	*ptr++ = (mDNSu8)(numAnswers         );
	*ptr++ = (mDNSu8)(numAuthorities >> 8);
	*ptr++ = (mDNSu8)(numAuthorities     );
	*ptr++ = (mDNSu8)(numAdditionals >> 8);
	*ptr++ = (mDNSu8)(numAdditionals     );
	
	// Send the packet on the wire
	status = mDNSPlatformSendUDP(m, msg, end, src, srcport, dst, dstport);
	
	// Put all the integer values back the way they were before we return
	msg->h.numQuestions   = numQuestions;
	msg->h.numAnswers     = numAnswers;
	msg->h.numAuthorities = numAuthorities;
	msg->h.numAdditionals = numAdditionals;

	return(status);
	}

mDNSlocal mDNSBool HaveResponses(const mDNS *const m, const mDNSs32 timenow)
	{
	ResourceRecord *rr;
	if (m->SleepState)
		{
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			if (rr->RecordType == kDNSRecordTypeShared && rr->rrremainingttl == 0)
				return(mDNStrue);
		}
	else
		{
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			{
			if (rr->RecordType == kDNSRecordTypeDeregistering)
				return(mDNStrue);
	
			if (rr->AnnounceCount && ResourceRecordIsValidAnswer(rr) && timenow - rr->NextSendTime >= 0)
				return(mDNStrue);
	
			if (rr->SendPriority >= kDNSSendPriorityAnswer && ResourceRecordIsValidAnswer(rr))
				return(mDNStrue);
			}
		}
	return(mDNSfalse);
	}

// NOTE: DiscardDeregistrations calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void DiscardDeregistrations(mDNS *const m, mDNSs32 timenow)
	{
	if (m->CurrentRecord) debugf("DiscardDeregistrations ERROR m->CurrentRecord already set");
	m->CurrentRecord = m->ResourceRecords;
	
	while (m->CurrentRecord)
		{
		ResourceRecord *rr = m->CurrentRecord;
		m->CurrentRecord = rr->next;
		if (rr->RecordType == kDNSRecordTypeDeregistering)
			{
			rr->RecordType    = kDNSRecordTypeShared;
			rr->AnnounceCount = DefaultAnnounceCountForTypeShared;
			mDNS_Deregister_internal(m, rr, timenow, mDNS_Dereg_normal);
			}
		}
	}

// This routine sends as many records as it can fit in a single DNS Response Message, in order of priority.
// If there are any deregistrations, announcements, or answers that don't fit, they are left in the work list for next time.
// If there are any additionals that don't fit, they are discarded -- they were optional anyway.
// NOTE: BuildResponse calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal mDNSu8 *BuildResponse(mDNS *const m,
	DNSMessage *const response, mDNSu8 *responseptr, const mDNSIPAddr InterfaceAddr, const mDNSs32 timenow)
	{
	ResourceRecord *rr;
	mDNSu8 *newptr;
	int numDereg    = 0;
	int numAnnounce = 0;
	int numAnswer   = 0;

	if (m->CurrentRecord) debugf("BuildResponse ERROR m->CurrentRecord already set");
	m->CurrentRecord = m->ResourceRecords;
	
	// If we're sleeping, only send deregistrations
	if (m->SleepState)
		{
		while (m->CurrentRecord)
			{
			ResourceRecord *rr = m->CurrentRecord;
			m->CurrentRecord = rr->next;
			if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
				rr->RecordType == kDNSRecordTypeShared && rr->rrremainingttl == 0 &&
				(newptr = putResourceRecord(response, responseptr, &response->h.numAnswers, rr, mDNSNULL, 0)))
				{
				numDereg++;
				responseptr = newptr;
				rr->rrremainingttl = rr->rroriginalttl;
				}
			}
		}
	else
		{
		// 1. Look for deregistrations we need to send
		while (m->CurrentRecord)
			{
			ResourceRecord *rr = m->CurrentRecord;
			m->CurrentRecord = rr->next;
			if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger)
				{
				if (rr->NewRData)								// If we have new data for this record
					{
					RData *OldRData = rr->rdata;
					if (ResourceRecordIsValidAnswer(rr))		// First see if we have to de-register the old data
						{
						rr->rrremainingttl = 0;					// Clear rroriginalttl before putting record
						newptr = putResourceRecord(response, responseptr, &response->h.numAnswers, rr, mDNSNULL, 0);
						if (newptr)
							{
							numDereg++;
							responseptr = newptr;
							}
						rr->rrremainingttl = rr->rroriginalttl;	// Now restore rroriginalttl
						}
					rr->rdata = rr->NewRData;	// Update our rdata
					rr->NewRData = mDNSNULL;	// Clear the NewRData pointer ...
					if (rr->UpdateCallback) rr->UpdateCallback(m, rr, OldRData);	// ... and let the client know
					}
				if (rr->RecordType == kDNSRecordTypeDeregistering &&
					(newptr = putResourceRecord(response, responseptr, &response->h.numAnswers, rr, mDNSNULL, 0)))
					{
					numDereg++;
					responseptr = newptr;
					rr->RecordType    = kDNSRecordTypeShared;
					rr->AnnounceCount = DefaultAnnounceCountForTypeShared;
					mDNS_Deregister_internal(m, rr, timenow, mDNS_Dereg_normal);
					}
				}
			}
	
		// 2. Look for announcements we are due to send in the next second
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			{
			if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
				rr->AnnounceCount && ResourceRecordIsValidAnswer(rr) &&
				timenow + mDNSPlatformOneSecond - rr->NextSendTime >= 0)
					{
					newptr = putResourceRecord(response, responseptr, &response->h.numAnswers, rr, m, timenow);
					if (newptr)
						{
						numAnnounce++;
						responseptr = newptr;
						}
					// If we were able to put the record, then update the state variables
					// If we were unable to put the record because it is too large to fit, even though
					// there are no other answers in the packet, then pretend we succeeded anyway,
					// or we'll end up in an infinite loop trying to send a record that will never fit
					if (response->h.numAnswers == 0) debugf("BuildResponse announcements failed");
					if (newptr || response->h.numAnswers == 0)
						{
						rr->SendPriority      = 0;
						rr->Requester         = zeroIPAddr;
						rr->AnnounceCount--;
						rr->NextSendTime     += rr->NextSendInterval;
						if (rr->NextSendTime - (timenow + rr->NextSendInterval/2) < 0)
							rr->NextSendTime = (timenow + rr->NextSendInterval/2);
						rr->NextSendInterval *= 2;
						}
					}
			}
	
		// 3. Look for answers we need to send
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
				rr->SendPriority >= kDNSSendPriorityAnswer && ResourceRecordIsValidAnswer(rr))
					{
					newptr = putResourceRecord(response, responseptr, &response->h.numAnswers, rr, m, timenow);
					if (newptr)
						{
						numAnswer++;
						responseptr = newptr;
						}
					// If we were able to put the record, then update the state variables
					// If we were unable to put the record because it is too large to fit, even though
					// there are no other answers in the packet then pretend we succeeded anyway,
					// or we'll end up in an infinite loop trying to send a record that will never fit
					if (response->h.numAnswers == 0) debugf("BuildResponse answers failed");
					if (newptr || response->h.numAnswers == 0)
						{
						rr->SendPriority = 0;
						rr->Requester    = zeroIPAddr;
						}
					}
	
		// 4. Add additionals, if there's space
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
				rr->SendPriority == kDNSSendPriorityAdditional)
				{
				if (ResourceRecordIsValidAnswer(rr) &&
					(newptr = putResourceRecord(response, responseptr, &response->h.numAdditionals, rr, m, timenow)))
						responseptr = newptr;
				rr->SendPriority = 0;	// Clear SendPriority anyway, even if we didn't put the additional in the packet
				rr->Requester    = zeroIPAddr;
				}
		}

	if (numDereg || numAnnounce || numAnswer || response->h.numAdditionals)
		verbosedebugf("BuildResponse Built %d Deregistration%s, %d Announcement%s, %d Answer%s, %d Additional%s",
			numDereg,                   numDereg                   == 1 ? "" : "s",
			numAnnounce,                numAnnounce                == 1 ? "" : "s",
			numAnswer,                  numAnswer                  == 1 ? "" : "s",
			response->h.numAdditionals, response->h.numAdditionals == 1 ? "" : "s");

	return(responseptr);
	}

mDNSlocal void SendResponses(mDNS *const m, const mDNSs32 timenow)
	{
	DNSMessage response;
	DNSMessageHeader baseheader;
	mDNSu8 *baselimit, *responseptr;
	NetworkInterfaceInfo *intf;
	ResourceRecord *rr, *r2;

	// Run through our list of records, and if there's a record which is supposed to be unique that we're
	// proposing to put in the packet, then make sure that we give the whole RRSet as an atomic unit.
	// That means that if we have any other records with the same name/type/class which haven't
	// already been sent in the last quarter second, then we need to mark them for sending immediately.
	// Otherwise, if we set the kDNSClass_UniqueRRSet bit on a record, then other RRSet members
	// that have not been sent recently will get flushed out of client caches.
	// Note: We have to be careful to only mark records that *haven't* been sent recently.
	// Otherwise, we can get into a pathological case where a large RRSet won't fit in a single packet,
	// so some records are left over for the next packet, and then when we come to send the next packet
	// we would mark the entire RRSet for sending again, resulting in an infinite loop packet storm.
	for (rr = m->ResourceRecords; rr; rr=rr->next)
		if (rr->RecordType & kDNSRecordTypeUniqueMask)
			if (TimeToSendThisRecord(rr,timenow))
				for (r2 = m->ResourceRecords; r2; r2=r2->next)
					if (r2 != rr && timenow - r2->LastSendTime > mDNSPlatformOneSecond/4)
						if (SameResourceRecordSignatureAnyInterface(rr, r2))
							r2->SendPriority = kDNSSendPriorityAnswer;

	// First build the generic part of the message
	InitializeDNSMessage(&response.h, zeroID, ResponseFlags);
	baselimit = BuildResponse(m, &response, response.data, zeroIPAddr, timenow);
	baseheader = response.h;

	for (intf = m->HostInterfaces; intf; intf = intf->next)
		{
		// Restore the header to the counts for the generic records
		response.h = baseheader;
		// Now add any records specific to this interface
		responseptr = BuildResponse(m, &response, baselimit, intf->ip, timenow);
		if (response.h.numAnswers > 0)	// We *never* send a packet with only additionals in it
			{
			mDNSSendDNSMessage(m, &response, responseptr, intf->ip, MulticastDNSPort, AllDNSLinkGroup, MulticastDNSPort);
			debugf("SendResponses Sent %d Answer%s, %d Additional%s on %.4a",
				response.h.numAnswers,     response.h.numAnswers     == 1 ? "" : "s",
				response.h.numAdditionals, response.h.numAdditionals == 1 ? "" : "s", &intf->ip);
			}
		}
	}

#define TimeToSendThisQuestion(Q,time) (!(Q)->DuplicateOf && time - (Q)->NextQTime >= 0)

mDNSlocal mDNSBool HaveQueries(const mDNS *const m, const mDNSs32 timenow)
	{
	ResourceRecord *rr;
	DNSQuestion *q;

	// 1. See if we've got any cache records in danger of expiring
	for (rr = m->rrcache; rr; rr=rr->next)
		if (rr->UnansweredQueries < 2)
			{
			mDNSs32 onetenth = ((mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond) / 10;
			mDNSs32 t0 = rr->TimeRcvd + (mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond;
			mDNSs32 t1 = t0 - onetenth;
			mDNSs32 t2 = t1 - onetenth;

			if (timenow - t1 >= 0 || (rr->UnansweredQueries < 1 && timenow - t2 >= 0))
				{
				DNSQuestion *q = CacheRRActive(m, rr);
				if (q) q->NextQTime = timenow;
				}
			}
	
	// 2. Scan our list of questions to see if it's time to send any of them
	for (q = m->ActiveQuestions; q; q=q->next)
		if (TimeToSendThisQuestion(q, timenow))
			return(mDNStrue);

	// 3. Scan our list of Resource Records to see if we need to send any probe questions
	for (rr = m->ResourceRecords; rr; rr=rr->next)		// Scan our list of records
		if (rr->RecordType == kDNSRecordTypeUnique && timenow - rr->NextSendTime >= 0)
			return(mDNStrue);

	return(mDNSfalse);
	}

// BuildProbe puts a probe question into a DNS Query packet and if successful, updates the value of queryptr.
// It also sets the record's IncludeInProbe flag so that we know to add an Update Record too
// and updates the forcast for the size of the duplicate suppression (answer) section.
// NOTE: BuildProbe can call a user callback, which may change the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void BuildProbe(mDNS *const m, DNSMessage *query, mDNSu8 **queryptr,
	ResourceRecord *rr, mDNSu32 *answerforecast, const mDNSs32 timenow)
	{
	if (rr->ProbeCount == 0)
		{
		rr->RecordType    = kDNSRecordTypeVerified;
		rr->AnnounceCount = DefaultAnnounceCountForRecordType(rr->RecordType);
		debugf("Probing for %##s (%s) complete", rr->name.c, DNSTypeName(rr->rrtype));
		if (!rr->Acknowledged && rr->Callback)	
			{
			// CAUTION: MUST NOT do anything more with rr after calling rr->Callback(), because the client's callback function
			// is allowed to do anything, including starting/stopping queries, registering/deregistering records, etc.
			// Right now the only code that calls BuildProbe() is BuildQueryPacketProbes(), and that uses the "m->CurrentRecord"
			// mechanism to protect against records being deleted out from under it.
			rr->Acknowledged = mDNStrue;
			rr->Callback(m, rr, mStatus_NoError);
			}
		}
	else
		{
		const mDNSu8 *const limit = query->data + ((query->h.numQuestions) ? NormalMaxDNSMessageData : AbsoluteMaxDNSMessageData);
		mDNSu8 *newptr = putQuestion(query, *queryptr, limit, &rr->name, kDNSQType_ANY, rr->rrclass);
		// We forecast: compressed name (2) type (2) class (2) TTL (4) rdlength (2) rdata (n)
		mDNSu32 forecast = *answerforecast + 12 + rr->rdestimate;
		if (newptr && newptr + forecast < limit)
			{
			*queryptr       = newptr;
			*answerforecast = forecast;
			rr->ProbeCount--;		// Only decrement ProbeCount if we successfully added the record to the packet
			rr->IncludeInProbe = mDNStrue;
			rr->NextSendTime = timenow + rr->NextSendInterval;
			}
		else
			{
			debugf("BuildProbe retracting Question %##s (%s)", rr->name.c, DNSTypeName(rr->rrtype));
			query->h.numQuestions--;
			}
		}
	}

#define MaxQuestionInterval         (3600 * mDNSPlatformOneSecond)
#define GetNextQInterval(X)         (((X)*2) <= MaxQuestionInterval ? ((X)*2) : MaxQuestionInterval)
#define GetNextSendTime(T,EARLIEST) (((T) - (EARLIEST) >= 0) ? (T) : (EARLIEST) )

// BuildQuestion puts a question into a DNS Query packet and if successful, updates the value of queryptr.
// It also appends to the list of duplicate suppression records that need to be included,
// and updates the forcast for the size of the duplicate suppression (answer) section.
mDNSlocal void BuildQuestion(mDNS *const m, DNSMessage *query, mDNSu8 **queryptr, DNSQuestion *q,
	ResourceRecord ***dups_ptr, mDNSu32 *answerforecast, const mDNSs32 timenow)
	{
	const mDNSu8 *const limit = query->data + (query->h.numQuestions ? NormalMaxDNSMessageData : AbsoluteMaxDNSMessageData);
	mDNSu8 *newptr = putQuestion(query, *queryptr, limit, &q->name, q->rrtype, q->rrclass);
	if (!newptr)
		debugf("BuildQuestion: No more space for queries");
	else
		{
		mDNSu32 forecast = *answerforecast;
		ResourceRecord *rr;
		ResourceRecord **d = *dups_ptr;
		mDNSs32 nst = timenow + q->NextQInterval;

		// If we have a resource record in our cache,
		// which is not already in the duplicate suppression list
		// which answers our question,
		// then add it to the duplicate suppression list
		for (rr=m->rrcache; rr; rr=rr->next)
			if (rr->NextDupSuppress == mDNSNULL && d != &rr->NextDupSuppress &&
				ResourceRecordAnswersQuestion(rr, q))
				{
				// Work out the latest time we should ask about this record to refresh it before it expires
				mDNSs32 onetenth = ((mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond) / 10;
				mDNSs32 t0 = rr->TimeRcvd + (mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond;
				mDNSs32 t3 = t0 - onetenth*3;

				// If we'll ask again at least twice before it expires, okay to suppress it this time
				if (t3 - nst >= 0)
					{
					*d = rr;	// Link this record into our duplicate suppression chain
					d = &rr->NextDupSuppress;
					// We forecast: compressed name (2) type (2) class (2) TTL (4) rdlength (2) rdata (n)
					forecast += 12 + rr->rdestimate;
					}
				else
					rr->UnansweredQueries++;
				}

		// If we're trying to put more than one question in this packet, and it doesn't fit
		// then undo that last question and try again next time
		if (query->h.numQuestions > 1 && newptr + forecast >= limit)
			{
			debugf("BuildQuestion retracting question %##s answerforecast %d", q->name.c, *answerforecast);
			query->h.numQuestions--;
			d = *dups_ptr;		// Go back to where we started and retract these answer records
			while (*d) { ResourceRecord *rr = *d; *d = mDNSNULL; d = &rr->NextDupSuppress; }
			}
		else
			{
			*queryptr       = newptr;		// Update the packet pointer
			*answerforecast = forecast;		// Update the forecast
			*dups_ptr       = d;			// Update the dup suppression pointer
			q->NextQTime     = nst;
			q->ThisQInterval = q->NextQInterval;
			q->NextQInterval = GetNextQInterval(q->ThisQInterval);
			}
		}
	}

// How Standard Queries are generated:
// 1. The Question Section contains the question
// 2. The Additional Section contains answers we already know, to suppress duplicate replies

// How Probe Queries are generated:
// 1. The Question Section contains queries for the name we intend to use, with QType=ANY because
// if some other host is already using *any* records with this name, we want to know about it.
// 2. The Authority Section contains the proposed values we intend to use for one or more
// of our records with that name (analogous to the Update section of DNS Update packets)
// because if some other host is probing at the same time, we each want to know what the other is
// planning, in order to apply the tie-breaking rule to see who gets to use the name and who doesn't.

mDNSlocal mDNSu8 *BuildQueryPacketQuestions(mDNS *const m, DNSMessage *query, mDNSu8 *queryptr,
	ResourceRecord ***dups_ptr, mDNSu32 *answerforecast,
	const mDNSIPAddr InterfaceAddr, const mDNSs32 timenow)
	{
	DNSQuestion *q;
	
	// See which questions need to go out right now
	for (q = m->ActiveQuestions; q; q=q->next)
		if (q->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
			TimeToSendThisQuestion(q, timenow))
			BuildQuestion(m, query, &queryptr, q, dups_ptr, answerforecast, timenow);

	// See which questions are more than half way to their NextSendTime, and send them too, if we have space
	for (q = m->ActiveQuestions; q; q=q->next)
		if (q->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
			TimeToSendThisQuestion(q, timenow + q->ThisQInterval/2))
			BuildQuestion(m, query, &queryptr, q, dups_ptr, answerforecast, timenow);

	return(queryptr);
	}

mDNSlocal mDNSu8 *BuildQueryPacketAnswers(DNSMessage *query, mDNSu8 *queryptr,
	ResourceRecord **dups_ptr, const mDNSs32 timenow)
	{
	while (*dups_ptr)
		{
		ResourceRecord *rr = *dups_ptr;
		mDNSu32 timesincercvd = (mDNSu32)(timenow - rr->TimeRcvd);
		mDNSu8 *newptr;
		// Need to update rrremainingttl correctly before we put this cache record in the packet
		rr->rrremainingttl = rr->rroriginalttl - timesincercvd / mDNSPlatformOneSecond;
		newptr = putResourceRecord(query, queryptr, &query->h.numAnswers, rr, mDNSNULL, 0);
		if (newptr)
			{
			*dups_ptr = rr->NextDupSuppress;
			rr->NextDupSuppress = mDNSNULL;
			queryptr = newptr;
			}
		else
			{
			debugf("BuildQueryPacketAnswers: Put %d answers; No more space for duplicate suppression",
				query->h.numAnswers);
			query->h.flags.b[0] |= kDNSFlag0_TC;
			break;
			}
		}
	return(queryptr);
	}

mDNSlocal mDNSu8 *BuildQueryPacketProbes(mDNS *const m, DNSMessage *query, mDNSu8 *queryptr,
	mDNSu32 *answerforecast, const mDNSIPAddr InterfaceAddr, const mDNSs32 timenow)
	{
	if (m->CurrentRecord) debugf("BuildQueryPacketProbes ERROR m->CurrentRecord already set");
	m->CurrentRecord = m->ResourceRecords;
	while (m->CurrentRecord)
		{
		ResourceRecord *rr = m->CurrentRecord;
		m->CurrentRecord = rr->next;
		if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger &&
			rr->RecordType == kDNSRecordTypeUnique && timenow - rr->NextSendTime >= 0)
			BuildProbe(m, query, &queryptr, rr, answerforecast, timenow);
		}
	return(queryptr);
	}

mDNSlocal mDNSu8 *BuildQueryPacketUpdates(mDNS *const m, DNSMessage *query, mDNSu8 *queryptr)
	{
	ResourceRecord *rr;
	for (rr = m->ResourceRecords; rr; rr=rr->next)
		if (rr->IncludeInProbe)
			{
			mDNSu8 *newptr = putResourceRecord(query, queryptr, &query->h.numAuthorities, rr, mDNSNULL, 0);
			rr->IncludeInProbe = mDNSfalse;
			if (newptr)
				queryptr = newptr;
			else
				{
				debugf("BuildQueryPacketUpdates: How did we fail to have space for the Update record %##s (%s)?",
					rr->name.c, DNSTypeName(rr->rrtype));
				break;
				}
			}
	return(queryptr);
	}

mDNSlocal void SendQueries(mDNS *const m, const mDNSs32 timenow)
	{
	ResourceRecord *NextDupSuppress = mDNSNULL;
	do
		{
		DNSMessage query;
		DNSMessageHeader baseheader;
		mDNSu8 *baselimit = query.data;
		NetworkInterfaceInfo *intf;
	
		// First build the generic part of the message
		InitializeDNSMessage(&query.h, zeroID, QueryFlags);
		if (!NextDupSuppress)
			{
			ResourceRecord **dups = &NextDupSuppress;
			mDNSu32 answerforecast = 0;
			baselimit = BuildQueryPacketQuestions(m, &query, baselimit, &dups, &answerforecast, zeroIPAddr, timenow);
			baselimit = BuildQueryPacketProbes(m, &query, baselimit, &answerforecast, zeroIPAddr, timenow);
			}
		baselimit = BuildQueryPacketAnswers(&query, baselimit, &NextDupSuppress, timenow);
		baselimit = BuildQueryPacketUpdates(m, &query, baselimit);
		baseheader = query.h;
		
		if (NextDupSuppress) debugf("SendQueries: NextDupSuppress still set... Will continue in next packet");

		for (intf = m->HostInterfaces; intf; intf = intf->next)
			{
			ResourceRecord *NextDupSuppress2 = mDNSNULL;
			do
				{
				// Restore the header to the counts for the generic records
				mDNSu8 *queryptr = baselimit;
				query.h = baseheader;
				// Now add any records specific to this interface, if we can
				if (query.h.numAnswers == 0 && query.h.numAuthorities == 0 && !NextDupSuppress)
					{
					if (!NextDupSuppress2)
						{
						ResourceRecord **dups2 = &NextDupSuppress2;
						mDNSu32 answerforecast2 = 0;
						queryptr = BuildQueryPacketQuestions(m, &query, queryptr, &dups2, &answerforecast2, intf->ip, timenow);
						queryptr = BuildQueryPacketProbes(m, &query, queryptr, &answerforecast2, intf->ip, timenow);
						}
					queryptr = BuildQueryPacketAnswers(&query, queryptr, &NextDupSuppress2, timenow);
					queryptr = BuildQueryPacketUpdates(m, &query, queryptr);
					}
	
				if (queryptr > query.data)
					{
					mDNSSendDNSMessage(m, &query, queryptr, intf->ip, MulticastDNSPort, AllDNSLinkGroup, MulticastDNSPort);
					debugf("SendQueries Sent %d Question%s %d Answer%s %d Update%s on %.4a",
						query.h.numQuestions,   query.h.numQuestions   == 1 ? "" : "s",
						query.h.numAnswers,     query.h.numAnswers     == 1 ? "" : "s",
						query.h.numAuthorities, query.h.numAuthorities == 1 ? "" : "s", &intf->ip);
					}
				} while (NextDupSuppress2);
			}
		} while (NextDupSuppress);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - RR List Management & Task Management
#endif

// rr is a new ResourceRecord just received into our cache
// (kDNSRecordTypePacketAnswer/kDNSRecordTypePacketAdditional/kDNSRecordTypePacketUniqueAns/kDNSRecordTypePacketUniqueAdd)
mDNSlocal void TriggerImmediateQuestions(mDNS *const m, const ResourceRecord *const rr, const mDNSs32 timenow)
	{
	// If we just received a new record off the wire that we've never seen before, we want to ask our question again
	// soon, and keep doing that repeatedly (with duplicate suppression) until we stop getting any more responses
	mDNSs32 needquery = timenow + mDNSPlatformOneSecond;
	DNSQuestion *q;
	for (q = m->ActiveQuestions; q; q=q->next)		// Scan our list of questions
		if (!q->DuplicateOf && q->NextQTime - needquery > 0 && ResourceRecordAnswersQuestion(rr, q))
			{
			q->NextQTime     = needquery;
			// As long as responses are still coming in, don't do the exponential backoff
			q->NextQInterval = q->ThisQInterval;
			}
	}

// NOTE: AnswerQuestionWithResourceRecord can call a user callback, which may change the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void AnswerQuestionWithResourceRecord(mDNS *const m, DNSQuestion *q, ResourceRecord *rr, const mDNSs32 timenow)
	{
	mDNSu32 timesincercvd = (mDNSu32)(timenow - rr->TimeRcvd);
	if (rr->rroriginalttl <= timesincercvd / mDNSPlatformOneSecond) rr->rrremainingttl = 0;
	else rr->rrremainingttl = rr->rroriginalttl - timesincercvd / mDNSPlatformOneSecond;

#if MDNS_DEBUGMSGS
	if (rr->rrremainingttl)
		{
		if (rr->rrtype == kDNSType_TXT)
			debugf("AnswerQuestionWithResourceRecord Add %##s TXT %#.20s remaining ttl %d",
				rr->name.c, rr->rdata->u.txt.c, rr->rrremainingttl);
		else
			debugf("AnswerQuestionWithResourceRecord Add %##s (%s) remaining ttl %d",
				rr->name.c, DNSTypeName(rr->rrtype), rr->rrremainingttl);
		}
	else
		{
		if (rr->rrtype == kDNSType_TXT)
			debugf("AnswerQuestionWithResourceRecord Del %##s TXT %#.20s UnansweredQueries %d",
				rr->name.c, rr->rdata->u.txt.c, rr->UnansweredQueries);
		else
			debugf("AnswerQuestionWithResourceRecord Del %##s (%s) UnansweredQueries %d",
				rr->name.c, DNSTypeName(rr->rrtype), rr->UnansweredQueries);
		}
#endif

	rr->LastUsed = timenow;
	rr->UseCount++;

	// CAUTION: MUST NOT do anything more with q after calling q->Callback(), because the client's callback function
	// is allowed to do anything, including starting/stopping queries, registering/deregistering records, etc.
	// Right now the only routines that call AnswerQuestionWithResourceRecord() are AnswerLocalQuestions() and
	// AnswerNewQuestion(), and both of them use the "m->CurrentQuestion" mechanism to protect against questions
	// being deleted out from under them.
	if (q->Callback) q->Callback(m, q, rr);
	}

// AnswerLocalQuestions is called from mDNSCoreReceiveResponse,
// and from TidyRRCache, which is called from mDNSCoreTask and from mDNSCoreReceiveResponse
// AnswerLocalQuestions is *never* called directly as a result of a client API call
// If new questions are created as a result of invoking client callbacks, they will be added to
// the end of the question list, and m->NewQuestions will be set to indicate the first new question.
// rr is a ResourceRecord in our cache
// (kDNSRecordTypePacketAnswer/kDNSRecordTypePacketAdditional/kDNSRecordTypePacketUniqueAns/kDNSRecordTypePacketUniqueAdd)
// NOTE: AnswerLocalQuestions calls AnswerQuestionWithResourceRecord which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void AnswerLocalQuestions(mDNS *const m, ResourceRecord *rr, const mDNSs32 timenow)
	{
	if (m->CurrentQuestion) debugf("AnswerLocalQuestions ERROR m->CurrentQuestion already set");
	m->CurrentQuestion = m->ActiveQuestions;
	while (m->CurrentQuestion && m->CurrentQuestion != m->NewQuestions)
		{
		DNSQuestion *q = m->CurrentQuestion;
		m->CurrentQuestion = q->next;
		if (ResourceRecordAnswersQuestion(rr, q))
			AnswerQuestionWithResourceRecord(m, q, rr, timenow);
		}
	m->CurrentQuestion = mDNSNULL;
	}

mDNSlocal void AnswerNewQuestion(mDNS *const m, const mDNSs32 timenow)
	{
	ResourceRecord *rr;
	DNSQuestion *q = m->NewQuestions;		// Grab the question we're going to answer
	m->NewQuestions = q->next;				// Advance NewQuestions to the next (if any)

	if (m->lock_rrcache) debugf("AnswerNewQuestion ERROR! Cache already locked!");
	// This should be safe, because calling the client's question callback may cause the
	// question list to be modified, but should not ever cause the rrcache list to be modified.
	// If the client's question callback deletes the question, then m->CurrentQuestion will
	// be advanced, and we'll exit out of the loop
	m->lock_rrcache = 1;
	if (m->CurrentQuestion) debugf("AnswerNewQuestion ERROR m->CurrentQuestion already set");
	m->CurrentQuestion = q;		// Indicate which question we're answering, so we'll know if it gets deleted
	for (rr=m->rrcache; rr && m->CurrentQuestion == q; rr=rr->next)
		if (ResourceRecordAnswersQuestion(rr, q))
			AnswerQuestionWithResourceRecord(m, q, rr, timenow);
	m->CurrentQuestion = mDNSNULL;
	m->lock_rrcache = 0;
	}

mDNSlocal void FlushCacheRecords(mDNS *const m, mDNSIPAddr InterfaceAddr, const mDNSs32 timenow)
	{
	mDNSu32 count = 0;
	ResourceRecord *rr;
	for (rr = m->rrcache; rr; rr=rr->next)
		{
		if (rr->InterfaceAddr.NotAnInteger == InterfaceAddr.NotAnInteger)
			{
			// If the record's interface matches the one we're flushing,
			// then pretend we just received a 'goodbye' packet for this record.
			rr->TimeRcvd          = timenow;
			rr->UnansweredQueries = 0;
			rr->rroriginalttl     = 1;
			count++;
			}
		}
	
	if (count) debugf("FlushCacheRecords Flushing %d Cache Entries on interface %.4a", count, &InterfaceAddr);
	}

// TidyRRCache
// Throw away any cache records that have passed their TTL
// First we prepare a list of records to delete, and pull them off the rrcache list
// Then we go through the list of records to delete, calling the user's question callbacks if necessary
// We do it in two phases like this to guard against the user's question callbacks modifying
// the rrcache list while we're walking it.
mDNSlocal void TidyRRCache(mDNS *const m, const mDNSs32 timenow)
	{
	mDNSu32 count = 0;
	ResourceRecord **rr = &m->rrcache;
	ResourceRecord *deletelist = mDNSNULL;
	
	if (m->lock_rrcache) { debugf("TidyRRCache ERROR! Cache already locked!"); return; }
	m->lock_rrcache = 1;
	
	while (*rr)
		{
		mDNSu32 timesincercvd = (mDNSu32)(timenow - (*rr)->TimeRcvd);
		if ((*rr)->rroriginalttl > timesincercvd / mDNSPlatformOneSecond)
			rr=&(*rr)->next;			// If TTL is greater than time elapsed, save this record
		else
			{
			ResourceRecord *r = *rr;	// Else,
			*rr = r->next;				// detatch this record from the cache list
			r->next = deletelist;		// and move it onto the list of things to delete
			deletelist = r;
			count++;
			}
		}
	
	if (count) verbosedebugf("TidyRRCache Deleting %d Expired Cache Entries", count);

	m->lock_rrcache = 0;

	while (deletelist)
		{
		ResourceRecord *r = deletelist;
		verbosedebugf("TidyRRCache: Deleted %##s (%s)", r->name.c, DNSTypeName(r->rrtype));
		deletelist = deletelist->next;
		AnswerLocalQuestions(m, r, timenow);
		r->next = m->rrcache_free;	// and move it back to the free list
		m->rrcache_free = r;
		m->rrcache_used--;
		}
	}

mDNSlocal ResourceRecord *GetFreeCacheRR(mDNS *const m, const mDNSs32 timenow)
	{
	ResourceRecord *r = m->rrcache_free;

	if (m->lock_rrcache) { debugf("GetFreeCacheRR ERROR! Cache already locked!"); return(mDNSNULL); }
	m->lock_rrcache = 1;
	
	if (r)		// If there are records in the free list, take one
		{
		m->rrcache_free = r->next;
		m->rrcache_used++;
		if (m->rrcache_used >= m->rrcache_report)
			{
			debugf("RR Cache now using %d records", m->rrcache_used);
			m->rrcache_report *= 2;
			}
		}
	else		// Else search for a candidate to recycle
		{
		ResourceRecord **rr = &m->rrcache;
		ResourceRecord **best = mDNSNULL;
		mDNSs32 bestage = -1;

		while (*rr)
			{
			mDNSs32 timesincercvd = timenow - (*rr)->TimeRcvd;

			// Records we've only just received are not candidates for deletion
			if (timesincercvd > 0)
				{
				// Work out a weighted age, which is the number of seconds since this record was last used,
				// divided by the number of times it has been used (we want to keep frequently used records longer).
				mDNSs32 count = (*rr)->UseCount < 100 ? 1 + (mDNSs32)(*rr)->UseCount : 100;
				mDNSs32 age = (timenow - (*rr)->LastUsed) / count;
				mDNSu8 rtype = (mDNSu8)(((*rr)->RecordType) & ~kDNSRecordTypeUniqueMask);
				if (rtype == kDNSRecordTypePacketAnswer) age /= 2;		// Keep answer records longer than additionals

				// Records that answer still-active questions are not candidates for deletion
				if (bestage < age && !CacheRRActive(m, *rr)) { best = rr; bestage = age; }
				}

			rr=&(*rr)->next;
			}

		if (best)
			{
			r = *best;							// Remember the record we chose
			*best = r->next;					// And detatch it from the free list
			}
		}

	m->lock_rrcache = 0;

	if (r) mDNSPlatformMemZero(r, sizeof(*r));
	return(r);
	}

mDNSlocal void ScheduleNextTask(const mDNS *const m)
	{
	const mDNSs32 timenow = mDNSPlatformTimeNow();
	mDNSs32 nextevent = timenow + 0x78000000;
	const char *msg = "No Event", *sign="";
	mDNSs32 interval, fraction;

	DNSQuestion *q;
	ResourceRecord *rr;
	
	if (m->mDNSPlatformStatus != mStatus_NoError)
		return;
	
	// 1. If sleeping, do nothing
	if (m->SleepState)
		{
		debugf("ScheduleNextTask: Sleeping");
		return;
		}
	
	// 2. If we have new questions added to the list, we need to answer them from cache ASAP
	if (m->NewQuestions)
		{
		nextevent = timenow;
		msg = "New Questions";
		}
	else
		{
		// 3. Scan cache to see if any resource records are going to expire
		for (rr = m->rrcache; rr; rr=rr->next)
			{
			mDNSs32 onetenth = ((mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond) / 10;
			mDNSs32 t0 = rr->TimeRcvd + (mDNSs32)rr->rroriginalttl * mDNSPlatformOneSecond;
			mDNSs32 t1 = t0 - onetenth;
			mDNSs32 t2 = t1 - onetenth;
			if (rr->UnansweredQueries < 1 && nextevent - t2 > 0 && CacheRRActive(m, rr))
				{
				nextevent = t2;
				msg = "Penultimate Query";
				}
			else if (rr->UnansweredQueries < 2 && nextevent - t1 > 0 && CacheRRActive(m, rr))
				{
				nextevent = t1;
				msg = "Final Expiration Query";
				}
			else if (nextevent - t0 > 0)
				{
				nextevent = t0;
				msg = "Cache Tidying";
				}
			}
		
		// 4. If we're suppressing sending right now, don't bother searching for packet generation events --
		// but do make sure we come back at the end of the suppression time to check again
		if (m->SuppressSending)
			{
			if (nextevent - m->SuppressSending > 0)
				{
				nextevent = m->SuppressSending;
				msg = "Send Suppressed Packets";
				}
			}
		else
			{
			// 5. Scan list of active questions to see if we need to send any queries
			for (q = m->ActiveQuestions; q; q=q->next)
				if (TimeToSendThisQuestion(q, nextevent))
					{
					nextevent = q->NextQTime;
					msg = "Send Questions";
					}

			// 6. Scan list of local resource records to see if we have any
			// deregistrations, probes, announcements, or replies to send
			for (rr = m->ResourceRecords; rr; rr=rr->next)
				{
				if (rr->RecordType == kDNSRecordTypeDeregistering)
					{
					nextevent = timenow;
					msg = "Send Deregistrations";
					}
				else if (rr->SendPriority >= kDNSSendPriorityAnswer && ResourceRecordIsValidAnswer(rr))
					{
					nextevent = timenow;
					msg = "Send Answers";
					}
				else if (rr->RecordType == kDNSRecordTypeUnique && nextevent - rr->NextSendTime > 0)
					{
					nextevent = rr->NextSendTime;
					msg = "Send Probes";
					}
				else if (rr->AnnounceCount && nextevent - rr->NextSendTime > 0 && ResourceRecordIsValidAnswer(rr))
					{
					nextevent = rr->NextSendTime;
					msg = "Send Announcements";
					}
				}
			}
		}

	interval = nextevent - timenow;
	if (interval < 0) { interval = -interval; sign = "-"; }
	fraction = interval % mDNSPlatformOneSecond;
	verbosedebugf("ScheduleNextTask: Next event: <%s> in %s%d.%03d seconds", msg, sign,
		interval / mDNSPlatformOneSecond, fraction * 1000 / mDNSPlatformOneSecond);

	mDNSPlatformScheduleTask(m, nextevent);
	}

mDNSlocal mDNSs32 mDNS_Lock(mDNS *const m)
	{
	mDNSPlatformLock(m);
	++m->mDNS_busy;
	return(mDNSPlatformTimeNow());
	}

mDNSlocal void mDNS_Unlock(mDNS *const m)
	{
	// Upon unlocking, we've usually added some new work to the task list.
	// If we don't decrement mDNS_busy to zero, then we don't have to worry about calling
	// ScheduleNextTask(), because the last lock holder will do it for us on the way out.
	if (--m->mDNS_busy == 0) ScheduleNextTask(m);
	mDNSPlatformUnlock(m);
	}

mDNSexport void mDNSCoreTask(mDNS *const m)
	{
	const mDNSs32 timenow = mDNS_Lock(m);

	verbosedebugf("mDNSCoreTask");
	if (m->mDNS_busy > 1) debugf("mDNSCoreTask: Locking failure! mDNS already busy");
	if (m->CurrentQuestion) debugf("mDNSCoreTask: ERROR! m->CurrentQuestion already set");
	
	if (m->SuppressProbes && timenow - m->SuppressProbes >= 0)
		m->SuppressProbes = 0;

	// 1. See if we can answer any of our new local questions from the cache
	while (m->NewQuestions) AnswerNewQuestion(m, timenow);

	// 2. See what packets we need to send
	if (m->mDNSPlatformStatus != mStatus_NoError || m->SleepState)
		{
		// If the platform code is currently non-operational,
		// then we'll just complete deregistrations immediately,
		// without waiting for the goodbye packet to be sent
		DiscardDeregistrations(m, timenow);
		}
	else if (m->SuppressSending == 0 || timenow - m->SuppressSending >= 0)
		{
		// If the platform code is ready,
		// and we're not suppressing packet generation right now
		// send our responses, probes, and questions
		m->SuppressSending = 0;
		while (HaveResponses(m, timenow)) SendResponses(m, timenow);
		while (HaveQueries  (m, timenow)) SendQueries  (m, timenow);
		}

	if (m->rrcache_size) TidyRRCache(m, timenow);

	mDNS_Unlock(m);
	}

// Call mDNSCoreSleep(m, mDNStrue) when the machine is about to go to sleep.
// Call mDNSCoreSleep(m, mDNSfalse) when the machine is has just woken up.
// Normally, the platform support layer below mDNSCore should call this, not the client layer above.
// Note that sleep/wake calls do not have to be paired one-for-one; it is acceptable to call
// mDNSCoreSleep(m, mDNSfalse) any time there is reason to believe that the machine may have just
// found itself in a new network environment. For example, if the Ethernet hardware indicates that the
// cable has just been connected, the platform support layer should call mDNSCoreSleep(m, mDNSfalse)
// to make mDNSCore re-issue its outstanding queries, probe for record uniqueness, etc.
// While it is safe to call mDNSCoreSleep(m, mDNSfalse) at any time, it does cause extra network
// traffic, so it should only be called when there is legitimate reason to believe the machine
// may have become attached to a new network.
mDNSexport void mDNSCoreSleep(mDNS *const m, mDNSBool sleepstate)
	{
	ResourceRecord *rr;
	const mDNSs32 timenow = mDNS_Lock(m);

	m->SleepState = sleepstate;
	debugf("mDNSCoreSleep: %d", sleepstate);

	if (sleepstate)
		{
		// First mark all the records we need to deregister
		for (rr = m->ResourceRecords; rr; rr=rr->next)
			if (rr->RecordType == kDNSRecordTypeShared && rr->AnnounceCount < DefaultAnnounceCountForTypeShared)
				rr->rrremainingttl = 0;
		while (HaveResponses(m, timenow)) SendResponses(m, timenow);
		}
	else
		{
		DNSQuestion *q;

		for (rr = m->ResourceRecords; rr; rr=rr->next)
			{
			if (rr->RecordType == kDNSRecordTypeVerified) rr->RecordType = kDNSRecordTypeUnique;
			rr->ProbeCount        = (rr->RecordType == kDNSRecordTypeUnique) ? DefaultProbeCountForTypeUnique : (mDNSu8)0;
			rr->AnnounceCount     = DefaultAnnounceCountForRecordType(rr->RecordType);
			rr->NextSendInterval  = DefaultSendIntervalForRecordType(rr->RecordType);
			rr->NextSendTime      = timenow;
			}
		for (q = m->ActiveQuestions; q; q=q->next)		// Scan our list of questions
			if (!q->DuplicateOf)
				{
				q->NextQTime     = timenow;
				q->ThisQInterval = mDNSPlatformOneSecond;  // MUST NOT be zero for an active question
				q->NextQInterval = mDNSPlatformOneSecond;
				}
		}

	mDNS_Unlock(m);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Packet Reception Functions
#endif

mDNSlocal mDNSBool AddRecordToResponseList(ResourceRecord **nrp,
	ResourceRecord *rr, const mDNSu8 *answerto, ResourceRecord *additionalto)
	{
	if (rr->NextResponse == mDNSNULL && nrp != &rr->NextResponse)
		{
		*nrp = rr;
		rr->NR_AnswerTo     = answerto;
		rr->NR_AdditionalTo = additionalto;
		return(mDNStrue);
		}
	else debugf("AddRecordToResponseList: %##s (%s) already in list", rr->name.c, DNSTypeName(rr->rrtype));
	return(mDNSfalse);
	}

#define MustSendRecord(RR) ((RR)->NR_AnswerTo || (RR)->NR_AdditionalTo)

mDNSlocal mDNSu8 *GenerateUnicastResponse(const DNSMessage *const query, const mDNSu8 *const end,
	const mDNSIPAddr InterfaceAddr, DNSMessage *const reply, ResourceRecord  *ResponseRecords)
	{
	const mDNSu8    *const limit     = reply->data + sizeof(reply->data);
	const mDNSu8    *ptr             = query->data;
	mDNSu8          *responseptr     = reply->data;
	ResourceRecord  *rr;
	int i;

	// Initialize the response fields so we can answer the questions
	InitializeDNSMessage(&reply->h, query->h.id, ResponseFlags);

	// ***
	// *** 1. Write out the list of questions we are actually going to answer with this packet
	// ***
	for (i=0; i<query->h.numQuestions; i++)						// For each question...
		{
		DNSQuestion q;
		ptr = getQuestion(query, ptr, end, InterfaceAddr, &q);	// get the question...
		if (!ptr) return(mDNSNULL);

		for (rr=ResponseRecords; rr; rr=rr->NextResponse)		// and search our list of proposed answers
			{
			if (rr->NR_AnswerTo == ptr)							// If we're going to generate a record answering this question
				{												// then put the question in the question section
				responseptr = putQuestion(reply, responseptr, limit, &q.name, q.rrtype, q.rrclass);
				if (!responseptr) { debugf("GenerateUnicastResponse: Ran out of space for questions!"); return(mDNSNULL); }
				break;		// break out of the ResponseRecords loop, and go on to the next question
				}
			}
		}

	if (reply->h.numQuestions == 0) { debugf("GenerateUnicastResponse: ERROR! Why no questions?"); return(mDNSNULL); }

	// ***
	// *** 2. Write answers and additionals
	// ***
	for (rr=ResponseRecords; rr; rr=rr->NextResponse)
		{
		if (MustSendRecord(rr))
			{
			if (rr->NR_AnswerTo)
				{
				mDNSu8 *p = putResourceRecord(reply, responseptr, &reply->h.numAnswers, rr, mDNSNULL, 0);
				if (p) responseptr = p;
				else { debugf("GenerateUnicastResponse: Ran out of space for answers!"); reply->h.flags.b[0] |= kDNSFlag0_TC; }
				}
			else
				{
				mDNSu8 *p = putResourceRecord(reply, responseptr, &reply->h.numAdditionals, rr, mDNSNULL, 0);
				if (p) responseptr = p;
				else debugf("GenerateUnicastResponse: No more space for additionals");
				}
			}
		}
	return(responseptr);
	}

// ResourceRecord *pktrr is the ResourceRecord from the response packet we've witnessed on the network
// ResourceRecord *rr is our ResourceRecord
// Returns 0 if there is no conflict
// Returns +1 if there was a conflict and we won
// Returns -1 if there was a conflict and we lost and have to rename
mDNSlocal int CompareRData(ResourceRecord *pkt, ResourceRecord *our)
	{
	mDNSu8 pktdata[256], *pktptr = pktdata, *pktend;
	mDNSu8 ourdata[256], *ourptr = ourdata, *ourend;
	if (!pkt) { debugf("CompareRData ERROR: pkt is NULL"); return(+1); }
	if (!our) { debugf("CompareRData ERROR: our is NULL"); return(+1); }

	pktend = putRData(mDNSNULL, pktdata, pktdata + sizeof(pktdata), pkt->rrtype, pkt->rdata);
	ourend = putRData(mDNSNULL, ourdata, ourdata + sizeof(ourdata), our->rrtype, our->rdata);
	while (pktptr < pktend && ourptr < ourend && *pktptr == *ourptr) { pktptr++; ourptr++; }
	if (pktptr >= pktend && ourptr >= ourend) return(0);			// If data identical, not a conflict

	if (pktptr >= pktend) return(-1);								// Packet data is substring; We lost
	if (ourptr >= ourend) return(+1);								// Our data is substring; We won
	if (*pktptr < *ourptr) return(-1);								// Packet data is numerically lower; We lost
	if (*pktptr > *ourptr) return(+1);								// Our data is numerically lower; We won
	
	debugf("CompareRData: How did we get here?");
	return(-1);
	}

// Find the canonical DependentOn record for this RR received in a packet.
// The DependentOn pointer is typically used for the TXT record of service registrations
// It indicates that there is no inherent conflict detection for the TXT record
// -- it depends on the SRV record to resolve name conflicts
// If we find any identical ResourceRecord in our authoritative list, then follow its DependentOn
// pointers (if any) to make sure we return the canonical DependentOn record
// If the record has no DependentOn, then just return that record's pointer
// Returns NULL if we don't have any local RRs that are identical to the one from the packet
mDNSlocal const ResourceRecord *FindDependentOn(const mDNS *const m, const ResourceRecord *const pktrr)
	{
	const ResourceRecord *rr;
	for (rr = m->ResourceRecords; rr; rr=rr->next)
		{
		if (IdenticalResourceRecordAnyInterface(rr, pktrr))
			{
			while (rr->DependentOn) rr = rr->DependentOn;
			return(rr);
			}
		}
	return(mDNSNULL);
	}

// Find the canonical RRSet pointer for this RR received in a packet.
// If we find any identical ResourceRecord in our authoritative list, then follow its RRSet
// pointers (if any) to make sure we return the canonical member of this name/type/class
// Returns NULL if we don't have any local RRs that are identical to the one from the packet
mDNSlocal const ResourceRecord *FindRRSet(const mDNS *const m, const ResourceRecord *const pktrr)
	{
	const ResourceRecord *rr;
	for (rr = m->ResourceRecords; rr; rr=rr->next)
		{
		if (IdenticalResourceRecordAnyInterface(rr, pktrr))
			{
			while (rr->RRSet && rr != rr->RRSet) rr = rr->RRSet;
			return(rr);
			}
		}
	return(mDNSNULL);
	}

// PacketRRConflict is called when we've received an RR (pktrr) which has the same name
// as one of our records (our) but different rdata.
// 1. If our record is not a type that's supposed to be unique, we don't care.
// 2a. If our record is marked as dependent on some other record for conflict detection, ignore this one.
// 2b. If the packet rr exactly matches one of our other RRs, and *that* record's DependentOn pointer
//     points to our record, ignore this conflict (e.g. the packet record matches one of our
//     TXT records, and that record is marked as dependent on 'our', its SRV record).
// 3. If we have some *other* RR that exactly matches the one from the packet, and that record and our record
//    are members of the same RRSet, then this is not a conflict.
mDNSlocal mDNSBool PacketRRConflict(const mDNS *const m, const ResourceRecord *const our, const ResourceRecord *const pktrr)
	{
	const ResourceRecord *ourset = our->RRSet ? our->RRSet : our;

	// If not supposed to be unique, not a conflict
	if (!(our->RecordType & kDNSRecordTypeUniqueMask)) return(mDNSfalse);

	// If a dependent record, not a conflict
	if (our->DependentOn || FindDependentOn(m, pktrr) == our) return(mDNSfalse);

	// If the pktrr matches a member of ourset, not a conflict
	if (FindRRSet(m, pktrr) == ourset) return(mDNSfalse);

	// Okay, this is a conflict
	return(mDNStrue);
	}

// NOTE: ResolveSimultaneousProbe calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void ResolveSimultaneousProbe(mDNS *const m, const DNSMessage *const query, const mDNSu8 *const end,
	DNSQuestion *q, ResourceRecord *our, const mDNSs32 timenow)
	{
	int i;
	const mDNSu8 *ptr = LocateAuthorities(query, end);
	mDNSBool FoundUpdate = mDNSfalse;

	for (i = 0; i < query->h.numAuthorities; i++)
		{
		ResourceRecord pktrr;
		ptr = getResourceRecord(query, ptr, end, q->InterfaceAddr, 0, 0, &pktrr, mDNSNULL);
		if (!ptr) break;
		if (ResourceRecordAnswersQuestion(&pktrr, q))
			{
			FoundUpdate = mDNStrue;
			if (PacketRRConflict(m, our, &pktrr))
				{
				int result          = (int)pktrr.rrclass - (int)our->rrclass;
				if (!result) result = (int)pktrr.rrtype  - (int)our->rrtype;
				if (!result) result = CompareRData(&pktrr, our);
				switch (result)
					{
					case  1:	debugf("ResolveSimultaneousProbe: %##s (%s): We won",  our->name.c, DNSTypeName(our->rrtype));
								break;
					case  0:	break;
					case -1:	debugf("ResolveSimultaneousProbe: %##s (%s): We lost", our->name.c, DNSTypeName(our->rrtype));
								mDNS_Deregister_internal(m, our, timenow, mDNS_Dereg_conflict);
								return;
					}
				}
			}
		}
	if (!FoundUpdate)
		debugf("ResolveSimultaneousProbe: %##s (%s): No Update Record found", our->name.c, DNSTypeName(our->rrtype));
	}

// ProcessQuery examines a received query to see if we have any answers to give
mDNSlocal mDNSu8 *ProcessQuery(mDNS *const m, const DNSMessage *const query, const mDNSu8 *const end,
	const mDNSIPAddr srcaddr, const mDNSIPAddr InterfaceAddr,
	DNSMessage *const replyunicast, mDNSBool replymulticast, const mDNSs32 timenow)
	{
	ResourceRecord  *ResponseRecords = mDNSNULL;
	ResourceRecord **nrp             = &ResponseRecords;
	mDNSBool         delayresponse   = mDNSfalse;
	mDNSBool         answers         = mDNSfalse;
	const mDNSu8    *ptr             = query->data;
	mDNSu8          *responseptr     = mDNSNULL;
	ResourceRecord  *rr, *rr2;
	int i;

	// If TC flag is set, it means we should expect additional duplicate suppression info may be coming in another packet.
	if (query->h.flags.b[0] & kDNSFlag0_TC) delayresponse = mDNStrue;

	// ***
	// *** 1. Parse Question Section and mark potential answers
	// ***
	for (i=0; i<query->h.numQuestions; i++)						// For each question...
		{
		int NumAnswersForThisQuestion = 0;
		DNSQuestion q;
		ptr = getQuestion(query, ptr, end, InterfaceAddr, &q);	// get the question...
		if (!ptr) goto exit;
		
		// Note: We use the m->CurrentRecord mechanism here because calling ResolveSimultaneousProbe
		// can result in user callbacks which may change the record list and/or question list.
		// Also note: we just mark potential answer records here, without trying to build the
		// "ResponseRecords" list, because we don't want to risk user callbacks deleting records
		//  from that list while we're in the middle of trying to build it.
		if (m->CurrentRecord) debugf("ProcessQuery ERROR m->CurrentRecord already set");
		m->CurrentRecord = m->ResourceRecords;
		while (m->CurrentRecord)
			{
			rr = m->CurrentRecord;
			m->CurrentRecord = rr->next;
			if (ResourceRecordAnswersQuestion(rr, &q))
				{
				if (rr->RecordType == kDNSRecordTypeUnique)
					ResolveSimultaneousProbe(m, query, end, &q, rr, timenow);
				else if (ResourceRecordIsValidAnswer(rr))
					{
					NumAnswersForThisQuestion++;
					if (!rr->NR_AnswerTo) rr->NR_AnswerTo = ptr;	// Mark as potential answer
					}
				}
			}
		// If we couldn't answer this question, someone else might be able to,
		// so use random delay on response to reduce collisions
		if (NumAnswersForThisQuestion == 0) delayresponse = mDNStrue;
		}

	// ***
	// *** 2. Now we can safely build the list of marked answers
	// ***
	for (rr = m->ResourceRecords; rr; rr=rr->next)				// Now build our list of potential answers
		if (rr->NR_AnswerTo)									// If we marked the record...
			if (AddRecordToResponseList(nrp, rr, rr->NR_AnswerTo, mDNSNULL))	// ... add it to the list
				{
				nrp = &rr->NextResponse;
				if (rr->RecordType == kDNSRecordTypeShared) delayresponse = mDNStrue;
				}

	// ***
	// *** 3. Add additional records
	// ***
	for (rr=ResponseRecords; rr; rr=rr->NextResponse)			// For each record we plan to put
		{
		// (Note: This is an "if", not a "while". If we add a record, we'll find it again
		// later in the "for" loop, and we will follow further "additional" links then.)
		if (rr->Additional1 && ResourceRecordIsValidInterfaceAnswer(rr->Additional1, InterfaceAddr) &&
			AddRecordToResponseList(nrp, rr->Additional1, mDNSNULL, rr))
			nrp = &rr->Additional1->NextResponse;

		if (rr->Additional2 && ResourceRecordIsValidInterfaceAnswer(rr->Additional2, InterfaceAddr) &&
			AddRecordToResponseList(nrp, rr->Additional2, mDNSNULL, rr))
			nrp = &rr->Additional2->NextResponse;
		
		// For SRV records, automatically add the Address record(s) for the target host
		if (rr->rrtype == kDNSType_SRV)
			for (rr2=m->ResourceRecords; rr2; rr2=rr2->next)					// Scan list of resource records
				if (rr2->rrtype == kDNSType_A &&								// For all records type "A" ...
					ResourceRecordIsValidInterfaceAnswer(rr2, InterfaceAddr) &&	// ... which are valid for answer ...
					SameDomainName(&rr->rdata->u.srv.target, &rr2->name) &&		// ... whose name is the name of the SRV target
					AddRecordToResponseList(nrp, rr2, mDNSNULL, rr))
					nrp = &rr2->NextResponse;
		}

	// ***
	// *** 4. Parse Answer Section and cancel any records disallowed by duplicate suppression
	// ***
	for (i=0; i<query->h.numAnswers; i++)						// For each record in the query's answer section...
		{
		// Get the record...
		ResourceRecord pktrr, *rr;
		ptr = getResourceRecord(query, ptr, end, InterfaceAddr, timenow, kDNSRecordTypePacketAnswer, &pktrr, mDNSNULL);
		if (!ptr) goto exit;

		// See if it suppresses any of our planned answers
		for (rr=ResponseRecords; rr; rr=rr->NextResponse)
			if (MustSendRecord(rr) && SuppressDuplicate(&pktrr, rr))
				{ rr->NR_AnswerTo = mDNSNULL; rr->NR_AdditionalTo = mDNSNULL; }

		// And see if it suppresses any previously scheduled answers
		for (rr=m->ResourceRecords; rr; rr=rr->next)
			{
			// If this record has been requested by exactly one client, and that client is
			// the same one sending this query, then allow inter-packet duplicate suppression
			if (rr->Requester.NotAnInteger && rr->Requester.NotAnInteger == srcaddr.NotAnInteger)
				if (SuppressDuplicate(&pktrr, rr))
					{
					rr->SendPriority = 0;
					rr->Requester    = zeroIPAddr;
					}
			}
		}

	// ***
	// *** 5. Cancel any additionals that were added because of now-deleted records
	// ***
	for (rr=ResponseRecords; rr; rr=rr->NextResponse)
		if (rr->NR_AdditionalTo && !MustSendRecord(rr->NR_AdditionalTo))
			{ rr->NR_AnswerTo = mDNSNULL; rr->NR_AdditionalTo = mDNSNULL; }

	// ***
	// *** 6. Mark the send flags on the records we plan to send
	// ***
	for (rr=ResponseRecords; rr; rr=rr->NextResponse)
		{
		if (MustSendRecord(rr))
			{
			// For oversized records which we are going to send back to the requester via unicast
			// anyway, don't waste network bandwidth by also sending them via multicast.
			// This means we lose passive conflict detection for these oversized records, but
			// that is a reasonable tradeoff -- these large records usually have an associated
			// SRV record with the same name which will catch conflicts for us anyway.
			mDNSBool LargeRecordWithUnicastReply = (rr->rdestimate > 1024 && replyunicast);

			if (rr->NR_AnswerTo)
				answers = mDNStrue;

			if (replymulticast && !LargeRecordWithUnicastReply)
				{
				// If this query has additional duplicate suppression info
				// coming in another packet, then remember the requesting IP address
				if (query->h.flags.b[0] & kDNSFlag0_TC)
					{
					// We can only store one IP address at a time per record, so if we've already
					// stored one address, set it to some special distinguished value instead
					if (rr->Requester.NotAnInteger == zeroIPAddr.NotAnInteger) rr->Requester = srcaddr;
					else                                                       rr->Requester = onesIPAddr;
					}
				if (rr->NR_AnswerTo)
					{
					// This is a direct answer in response to one of the questions
					rr->SendPriority = kDNSSendPriorityAnswer;
					}
				else
					{
					// This is an additional record supporting one of our answers
					if (rr->SendPriority < kDNSSendPriorityAdditional)
						rr->SendPriority = kDNSSendPriorityAdditional;
					}
				}
			}
		}

	// ***
	// *** 7. If we think other machines are likely to answer these questions, set our packet suppression timer
	// ***
	if (delayresponse && !m->SuppressSending)
		{
		// Pick a random delay between 20ms and 120ms.
		m->SuppressSending = timenow + (mDNSPlatformOneSecond*2 + (mDNSs32)mDNSRandom((mDNSu32)mDNSPlatformOneSecond*10)) / 100;
		if (m->SuppressSending == 0) m->SuppressSending = 1;
		}

	// ***
	// *** 8. If query is from a legacy client, generate a unicast reply too
	// ***
	if (answers && replyunicast)
		responseptr = GenerateUnicastResponse(query, end, InterfaceAddr, replyunicast, ResponseRecords);

exit:
	// ***
	// *** 9. Finally, clear our NextResponse link chain ready for use next time
	// ***
	while (ResponseRecords)
		{
		rr = ResponseRecords;
		ResponseRecords = rr->NextResponse;
		rr->NextResponse    = mDNSNULL;
		rr->NR_AnswerTo     = mDNSNULL;
		rr->NR_AdditionalTo = mDNSNULL;
		}
	
	return(responseptr);
	}

mDNSlocal void mDNSCoreReceiveQuery(mDNS *const m, const DNSMessage *const msg, const mDNSu8 *const end,
	const mDNSIPAddr srcaddr, const mDNSIPPort srcport, const mDNSIPAddr dstaddr, mDNSIPPort dstport,
	const mDNSIPAddr InterfaceAddr)
	{
	const mDNSs32 timenow        = mDNSPlatformTimeNow();
	DNSMessage    response;
	const mDNSu8 *responseend    = mDNSNULL;
	DNSMessage   *replyunicast   = mDNSNULL;
	mDNSBool      replymulticast = mDNSfalse;
	
	verbosedebugf("Received Query from %.4a:%d to %.4a:%d on %.4a with %d Question%s, %d Answer%s, %d Authorit%s, %d Additional%s",
		&srcaddr, (mDNSu16)srcport.b[0]<<8 | srcport.b[1],
		&dstaddr, (mDNSu16)dstport.b[0]<<8 | dstport.b[1],
		&InterfaceAddr,
		msg->h.numQuestions,   msg->h.numQuestions   == 1 ? "" : "s",
		msg->h.numAnswers,     msg->h.numAnswers     == 1 ? "" : "s",
		msg->h.numAuthorities, msg->h.numAuthorities == 1 ? "y" : "ies",
		msg->h.numAdditionals, msg->h.numAdditionals == 1 ? "" : "s");

	// If this was a unicast query, or it was from an old (non-port-5353) client, then send a unicast response
	if (dstaddr.NotAnInteger != AllDNSLinkGroup.NotAnInteger || srcport.NotAnInteger != MulticastDNSPort.NotAnInteger)
		replyunicast = &response;
	
	// If this was a multicast query, then we need to send a multicast response
	if (dstaddr.NotAnInteger == AllDNSLinkGroup.NotAnInteger) replymulticast = mDNStrue;
	
	responseend = ProcessQuery(m, msg, end, srcaddr, InterfaceAddr, replyunicast, replymulticast, timenow);
	if (replyunicast && responseend)
		{
		mDNSSendDNSMessage(m, replyunicast, responseend, InterfaceAddr, dstport, srcaddr, srcport);
		verbosedebugf("Unicast Response: %d Answer%s, %d Additional%s on %.4a",
			replyunicast->h.numAnswers,     replyunicast->h.numAnswers     == 1 ? "" : "s",
			replyunicast->h.numAdditionals, replyunicast->h.numAdditionals == 1 ? "" : "s", &InterfaceAddr);
		}
	}

// NOTE: mDNSCoreReceiveResponse calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSlocal void mDNSCoreReceiveResponse(mDNS *const m,
	const DNSMessage *const response, const mDNSu8 *end, const mDNSIPAddr dstaddr, const mDNSIPAddr InterfaceAddr)
	{
	int i;
	const mDNSs32 timenow = mDNSPlatformTimeNow();
	
	// We ignore questions (if any) in a DNS response packet
	const mDNSu8 *ptr = LocateAnswers(response, end);

	// All records in a DNS response packet are treated as equally valid statements of truth. If we want
	// to guard against spoof replies, then the only credible protection against that is cryptographic
	// security, e.g. DNSSEC., not worring about which section in the spoof packet contained the record
	int totalrecords = response->h.numAnswers + response->h.numAuthorities + response->h.numAdditionals;

	verbosedebugf("Received Response addressed to %.4a on %.4a with %d Question%s, %d Answer%s, %d Authorit%s, %d Additional%s",
		&dstaddr, &InterfaceAddr,
		response->h.numQuestions,   response->h.numQuestions   == 1 ? "" : "s",
		response->h.numAnswers,     response->h.numAnswers     == 1 ? "" : "s",
		response->h.numAuthorities, response->h.numAuthorities == 1 ? "y" : "ies",
		response->h.numAdditionals, response->h.numAdditionals == 1 ? "" : "s");

	// Other mDNS devices may issue unicast queries (which we correctly answer),
	// but we never *issue* unicast queries, so if we ever receive a unicast
	// response then it is someone trying to spoof us, so ignore it!
	if (dstaddr.NotAnInteger != AllDNSLinkGroup.NotAnInteger)
		{ debugf("** Ignored attempted spoof unicast mDNS response packet **"); return; }

	for (i = 0; i < totalrecords && ptr && ptr < end; i++)
		{
		ResourceRecord pktrr;
		mDNSu8 RecordType = (i < response->h.numAnswers) ? kDNSRecordTypePacketAnswer : kDNSRecordTypePacketAdditional;
		ptr = getResourceRecord(response, ptr, end, InterfaceAddr, timenow, RecordType, &pktrr, mDNSNULL);
		if (!ptr) return;

		// 1. Check that this packet resource record does not conflict with any of ours
		if (m->CurrentRecord) debugf("mDNSCoreReceiveResponse ERROR m->CurrentRecord already set");
		m->CurrentRecord = m->ResourceRecords;
		while (m->CurrentRecord)
			{
			ResourceRecord *rr = m->CurrentRecord;
			m->CurrentRecord = rr->next;
			if (SameResourceRecordSignature(&pktrr, rr))		// If interface, name, type and class match...
				{												// ... check to see if rdata is identical
				if (SameRData(pktrr.rrtype, pktrr.rdata, rr->rdata))
					{
					// If the RR in the packet is identical to ours, just check they're not trying to lower the TTL on us
					if (pktrr.rroriginalttl >= rr->rroriginalttl || m->SleepState)
						rr->SendPriority = kDNSSendPriorityNone;
					else
						rr->SendPriority = kDNSSendPriorityAnswer;
					}
				else
					{
					// else, the packet RR has different rdata -- check to see if this is a conflict
					if (PacketRRConflict(m, rr, &pktrr))
						{
						if (rr->rrtype == kDNSType_SRV)
							{
							debugf("mDNSCoreReceiveResponse: Our Data %d %##s", rr->rdata->RDLength,   rr->rdata->u.srv.target.c);
							debugf("mDNSCoreReceiveResponse: Pkt Data %d %##s", pktrr.rdata->RDLength, pktrr.rdata->u.srv.target.c);
							}
						else if (rr->rrtype == kDNSType_TXT)
							{
							debugf("mDNSCoreReceiveResponse: Our Data %d %#s", rr->rdata->RDLength,   rr->rdata->u.txt.c);
							debugf("mDNSCoreReceiveResponse: Pkt Data %d %#s", pktrr.rdata->RDLength, pktrr.rdata->u.txt.c);
							}
						else if (rr->rrtype == kDNSType_A)
							{
							debugf("mDNSCoreReceiveResponse: Our Data %.4a", &rr->rdata->u.ip);
							debugf("mDNSCoreReceiveResponse: Pkt Data %.4a", &pktrr.rdata->u.ip);
							}
						// If we've just whacked this record's ProbeCount, don't need to do it again
						if (rr->ProbeCount <= DefaultProbeCountForTypeUnique)
							{
							if (rr->RecordType == kDNSRecordTypeVerified)
								{
								debugf("mDNSCoreReceiveResponse: Reseting to Probing: %##s (%s)", rr->name.c, DNSTypeName(rr->rrtype));
								// If we'd previously verified this record, put it back to probing state and try again
								rr->RecordType       = kDNSRecordTypeUnique;
								rr->ProbeCount       = DefaultProbeCountForTypeUnique + 1;
								rr->NextSendTime     = timenow;
								rr->NextSendInterval = DefaultSendIntervalForRecordType(kDNSRecordTypeUnique);
								}
							else
								{
								debugf("mDNSCoreReceiveResponse: Will rename %##s (%s)", rr->name.c, DNSTypeName(rr->rrtype));
								// If we're probing for this record (or we assumed it must be unique) we just failed
								mDNS_Deregister_internal(m, rr, timenow, mDNS_Dereg_conflict);
								}
							}
						}
					}
				}
			}

		// 2. See if we want to add this packet resource record to our cache
		if (m->rrcache_size)	// Only try to cache answers if we have a cache to put them in
			{
			ResourceRecord *rr;
			// 2a. Check if this packet resource record is already in our cache
			for (rr = m->rrcache; rr; rr=rr->next)
				{
				// If we found this exact resource record, refresh its TTL
				if (IdenticalResourceRecord(&pktrr, rr))
					{
					//debugf("Found RR %##s size %d already in cache", pktrr.name.c, pktrr.rdata->RDLength);
					rr->TimeRcvd = timenow;
					rr->UnansweredQueries = 0;
					rr->NewData = mDNStrue;
					// If we're deleting a record, push it out one second into the future
					// to give other hosts on the network a chance to protest
					if (pktrr.rroriginalttl == 0) rr->rroriginalttl = 1;
					else rr->rroriginalttl = pktrr.rroriginalttl;
					break;
					}
				}

			// If packet resource record not in our cache, add it now
			// (unless it is just a deletion of a record we never had, in which case we don't care)
			if (!rr && pktrr.rroriginalttl > 0)
				{
				rr = GetFreeCacheRR(m, timenow);
				if (!rr) debugf("No cache space to add record for %#s", pktrr.name.c);
				else
					{
					*rr = pktrr;
					rr->rdata = &rr->rdatastorage;	// For now, all cache records use local storage
					rr->next = m->rrcache;
					m->rrcache = rr;
					if ((rr->RecordType & kDNSRecordTypeUniqueMask) == 0)
						TriggerImmediateQuestions(m, rr, timenow);
					//debugf("Adding RR %##s to cache (%d)", pktrr.name.c, m->rrcache_used);
					AnswerLocalQuestions(m, rr, timenow);
					}
				}
			}
		}

	// If we have a cache, then run through all the new records that we've just added,
	// clear their 'NewData' flags, and if they were marked as unique in the packet,
	// then search our cache for any records with the same name/type/class,
	// and purge them if they are more than one second old.
	if (m->rrcache_size)
		{
		ResourceRecord *rr;
		for (rr = m->rrcache; rr; rr=rr->next)
			{
			if (rr->NewData)
				{
				rr->NewData = mDNSfalse;
				if (rr->RecordType & kDNSRecordTypeUniqueMask)
					{
					ResourceRecord *r;
					for (r = m->rrcache; r; r=r->next)
						if (SameResourceRecordSignature(rr, r) && timenow - r->TimeRcvd > mDNSPlatformOneSecond)
							r->rroriginalttl = 0;
					}
				}
			}
		TidyRRCache(m, timenow);
		}
	}

mDNSexport void mDNSCoreReceive(mDNS *const m, DNSMessage *const msg, const mDNSu8 *const end,
	mDNSIPAddr srcaddr, mDNSIPPort srcport, mDNSIPAddr dstaddr, mDNSIPPort dstport, mDNSIPAddr InterfaceAddr)
	{
	const mDNSu8 StdQ = kDNSFlag0_QR_Query    | kDNSFlag0_OP_StdQuery;
	const mDNSu8 StdR = kDNSFlag0_QR_Response | kDNSFlag0_OP_StdQuery;
	mDNSu8 QR_OP = (mDNSu8)(msg->h.flags.b[0] & kDNSFlag0_QROP_Mask);
	
	// Read the integer parts which are in IETF byte-order (MSB first, LSB second)
	mDNSu8 *ptr = (mDNSu8 *)&msg->h.numQuestions;
	msg->h.numQuestions   = (mDNSu16)((mDNSu16)ptr[0] <<  8 | ptr[1]);
	msg->h.numAnswers     = (mDNSu16)((mDNSu16)ptr[2] <<  8 | ptr[3]);
	msg->h.numAuthorities = (mDNSu16)((mDNSu16)ptr[4] <<  8 | ptr[5]);
	msg->h.numAdditionals = (mDNSu16)((mDNSu16)ptr[6] <<  8 | ptr[7]);
	
	if (!m) { debugf("mDNSCoreReceive ERROR m is NULL"); return; }
	
	mDNS_Lock(m);
	if (m->mDNS_busy > 1) debugf("mDNSCoreReceive: Locking failure! mDNS already busy");

	if      (QR_OP == StdQ) mDNSCoreReceiveQuery   (m, msg, end, srcaddr, srcport, dstaddr, dstport, InterfaceAddr);
	else if (QR_OP == StdR) mDNSCoreReceiveResponse(m, msg, end,                   dstaddr,          InterfaceAddr);
	else debugf("Unknown DNS packet type %02X%02X (ignored)", msg->h.flags.b[0], msg->h.flags.b[1]);

	// Packet reception often causes a change to the task list:
	// 1. Inbound queries can cause us to need to send responses
	// 2. Conflicing response packets received from other hosts can cause us to need to send defensive responses
	// 3. Other hosts announcing deletion of shared records can cause us to need to re-assert those records
	// 4. Response packets that answer questions may cause our client to issue new questions
	mDNS_Unlock(m);
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark -
#pragma mark - Searcher Functions
#endif

mDNSlocal DNSQuestion *FindDuplicateQuestion(const mDNS *const m, const DNSQuestion *const question)
	{
	DNSQuestion *q;
	for (q = m->ActiveQuestions; q; q=q->next)		// Scan our list of questions
		if (q->rrtype  == question->rrtype  &&
			q->rrclass == question->rrclass &&
			SameDomainName(&q->name, &question->name)) return(q);
	return(mDNSNULL);
	}

// This is called after a question is deleted, in case other identical questions were being
// suppressed as duplicates
mDNSlocal void UpdateQuestionDuplicates(const mDNS *const m, const DNSQuestion *const question)
	{
	DNSQuestion *q;
	for (q = m->ActiveQuestions; q; q=q->next)	// Scan our list of questions
		if (q->DuplicateOf == question)			// To see if any questions were referencing this as their duplicate
			{
			q->NextQTime     = question->NextQTime;
			q->ThisQInterval = question->ThisQInterval;
			q->NextQInterval = question->NextQInterval;
			q->DuplicateOf   = FindDuplicateQuestion(m, q);
			}
	}

mDNSlocal mStatus mDNS_StartQuery_internal(mDNS *const m, DNSQuestion *const question, const mDNSs32 timenow)
	{
	if (m->rrcache_size == 0)	// Can't do queries if we have no cache space allocated
		return(mStatus_NoCache);
	else
		{
		DNSQuestion **q = &m->ActiveQuestions;
		while (*q && *q != question) q=&(*q)->next;

		if (*q)
			{
			debugf("Error! Tried to add a question that's already in the active list");
			return(mStatus_AlreadyRegistered);
			}

		question->next          = mDNSNULL;
		question->NextQTime     = timenow;
		question->ThisQInterval = mDNSPlatformOneSecond;  // MUST NOT be zero for an active question
		question->NextQInterval = mDNSPlatformOneSecond;
		question->DuplicateOf   = FindDuplicateQuestion(m, question);
		*q = question;
		
		if (!m->NewQuestions) m->NewQuestions = question;

		return(mStatus_NoError);
		}
	}

mDNSlocal void mDNS_StopQuery_internal(mDNS *const m, DNSQuestion *const question)
	{
	DNSQuestion **q = &m->ActiveQuestions;
	while (*q && *q != question) q=&(*q)->next;
	if (*q) *q = (*q)->next;
	else debugf("mDNS_StopQuery_internal: Question %##s (%s) not found in active list",
		question->name.c, DNSTypeName(question->rrtype));

	UpdateQuestionDuplicates(m, question);

	question->next = mDNSNULL;
	question->ThisQInterval = 0;
	question->NextQInterval = 0;
	
	// If we just deleted the question that AnswerLocalQuestions() is about to look at,
	// bump its pointer forward one question.
	if (m->CurrentQuestion == question)
		{
		debugf("mDNS_StopQuery_internal: Just deleted the currently active question.");
		m->CurrentQuestion = m->CurrentQuestion->next;
		}

	if (m->NewQuestions    == question)
		{
		debugf("mDNS_StopQuery_internal: Just deleted a new question that wasn't even answered yet.");
		m->NewQuestions    = m->NewQuestions->next;
		}
	
	}

mDNSexport mStatus mDNS_StartQuery(mDNS *const m, DNSQuestion *const question)
	{
	const mDNSs32 timenow = mDNS_Lock(m);
	mStatus status = mDNS_StartQuery_internal(m, question, timenow);
	mDNS_Unlock(m);
	return(status);
	}

mDNSexport void mDNS_StopQuery(mDNS *const m, DNSQuestion *const question)
	{
	mDNS_Lock(m);
	mDNS_StopQuery_internal(m, question);
	mDNS_Unlock(m);
	}

mDNSexport mStatus mDNS_StartBrowse(mDNS *const m, DNSQuestion *const question,
	const domainname *const srv, const domainname *const domain,
	const mDNSIPAddr InterfaceAddr, mDNSQuestionCallback *Callback, void *Context)
	{
	question->InterfaceAddr = InterfaceAddr;
	question->name = *srv;
	AppendDomainNameToName(&question->name, domain);
	question->rrtype    = kDNSType_PTR;
	question->rrclass   = kDNSClass_IN;
	question->Callback  = Callback;
	question->Context   = Context;
	return(mDNS_StartQuery(m, question));
	}

mDNSlocal void FoundServiceInfoSRV(mDNS *const m, DNSQuestion *question, const ResourceRecord *const answer)
	{
	ServiceInfoQuery *query = (ServiceInfoQuery *)question->Context;
	if (answer->rrremainingttl == 0) return;
	if (answer->rrtype != kDNSType_SRV) return;

	query->info->port = answer->rdata->u.srv.port;

	// If this is our first answer, then set the GotSRV flag and start the address query
	if (!query->GotSRV)
		{
		query->GotSRV             = mDNStrue;
		query->qADD.name          = answer->rdata->u.srv.target;
		mDNS_StartQuery_internal(m, &query->qADD, mDNSPlatformTimeNow());
		}
	// If this is not our first answer, only re-issue the address query if the target host name has changed
	else if (!SameDomainName(&query->qADD.name, &answer->rdata->u.srv.target))
		{
		mDNS_StopQuery_internal(m, &query->qADD);
		query->qADD.name          = answer->rdata->u.srv.target;
		mDNS_StartQuery_internal(m, &query->qADD, mDNSPlatformTimeNow());
		}

	// Don't need to do ScheduleNextTask because this callback can only ever happen
	// (a) as a result of an immediate result from the mDNS_StartQuery call, or
	// (b) as a result of receiving a packet on the wire
	// both of which will result in a subsequent ScheduleNextTask call of their own
	}

mDNSlocal void FoundServiceInfoTXT(mDNS *const m, DNSQuestion *question, const ResourceRecord *const answer)
	{
	ServiceInfoQuery *query = (ServiceInfoQuery *)question->Context;
	if (answer->rrremainingttl == 0) return;
	if (answer->rrtype != kDNSType_TXT) return;
	if (answer->rdata->RDLength > sizeof(query->info->TXTinfo)) return;

	query->GotTXT       = (mDNSu8)(1 + (query->GotTXT || query->GotADD));
	query->info->TXTlen = answer->rdata->RDLength;
	mDNSPlatformMemCopy(answer->rdata->u.txt.c, query->info->TXTinfo, answer->rdata->RDLength);

	debugf("FoundServiceInfoTXT: %##s GotADD=%d", &query->info->name, query->GotADD);

	// CAUTION: MUST NOT do anything more with query after calling query->Callback(), because the client's
	// callback function is allowed to do anything, including deleting this query and freeing its memory.
	if (query->Callback && query->GotADD)
		query->Callback(m, query);
	}

mDNSlocal void FoundServiceInfoADD(mDNS *const m, DNSQuestion *question, const ResourceRecord *const answer)
	{
	ServiceInfoQuery *query = (ServiceInfoQuery *)question->Context;
	if (answer->rrremainingttl == 0) return;
	if (answer->rrtype != kDNSType_A) return;
	query->GotADD = mDNStrue;
	query->info->InterfaceAddr = answer->InterfaceAddr;
	query->info->ip            = answer->rdata->u.ip;

	debugf("FoundServiceInfoADD: %##s GotTXT=%d", &query->info->name, query->GotTXT);

	// If query->GotTXT is 1 that means we already got a single TXT answer but didn't
	// deliver it to the client at that time, so no further action is required.
	// If query->GotTXT is 2 that means we either got more than one TXT answer,
	// or we got a TXT answer and delivered it to the client at that time, so in either
	// of these cases we may have lost information, so we should re-issue the TXT question.
	if (query->GotTXT > 1)
		{
		mDNS_StopQuery_internal(m, &query->qTXT);
		mDNS_StartQuery_internal(m, &query->qTXT, mDNSPlatformTimeNow());
		}

	// CAUTION: MUST NOT do anything more with query after calling query->Callback(), because the client's
	// callback function is allowed to do anything, including deleting this query and freeing its memory.
	if (query->Callback && query->GotTXT)
		query->Callback(m, query);
	}

// On entry, the client must have set the name and InterfaceAddr fields of the ServiceInfo structure
// If the query is not interface-specific, then InterfaceAddr may be zero
// Each time the Callback is invoked, the remainder of the fields will have been filled in
// In addition, InterfaceAddr will be updated to give the interface address corresponding to that reply
mDNSexport mStatus mDNS_StartResolveService(mDNS *const m,
	ServiceInfoQuery *query, ServiceInfo *info, ServiceInfoQueryCallback *Callback, void *Context)
	{
	mStatus status;
	const mDNSs32 timenow = mDNS_Lock(m);
	
	query->qSRV.InterfaceAddr = info->InterfaceAddr;
	query->qSRV.name          = info->name;
	query->qSRV.rrtype        = kDNSType_SRV;
	query->qSRV.rrclass       = kDNSClass_IN;
	query->qSRV.Callback      = FoundServiceInfoSRV;
	query->qSRV.Context       = query;

	query->qTXT.InterfaceAddr = info->InterfaceAddr;
	query->qTXT.name          = info->name;
	query->qTXT.rrtype        = kDNSType_TXT;
	query->qTXT.rrclass       = kDNSClass_IN;
	query->qTXT.Callback      = FoundServiceInfoTXT;
	query->qTXT.Context       = query;

	query->qADD.InterfaceAddr = info->InterfaceAddr;
	query->qADD.name.c[0]     = 0;
	query->qADD.rrtype        = kDNSType_A;
	query->qADD.rrclass       = kDNSClass_IN;
	query->qADD.Callback      = FoundServiceInfoADD;
	query->qADD.Context       = query;

	query->GotSRV             = mDNSfalse;
	query->GotTXT             = mDNSfalse;
	query->GotADD             = mDNSfalse;

	query->info               = info;
	query->Callback           = Callback;
	query->Context            = Context;

//	info->name      = Must already be set up by client
//	info->interface = Must already be set up by client
	info->ip        = zeroIPAddr;
	info->port      = zeroIPPort;
	info->TXTlen    = 0;

	status = mDNS_StartQuery_internal(m, &query->qSRV, timenow);
	if (status == mStatus_NoError) status = mDNS_StartQuery_internal(m, &query->qTXT, timenow);
	if (status != mStatus_NoError) mDNS_StopResolveService(m, query);

	mDNS_Unlock(m);
	return(status);
	}

mDNSexport void    mDNS_StopResolveService (mDNS *const m, ServiceInfoQuery *query)
	{
	mDNS_Lock(m);
	if (query->qSRV.ThisQInterval) mDNS_StopQuery_internal(m, &query->qSRV);
	if (query->qTXT.ThisQInterval) mDNS_StopQuery_internal(m, &query->qTXT);
	if (query->qADD.ThisQInterval) mDNS_StopQuery_internal(m, &query->qADD);
	mDNS_Unlock(m);
	}

mDNSexport mStatus mDNS_GetDomains(mDNS *const m, DNSQuestion *const question, mDNSu8 DomainType,
	const mDNSIPAddr InterfaceAddr, mDNSQuestionCallback *Callback, void *Context)
	{
	question->InterfaceAddr = InterfaceAddr;
	ConvertCStringToDomainName(mDNS_DomainTypeNames[DomainType], &question->name);
	question->rrtype    = kDNSType_PTR;
	question->rrclass   = kDNSClass_IN;
	question->Callback  = Callback;
	question->Context   = Context;
	return(mDNS_StartQuery(m, question));
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark - Responder Functions
#endif

// Set up a ResourceRecord with sensible default values.
// These defaults may be overwritten with new values before mDNS_Register is called
mDNSexport void mDNS_SetupResourceRecord(ResourceRecord *rr, RData *RDataStorage, mDNSIPAddr InterfaceAddr,
	mDNSu16 rrtype, mDNSu32 ttl, mDNSu8 RecordType, mDNSRecordCallback Callback, void *Context)
	{
	// Don't try to store a TTL bigger than we can represent in platform time units
	if (ttl > 0x7FFFFFFFUL / mDNSPlatformOneSecond)
		ttl = 0x7FFFFFFFUL / mDNSPlatformOneSecond;
	else if (ttl == 0)		// And Zero TTL is illegal
		ttl = 1;

	// Field Group 1: Persistent metadata for Authoritative Records
	rr->Additional1       = mDNSNULL;
	rr->Additional2       = mDNSNULL;
	rr->DependentOn       = mDNSNULL;
	rr->RRSet             = mDNSNULL;
	rr->Callback          = Callback;
	rr->Context           = Context;

	rr->RecordType        = RecordType;
	rr->HostTarget        = mDNSfalse;
	
	// Field Group 2: Transient state for Authoritative Records (set in mDNS_Register_internal)
	// Field Group 3: Transient state for Cache Records         (set in mDNS_Register_internal)

	// Field Group 4: The actual information pertaining to this resource record
	rr->InterfaceAddr     = InterfaceAddr;
	rr->name.c[0]         = 0;		// MUST be set by client
	rr->rrtype            = rrtype;
	rr->rrclass           = kDNSClass_IN;
	rr->rroriginalttl     = ttl;
	rr->rrremainingttl    = ttl;
//	rr->rdlength          = MUST set by client and/or in mDNS_Register_internal
//	rr->rdestimate        = set in mDNS_Register_internal
//	rr->rdata             = MUST be set by client

	if (RDataStorage)
		rr->rdata = RDataStorage;
	else
		{
		rr->rdata = &rr->rdatastorage;
		rr->rdata->MaxRDLength = sizeof(RDataBody);
		}
	}

mDNSexport mStatus mDNS_Register(mDNS *const m, ResourceRecord *const rr)
	{
	const mDNSs32 timenow = mDNS_Lock(m);
	mStatus status = mDNS_Register_internal(m, rr, timenow);
	mDNS_Unlock(m);
	return(status);
	}

mDNSexport mStatus mDNS_Update(mDNS *const m, ResourceRecord *const rr, mDNSu32 newttl,
	RData *const newrdata, mDNSRecordUpdateCallback *Callback)
	{
	const mDNSs32 timenow = mDNS_Lock(m);

	// If we already have an update queued up which has not gone through yet,
	// give the client a chance to free that memory
	if (rr->NewRData)
		{
		RData *n = rr->NewRData;
		rr->NewRData = mDNSNULL;	// Clear the NewRData pointer ...
		if (rr->UpdateCallback) rr->UpdateCallback(m, rr, n); // ...and let the client free this memory, if necessary
		}
	
	rr->AnnounceCount    = DefaultAnnounceCountForRecordType(rr->RecordType);
	rr->NextSendTime     = timenow;
	if (rr->RecordType == kDNSRecordTypeUnique && m->SuppressProbes) rr->NextSendTime = m->SuppressProbes;
	rr->NextSendInterval = DefaultSendIntervalForRecordType(rr->RecordType);
	rr->NewRData         = newrdata;
	rr->UpdateCallback   = Callback;
	rr->rroriginalttl    = newttl;
	rr->rrremainingttl   = newttl;
	mDNS_Unlock(m);
	return(mStatus_NoError);
	}

// NOTE: mDNS_Deregister calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSexport void mDNS_Deregister(mDNS *const m, ResourceRecord *const rr)
	{
	const mDNSs32 timenow = mDNS_Lock(m);
	mDNS_Deregister_internal(m, rr, timenow, mDNS_Dereg_normal);
	mDNS_Unlock(m);
	}

mDNSexport void mDNS_GenerateFQDN(mDNS *const m)
	{
	// Set up the Primary mDNS FQDN
	m->hostname1.c[0] = 0;
	AppendDomainLabelToName(&m->hostname1, &m->hostlabel);
	AppendStringLabelToName(&m->hostname1, "local");

	// Set up the Secondary mDNS FQDN
	m->hostname2.c[0] = 0;
	AppendDomainLabelToName(&m->hostname2, &m->hostlabel);
	AppendStringLabelToName(&m->hostname2, "local");
	AppendStringLabelToName(&m->hostname2, "arpa");

	// Make sure that any SRV records (and the like) that reference our 
	// host name in their rdata get updated to reference this new host name
	UpdateHostNameTargets(m);
	}

mDNSlocal void HostNameCallback(mDNS *const m, ResourceRecord *const rr, mStatus result)
	{
	#pragma unused(rr)
	switch (result)
		{
		case mStatus_NoError:
			debugf("HostNameCallback: %##s (%s) Name registered",   rr->name.c, DNSTypeName(rr->rrtype));
			break;
		case mStatus_NameConflict:
			debugf("HostNameCallback: %##s (%s) Name conflict",     rr->name.c, DNSTypeName(rr->rrtype));
			break;
		default:
			debugf("HostNameCallback: %##s (%s) Unknown result %d", rr->name.c, DNSTypeName(rr->rrtype), result);
			break;
		}

	if (result == mStatus_NameConflict)
		{
		NetworkInterfaceInfo *hr = mDNSNULL;
		NetworkInterfaceInfo **p = &hr;
		domainlabel oldlabel = m->hostlabel;

		// 1. Deregister all our host sets
		while (m->HostInterfaces)
			{
			NetworkInterfaceInfo *set = m->HostInterfaces;
			mDNS_DeregisterInterface(m, set);
			*p = set;
			p = &set->next;
			}
		
		// 2. Pick a new name
		// First give the client callback a chance to pick a new name
		if (m->Callback) m->Callback(m, mStatus_NameConflict);
		// If the client callback didn't do it, add (or increment) an index ourselves
		if (SameDomainLabel(m->hostlabel.c, oldlabel.c))
			IncrementLabelSuffix(&m->hostlabel, mDNSfalse);
		mDNS_GenerateFQDN(m);
		
		// 3. Re-register all our host sets
		while (hr)
			{
			NetworkInterfaceInfo *set = hr;
			hr = hr->next;
			mDNS_RegisterInterface(m, set);
			}
		}
	}

mDNSlocal NetworkInterfaceInfo *FindFirstAdvertisedInterface(mDNS *const m)
	{
	NetworkInterfaceInfo *i;
	for (i=m->HostInterfaces; i; i=i->next) if (i->Advertise) break;
	return(i);
	}

mDNSexport mStatus mDNS_RegisterInterface(mDNS *const m, NetworkInterfaceInfo *set)
	{
	const mDNSs32 timenow = mDNS_Lock(m);
	NetworkInterfaceInfo **p = &m->HostInterfaces;
	
	while (*p && *p != set) p=&(*p)->next;
	if (*p)
		{
		debugf("Error! Tried to register a NetworkInterfaceInfo that's already in the list");
		mDNS_Unlock(m);
		return(mStatus_AlreadyRegistered);
		}

	if (set->Advertise)
		{
		char buffer[256];
		NetworkInterfaceInfo *primary = FindFirstAdvertisedInterface(m);
		if (!primary) primary = set; // If no existing advertised interface, this new NetworkInterfaceInfo becomes our new primary
		
		mDNS_SetupResourceRecord(&set->RR_A1,  mDNSNULL, set->ip, kDNSType_A,   60, kDNSRecordTypeUnique,      HostNameCallback, set);
		mDNS_SetupResourceRecord(&set->RR_A2,  mDNSNULL, set->ip, kDNSType_A,   60, kDNSRecordTypeUnique,      HostNameCallback, set);
		mDNS_SetupResourceRecord(&set->RR_PTR, mDNSNULL, set->ip, kDNSType_PTR, 60, kDNSRecordTypeKnownUnique, mDNSNULL, mDNSNULL);
	
		// 1. Set up primary Address record to map from primary host name ("foo.local.") to IP address
		set->RR_A1.name        = m->hostname1;
		set->RR_A1.rdata->u.ip = set->ip;
	
		// 2. Set up secondary Address record to map from secondary host name ("foo.local.arpa.") to IP address
		set->RR_A2.name        = m->hostname2;
		set->RR_A2.rdata->u.ip = set->ip;
	
		// 3. Set up reverse-lookup PTR record to map from our address back to our primary host name
		// Setting HostTarget tells DNS that the target of this PTR is to be automatically kept in sync if our host name changes
		// Note: This is reverse order compared to a normal dotted-decimal IP address
		mDNS_sprintf(buffer, "%d.%d.%d.%d.in-addr.arpa.", set->ip.b[3], set->ip.b[2], set->ip.b[1], set->ip.b[0]);
		ConvertCStringToDomainName(buffer, &set->RR_PTR.name);
		set->RR_PTR.HostTarget = mDNStrue;	// Tell mDNS that the target of this PTR is to be kept in sync with our host name
	
		set->RR_A1.RRSet = &primary->RR_A1;	// May refer to self
		set->RR_A2.RRSet = &primary->RR_A2;	// May refer to self
	
		mDNS_Register_internal(m, &set->RR_A1,  timenow);
		mDNS_Register_internal(m, &set->RR_A2,  timenow);
		mDNS_Register_internal(m, &set->RR_PTR, timenow);
	
		// ... Add an HINFO record, etc.?
		}

	set->next = mDNSNULL;
	*p = set;
	mDNS_Unlock(m);
	return(mStatus_NoError);
	}

mDNSlocal void mDNS_DeadvertiseInterface(mDNS *const m, NetworkInterfaceInfo *set, const mDNSs32 timenow)
	{
	NetworkInterfaceInfo *i;
	// If we still have address records referring to this one, update them
	NetworkInterfaceInfo *primary = FindFirstAdvertisedInterface(m);
	ResourceRecord *A1 = primary ? &primary->RR_A1 : mDNSNULL;
	ResourceRecord *A2 = primary ? &primary->RR_A2 : mDNSNULL;
	for (i=m->HostInterfaces; i; i=i->next)
		{
		if (i->RR_A1.RRSet == &set->RR_A1) i->RR_A1.RRSet = A1;
		if (i->RR_A2.RRSet == &set->RR_A2) i->RR_A2.RRSet = A2;
		}

	// Unregister these records
	mDNS_Deregister_internal(m, &set->RR_A1,  timenow, mDNS_Dereg_normal);
	mDNS_Deregister_internal(m, &set->RR_A2,  timenow, mDNS_Dereg_normal);
	mDNS_Deregister_internal(m, &set->RR_PTR, timenow, mDNS_Dereg_normal);
	}

// NOTE: mDNS_DeregisterInterface calls mDNS_Deregister_internal which can call a user callback, which may change
// the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSexport void mDNS_DeregisterInterface(mDNS *const m, NetworkInterfaceInfo *set)
	{
	NetworkInterfaceInfo **p = &m->HostInterfaces;
	const mDNSs32 timenow = mDNS_Lock(m);

	// Find this record in our list
	while (*p && *p != set) p=&(*p)->next;
	if (!*p) { debugf("mDNS_DeregisterInterface: NetworkInterfaceInfo not found in list"); return; }

	// Unlink this record from our list
	*p = (*p)->next;
	set->next = mDNSNULL;

	// Flush any cache entries we received on this interface
	FlushCacheRecords(m, set->ip, timenow);

	// If we were advertising on this interface, deregister now
	// When doing the mDNS_Close processing, we first call mDNS_DeadvertiseInterface for each interface
	// so by the time the platform support layer gets to call mDNS_DeregisterInterface,
	// the address and PTR records have already been deregistered for it
	if (set->Advertise && set->RR_A1.RecordType) mDNS_DeadvertiseInterface(m, set, timenow);

	mDNS_Unlock(m);
	}

mDNSlocal void ServiceCallback(mDNS *const m, ResourceRecord *const rr, mStatus result)
	{
	#pragma unused(m)
	ServiceRecordSet *sr = (ServiceRecordSet *)rr->Context;
	switch (result)
		{
		case mStatus_NoError:
			if (rr == &sr->RR_SRV)
				debugf("ServiceCallback: Service RR_SRV %##s Registered", rr->name.c);
			else
				debugf("ServiceCallback: %##s (%s) ERROR Should only get mStatus_NoError callback for RR_SRV",
					rr->name.c, DNSTypeName(rr->rrtype));
			break;

		case mStatus_NameConflict:
			debugf("ServiceCallback: %##s (%s) Name Conflict", rr->name.c, DNSTypeName(rr->rrtype));
			break;

		case mStatus_MemFree:
			if (rr == &sr->RR_PTR)
				debugf("ServiceCallback: Service RR_PTR %##s Memory Free", rr->name.c);
			else
				debugf("ServiceCallback: %##s (%s) ERROR Should only get mStatus_MemFree callback for RR_PTR",
					rr->name.c, DNSTypeName(rr->rrtype));
			break;

		default:
			debugf("ServiceCallback: %##s (%s) Unknown Result %d", rr->name.c, DNSTypeName(rr->rrtype), result);
			break;
		}

	// If we got a name conflict on either SRV or TXT, forcibly deregister this service, and record that we did that
	if (result == mStatus_NameConflict) { sr->Conflict = mDNStrue; mDNS_DeregisterService(m, sr); return; }
	
	// If this ServiceRecordSet was forcibly deregistered, and now it's memory is ready for reuse,
	// then we can now report the NameConflict to the client
	if (result == mStatus_MemFree && sr->Conflict) result = mStatus_NameConflict;

	// CAUTION: MUST NOT do anything more with sr after calling sr->Callback(), because the client's callback
	// function is allowed to do anything, including deregistering this service and freeing its memory.
	if (sr->Callback) sr->Callback(m, sr, result);
	}

// Note:
// Name is first label of domain name (any dots in the name are actual dots, not label separators)
// Type is service type (e.g. "_printer._tcp.")
// Domain is fully qualified domain name (i.e. ending with a null label)
// We always register a TXT, even if it is empty (so that clients are not
// left waiting forever looking for a nonexistent record.)
// If the host parameter is mDNSNULL or the root domain (ASCII NUL),
// then the default host name (m->hostname1) is automatically used
mDNSexport mStatus mDNS_RegisterService(mDNS *const m, ServiceRecordSet *sr,
	const domainlabel *const name, const domainname *const type, const domainname *const domain,
	const domainname *const host, mDNSIPPort port, const mDNSu8 txtinfo[], mDNSu16 txtlen,
	mDNSServiceCallback Callback, void *Context)
	{
	mDNSs32 timenow;
	mStatus err;

	sr->Callback = Callback;
	sr->Context  = Context;
	sr->Conflict = mDNSfalse;
	if (host && host->c[0]) sr->Host = *host;
	else sr->Host.c[0] = 0;
	
	mDNS_SetupResourceRecord(&sr->RR_PTR, mDNSNULL, zeroIPAddr, kDNSType_PTR, 24*3600, kDNSRecordTypeShared, ServiceCallback, sr);
	mDNS_SetupResourceRecord(&sr->RR_SRV, mDNSNULL, zeroIPAddr, kDNSType_SRV, 60,      kDNSRecordTypeUnique, ServiceCallback, sr);
 	mDNS_SetupResourceRecord(&sr->RR_TXT, mDNSNULL, zeroIPAddr, kDNSType_TXT, 60,      kDNSRecordTypeUnique, ServiceCallback, sr);
	
	// If the client is registering an oversized TXT record,
	// it is the client's responsibility to alloate a ServiceRecordSet structure that is large enough for it
	if (sr->RR_TXT.rdata->MaxRDLength < txtlen)
		sr->RR_TXT.rdata->MaxRDLength = txtlen;

	if (ConstructServiceName(&sr->RR_PTR.name, mDNSNULL, type, domain) == mDNSNULL) return(mStatus_BadParamErr);
	if (ConstructServiceName(&sr->RR_SRV.name, name,     type, domain) == mDNSNULL) return(mStatus_BadParamErr);
	sr->RR_TXT.name = sr->RR_SRV.name;
	
	// 1. Set up the PTR record rdata to point to our service name
	// We set up two additionals, so when a client asks for this PTR we automatically send the SRV and the TXT too
	sr->RR_PTR.rdata->u.name = sr->RR_SRV.name;
	sr->RR_PTR.Additional1 = &sr->RR_SRV;
	sr->RR_PTR.Additional2 = &sr->RR_TXT;

	// 2. Set up the SRV record rdata.
	sr->RR_SRV.rdata->u.srv.priority = 0;
	sr->RR_SRV.rdata->u.srv.weight   = 0;
	sr->RR_SRV.rdata->u.srv.port     = port;

	// Setting HostTarget tells DNS that the target of this SRV is to be automatically kept in sync with our host name
	if (sr->Host.c[0]) sr->RR_SRV.rdata->u.srv.target = sr->Host;
	else sr->RR_SRV.HostTarget = mDNStrue;

	// 3. Set up the TXT record rdata,
	// and set DependentOn because we're depending on the SRV record to find and resolve conflicts for us
	if (txtinfo == mDNSNULL) sr->RR_TXT.rdata->RDLength = 0;
	else if (txtinfo != sr->RR_TXT.rdata->u.txt.c)
		{
		sr->RR_TXT.rdata->RDLength = txtlen;
		if (sr->RR_TXT.rdata->RDLength > sr->RR_TXT.rdata->MaxRDLength) return(mStatus_BadParamErr);
		mDNSPlatformMemCopy(txtinfo, sr->RR_TXT.rdata->u.txt.c, txtlen);
		}
	sr->RR_TXT.DependentOn = &sr->RR_SRV;

	// 4. We have no Extras yet
	sr->Extras = mDNSNULL;

	timenow = mDNS_Lock(m);
	err = mDNS_Register_internal(m, &sr->RR_SRV, timenow);
	if (!err) err = mDNS_Register_internal(m, &sr->RR_TXT, timenow);
	// We register the RR_PTR last, because we want to be sure that in the event of a forced call to
	// mDNS_Close, the RR_PTR will be the last one to be forcibly deregistered, since that is what triggers
	// the mStatus_MemFree callback to ServiceCallback, which in turn passes on the mStatus_MemFree back to
	// the client callback, which is then at liberty to free the ServiceRecordSet memory at will. We need to
	// make sure we've deregistered all our records and done any other necessary cleanup before that happens.
	if (!err) err = mDNS_Register_internal(m, &sr->RR_PTR, timenow);
	if (err) mDNS_DeregisterService(m, sr);
	mDNS_Unlock(m);

	return(err);
	}

mDNSexport mStatus mDNS_AddRecordToService(mDNS *const m, ServiceRecordSet *sr,
	ExtraResourceRecord *extra, RData *rdata, mDNSu32 ttl)
	{
	ExtraResourceRecord **e = &sr->Extras;
	while (*e) e = &(*e)->next;

	// If TTL is unspecified, make it 60 seconds, the same as the service's TXT and SRV default
	if (ttl == 0) ttl = 60;

	extra->next          = mDNSNULL;
 	mDNS_SetupResourceRecord(&extra->r, rdata, zeroIPAddr, extra->r.rrtype, ttl, kDNSRecordTypeUnique, ServiceCallback, sr);
	extra->r.name        = sr->RR_SRV.name;
	extra->r.DependentOn = &sr->RR_SRV;
	
	debugf("mDNS_AddRecordToService adding record to %##s", extra->r.name.c);
	
	*e = extra;
	return(mDNS_Register(m, &extra->r));
	}

mDNSexport mStatus mDNS_RemoveRecordFromService(mDNS *const m, ServiceRecordSet *sr, ExtraResourceRecord *extra)
	{
	ExtraResourceRecord **e = &sr->Extras;
	while (*e && *e != extra) e = &(*e)->next;
	if (!*e)
		{
		debugf("mDNS_RemoveRecordFromService failed to remove record from %##s", extra->r.name.c);
		return(mStatus_BadReferenceErr);
		}

	debugf("mDNS_RemoveRecordFromService removing record from %##s", extra->r.name.c);
	
	*e = (*e)->next;
	mDNS_Deregister(m, &extra->r);
	return(mStatus_NoError);
	}

mDNSexport mStatus mDNS_RenameAndReregisterService(mDNS *const m, ServiceRecordSet *const sr)
	{
	domainlabel name;
	domainname type, domain;
	domainname *host = mDNSNULL;
	ExtraResourceRecord *extras = sr->Extras;
	mStatus err;

	DeconstructServiceName(&sr->RR_SRV.name, &name, &type, &domain);
	IncrementLabelSuffix(&name, mDNStrue);
	debugf("Reregistering as %#s", name.c);
	if (sr->RR_SRV.HostTarget == mDNSfalse && sr->Host.c[0]) host = &sr->Host;
	
	err = mDNS_RegisterService(m, sr, &name, &type, &domain,
		host, sr->RR_SRV.rdata->u.srv.port, sr->RR_TXT.rdata->u.txt.c, sr->RR_TXT.rdata->RDLength,
		sr->Callback, sr->Context);

	// mDNS_RegisterService() just reset sr->Extras to NULL.
	// Fortunately we already grabbed ourselves a copy of this pointer (above), so we can now run
	// through the old list of extra records, and re-add them to our freshly created service registration
	while (!err && extras)
		{
		ExtraResourceRecord *e = extras;
		extras = extras->next;
		err = mDNS_AddRecordToService(m, sr, e, e->r.rdata, e->r.rroriginalttl);
		}
	
	return(err);
	}

// NOTE: mDNS_DeregisterService calls mDNS_Deregister_internal which can call a user callback,
// which may change the record list and/or question list.
// Any code walking either list must use the CurrentQuestion and/or CurrentRecord mechanism to protect against this.
mDNSexport void mDNS_DeregisterService(mDNS *const m, ServiceRecordSet *sr)
	{
	const mDNSs32 timenow = mDNS_Lock(m);
	ExtraResourceRecord *e = sr->Extras;

	// We use mDNS_Dereg_repeat because, in the event of a collision, some or all of
	// these records could have already been automatically deregistered, and that's okay
	mDNS_Deregister_internal(m, &sr->RR_SRV, timenow, mDNS_Dereg_repeat);
	mDNS_Deregister_internal(m, &sr->RR_TXT, timenow, mDNS_Dereg_repeat);
	
	// We deregister all of the extra records, but we leave the sr->Extras list intact
	// in case the client wants to do a RenameAndReregister and reinstate the registration
	while (e)
		{
		mDNS_Deregister_internal(m, &e->r, timenow, mDNS_Dereg_repeat);
		e = e->next;
		}

	// Be sure to deregister the PTR last!
	// Deregistering this record is what triggers the mStatus_MemFree callback to ServiceCallback,
	// which in turn passes on the mStatus_MemFree (or mStatus_NameConflict) back to the client callback,
	// which is then at liberty to free the ServiceRecordSet memory at will. We need to make sure
	// we've deregistered all our records and done any other necessary cleanup before that happens.
	mDNS_Deregister_internal(m, &sr->RR_PTR, timenow, mDNS_Dereg_normal);

	mDNS_Unlock(m);
	}

// Create a registration that asserts that no such service exists with this name.
// This can be useful where there is a given function is available through several protocols.
// For example, a printer called "Stuart's Printer" may implement printing via the "pdl-datastream" and "IPP"
// protocols, but not via "LPR". In this case it would be prudent for the printer to assert the non-existence of an
// "LPR" service called "Stuart's Printer". Without this precaution, another printer than offers only "LPR" printing
// could inadvertently advertise its service under the same name "Stuart's Printer", which might be confusing for users.
mDNSexport mStatus mDNS_RegisterNoSuchService(mDNS *const m, ResourceRecord *const rr,
	const domainlabel *const name, const domainname *const type, const domainname *const domain,
	mDNSRecordCallback Callback, void *Context)
	{
	mDNS_SetupResourceRecord(rr, mDNSNULL, zeroIPAddr, kDNSType_SRV, 60, kDNSRecordTypeUnique, Callback, Context);
	if (ConstructServiceName(&rr->name, name, type, domain) == mDNSNULL) return(mStatus_BadParamErr);
	rr->rdata->u.srv.priority    = 0;
	rr->rdata->u.srv.weight      = 0;
	rr->rdata->u.srv.port        = zeroIPPort;
	rr->rdata->u.srv.target.c[0] = 0;
	return(mDNS_Register(m, rr));
	}

mDNSexport mStatus mDNS_AdvertiseDomains(mDNS *const m, ResourceRecord *rr,
	mDNSu8 DomainType, const mDNSIPAddr InterfaceAddr, char *domname)
	{
	mDNS_SetupResourceRecord(rr, mDNSNULL, InterfaceAddr, kDNSType_PTR, 24*3600, kDNSRecordTypeShared, mDNSNULL, mDNSNULL);
	ConvertCStringToDomainName(mDNS_DomainTypeNames[DomainType], &rr->name);
	ConvertCStringToDomainName(domname, &rr->rdata->u.name);
	return(mDNS_Register(m, rr));
	}

// ***************************************************************************
#if 0
#pragma mark -
#pragma mark -
#pragma mark - Startup and Shutdown
#endif

mDNSexport mStatus mDNS_Init(mDNS *const m, mDNS_PlatformSupport *const p,
	ResourceRecord *rrcachestorage, mDNSu32 rrcachesize,
	mDNSBool AdvertiseLocalAddresses, mDNSCallback *Callback, void *Context)
	{
	mStatus result;
	mDNSu32 i;
	
	if (!rrcachestorage) rrcachesize = 0;
	
	m->p                  = p;
	m->AdvertiseLocalAddresses = AdvertiseLocalAddresses;
	m->mDNSPlatformStatus = mStatus_Waiting;
	m->Callback           = Callback;
	m->Context            = Context;

	m->mDNS_busy          = 0;

	m->lock_rrcache       = 0;
	m->lock_Questions     = 0;
	m->lock_Records       = 0;

	m->ActiveQuestions    = mDNSNULL;
	m->NewQuestions       = mDNSNULL;
	m->CurrentQuestion    = mDNSNULL;
	m->rrcache_size       = rrcachesize;
	m->rrcache_used       = 0;
	m->rrcache_report     = 10;
	m->rrcache_free       = rrcachestorage;
	if (rrcachesize)
		{
		for (i=0; i<rrcachesize; i++) rrcachestorage[i].next = &rrcachestorage[i+1];
		rrcachestorage[rrcachesize-1].next = mDNSNULL;
		}
	m->rrcache = mDNSNULL;

	m->hostlabel.c[0]     = 0;
	m->nicelabel.c[0]     = 0;
	m->ResourceRecords    = mDNSNULL;
	m->CurrentRecord      = mDNSNULL;
	m->HostInterfaces     = mDNSNULL;
	m->SuppressSending    = 0;
	m->SleepState         = mDNSfalse;
	m->NetChanged         = mDNSfalse;

	result = mDNSPlatformInit(m);
	
	return(result);
	}

extern void mDNSCoreInitComplete(mDNS *const m, mStatus result)
	{
	m->mDNSPlatformStatus = result;
	if (m->Callback) m->Callback(m, mStatus_NoError);
	mDNS_Lock(m);	// This lock/unlock causes a ScheduleNextTask(m) to get things started
	mDNS_Unlock(m);
	}

extern void mDNS_Close(mDNS *const m)
	{
	NetworkInterfaceInfo *i;
	const mDNSs32 timenow = mDNS_Lock(m);

#if MDNS_DEBUGMSGS
	ResourceRecord *rr;
	int rrcache_active = 0;
	for (rr = m->rrcache; rr; rr=rr->next) if (CacheRRActive(m, rr)) rrcache_active++;
	debugf("mDNS_Close: RR Cache now using %d records, %d active", m->rrcache_used, rrcache_active);
#endif

	m->ActiveQuestions = mDNSNULL;		// We won't be answering any more questions!
	
	for (i=m->HostInterfaces; i; i=i->next)
		if (i->Advertise)
			mDNS_DeadvertiseInterface(m, i, timenow);

	// Make sure there are nothing but deregistering records remaining in the list
	if (m->CurrentRecord) debugf("DiscardDeregistrations ERROR m->CurrentRecord already set");
	m->CurrentRecord = m->ResourceRecords;
	while (m->CurrentRecord)
		{
		ResourceRecord *rr = m->CurrentRecord;
		m->CurrentRecord = rr->next;
		if (rr->RecordType != kDNSRecordTypeDeregistering)
			{
			debugf("mDNS_Close: Record type %X still in ResourceRecords list %##s", rr->RecordType, rr->name.c);
			mDNS_Deregister_internal(m, rr, timenow, mDNS_Dereg_normal);
			}
		}

	if (m->ResourceRecords) debugf("mDNS_Close: Sending final packets for deregistering records");
	else debugf("mDNS_Close: No deregistering records remain");

	// If any deregistering records remain, send their deregistration announcements before we exit
	if (m->mDNSPlatformStatus != mStatus_NoError)
		DiscardDeregistrations(m, timenow);
	else
		while (m->ResourceRecords)
			SendResponses(m, timenow);
	
	mDNS_Unlock(m);
	debugf("mDNS_Close: mDNSPlatformClose");
	mDNSPlatformClose(m);
	debugf("mDNS_Close: done");
	}
