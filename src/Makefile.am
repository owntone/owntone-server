
sbin_PROGRAMS = mt-daapd

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

mt_daapd_CPPFLAGS = -D_GNU_SOURCE @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @FFMPEG_CFLAGS@ @CONFUSE_CFLAGS@ @TAGLIB_CFLAGS@ @MINIXML_CFLAGS@
mt_daapd_LDADD = @AVAHI_LIBS@ @SQLITE3_LIBS@ @FFMPEG_LIBS@ @CONFUSE_LIBS@ @FLAC_LIBS@ @TAGLIB_LIBS@ @LIBEVENT_LIBS@ @LIBAVL_LIBS@ @MINIXML_LIBS@
mt_daapd_SOURCES = main.c \
	err.c err.h \
	conffile.c conffile.h \
	filescanner.c filescanner.h \
	filescanner_ffmpeg.c filescanner_urlfile.c filescanner_m3u.c \
	mdns_avahi.c mdns_avahi.h \
	evhttp/http.c evhttp/evhttp.h evhttp/strlcpy.c evhttp/log.c \
	evhttp/http-internal.h evhttp/log.h evhttp/strlcpy-internal.h \
	httpd.c httpd.h \
	httpd_rsp.c httpd_rsp.h \
	httpd_daap.c httpd_daap.h \
	transcode.c transcode.h \
	misc.c misc.h \
	db-generic.c db-generic.h \
	scan-wma.c \
	smart-parser.c smart-parser.h \
	db-sql-updates.c \
	db-sql.c db-sql.h db-sql-sqlite3.c db-sql-sqlite3.h\
	$(FLACSRC) $(MUSEPACKSRC)

EXTRA_DIST = scan-mpc.c \
	scan-flac.c \
	ff-dbstruct.h
