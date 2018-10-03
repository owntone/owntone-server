dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([forked-daapd], [26.4])

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects 1.11])
AM_SILENT_RULES([no])

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
If you modify any gperf or ANTLR grammar files (.g), you will need
to install it.]])],
		[AC_MSG_ERROR([[GNU gperf required, please install it.]])])
	])

AC_PATH_PROG([ANTLR], [[antlr3]])
AS_IF([[test -z "$ANTLR"]],
	[AS_IF([[test -f "$srcdir/src/SMARTPLLexer.h"]],
		[AM_MISSING_PROG([ANTLR], [[antlr3]])
		AC_MSG_NOTICE([[

antlr3 not found, but it's output appears to be present.
If you modify any ANTLR grammar files (.g), you will need to install it.]])],
		[AC_MSG_ERROR([[antlr3 wrapper required, please install it.]])])
	])

dnl Enable all warnings by default.
AM_CPPFLAGS="-Wall"
AC_SUBST([AM_CPPFLAGS])

dnl Checks for header files.
AC_CHECK_HEADERS_ONCE([regex.h pthread_np.h])
AC_CHECK_HEADERS([sys/wait.h sys/param.h dirent.h getopt.h stdint.h], [],
	[AC_MSG_ERROR([[Missing header required to build forked-daapd]])])
AC_CHECK_HEADERS([time.h], [],
	[AC_MSG_ERROR([[Missing header required to build forked-daapd]])])
AC_CHECK_FUNCS_ONCE([posix_fadvise pipe2])
AC_CHECK_FUNCS([strptime strtok_r], [],
	[AC_MSG_ERROR([[Missing function required to build forked-daapd]])])

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
	 AC_TRY_LINK([@%:@include <pthread.h>],
		[pthread_setname_np(pthread_self(), "name");],
		[AC_MSG_RESULT([yes])
		 AC_DEFINE([HAVE_PTHREAD_SETNAME_NP], 1,
			[Define to 1 if you have pthread_setname_np])],
		[AC_MSG_RESULT([[no]])])],
	[AC_SEARCH_LIBS([pthread_set_name_np], [pthread],
		[AC_CHECK_FUNCS([pthread_set_name_np])])])

dnl Large File Support (LFS)
AC_SYS_LARGEFILE
AC_TYPE_OFF_T

dnl Checks for libraries.

AC_SUBST([COMMON_LIBS])
AC_SUBST([COMMON_CPPFLAGS])
AC_SUBST([FORKED_LIBS])
AC_SUBST([FORKED_CPPFLAGS])
AC_SUBST([FORKED_OPTS_LIBS])
AC_SUBST([FORKED_OPTS_CPPFLAGS])

AM_ICONV
dnl All FORK_ macros defined in m4/fork_checks.m4
FORK_FUNC_REQUIRE([COMMON], [GNU libunistring], [LIBUNISTRING], [unistring],
	[u8_strconv_from_locale], [uniconv.h], [],
	[dnl Retry test with iconv library
	 FORK_VARS_PREPEND([COMMON], [LIBICONV], [INCICONV])
	 FORK_FUNC_REQUIRE([COMMON], [GNU libunistring], [LIBUNISTRING],
		[unistring], [u8_strconv_from_locale], [uniconv.h])])

FORK_MODULES_CHECK([FORKED], [ZLIB], [zlib], [deflate], [zlib.h])
FORK_MODULES_CHECK([FORKED], [CONFUSE], [libconfuse], [cfg_init], [confuse.h])
FORK_MODULES_CHECK([FORKED], [MINIXML], [mxml], [mxmlNewElement], [mxml.h],
	[AC_CHECK_FUNCS([mxmlGetOpaque] [mxmlGetText] [mxmlGetType] [mxmlGetFirstChild])])

dnl SQLite3 requires extra checks
FORK_MODULES_CHECK([COMMON], [SQLITE3], [sqlite3 >= 3.5.0],
	[sqlite3_initialize], [sqlite3.h],
	[dnl Check that SQLite3 has the unlock notify API built-in
	 AC_CHECK_FUNC([[sqlite3_unlock_notify]], [],
		[AC_MSG_ERROR([[SQLite3 was built without unlock notify support]])])
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

dnl libevent2 requires version checks
FORK_MODULES_CHECK([FORKED], [LIBEVENT], [libevent >= 2],
	[event_base_new], [event2/event.h],
	[dnl check for old version
	 PKG_CHECK_EXISTS([libevent >= 2.1.4], [],
		[AC_DEFINE([HAVE_LIBEVENT2_OLD], 1,
			[Define to 1 if you have libevent 2 (<2.1.4)])])
	])

dnl json-c version checks
FORK_MODULES_CHECK([FORKED], [JSON_C], [json-c],
	[json_tokener_parse], [json.h],
	[dnl check for old version
	 PKG_CHECK_EXISTS([json-c >= 0.11], [],
		[AC_DEFINE([HAVE_JSON_C_OLD], 1,
			[Define to 1 if you have json-c < 0.11])])
	])

dnl antlr version checks
FORK_FUNC_REQUIRE([FORKED], [ANTLR3 C runtime], [ANTLR3C], [antlr3c],
	[antlr3BaseRecognizerNew], [antlr3.h],
	[AC_CHECK_FUNC([[antlr3NewAsciiStringInPlaceStream]],
		[AC_DEFINE([ANTLR3C_NEW_INPUT], 0,
			[define if antlr3 C runtime uses new input routines])],
		[AC_DEFINE([ANTLR3C_NEW_INPUT], 1,
			[define if antlr3 C runtime uses new input routines])])
	])

AM_PATH_LIBGCRYPT([1:1.2.0])
FORK_FUNC_REQUIRE([FORKED], [GNU Crypt Library], [LIBGCRYPT], [gcrypt],
	[gcry_control], [gcrypt.h])
AM_PATH_GPG_ERROR([1.6])
FORK_FUNC_REQUIRE([FORKED], [GNUPG Error Values], [GPG_ERROR_MT], [gpg-error],
	[gpg_err_init], [gpg-error.h])

AC_CHECK_HEADER([sys/eventfd.h], [AC_CHECK_FUNCS([eventfd])])

AC_CHECK_HEADER([sys/timerfd.h], [AC_CHECK_FUNC([timerfd_create],
	[AC_DEFINE([HAVE_TIMERFD], 1, [Define to 1 if you have timerfd])])])

FORK_FUNC_REQUIRE([FORKED], [inotify], [INOTIFY], [inotify],
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
FORK_MODULES_CHECK([FORKED], [LIBAV],
	[libavformat$LIBAV libavcodec$LIBAV libswscale$LIBAV libavutil$LIBAV libavfilter$LIBAV],
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
	 FORK_CHECK_DECLS([av_version_info],
		[libavutil/avutil.h])
	 FORK_CHECK_DECLS([avformat_network_init],
		[libavformat/avformat.h])
	 dnl Check if we have modern or legacy AV api
	 FORK_CHECK_DECLS([avcodec_send_packet, avcodec_parameters_from_context],
		[libavcodec/avcodec.h],	[[libav_modern_api=yes]], [[libav_modern_api=no]])
	 dnl The below we need to know only if we are going to use the legacy AV api
	 FORK_CHECK_DECLS([av_buffersrc_add_frame_flags],
		[libavfilter/buffersrc.h])
	 FORK_CHECK_DECLS([av_buffersink_get_frame],
		[libavfilter/buffersink.h])
	 FORK_CHECK_DECLS([avfilter_graph_parse_ptr],
		[libavfilter/avfilter.h])
	 FORK_CHECK_DECLS([av_packet_unref],
		[libavcodec/avcodec.h])
	 FORK_CHECK_DECLS([av_packet_rescale_ts],
		[libavcodec/avcodec.h])
	 FORK_CHECK_DECLS([avformat_alloc_output_context2],
		[libavformat/avformat.h])
	 FORK_CHECK_DECLS([av_frame_alloc],
		[libavutil/frame.h])
	 FORK_CHECK_DECLS([av_frame_get_best_effort_timestamp],
		[libavutil/frame.h])
	 FORK_CHECK_DECLS([av_image_fill_arrays],
		[libavutil/imgutils.h])
	 FORK_CHECK_DECLS([av_image_get_buffer_size],
		[libavutil/imgutils.h])
	 AC_CHECK_HEADERS([libavutil/channel_layout.h libavutil/mathematics.h])
	])
dnl Option to choose old ffmpeg/libav API even if modern api was found
FORK_ARG_DISABLE([use of ffmpeg/libav API with avcodec_send_packet() and family],
	[avcodecsend], [USE_AVCODEC_SEND])
AM_CONDITIONAL([COND_FFMPEG_LEGACY],
	[[test "x$libav_modern_api" = "xno" || test "x$enable_avcodecsend" = "xno" ]])

AC_CHECK_SIZEOF([void *])

dnl --- Begin configuring the options ---
dnl ALSA
FORK_ARG_WITH_CHECK([FORKED_OPTS], [ALSA support], [alsa], [ALSA],
	[alsa], [snd_mixer_open], [asoundlib.h])
AM_CONDITIONAL([COND_ALSA], [[test "x$with_alsa" = "xyes"]])

dnl PULSEAUDIO
FORK_ARG_WITH_CHECK([FORKED_OPTS], [Pulseaudio support], [pulseaudio], [LIBPULSE],
	[libpulse], [pa_stream_get_state], [pulse/pulseaudio.h],
	[AC_CHECK_FUNCS([pa_threaded_mainloop_set_name])])
AM_CONDITIONAL([COND_PULSEAUDIO], [[test "x$with_pulseaudio" = "xyes"]])

dnl Build with libcurl
FORK_ARG_WITH_CHECK([FORKED_OPTS], [libcurl support], [libcurl], [LIBCURL],
	[libcurl], [curl_global_init], [curl/curl.h])

dnl Build with libwebsockets
FORK_ARG_WITH_CHECK([FORKED_OPTS], [libwebsockets support], [libwebsockets], [LIBWEBSOCKETS],
	[libwebsockets >= 2.0.2])
AM_CONDITIONAL([COND_LIBWEBSOCKETS], [[test "x$with_libwebsockets" = "xyes"]])

dnl Build with libsodium
FORK_ARG_WITH_CHECK([FORKED_OPTS], [libsodium support], [libsodium], [LIBSODIUM],
	[libsodium], [sodium_init], [sodium.h])

dnl Build with libplist
FORK_ARG_WITH_CHECK([FORKED_OPTS], [libplist support], [libplist], [LIBPLIST],
	[libplist >= 0.16], [plist_dict_get_item], [plist/plist.h])

dnl Build with libevent_pthreads
FORK_ARG_WITH_CHECK([FORKED_OPTS], [libevent_pthreads support],
	[libevent_pthreads], [LIBEVENT_PTHREADS], [libevent_pthreads],
	[evthread_use_pthreads], [event2/thread.h])

dnl Build with Avahi (or Bonjour if not)
FORK_ARG_WITH_CHECK([FORKED_OPTS], [Avahi mDNS], [avahi], [AVAHI],
	[avahi-client >= 0.6.24], [avahi_client_new], [avahi-client/client.h])
AS_IF([[test "x$with_avahi" = "xno"]],
	[FORK_FUNC_REQUIRE([FORKED_OPTS], [Bonjour DNS_SD], [DNSSD], [dns_sd],
		[DNSServiceGetAddrInfo], [dns_sd.h], [],
		[AC_MSG_ERROR([[Avahi client or Bonjour DNS_SD required, please install one.]])])])
AM_CONDITIONAL([COND_AVAHI], [[test "x$with_avahi" = "xyes"]])

dnl Spotify with dynamic linking to libspotify
FORK_ARG_ENABLE([Spotify support], [spotify], [SPOTIFY],
	[AS_IF([[test "x$with_libcurl" = "xno"]],
		[AC_MSG_ERROR([[Spotify support requires libcurl]])])
	 AS_IF([[test "x$with_libevent_pthreads" = "xno"]],
		[AC_MSG_ERROR([[Spotify support requires libevent_pthreads]])])
	 FORK_MODULES_CHECK([SPOTIFY], [LIBSPOTIFY], [libspotify],
		[], [libspotify/api.h])
	 AC_DEFINE([HAVE_SPOTIFY_H], 1,
		[Define to 1 if you have the <libspotify/api.h> header file.])
	 dnl Don't link with libspotify, use dynamic linking
	 AC_SEARCH_LIBS([dlopen], [dl], [],
		[AC_MSG_ERROR([[Spotify support requires dlopen]])])
	 FORK_VAR_PREPEND([FORKED_OPTS_CPPFLAGS], [$SPOTIFY_CPPFLAGS])
	 FORK_VAR_PREPEND([FORKED_OPTS_LIBS], [-rdynamic])
	])
AM_CONDITIONAL([COND_SPOTIFY], [[test "x$enable_spotify" = "xyes"]])

dnl LastFM support with libcurl
FORK_ARG_ENABLE([LastFM support], [lastfm], [LASTFM],
	[AS_IF([[test "x$with_libcurl" = "xno"]],
		[AC_MSG_ERROR([[LastFM support requires libcurl]])])
	])
AM_CONDITIONAL([COND_LASTFM], [[test "x$enable_lastfm" = "xyes"]])

dnl ChromeCast support with libprotobuf-c
FORK_ARG_ENABLE([Chromecast support], [chromecast], [CHROMECAST],
	[FORK_MODULES_CHECK([FORKED_OPTS], [LIBPROTOBUF_C],
		[libprotobuf-c >= 1.0.0], [protobuf_c_message_pack],
		[protobuf-c/protobuf-c.h], [],
		[FORK_FUNC_REQUIRE([FORKED_OPTS], [v0 libprotobuf-c],
			[LIBPROTOBUF_OLD], [protobuf-c],
			[protobuf_c_message_pack],
			[google/protobuf-c/protobuf-c.h],
			[AC_DEFINE([HAVE_PROTOBUF_OLD], 1,
				[Define to 1 if you have libprotobuf < 1.0.0])
			 [protobuf_old=yes]],
			[AC_MSG_ERROR([[Chromecast support requires protobuf-c]])])
		])
	 FORK_MODULES_CHECK([FORKED_OPTS], [GNUTLS], [gnutls], [gnutls_init],
		[gnutls/gnutls.h])
	])
AM_CONDITIONAL([COND_CHROMECAST], [[test "x$enable_chromecast" = "xyes"]])
AM_CONDITIONAL([COND_PROTOBUF_OLD], [[test "x$protobuf_old" = "xyes"]])

dnl DB profiling support
FORK_ARG_ENABLE([DB profiling support], [dbprofile], [DB_PROFILE])

dnl iTunes playlists with libplist
FORK_ARG_DISABLE([iTunes Music Library XML support], [itunes], [ITUNES],
	[AS_IF([[test "x$with_libplist" = "xno"]],
		[AC_MSG_ERROR([[iTunes Music Library XML support requires libplist]])])
	])
AM_CONDITIONAL([COND_ITUNES], [[test "x$enable_itunes" = "xyes"]])

dnl MPD support
FORK_ARG_DISABLE([MPD client protocol support], [mpd], [MPD])
AM_CONDITIONAL([COND_MPD], [[test "x$enable_mpd" = "xyes"]])

dnl Apple device verification
FORK_ARG_DISABLE([Apple TV device verification], [verification], [RAOP_VERIFICATION],
	[
	 AS_IF([[test "x$with_libsodium" = "xno"]],
		[AC_MSG_ERROR([[Apple TV device verification requires libsodium]])])
	 AS_IF([[test "x$with_libplist" = "xno"]],
		[AC_MSG_ERROR([[Apple TV device verification requires libplist]])])
	])
AM_CONDITIONAL([COND_RAOP_VERIFICATION], [[test "x$enable_verification" = "xyes"]])

dnl Include default webinterface
FORK_ARG_DISABLE([Include default web interface], [webinterface], [WEBINTERFACE],
	[AS_IF([[test "x$with_libwebsockets" = "xno"]],
		[AC_MSG_ERROR([[Web interface requires libwebsockets >= 2.0.2 (or use --disable-webinterface)]])])
	])
AM_CONDITIONAL([COND_WEBINTERFACE], [[test "x$enable_webinterface" = "xyes"]])

dnl Defining users and groups
AC_ARG_WITH([daapd_user],
	[AS_HELP_STRING([--with-daapd-user=USER],
		[User for running forked-daapd (default=daapd)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
DAAPD_USER=${withval:-daapd}
AC_SUBST([DAAPD_USER])

AC_ARG_WITH([daapd_group],
	[AS_HELP_STRING([--with-daapd-group=GROUP],
		[Group for daapd user (default=USER)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
DAAPD_GROUP=${withval:-$DAAPD_USER}
AC_SUBST([DAAPD_GROUP])
dnl --- End options ---

AC_CONFIG_FILES([
	src/Makefile
	sqlext/Makefile
	htdocs/Makefile
	Makefile
	forked-daapd.spec
])
AC_OUTPUT
