# $Id$
#
SUBDIRS = plugins

sbin_PROGRAMS = mt-daapd
bin_PROGRAMS = wavstreamer

if COND_OGGVORBIS
OGGVORBISSRC=scan-ogg.c
endif

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

if COND_UPNP
UPNP=upnp.c upnp.h
endif


wavstreamer_SOURCES = wavstreamer.c

mt_daapd_CPPFLAGS = @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @ID3TAG_CFLAGS@
mt_daapd_LDADD = @AVAHI_LIBS@ @SQLITE3_LIBS@ @ID3TAG_LIBS@
mt_daapd_SOURCES = main.c daapd.h rend.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h restart.c restart.h \
	mp3-scanner.h mp3-scanner.c \
	db-generic.c db-generic.h ff-plugins.c ff-plugins.h \
	rxml.c rxml.h redblack.c redblack.h scan-mp3.c scan-aif.c \
	scan-xml.c scan-wma.c scan-aac.c scan-aac.h scan-wav.c scan-url.c \
	smart-parser.c smart-parser.h xml-rpc.c xml-rpc.h \
	os.h ll.c ll.h conf.c conf.h compat.c compat.h util.c util.h \
	os-unix.h os-unix.c os.h plugin.c plugin.h db-sql-updates.c \
	io.h io.c io-errors.h io-plugin.h \
	bsd-snprintf.c bsd-snprintf.h \
	rend-avahi.c \
	db-sql.c db-sql.h db-sql-sqlite3.c db-sql-sqlite3.h\
	$(OGGVORBISSRC) \
	$(FLACSRC) $(MUSEPACKSRC) \
	$(UPNP)

EXTRA_DIST = scan-mpc.c \
	scan-ogg.c scan-flac.c \
	ff-plugins.h ff-dbstruct.h upnp.c upnp.h ff-plugin-events.h
