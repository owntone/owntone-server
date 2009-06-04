
sbin_PROGRAMS = mt-daapd

if COND_FLAC
FLACSRC=scan-flac.c
endif

if COND_MUSEPACK
MUSEPACKSRC=scan-mpc.c
endif

ANTLR_GRAMMARS = \
	RSP.g RSP2SQL.g \
	DAAP.g DAAP2SQL.g

ANTLR_SOURCES = \
	RSPLexer.c RSPLexer.h RSPParser.c RSPParser.h \
	RSP2SQL.c RSP2SQL.h \
	DAAPLexer.c DAAPLexer.h DAAPParser.c DAAPParser.h \
	DAAP2SQL.c DAAP2SQL.h

ANTLR_PRODUCTS =

mt_daapd_CPPFLAGS = -D_GNU_SOURCE @AVAHI_CFLAGS@ @SQLITE3_CFLAGS@ @FFMPEG_CFLAGS@ @CONFUSE_CFLAGS@ @TAGLIB_CFLAGS@ @MINIXML_CFLAGS@
mt_daapd_LDADD = @AVAHI_LIBS@ @SQLITE3_LIBS@ @FFMPEG_LIBS@ @CONFUSE_LIBS@ @FLAC_LIBS@ @TAGLIB_LIBS@ @LIBEVENT_LIBS@ @LIBAVL_LIBS@ @MINIXML_LIBS@ @ANTLR3C_LIBS@
mt_daapd_SOURCES = main.c \
	logger.c logger.h \
	conffile.c conffile.h \
	filescanner.c filescanner.h \
	filescanner_ffmpeg.c filescanner_urlfile.c filescanner_m3u.c \
	mdns_avahi.c mdns_avahi.h \
	evhttp/http.c evhttp/evhttp.h evhttp/strlcpy.c \
	evhttp/http-internal.h evhttp/log.h evhttp/strlcpy-internal.h \
	httpd.c httpd.h \
	httpd_rsp.c httpd_rsp.h \
	httpd_daap.c httpd_daap.h \
	transcode.c transcode.h \
	misc.c misc.h \
	rsp_query.c rsp_query.h \
	daap_query.c daap_query.h \
	db-generic.c db-generic.h \
	scan-wma.c \
	smart-parser.c smart-parser.h \
	db-sql-updates.c \
	db-sql.c db-sql.h db-sql-sqlite3.c db-sql-sqlite3.h\
	$(FLACSRC) $(MUSEPACKSRC)

nodist_mt_daapd_SOURCES = \
	$(ANTLR_SOURCES)

EXTRA_DIST = \
	$(ANTLR_GRAMMARS) \
	scan-mpc.c \
	scan-flac.c \
	ff-dbstruct.h


# Let's help the dependencies a little.
rsp_query.c: RSPLexer.h RSPParser.h RSP2SQL.h
daap_query.c: DAAPLexer.h DAAPParser.h DAAP2SQL.h


SUFFIXES = .g .u

%.tokens %.c %Lexer.c %Parser.c %Lexer.h %Parser.h %.h: %.g
	@ANTLR@ $(ANTLR_OPTIONS) $<

%.u: %.g
	@ANTLR@ -depend $< > $@
	@echo -n "ANTLR_PRODUCTS += " > $@.tmp
	@grep : $@ | cut -d : -f 1 | tr -d ' ' | { while read f; do test "$$f" != "$<" && echo -n "$$f "; done } >> $@.tmp
	@cat $@.tmp >> $@
	@rm $@.tmp

clean-local:
	-rm $(ANTLR_PRODUCTS)
	rm $(ANTLR_GRAMMARS:.g=.u)

-include $(ANTLR_GRAMMARS:.g=.u)
