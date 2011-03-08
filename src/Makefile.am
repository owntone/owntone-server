
sbin_PROGRAMS = forked-daapd

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

if COND_ITUNES
ITUNESSRC=filescanner_itunes.c
endif

if COND_ALSA
ALSASRC=laudio_alsa.c
endif

if COND_OSS4
OSS4SRC=laudio_oss4.c
endif

GPERF_FILES = \
	daap_query.gperf \
	rsp_query.gperf \
	dacp_prop.gperf \
	dmap_fields.gperf

GPERF_PRODUCTS = \
	daap_query_hash.c \
	rsp_query_hash.c \
	dacp_prop_hash.c \
	dmap_fields_hash.c

ANTLR_GRAMMARS = \
	RSP.g RSP2SQL.g \
	DAAP.g DAAP2SQL.g

ANTLR_SOURCES = \
	RSPLexer.c RSPLexer.h RSPParser.c RSPParser.h \
	RSP2SQL.c RSP2SQL.h \
	DAAPLexer.c DAAPLexer.h DAAPParser.c DAAPParser.h \
	DAAP2SQL.c DAAP2SQL.h

ANTLR_PRODUCTS =

forked_daapd_CPPFLAGS = -D_GNU_SOURCE \
	-DDATADIR="\"$(pkgdatadir)\"" -DCONFDIR="\"$(sysconfdir)\"" \
	-DSTATEDIR="\"$(localstatedir)\"" -DPKGLIBDIR="\"$(pkglibdir)\"" \
	@OSS4CPPFLAGS@

forked_daapd_CFLAGS = \
	@ZLIB_CFLAGS@ @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @FFMPEG_CFLAGS@ \
	@CONFUSE_CFLAGS@ @TAGLIB_CFLAGS@ @MINIXML_CFLAGS@ @LIBPLIST_CFLAGS@ \
	@LIBGCRYPT_CFLAGS@ @GPG_ERROR_CFLAGS@ @ALSA_CFLAGS@

forked_daapd_LDADD = -lrt \
	@ZLIB_LIBS@ @AVAHI_LIBS@ @SQLITE3_LIBS@ @FFMPEG_LIBS@ \
	@CONFUSE_LIBS@ @FLAC_LIBS@ @TAGLIB_LIBS@ @LIBEVENT_LIBS@ \
	@LIBAVL_LIBS@ @MINIXML_LIBS@ @ANTLR3C_LIBS@ @LIBPLIST_LIBS@ \
	@LIBGCRYPT_LIBS@ @GPG_ERROR_LIBS@ @ALSA_LIBS@ @LIBUNISTRING@

forked_daapd_SOURCES = main.c \
	db.c db.h \
	logger.c logger.h \
	conffile.c conffile.h \
	filescanner.c filescanner.h \
	filescanner_ffmpeg.c filescanner_urlfile.c filescanner_m3u.c $(ITUNESSRC) \
	mdns_avahi.c mdns.h \
	remote_pairing.c remote_pairing.h \
	evhttp/http.c evhttp/evhttp.h \
	evhttp/http-internal.h evhttp/log.h \
	ffmpeg_url_evbuffer.c ffmpeg_url_evbuffer.h \
	httpd.c httpd.h \
	httpd_rsp.c httpd_rsp.h \
	httpd_daap.c httpd_daap.h \
	httpd_dacp.c httpd_dacp.h \
	dmap_common.c dmap_common.h \
	transcode.c transcode.h \
	artwork.c artwork.h \
	misc.c misc.h \
	rng.c rng.h \
	rsp_query.c rsp_query.h \
	daap_query.c daap_query.h \
	player.c player.h \
	$(ALSASRC) $(OSS4SRC) laudio.h \
	raop.c raop.h \
	evrtsp/rtsp.c evrtp/evrtsp.h \
	evrtsp/rtsp-internal.h evrtsp/log.h \
	scan-wma.c \
	$(FLACSRC) $(MUSEPACKSRC)

nodist_forked_daapd_SOURCES = \
	$(ANTLR_SOURCES)

BUILT_SOURCES = \
	$(GPERF_PRODUCTS)

EXTRA_DIST = \
	$(ANTLR_GRAMMARS) \
	scan-mpc.c \
	scan-flac.c

CLEANFILES = \
	$(GPERF_PRODUCTS)


# gperf construction rules
%_hash.c: %.gperf
	if $(GPERF) $< > $@.tmp; then \
		mv $@.tmp $@; \
	elif $(GPERF) --version >/dev/null 2>&1; then \
		rm $@.tmp; \
		exit 1; \
	else \
		rm $@.tmp; \
		touch $@; \
	fi

# Support for building the parsers when ANTLR3 is available
if COND_ANTLR
SUFFIXES = .g .u

%.tokens %.c %Lexer.c %Parser.c %Lexer.h %Parser.h %.h: %.g
	$(ANTLR) $(ANTLR_OPTIONS) $<

%.u: %.g
	$(ANTLR) -depend $< > $@
	@echo -n "ANTLR_PRODUCTS += " > $@.tmp
	@grep : $@ | cut -d : -f 1 | tr -d ' ' | { while read f; do test "$$f" != "$<" && echo -n "$$f "; done } >> $@.tmp
	@cat $@.tmp >> $@
	@rm $@.tmp

BUILT_SOURCES += $(ANTLR_SOURCES)

CLEANFILES += \
	$(ANTLR_PRODUCTS) \
	$(ANTLR_GRAMMARS:.g=.u)

else !COND_ANTLR
DISTCLEANFILES = \
	$(ANTLR_PRODUCTS) \
	$(ANTLR_GRAMMARS:.g=.u)

endif

-include $(ANTLR_GRAMMARS:.g=.u)
