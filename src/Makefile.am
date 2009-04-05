
SUBDIRS = plugins

sbin_PROGRAMS = mt-daapd

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


mt_daapd_CPPFLAGS = @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @FFMPEG_CFLAGS@ @OGGVORBIS_CFLAGS@ @TAGLIB_CFLAGS@
mt_daapd_LDADD = @AVAHI_LIBS@ @SQLITE3_LIBS@ @FFMPEG_LIBS@ @OGGVORBIS_LIBS@ @FLAC_LIBS@ @TAGLIB_LIBS@ @LIBICONV@ @LIBDL@
mt_daapd_SOURCES = main.c daapd.h rend.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h \
	mp3-scanner.h mp3-scanner.c \
	filescanner_ffmpeg.c \
	db-generic.c db-generic.h ff-plugins.c ff-plugins.h \
	rxml.c rxml.h redblack.c redblack.h \
	scan-xml.c scan-wma.c scan-url.c \
	smart-parser.c smart-parser.h xml-rpc.c xml-rpc.h \
	os.h ll.c ll.h conf.c conf.h compat.c compat.h util.c util.h \
	os-unix.h os-unix.c os.h plugin.c plugin.h db-sql-updates.c \
	io.h io.c io-errors.h io-plugin.h \
	rend-avahi.c \
	db-sql.c db-sql.h db-sql-sqlite3.c db-sql-sqlite3.h\
	$(OGGVORBISSRC) \
	$(FLACSRC) $(MUSEPACKSRC) \
	$(UPNP)

EXTRA_DIST = scan-mpc.c \
	scan-ogg.c scan-flac.c \
	ff-plugins.h ff-dbstruct.h upnp.c upnp.h ff-plugin-events.h
