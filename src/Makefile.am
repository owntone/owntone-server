
SUBDIRS = plugins

sbin_PROGRAMS = mt-daapd

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

mt_daapd_CPPFLAGS = -D_GNU_SOURCE @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @FFMPEG_CFLAGS@ @CONFUSE_CFLAGS@ @TAGLIB_CFLAGS@
mt_daapd_LDADD = @AVAHI_LIBS@ @SQLITE3_LIBS@ @FFMPEG_LIBS@ @CONFUSE_LIBS@ @FLAC_LIBS@ @TAGLIB_LIBS@ @LIBEVENT_LIBS@ @LIBAVL_LIBS@ @LIBDL@
mt_daapd_LDFLAGS = -export-dynamic
mt_daapd_SOURCES = main.c daapd.h webserver.c \
	webserver.h configfile.c configfile.h err.c err.h \
	conffile.c conffile.h \
	filescanner.c filescanner.h \
	filescanner_ffmpeg.c filescanner_urlfile.c filescanner_m3u.c \
	mdns_avahi.c mdns_avahi.h \
	httpd.c httpd.h \
	db-generic.c db-generic.h ff-plugins.c ff-plugins.h \
	scan-wma.c \
	smart-parser.c smart-parser.h xml-rpc.c xml-rpc.h \
	util.c util.h \
	plugin.c plugin.h db-sql-updates.c \
	io.h io.c io-errors.h io-plugin.h \
	db-sql.c db-sql.h db-sql-sqlite3.c db-sql-sqlite3.h\
	$(FLACSRC) $(MUSEPACKSRC)

EXTRA_DIST = scan-mpc.c \
	scan-flac.c \
	conf.h \
	ff-plugins.h ff-dbstruct.h ff-plugin-events.h
