dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([owntone], [28.11])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects 1.11])
AM_SILENT_RULES([yes])

dnl Requires autoconf 2.60
AC_USE_SYSTEM_EXTENSIONS

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CC_C_O
AC_PROG_SED
AC_PROG_MKDIR_P
LT_INIT([disable-static])

AC_PATH_PROG([GPERF], [[gperf]])
AS_IF([[test -z "$GPERF"]],
	[AS_IF([[test -f "$srcdir/src/dmap_fields_hash.h"]],
		[AM_MISSING_PROG([GPERF], [[gperf]])
		AC_MSG_NOTICE([[

GNU gperf not found, but it's output appears to be present.
If you modify any .gperf files, you will need to install it.]])],
		[AC_MSG_ERROR([[GNU gperf required, please install it.]])])
	])

AX_PROG_FLEX([AC_DEFINE([LEX], [flex], [flex found])],
	[AS_IF([test ! -f "$srcdir/src/parsers/smartpl_lexer.c"],
		[AC_MSG_ERROR([flex required, please install it])])
	])
AX_PROG_BISON([AC_DEFINE([YACC], [bison], [GNU bison found])],
	[AS_IF([test ! -f "$srcdir/src/parsers/smartpl_parser.c"],
		[AC_MSG_ERROR([GNU bison required, please install it])])
	])

dnl Enable all warnings by default.
AM_CPPFLAGS="-Wall"
AC_SUBST([AM_CPPFLAGS])

dnl Checks for header files.
AC_CHECK_HEADERS_ONCE([regex.h pthread_np.h])
AC_CHECK_HEADERS([sys/wait.h sys/param.h dirent.h getopt.h stdint.h], [],
	[AC_MSG_ERROR([[Missing header required to build OwnTone]])])
AC_CHECK_HEADERS([time.h], [],
	[AC_MSG_ERROR([[Missing header required to build OwnTone]])])
AC_CHECK_FUNCS_ONCE([posix_fadvise pipe2 gettid])
AC_CHECK_FUNCS([strptime strtok_r], [],
	[AC_MSG_ERROR([[Missing function required to build OwnTone]])])

dnl check for clock_gettime or replace it
AC_SEARCH_LIBS([clock_gettime], [rt],
	[AC_DEFINE([HAVE_CLOCK_GETTIME], 1,
		[Define to 1 if have clock_gettime function])],
	[AC_CHECK_HEADER([mach/mach_time.h],
	[AC_DEFINE([HAVE_MACH_CLOCK], 1,
		[Define to 1 if mach kernel clock replacement available])],
	[AC_MSG_ERROR([[Missing clock_gettime and any replacement]])])])

dnl check for timer_settime or replace it
AC_SEARCH_LIBS([timer_settime], [rt],
	[AC_DEFINE([HAVE_TIMER_SETTIME], 1,
		[Define to 1 if have timer_settime function])],
	[AC_CHECK_HEADER([mach/mach_time.h],
	[AC_DEFINE([HAVE_MACH_TIMER], 1,
		[Define to 1 if mach kernel clock replacement available])],
	[AC_MSG_ERROR([[Missing timer_settime and any replacement]])])])

AC_SEARCH_LIBS([pthread_exit], [pthread], [],
	[AC_MSG_ERROR([[pthreads library is required]])])
AC_SEARCH_LIBS([pthread_setname_np], [pthread],
	[dnl Validate pthread_setname_np with 2 args (some have 1)
	 AC_MSG_CHECKING([[for two-parameter pthread_setname_np]])
	 AC_LINK_IFELSE([AC_LANG_PROGRAM([[@%:@include <pthread.h>]],
		[[pthread_setname_np(pthread_self(), "name");]])],
		[AC_MSG_RESULT([yes])
		 AC_DEFINE([HAVE_PTHREAD_SETNAME_NP], 1,
			[Define to 1 if you have pthread_setname_np])],
		[AC_MSG_RESULT([[no]])])],
	[AC_SEARCH_LIBS([pthread_set_name_np], [pthread],
		[AC_CHECK_FUNCS([pthread_set_name_np])])])

AC_SEARCH_LIBS([uuid_generate_random], [uuid],
	[AC_DEFINE([HAVE_UUID], 1,
		[Define to 1 if you have uuid_generate_random function])])

AC_SEARCH_LIBS([log10], [m])
AC_SEARCH_LIBS([lrint], [m])
AC_SEARCH_LIBS([fabs], [m])

dnl Large File Support (LFS)
AC_SYS_LARGEFILE
AC_TYPE_OFF_T

dnl Checks for libraries.

AC_SUBST([COMMON_LIBS])
AC_SUBST([COMMON_CPPFLAGS])
AC_SUBST([OWNTONE_LIBS])
AC_SUBST([OWNTONE_CPPFLAGS])
AC_SUBST([OWNTONE_OPTS_LIBS])
AC_SUBST([OWNTONE_OPTS_CPPFLAGS])

AM_ICONV
dnl All OWNTONE_ macros defined in m4/owntone_checks.m4
OWNTONE_FUNC_REQUIRE([COMMON], [GNU libunistring], [LIBUNISTRING], [unistring],
	[u8_strconv_from_locale], [uniconv.h], [],
	[dnl Retry test with iconv library
	 OWNTONE_VARS_PREPEND([COMMON], [LIBICONV], [INCICONV])
	 OWNTONE_FUNC_REQUIRE([COMMON], [GNU libunistring], [LIBUNISTRING],
		[unistring], [u8_strconv_from_locale], [uniconv.h])])

OWNTONE_MODULES_CHECK([OWNTONE], [ZLIB], [zlib], [deflate], [zlib.h])
OWNTONE_MODULES_CHECK([OWNTONE], [CONFUSE], [libconfuse >= 3.0], [cfg_init], [confuse.h])
OWNTONE_MODULES_CHECK([OWNTONE], [LIBCURL], [libcurl], [curl_global_init], [curl/curl.h])
OWNTONE_MODULES_CHECK([OWNTONE], [LIBSODIUM], [libsodium], [sodium_init], [sodium.h])
OWNTONE_MODULES_CHECK([OWNTONE], [LIBXML2], [libxml-2.0], [xmlInitParser], [libxml/parser.h])

OWNTONE_MODULES_CHECK([COMMON], [SQLITE3], [sqlite3 >= 3.5.0],
	[sqlite3_initialize], [sqlite3.h],
	[dnl Check that SQLite3 has the unlock notify API built-in
	 AC_CHECK_FUNC([[sqlite3_unlock_notify]], [],
		[AC_MSG_ERROR([[SQLite3 was built without unlock notify support]])])
	 dnl Check for sqlite3_expanded_sql (optional)
	 AC_CHECK_FUNCS([sqlite3_expanded_sql])
	 dnl Check that SQLite3 has been built with threadsafe operations
	 AC_MSG_CHECKING([[if SQLite3 was built with threadsafe operations support]])
	 AC_RUN_IFELSE([AC_LANG_PROGRAM([[#include <sqlite3.h>
		]], [[
		int ret = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
		if (ret != SQLITE_OK)
		  return 1;]])],
		[AC_MSG_RESULT([[yes]])],
		[AC_MSG_RESULT([[no]])
		 AC_MSG_ERROR([[SQLite3 was not built with threadsafe operations support]])],
		[AC_MSG_RESULT([[runtime will tell]])])
	])

OWNTONE_MODULES_CHECK([OWNTONE], [LIBEVENT], [libevent >= 2.1.4],
	[event_base_new], [event2/event.h],
	[dnl check for version 2.2 (with websocket server support)
	 PKG_CHECK_EXISTS([libevent >= 2.2.1],
	 	[AC_DEFINE([HAVE_LIBEVENT22], 1,
			[Define to 1 if you have libevent > 2.2])],
		[])
	])
OWNTONE_MODULES_CHECK([OWNTONE], [LIBEVENT_PTHREADS], [libevent_pthreads],
	[evthread_use_pthreads], [event2/thread.h])

dnl Check for evhttp_connection_get_peer() signature
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
	#include <event2/http.h>
	void evhttp_connection_get_peer(struct evhttp_connection *, const char **, ev_uint16_t *);
	]], [[ ]]
	)],
	[AC_MSG_RESULT([yes]); AC_DEFINE([HAVE_EVHTTP_CONNECTION_GET_PEER_CONST_CHAR], 1,
		[Define to 1 if evhttp_connection_get_peer expects const char**])]
	)

OWNTONE_MODULES_CHECK([OWNTONE], [JSON_C], [json-c],
	[json_tokener_parse], [json.h],
	[dnl check for old version
	 PKG_CHECK_EXISTS([json-c >= 0.11], [],
		[AC_DEFINE([HAVE_JSON_C_OLD], 1,
			[Define to 1 if you have json-c < 0.11])])
	])

dnl Build with libplist (2.2.0 does not ship libplist.pc, only libplist-2.0.pc)
PKG_CHECK_EXISTS([libplist],
	[OWNTONE_MODULES_CHECK([OWNTONE], [LIBPLIST], [libplist >= 0.16],
		[plist_dict_get_item], [plist/plist.h])],
	[OWNTONE_MODULES_CHECK([OWNTONE], [LIBPLIST], [libplist-2.0],
		[plist_dict_get_item], [plist/plist.h])])

AM_PATH_LIBGCRYPT([1:1.7.0])
OWNTONE_FUNC_REQUIRE([OWNTONE], [GNU Crypt Library], [LIBGCRYPT], [gcrypt],
	[gcry_control], [gcrypt.h])
AM_PATH_GPG_ERROR([1.6])
OWNTONE_FUNC_REQUIRE([OWNTONE], [GNUPG Error Values], [GPG_ERROR_MT], [gpg-error],
	[gpg_err_init], [gpg-error.h])


AC_CHECK_HEADER([sys/eventfd.h], [AC_CHECK_FUNCS([eventfd])])

AC_CHECK_HEADER([sys/timerfd.h], [AC_CHECK_FUNC([timerfd_create],
	[AC_DEFINE([HAVE_TIMERFD], 1, [Define to 1 if you have timerfd])])])

OWNTONE_FUNC_REQUIRE([OWNTONE], [inotify], [INOTIFY], [inotify],
	[inotify_add_watch], [sys/inotify.h])

have_signal=no
AC_CHECK_HEADER([sys/signalfd.h], [AC_CHECK_FUNCS([signalfd], [have_signal=yes])])
AC_CHECK_HEADER([sys/event.h], [AC_CHECK_FUNCS([kqueue], [have_signal=yes])])
AS_IF([[test "$have_signal" = "no"]],
	[AC_MSG_ERROR([[Either signalfd or kqueue are required]])])

AC_CHECK_HEADERS_ONCE([endian.h sys/endian.h])
AC_CHECK_DECL([htobe16], [],
	[AC_CHECK_HEADERS([libkern/OSByteOrder.h], [],
		[AC_MSG_ERROR([[Missing functions to swap byte order]])])],
	[AC_INCLUDES_DEFAULT[
#ifdef HAVE_ENDIAN_H
# include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
# include <sys/endian.h>
#endif
	]])

dnl libav checks should be last, as they are sometimes both installed
dnl and the CPPFLAGS/LIBS needs to be at the front of the search list.
dnl Handle alternative package names for libav
PKG_CHECK_EXISTS([libavcodec-libav], [LIBAV=-libav], [LIBAV=])
dnl Preference for ffmpeg if we have both (this could be an option...)
PKG_CHECK_EXISTS([libavcodec], [LIBAV=])
dnl Option to choose libav even if ffmpeg is detected first
AC_ARG_WITH([libav], [AS_HELP_STRING([--with-libav],
	[choose libav even if ffmpeg present (default=no)])],
	[[LIBAV=-libav]], [[LIBAV=]])
dnl libav/ffmpeg requires many feature checks
OWNTONE_MODULES_CHECK([OWNTONE], [LIBAV],
	[libavformat$LIBAV libavcodec$LIBAV libavutil$LIBAV libavfilter$LIBAV],
	[av_init_packet], [libavcodec/avcodec.h],
	[dnl Checks for misc libav and ffmpeg API differences
	 AC_MSG_CHECKING([whether libav libraries are ffmpeg])
	 AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <libavcodec/avcodec.h>
	 ]], [[
#if LIBAVCODEC_VERSION_MICRO >= 100
  /* ffmpeg uses 100+ as its micro version */
#else
#error libav provider is not ffmpeg
#endif
	 ]])], [
	  [is_ffmpeg=yes]
	  AC_DEFINE([HAVE_FFMPEG], 1,
		[Define to 1 if you have ffmpeg (not libav)])],
	 [[is_ffmpeg=no]])
	 AC_MSG_RESULT([$is_ffmpeg])
	 OWNTONE_CHECK_DECLS([av_version_info],
		[libavutil/avutil.h])
	 OWNTONE_CHECK_DECLS([avformat_network_init],
		[libavformat/avformat.h])
	 OWNTONE_CHECK_DECLS([av_dict_iterate],
		[libavutil/dict.h])
	])

AC_CHECK_SIZEOF([void *])

dnl --- Begin configuring the options ---
dnl ALSA
OWNTONE_ARG_WITH_CHECK([OWNTONE_OPTS], [ALSA support], [alsa], [ALSA],
	[alsa], [snd_mixer_open], [alsa/asoundlib.h])
AM_CONDITIONAL([COND_ALSA], [[test "x$with_alsa" = "xyes"]])

dnl PULSEAUDIO
OWNTONE_ARG_WITH_CHECK([OWNTONE_OPTS], [Pulseaudio support], [pulseaudio], [LIBPULSE],
	[libpulse], [pa_stream_get_state], [pulse/pulseaudio.h],
	[AC_CHECK_FUNCS([pa_threaded_mainloop_set_name])])
AM_CONDITIONAL([COND_PULSEAUDIO], [[test "x$with_pulseaudio" = "xyes"]])

dnl Build with libwebsockets
OWNTONE_ARG_WITH_CHECK([OWNTONE_OPTS], [libwebsockets support], [libwebsockets], [LIBWEBSOCKETS],
	[libwebsockets >= 2.0.2])
AM_CONDITIONAL([COND_LIBWEBSOCKETS], [[test "x$with_libwebsockets" = "xyes"]])

dnl Build with Avahi (or Bonjour if not)
OWNTONE_ARG_WITH_CHECK([OWNTONE_OPTS], [Avahi mDNS], [avahi], [AVAHI],
	[avahi-client >= 0.6.24], [avahi_client_new], [avahi-client/client.h])
AS_IF([[test "x$with_avahi" = "xno"]],
	[OWNTONE_FUNC_REQUIRE([OWNTONE_OPTS], [Bonjour DNS_SD], [DNSSD], [dns_sd],
		[DNSServiceGetAddrInfo], [dns_sd.h], [],
		[AC_MSG_ERROR([[Avahi client or Bonjour DNS_SD required, please install one.]])])])
AM_CONDITIONAL([COND_AVAHI], [[test "x$with_avahi" = "xyes"]])

dnl Spotify support
OWNTONE_ARG_DISABLE([Spotify support], [spotify], [SPOTIFY_LIBRESPOTC],
	[OWNTONE_MODULES_CHECK([OWNTONE_OPTS], [LIBPROTOBUF_C],
		[libprotobuf-c >= 1.0.0], [protobuf_c_message_pack],
		[protobuf-c/protobuf-c.h])
	 OWNTONE_VAR_PREPEND([OWNTONE_OPTS_LIBS], [inputs/librespot-c/librespot-c.a])
	 AC_DEFINE([SPOTIFY], 1,
		[Define to 1 to enable Spotify])
	])
AM_CONDITIONAL([COND_SPOTIFY], [[test "x$enable_spotify" = "xyes"]])

dnl LastFM support
OWNTONE_ARG_DISABLE([LastFM support], [lastfm], [LASTFM])
AM_CONDITIONAL([COND_LASTFM], [[test "x$enable_lastfm" = "xyes"]])

dnl ChromeCast support with libprotobuf-c
OWNTONE_ARG_ENABLE([Chromecast support], [chromecast], [CHROMECAST],
	[OWNTONE_MODULES_CHECK([OWNTONE_OPTS], [LIBPROTOBUF_C],
		[libprotobuf-c >= 1.0.0], [protobuf_c_message_pack],
		[protobuf-c/protobuf-c.h], [],
		[OWNTONE_FUNC_REQUIRE([OWNTONE_OPTS], [v0 libprotobuf-c],
			[LIBPROTOBUF_OLD], [protobuf-c],
			[protobuf_c_message_pack],
			[google/protobuf-c/protobuf-c.h],
			[AC_DEFINE([HAVE_PROTOBUF_OLD], 1,
				[Define to 1 if you have libprotobuf < 1.0.0])
			 [protobuf_old=yes]],
			[AC_MSG_ERROR([[Chromecast support requires protobuf-c]])])
		])
	 OWNTONE_MODULES_CHECK([OWNTONE_OPTS], [GNUTLS], [gnutls], [gnutls_init],
		[gnutls/gnutls.h])
	])
AM_CONDITIONAL([COND_CHROMECAST], [[test "x$enable_chromecast" = "xyes"]])
AM_CONDITIONAL([COND_PROTOBUF_OLD], [[test "x$protobuf_old" = "xyes"]])

dnl Prefer AirPlay 2
OWNTONE_ARG_ENABLE([preference for AirPlay 2 for devices that support both 1 and 2], [preferairplay2], [PREFER_AIRPLAY2])

dnl DB profiling support
OWNTONE_ARG_ENABLE([DB profiling support], [dbprofile], [DB_PROFILE])

dnl MPD support
OWNTONE_ARG_DISABLE([MPD client protocol support], [mpd], [MPD])
AM_CONDITIONAL([COND_MPD], [[test "x$enable_mpd" = "xyes"]])

AC_PATH_PROG([NPM], [[npm]])

dnl Include default webinterface
OWNTONE_ARG_DISABLE([include default web interface], [webinterface], [WEBINTERFACE],
	[AS_IF(
		[[test "x$with_libwebsockets" = "xno"]],
			[AC_MSG_ERROR([[Web interface requires libwebsockets >= 2.0.2 (or use --disable-webinterface)]])],
		[[test -z "$NPM"]],
			[AS_IF([[test -f "$srcdir/web-src/htdocs/index.html"]],
				[AM_MISSING_PROG([NPM], [[npm]])
				AC_MSG_NOTICE([[
npm not found, but a prebuilt web interface is present.
If you modify any web interface source files, you will need to install npm.
				]])],
				[AC_MSG_ERROR([[npm required, please install it.]])])
			]
	)])
AM_CONDITIONAL([COND_WEBINTERFACE], [[test "x$enable_webinterface" = "xyes"]])

dnl Use prebuilt webinterface even if npm is available
AC_ARG_WITH([prebuilt-webinterface],
	[AS_HELP_STRING([--with-prebuilt-webinterface],
		[Use prebuilt webinterface even if npm is available (default=no)]
	)],
	[AS_IF([[test -f "$srcdir/web-src/htdocs/index.html"]],
		[[with_prebuilt_webinterface=yes]],
		[AC_MSG_ERROR([[No prebuilt web interface found.]])]
	)],
	[[with_prebuilt_webinterface=]])
if test "x$with_prebuilt_webinterface" = "xyes"; then
	BUILD_WEBINTERFACE=
else
	BUILD_WEBINTERFACE=true
fi
AC_SUBST([BUILD_WEBINTERFACE])

dnl Creating and defining users and groups
OWNTONE_ARG_ENABLE([having 'make install' add user/group and 'make uninstall' delete], [install_user], [INSTALL_USER],
	[AC_PATH_PROG([GETENT], [[getent]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AC_PATH_PROG([USERADD], [[useradd]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AC_PATH_PROG([USERDEL], [[userdel]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AC_PATH_PROG([USERMOD], [[usermod]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AC_PATH_PROG([GROUPADD], [[groupadd]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AC_PATH_PROG([CHOWN], [[chown]], [], [$PATH$PATH_SEPARATOR/usr/sbin])
	 AS_IF([[test -z "$GETENT" -o -z "$USERADD" -o -z "$USERDEL" -o -z "$USERMOD" -o -z "$GROUPADD" -o -z "$CHOWN"]],
		[AC_MSG_ERROR([[Required program for --enable-install-user not found]])])
	])
AM_CONDITIONAL([COND_INSTALL_USER], [[test "x$enable_install_user" = "xyes"]])

AC_ARG_WITH([owntone_user],
	[AS_HELP_STRING([--with-user=USER], [User for running OwnTone (default=owntone)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
OWNTONE_USER=${withval:-owntone}
AC_SUBST([OWNTONE_USER])

AC_ARG_WITH([owntone_group],
	[AS_HELP_STRING([--with-group=GROUP], [Group for owntone user (default=USER)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
OWNTONE_GROUP=${withval:-$OWNTONE_USER}
AC_SUBST([OWNTONE_GROUP])

dnl Install config file
OWNTONE_ARG_DISABLE([install configuration file], [install_conf_file], [INSTALL_CONF_FILE])
AM_CONDITIONAL([COND_INSTALL_CONF_FILE], [[test "x$enable_install_conf_file" = "xyes"]])

# Service files are not installed like https://www.freedesktop.org/software/systemd/man/daemon.html
# instructs, because that means they go to /lib/systemd/system (the destination
# for package service files!) instead of /usr/local/etc/systemd/system with a
# default ./configure, and I don't want that on my machine. Also, the suggested
# approach is incredibly ugly.
OWNTONE_ARG_DISABLE([install systemd service file], [install_systemd], [INSTALL_SYSTEMD])
AM_CONDITIONAL([COND_INSTALL_SYSTEMD], [[test "x$enable_install_systemd" = "xyes"]])
AC_ARG_WITH([systemddir],
	[AS_HELP_STRING([--with-systemddir=DIR], [Directory for systemd service files (default=SYSCONFDIR/systemd/system)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
SYSTEMD_DIR=${withval:-$sysconfdir/systemd/system}
AC_SUBST([SYSTEMD_DIR])

dnl --- End options ---

dnl Unconditional since we always want to produce Makefiles for dist targets
AC_CONFIG_SUBDIRS([
	src/inputs/librespot-c
])

AC_CONFIG_FILES([
	web-src/Makefile
	src/Makefile
	sqlext/Makefile
	Makefile
	owntone.spec
])

WEBSRC_SUBDIRS="\
	public \
	src \
	src/components \
	src/filter \
	src/i18n \
	src/lib \
	src/pages \
	src/router \
	src/stores \
	src/templates \
	src/webapi"
AC_SUBST([WEBSRC_SUBDIRS])

WEBSRC_SOURCE_FILES="\
	public/android-chrome-512x512.png \
	public/mstile-150x150.png \
	public/browserconfig.xml \
	public/site.webmanifest \
	public/favicon.ico \
	public/android-chrome-192x192.png \
	public/safari-pinned-tab.svg \
	public/apple-touch-icon.png \
	public/favicon-16x16.png \
	public/logo.svg \
	public/favicon-32x32.png \
	src/components/ModalDialogAddRss.vue \
	src/components/ModalDialogPlaylistSpotify.vue \
	src/components/ListPlaylistsSpotify.vue \
	src/components/ListArtistsSpotify.vue \
	src/components/ModalDialogComposer.vue \
	src/components/ListArtists.vue \
	src/components/ControlSlider.vue \
	src/components/ModalDialogArtist.vue \
	src/components/ListPlaylists.vue \
	src/components/ModalDialogArtistSpotify.vue \
	src/components/ModalDialogPlaylistSave.vue \
	src/components/ModalDialogGenre.vue \
	src/components/ModalDialogTrackSpotify.vue \
	src/components/ListTracks.vue \
	src/components/ListGenres.vue \
	src/components/ModalDialogAlbumSpotify.vue \
	src/components/TabsAudiobooks.vue \
	src/components/IndexButtonList.vue \
	src/components/TabsSettings.vue \
	src/components/ModalDialogAddUrlStream.vue \
	src/components/ModalDialogDirectory.vue \
	src/components/ListComposers.vue \
	src/components/CoverArtwork.vue \
	src/components/NavbarItemOutput.vue \
	src/components/ModalDialogAlbum.vue \
	src/components/ControlDropdown.vue \
	src/components/ModalDialogPlaylist.vue \
	src/components/ListDirectories.vue \
	src/components/ModalDialog.vue \
	src/components/ListAlbums.vue \
	src/components/ListAlbumsSpotify.vue \
	src/components/ListItemQueueItem.vue \
	src/components/ListTracksSpotify.vue \
	src/components/LyricsPane.vue \
	src/components/ModalDialogQueueItem.vue \
	src/components/ModalDialogRemotePairing.vue \
	src/components/ModalDialogTrack.vue \
	src/components/ModalDialogUpdate.vue \
	src/components/NavbarBottom.vue \
	src/components/NavbarItemLink.vue \
	src/components/NavbarTop.vue \
	src/components/NotificationList.vue \
	src/components/PlayerButtonConsume.vue \
	src/components/PlayerButtonLyrics.vue \
	src/components/PlayerButtonNext.vue \
	src/components/PlayerButtonPlayPause.vue \
	src/components/PlayerButtonPrevious.vue \
	src/components/PlayerButtonRepeat.vue \
	src/components/PlayerButtonSeekBack.vue \
	src/components/PlayerButtonSeekForward.vue \
	src/components/PlayerButtonShuffle.vue \
	src/components/SettingsCheckbox.vue \
	src/components/SettingsIntfield.vue \
	src/components/SettingsTextfield.vue \
	src/components/TabsMusic.vue \
	src/components/TabsSearch.vue \
	src/filter/index.js \
	src/i18n/index.js \
	src/i18n/de.json \
	src/i18n/en.json \
	src/i18n/fr.json \
	src/i18n/zh-CN.json \
	src/i18n/zh-TW.json \
	src/lib/Audio.js \
	src/lib/SVGRenderer.js \
	src/lib/GroupedList.js \
	src/pages/PageMusicRecentlyPlayed.vue \
	src/pages/PageMusicSpotifyFeaturedPlaylists.vue \
	src/pages/PageAudiobooksArtists.vue \
	src/pages/PageAudiobooksGenres.vue \
	src/pages/PageAlbum.vue \
	src/pages/PageComposerAlbums.vue \
	src/pages/PageMusicSpotify.vue \
	src/pages/PagePlaylistTracks.vue \
	src/pages/PagePodcast.vue \
	src/pages/PageMusicSpotifyNewReleases.vue \
	src/pages/PageRadioStreams.vue \
	src/pages/PageAudiobooksAlbums.vue \
	src/pages/PageComposers.vue \
	src/pages/PageAudiobooksArtist.vue \
	src/pages/PageGenres.vue \
	src/pages/PageMusic.vue \
	src/pages/PageGenreAlbums.vue \
	src/pages/PageAbout.vue \
	src/pages/PageAlbumSpotify.vue \
	src/pages/PageAlbums.vue \
	src/pages/PageArtist.vue \
	src/pages/PageArtistSpotify.vue \
	src/pages/PageArtistTracks.vue \
	src/pages/PageArtists.vue \
	src/pages/PageComposerTracks.vue \
	src/pages/PageFiles.vue \
	src/pages/PageGenreTracks.vue \
	src/pages/PageMusicRecentlyAdded.vue \
	src/pages/PagePlaylistFolder.vue \
	src/pages/PagePlaylistTracksSpotify.vue \
	src/pages/PagePodcasts.vue \
	src/pages/PageQueue.vue \
	src/pages/PageSearchLibrary.vue \
	src/pages/PageSearchSpotify.vue \
	src/pages/PageSettingsOnlineServices.vue \
	src/pages/PageSettingsRemotesOutputs.vue \
	src/pages/PageAudiobooksAlbum.vue \
	src/pages/PageNowPlaying.vue \
	src/pages/PageSettingsArtwork.vue \
	src/pages/PageSettingsWebinterface.vue \
	src/router/index.js \
	src/stores/configuration.js \
	src/stores/library.js \
	src/stores/lyrics.js \
	src/stores/notifications.js \
	src/stores/outputs.js \
	src/stores/player.js \
	src/stores/queue.js \
	src/stores/remotes.js \
	src/stores/search.js \
	src/stores/services.js \
	src/stores/settings.js \
	src/stores/ui.js \
	src/templates/ContentWithHeading.vue \
	src/templates/ContentWithHero.vue \
	src/webapi/index.js \
	src/App.vue \
	src/icons.js \
	src/main.js \
	src/mystyles.scss \
	.prettierrc.json \
	eslint.config.js \
	index.html \
	jsconfig.json \
	package-lock.json \
	package.json \
	vite.config.js"
AC_SUBST([WEBSRC_SOURCE_FILES])

AC_CONFIG_COMMANDS([web-src],
	[
		if test "x$enable_webinterface" = "xyes"; then
			websrc_builddir="$ac_abs_builddir/web-src"
			websrc_srcdir="$ac_abs_srcdir/web-src"
			if test "$websrc_srcdir" != "$websrc_builddir"; then
				echo "web-src: out-of-tree build, copy web-src to build dir ($websrc_srcdir = $websrc_builddir)"
				for dir in $dirs; do
					mkdir -p "$websrc_builddir/$dir"
					echo "web-src: created dir $websrc_builddir/$dir"
				done
				for file in $files; do
					cp "$websrc_srcdir/$file" "$websrc_builddir/$file"
					echo "web-src: copied $file from '$websrc_srcdir' to '$websrc_builddir'"
				done
			else
				echo "web-src: in-tree build, nothing to prepare ($websrc_srcdir = $websrc_builddir)"
			fi
		fi
	], [
		dirs="$WEBSRC_SUBDIRS"
		files="$WEBSRC_SOURCE_FILES"
		enable_webinterface="$enable_webinterface"
	])

HTDOCS_FILES="\
	index.html \
	android-chrome-192x192.png \
	android-chrome-512x512.png \
	apple-touch-icon.png \
	browserconfig.xml \
	favicon.ico \
	favicon-16x16.png \
	favicon-32x32.png \
	mstile-150x150.png \
	safari-pinned-tab.svg \
	site.webmanifest \
	assets/index.css \
	assets/index.js"
AC_SUBST([WEBSRC_BUILD_FILES])

AC_CONFIG_COMMANDS([htdocs],
	[
		if test "x$enable_webinterface" = "xyes"; then
			websrc_builddir="$ac_abs_builddir/web-src/htdocs"
			websrc_srcdir="$ac_abs_srcdir/web-src/htdocs"
			if test "$websrc_srcdir" != "$websrc_builddir"; then
				echo "htdocs: out-of-tree build, copy prebuilt htdocs to build dir ($websrc_srcdir = $websrc_builddir)"
				if test -d "$websrc_srcdir/htdocs"; then
					mkdir -p "$websrc_builddir"
					mkdir -p "$websrc_builddir/assets"
					for file in $files; do
						cp "$websrc_srcdir/$file" "$websrc_builddir/$file"
						echo "htdocs: copied $file from '$websrc_srcdir' to '$websrc_builddir'"
					done
					echo "htdocs: copied prebuilt htdocs from '$websrc_srcdir' to '$websrc_builddir'"
				else
					echo "htdocs: no prebuilt htdocs found, nothing to prepare"
				fi
			else
				echo "htdocs: in-tree build, nothing to prepare ($websrc_srcdir = $websrc_builddir)"
			fi
		fi
	], [
		files="$HTDOCS_FILES"
		enable_webinterface="$enable_webinterface"
	])

AC_OUTPUT
