
sbin_PROGRAMS = owntone

if COND_SPOTIFY
SPOTIFY_SRC = \
	library/spotify_webapi.c library/spotify_webapi.h \
	inputs/spotify.c inputs/spotify.h \
	inputs/spotifyc/spotifyc.c inputs/spotifyc/spotifyc.h inputs/spotifyc/spotifyc-internal.h \
	inputs/spotifyc/connection.c inputs/spotifyc/connection.h \
	inputs/spotifyc/channel.c inputs/spotifyc/channel.h \
	inputs/spotifyc/crypto.c inputs/spotifyc/crypto.h \
	inputs/spotifyc/shannon/ShannonFast.c \
	inputs/spotifyc/shannon/ShannonInternal.h inputs/spotifyc/shannon/Shannon.h \
	inputs/spotifyc/proto/keyexchange.pb-c.c inputs/spotifyc/proto/keyexchange.pb-c.h \
	inputs/spotifyc/proto/authentication.pb-c.c inputs/spotifyc/proto/authentication.pb-c.h \
	inputs/spotifyc/proto/mercury.pb-c.c inputs/spotifyc/proto/mercury.pb-c.h \
	inputs/spotifyc/proto/metadata.pb-c.c inputs/spotifyc/proto/metadata.pb-c.h
endif

if COND_LASTFM
LASTFM_SRC=lastfm.c lastfm.h
endif

if COND_CHROMECAST
if COND_PROTOBUF_OLD
CHROMECAST_SRC=outputs/cast.c outputs/cast_channel.v0.pb-c.h outputs/cast_channel.v0.pb-c.c
else
CHROMECAST_SRC=outputs/cast.c outputs/cast_channel.pb-c.h outputs/cast_channel.pb-c.c
endif
endif

if COND_MPD
MPD_SRC=mpd.c mpd.h
endif

PAIR_AP_SRC = \
	pair_ap/pair_fruit.c pair_ap/pair_homekit.c pair_ap/pair-tlv.c \
	pair_ap/pair.c pair_ap/pair-tlv.h pair_ap/pair-internal.h pair_ap/pair.h
AM_CPPFLAGS += -DCONFIG_GCRYPT

if COND_ALSA
ALSA_SRC=outputs/alsa.c
endif

if COND_PULSEAUDIO
PULSEAUDIO_SRC=outputs/pulse.c
endif

if COND_AVAHI
MDNS_SRC=mdns_avahi.c
else
MDNS_SRC=mdns_dnssd.c
endif

if COND_LIBWEBSOCKETS
LIBWEBSOCKETS_SRC=websocket.c websocket.h
endif

GPERF_FILES = \
	daap_query.gperf \
	rsp_query.gperf \
	dacp_prop.gperf \
	dmap_fields.gperf

GPERF_SRC = $(GPERF_FILES:.gperf=_hash.h)

ANTLR_GRAMMARS = \
	RSP.g RSP2SQL.g \
	DAAP.g DAAP2SQL.g \
	SMARTPL.g SMARTPL2SQL.g 

ANTLR_TOKENS = $(ANTLR_GRAMMARS:.g=.tokens)

ANTLR_DEPS = $(ANTLR_GRAMMARS:%.g=$(srcdir)/%.u)

ANTLR_SRC = \
	RSPLexer.c RSPLexer.h RSPParser.c RSPParser.h \
	RSP2SQL.c RSP2SQL.h \
	DAAPLexer.c DAAPLexer.h DAAPParser.c DAAPParser.h \
	DAAP2SQL.c DAAP2SQL.h \
	SMARTPLLexer.c SMARTPLLexer.h SMARTPLParser.c SMARTPLParser.h  \
	SMARTPL2SQL.c SMARTPL2SQL.h

ANTLR_OBJECTS = \
	RSPLexer.$(OBJEXT) RSPParser.$(OBJEXT) RSP2SQL.$(OBJEXT) \
	DAAPLexer.$(OBJEXT) DAAPParser.$(OBJEXT) DAAP2SQL.$(OBJEXT) \
	SMARTPLLexer.$(OBJEXT) SMARTPLParser.$(OBJEXT) SMARTPL2SQL.$(OBJEXT)

AM_CPPFLAGS += \
	$(OWNTONE_CPPFLAGS) \
	$(OWNTONE_OPTS_CPPFLAGS) \
	$(COMMON_CPPFLAGS) \
	\
	-D_GNU_SOURCE \
	-DDATADIR=\"$(pkgdatadir)\" \
	-DCONFDIR=\"$(sysconfdir)\" \
	-DSTATEDIR=\"$(localstatedir)\" \
	-DPKGLIBDIR=\"$(pkglibdir)\"

owntone_LDADD = \
	$(OWNTONE_LIBS) \
	$(OWNTONE_OPTS_LIBS) \
	$(COMMON_LIBS)

owntone_SOURCES = main.c \
	db.c db.h \
	db_init.c db_init.h \
	db_upgrade.c db_upgrade.h \
	logger.c logger.h \
	conffile.c conffile.h \
	cache.c cache.h \
	library/filescanner.c library/filescanner.h \
	library/filescanner_ffmpeg.c library/filescanner_playlist.c \
	library/filescanner_smartpl.c library/filescanner_itunes.c \
	library/rssscanner.c \
	library.c library.h \
	$(MDNS_SRC) mdns.h \
	remote_pairing.c remote_pairing.h \
	httpd.c httpd.h \
	httpd_rsp.c httpd_rsp.h \
	httpd_daap.c httpd_daap.h \
	httpd_dacp.c httpd_dacp.h \
	httpd_jsonapi.c httpd_jsonapi.h \
	httpd_streaming.c httpd_streaming.h \
	httpd_oauth.c httpd_oauth.h \
	httpd_artworkapi.c httpd_artworkapi.h \
	http.c http.h \
	dmap_common.c dmap_common.h \
	transcode.c transcode.h \
	artwork.c artwork.h \
	misc.c misc.h \
	misc_json.c misc_json.h \
	rng.c rng.h \
	rsp_query.c rsp_query.h \
	daap_query.c daap_query.h \
	smartpl_query.c smartpl_query.h \
	player.c player.h \
	worker.c worker.h \
	settings.c settings.h \
	input.h input.c \
	inputs/file.c inputs/http.c inputs/pipe.c inputs/timer.c \
	outputs.h outputs.c \
	outputs/rtp_common.h outputs/rtp_common.c \
	outputs/raop.c outputs/airplay.c $(PAIR_AP_SRC) \
	outputs/streaming.c outputs/dummy.c outputs/fifo.c \
	$(ALSA_SRC) $(PULSEAUDIO_SRC) $(CHROMECAST_SRC) \
	evrtsp/rtsp.c evrtsp/evrtsp.h evrtsp/rtsp-internal.h evrtsp/log.h \
	$(SPOTIFY_SRC) \
	$(LASTFM_SRC) \
	$(MPD_SRC) \
	listener.c listener.h \
	commands.c commands.h \
	mxml-compat.h \
	outputs/plist_wrap.h \
	$(LIBWEBSOCKETS_SRC) \
	$(GPERF_SRC) \
	$(ANTLR_SRC) 

# built by maintainers, and distributed. Clean with maintainer-clean
BUILT_SOURCES = \
	$(GPERF_SRC) \
	$(ANTLR_SRC) \
	$(ANTLR_TOKENS) \
	$(ANTLR_DEPS)

EXTRA_DIST = \
	$(GPERF_FILES) \
	$(ANTLR_GRAMMARS) \
	$(ANTLR_TOKENS) \
	$(ANTLR_DEPS)

# silence unused warnings from antlr generated files
$(ANTLR_OBJECTS): AM_CPPFLAGS += -Wno-unused

# gperf construction rules
%_hash.h: %.gperf
	$(AM_V_GEN)$(GPERF) --output-file=$@ $<

# silent rules for antlr
antlr_verbose = $(antlr_verbose_@AM_V@)
antlr_verbose_ = $(antlr_verbose_@AM_DEFAULT_V@)
antlr_verbose_0 = @echo "  GEN     " $< "products";

# ANTLR grammar products
%.tokens %.c %Lexer.c %Parser.c %Lexer.h %Parser.h %.h: %.g
	$(antlr_verbose)$(ANTLR) -Xconversiontimeout 30000 $(ANTLR_OPTIONS) -fo . $<

# ANTLR dependency files (bypass circular dependency of .g on .tokens)
%.u: %.g
	$(AM_V_GEN)$(ANTLR) -depend -fo . $< > $@
	$(AM_V_at)$(SED) -n -e '/^.*\.g[ ]*:\(.*\)/ { s//\1/;h;d; }' -e '/\.tokens.*:/ { p;d; }' -e '/:/ { G;s/\n/ /;p; }' $@ > $@-t
	$(AM_V_at)mv $@-t $@

-include $(ANTLR_DEPS)
