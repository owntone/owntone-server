# $Id$
#

sbin_PROGRAMS = mt-daapd
bin_PROGRAMS = wavstreamer

if COND_REND_POSIX
PRENDSRC=mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c mDNSUNP.c \
rend-posix.c mDNSPlatformFunctions.h rend-unix.c
endif

if COND_REND_HOWL
HRENDSRC=rend-howl.c rend-unix.c
endif

if COND_REND_OSX
ORENDSRC=rend-osx.c rend-unix.c
endif

if COND_OGGVORBIS
OGGVORBISSRC=scan-ogg.c
endif

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

if COND_SQLITE
SQLITEDB=db-sql-sqlite2.c db-sql-sqlite2.h
endif

if COND_SQLITE3
SQLITE3DB=db-sql-sqlite3.c db-sql-sqlite3.h
endif

if COND_SQL
SQLDB=db-sql.c db-sql.h
endif


wavstreamer_SOURCES = wavstreamer.c

mt_daapd_SOURCES = main.c daapd.h rend.h uici.c uici.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	mp3-scanner.h mp3-scanner.c rend-unix.h strcasestr.c strcasestr.h \
	strsep.c dynamic-art.c dynamic-art.h query.c query.h ssc.c ssc.h \
	db-generic.c db-generic.h dispatch.c dispatch.h \
	rxml.c rxml.h redblack.c redblack.h scan-mp3.c scan-mp4.c \
	scan-xml.c scan-wma.c scan-aac.c scan-aac.h scan-wav.c scan-url.c \
	smart-parser.c smart-parser.h xml-rpc.c xml-rpc.h \
	$(PRENDSRC) $(ORENDSRC) $(HRENDSRC) $(OGGVORBISSRC) $(FLACSRC) \
	$(MUSEPACKSRC) $(SQLITEDB) $(SQLITE3DB) $(SQLDB)

EXTRA_DIST = mDNS.c mDNSClientAPI.h mDNSDebug.h mDNSPosix.c \
	mDNSUNP.c mDNSPlatformFunctions.h mDNSPosix.h mDNSUNP.h \
	rend-howl.c rend-posix.c rend-osx.c scan-mpc.c \
	strcasestr.h scan-ogg.c scan-flac.c db-sql.c db-sql.h \
	db-sql-sqlite2.h db-sql-sqlite2.c \
	db-sql-sqlite3.h db-sql-sqlite3.c

