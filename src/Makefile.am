# $Id$
#

sbin_PROGRAMS = mt-daapd
bin_PROGRAMS = wavstreamer

if COND_REND_POSIX
PRENDSRC=mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c mDNSUNP.c rend-posix.c mDNSPlatformFunctions.h rend-unix.c
endif

if COND_REND_HOWL
HRENDSRC=rend-howl.c rend-unix.c
endif

if COND_REND_OSX
ORENDSRC=rend-osx.c rend-unix.c
endif

if COND_OGGVORBIS
OGGVORBISSRC=ogg.c
endif

if COND_FLAC
FLACSRC=flac.c
endif

if COND_SQLITE
SQLITEDB=dbs-sqlite.c
endif

wavstreamer_SOURCES = wavstreamer.c

mt_daapd_SOURCES = main.c daapd.h rend.h uici.c uici.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	mp3-scanner.h mp3-scanner.c rend-unix.h strcasestr.c strcasestr.h strsep.c \
	dynamic-art.c dynamic-art.h query.c query.h ssc.c ssc.h \
	db-generic.c db-generic.h dispatch.c dispatch.h \
	$(PRENDSRC) $(ORENDSRC) $(HRENDSRC) $(OGGVORBISSRC) $(FLACSRC) \
	$(SQLITEDB)

EXTRA_DIST = mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c \
	mDNSUNP.c mDNSPlatformFunctions.h mDNSPosix.h mDNSUNP.h \
	rend-howl.c rend-posix.c rend-osx.c db-memory.c \
	db-gdbm.c strcasestr.h redblack.c redblack.h db-memory.c \
	parser.h ogg.c flac.c dbs-sqlite.c dbs-sqlite.h
