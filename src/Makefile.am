# $Id$
#

BUILT_SOURCES=parser.h
AM_YFLAGS=-d

sbin_PROGRAMS = mt-daapd

if COND_REND_POSIX
RENDSRC=mdns/mDNS.c mdns/mDNSClientAPI.h mdns/mDNSDebug.h mdns/mDNSPosix.c mdns/mDNSUNP.c rend-posix.c
endif

if COND_REND_HOWL
RENDSRC=rend-howl.c
endif

if COND_REND_OSX
RENDSRC=rend-osx.c
endif

mt_daapd_SOURCES = main.c daapd.h rend.h uici.c uici.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	daap-proto.c daap-proto.h daap.c daap.h db-memory.c db-memory.h \
	mp3-scanner.h mp3-scanner.c playlist.c playlist.h \
	lexer.l parser.y $(RENDSRC)

EXTRA_DIST = mdns/mDNS.c mdns/mDNSClientAPI.h mdns/mDNSDebug.h mdns/mDNSPosix.c \
	mdns/mDNSUNP.c rend-howl.c rend-posix.c rend-osx.c


