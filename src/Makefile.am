
sbin_PROGRAMS = owntone

if COND_SPOTIFY
SPOTIFY_SRC = \
	library/spotify_webapi.c library/spotify_webapi.h inputs/spotify.c inputs/spotify.h
endif
if COND_LIBRESPOTC
LIBRESPOTC_SRC = \
	inputs/spotify_librespotc.c
endif
if COND_LIBSPOTIFY
LIBSPOTIFY_SRC = \
	inputs/spotify_libspotify.c \
	inputs/libspotify/libspotify.c inputs/libspotify/libspotify.h
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
	dacp_prop.gperf \
	dmap_fields.gperf

GPERF_SRC = $(GPERF_FILES:.gperf=_hash.h)

LEXER_SRC  = parsers/daap_lexer.l parsers/smartpl_lexer.l parsers/rsp_lexer.l
PARSER_SRC = parsers/daap_parser.y parsers/smartpl_parser.y parsers/rsp_parser.y

# This flag is given to Bison and tells it to produce headers. Note that
# automake recognizes this flag too, and has special logic around it, so don't
# change it to compound arguments (so for instance no "-dv"). I'm also not sure
# --defines will work instead of -d.
AM_YFLAGS = -d

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
	smartpl_query.c smartpl_query.h \
	player.c player.h \
	worker.c worker.h \
	settings.c settings.h \
	input.h input.c \
	inputs/file.c inputs/http.c inputs/pipe.c inputs/timer.c \
	outputs.h outputs.c \
	outputs/rtp_common.h outputs/rtp_common.c \
	outputs/raop.c outputs/airplay.c $(PAIR_AP_SRC) \
	outputs/airplay_events.c outputs/airplay_events.h \
	outputs/streaming.c outputs/dummy.c outputs/fifo.c outputs/rcp.c \
	$(ALSA_SRC) $(PULSEAUDIO_SRC) $(CHROMECAST_SRC) \
	evrtsp/rtsp.c evrtsp/evrtsp.h evrtsp/rtsp-internal.h evrtsp/log.h \
	$(SPOTIFY_SRC) $(LIBRESPOTC_SRC) $(LIBSPOTIFY_SRC) \
	$(LASTFM_SRC) \
	$(MPD_SRC) \
	listener.c listener.h \
	commands.c commands.h \
	mxml-compat.h \
	outputs/plist_wrap.h \
	$(LIBWEBSOCKETS_SRC) \
	$(GPERF_SRC) \
	$(LEXER_SRC) $(PARSER_SRC)

# This should ensure the headers are built first. automake knows how to make
# parser headers, but doesn't know how to do that for flex. So instead we set
# the C files as target, as the AM_LFLAGS will make sure headers are produced.
BUILT_SOURCES = \
	$(GPERF_SRC) \
	$(LEXER_SRC:.l=.h) $(PARSER_SRC:.y=.h)

# automake doesn't know how to make lexer headers, nor does it automatically
# include them, so need to specify them as EXTRA_DIST.
EXTRA_DIST = \
	$(GPERF_FILES) \
	$(LEXER_SRC:.l=.h)

# gperf construction rules
%_hash.h: %.gperf
	$(AM_V_GEN)$(GPERF) --output-file=$@ $<

# Rule for generating lexer headers. $@ is an automatic variable that is equal
# to the particular target name, so a header file name.
$(LEXER_SRC:.l=.h): $(LEXER_SRC)
	$(LEX) --header-file=$@ --stdout $(@:.h=.l) > /dev/null

# Anything built by make should be cleaned by make clean, but when it comes to
# flex/bison automake's support leaves something to be desired
clean-local:
	rm -f $(LEXER_SRC:.l=.[ch]) $(PARSER_SRC:.y=.[ch])
