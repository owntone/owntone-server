# $Id$
#
SUBDIRS = plugins

sbin_PROGRAMS = mt-daapd
bin_PROGRAMS = wavstreamer

LDADD=

if COND_REND_AVAHI
ARENDSRC=rend-avahi.c
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

if COND_GDBM
GDBM=db-gdbm.c db-gdbm.h
endif

if COND_UPNP
UPNP=upnp.c upnp.h
endif


wavstreamer_SOURCES = wavstreamer.c

mt_daapd_SOURCES = main.c daapd.h rend.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	mp3-scanner.h mp3-scanner.c \
	db-generic.c db-generic.h ff-plugins.c ff-plugins.h \
	rxml.c rxml.h redblack.c redblack.h scan-mp3.c scan-aif.c \
	scan-xml.c scan-wma.c scan-aac.c scan-aac.h scan-wav.c scan-url.c \
	smart-parser.c smart-parser.h xml-rpc.c xml-rpc.h \
	os.h ll.c ll.h conf.c conf.h compat.c compat.h util.c util.h \
	os-unix.h os-unix.c os.h plugin.c plugin.h db-sql-updates.c \
	memdebug.c memdebug.h ssl.h io.h io.c io-errors.h io-plugin.h \
	bsd-snprintf.c bsd-snprintf.h \
	$(ARENDSRC) $(OGGVORBISSRC) \
	$(FLACSRC) $(MUSEPACKSRC) $(SQLITEDB) $(SQLITE3DB) $(SQLDB) $(GDBM) \
	$(UPNP)

EXTRA_DIST = scan-mpc.c \
	scan-ogg.c scan-flac.c db-sql.c db-sql.h \
	db-sql-sqlite2.h db-sql-sqlite2.c \
	db-sql-sqlite3.h db-sql-sqlite3.c \
	db-gdbm.c db-gdbm.h \
	ff-plugins.h ff-dbstruct.h upnp.c upnp.h ff-plugin-events.h
