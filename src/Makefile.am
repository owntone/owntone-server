# $Id$
#

BUILT_SOURCES=parser.h
AM_YFLAGS=-d

sbin_PROGRAMS = mt-daapd

if COND_REND_POSIX
PRENDSRC=mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c mDNSUNP.c rend-posix.c mDNSPlatformFunctions.h
endif

if COND_REND_HOWL
HRENDSRC=rend-howl.c
endif

if COND_REND_OSX
ORENDSRC=rend-osx.c
endif

mt_daapd_SOURCES = main.c daapd.h rend.h uici.c uici.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	daap-proto.c daap-proto.h daap.c daap.h db-gdbm.c db-memory.h \
	mp3-scanner.h mp3-scanner.c playlist.c playlist.h rend-unix.c \
	rend-unix.h lexer.l parser.y strcasestr.c strcasestr.h strsep.c \
	$(PRENDSRC) $(ORENDSRC) $(HRENDSRC)

EXTRA_DIST = mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c \
	mDNSUNP.c mDNSPlatformFunctions.h mDNSPosix.h mDNSUNP.h \
	rend-howl.c rend-posix.c rend-osx.c strcasestr.c strsep.c db-memory.c \
	db-gdbm.c strcasestr.h redblack.c redblack.h db-memory.c




