dnl Process this file with autoconf to produce a configure script.

AC_INIT([forked-daapd], [24.2])
RPM_RELEASE=1
AC_SUBST(RPM_RELEASE)

AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_DEFINE_UNQUOTED([BUILDDATE], ["`date +%F`"], [Build date])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_USE_SYSTEM_EXTENSIONS

dnl Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT([disable-static])

AC_CHECK_PROG(GPERF, [gperf], [gperf])
AS_IF([test -z "$GPERF"],
	[AS_IF([test -f "$srcdir/src/dmap_fields_hash.h"],
		[AM_MISSING_PROG([GPERF], [gperf])
		AC_MSG_NOTICE([

GNU gperf not found, but it's output appears to be present.
If you modify any gperf or ANTLR grammer files, you will need to install it.
])],
		[AC_MSG_ERROR([GNU gperf required and not found, please install it.])])])

AC_CHECK_PROG(ANTLR, [antlr3], [antlr3])
AS_IF([test -z "$ANTLR"],
	[AS_IF([test -f "$srcdir/src/SMARTPLLexer.h"],
		[AM_MISSING_PROG([ANTLR], [antlr3])
		AC_MSG_NOTICE([

antlr3 not found, but it's output appears to be present.
If you modify any ANTLR grammer files (.g), you will need to install it.
])],
		[AC_MSG_ERROR([antlr3 wrapper required and not found, please install it.])])])

AM_CPPFLAGS="-D_LARGEFILE_SOURCE -Wall"
AC_SUBST([AM_CPPFLAGS])

dnl Checks for header files.
AC_CHECK_HEADERS_ONCE([regex.h pthread_np.h])
AC_CHECK_HEADERS([sys/wait.h sys/param.h dirent.h getopt.h stdint.h], [],
	[AC_MSG_ERROR([Missing header required to build forked-daapd])])
AC_CHECK_FUNCS_ONCE([posix_fadvise euidaccess pipe2])
AC_CHECK_FUNCS([strptime strtok_r], [],
	[AC_MSG_ERROR([Missing function required to build forked-daapd])])

AC_SEARCH_LIBS([pthread_setname_np], [pthread],
	[AC_DEFINE(HAVE_PTHREAD_SETNAME_NP, 1, [Define to 1 if you have pthread_setname_np])],
	[AC_SEARCH_LIBS([pthread_set_name_np], [pthread], AC_DEFINE(HAVE_PTHREAD_SET_NAME_NP, 1, [Define to 1 if you have pthread_set_name_np]))])

dnl Large File Support (LFS)
AC_SYS_LARGEFILE
AC_TYPE_OFF_T

dnl Checks for libraries.

dnl uses macros from gettext-devel
gl_LIBUNISTRING
AS_IF([test x$HAVE_LIBUNISTRING != xyes],
	[AC_MSG_ERROR([GNU libunistring is required])])

PKG_CHECK_MODULES(ZLIB, [ zlib ])
PKG_CHECK_MODULES(CONFUSE, [ libconfuse ])
PKG_CHECK_MODULES(AVAHI, [ avahi-client >= 0.6.24 ])
PKG_CHECK_MODULES(SQLITE3, [ sqlite3 >= 3.5.0 ])

save_LIBS=$LIBS
LIBS="$SQLITE3_LIBS $LIBS"
dnl Check that SQLite3 has the unlock notify API built-in
AC_CHECK_LIB([sqlite3], [sqlite3_unlock_notify], [],
	[AC_MSG_ERROR([SQLite3 was built without unlock notify support])])
dnl Check that SQLite3 has been built with threadsafe operations
AC_MSG_CHECKING([if SQLite3 was built with threadsafe operations support])
AC_LANG_PUSH([C])
AC_RUN_IFELSE(
  [AC_LANG_PROGRAM([dnl
    #include <sqlite3.h>
    ], [dnl
    int ret = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
    if (ret != SQLITE_OK)
      return 1;
    return 0;])],
  [AC_MSG_RESULT([yes])], [AC_MSG_ERROR([SQLite3 was not built with threadsafe operations support])],
  [AC_MSG_RESULT([runtime will tell])])
AC_LANG_POP([C])
LIBS=$save_LIBS

PKG_CHECK_MODULES(LIBAV, [ libavformat libavcodec libswscale libavutil libavfilter ])

dnl Checks for misc libav and ffmpeg API differences
save_LIBS=$LIBS
LIBS="$LIBAV_LIBS $LIBS"
AC_CHECK_LIB([avcodec], [avcodec_find_best_pix_fmt_of_list],
	[AC_DEFINE(HAVE_FFMPEG, 1, [Define to 1 if you have ffmpeg/libav with avcodec_find_best_pix_fmt_of_list])])
AC_CHECK_LIB([avfilter], [av_buffersrc_add_frame_flags],
	[AC_DEFINE(HAVE_LIBAV_BUFFERSRC_ADD_FRAME_FLAGS, 1, [Define to 1 if you have ffmpeg/libav with av_buffersrc_add_frame_flags])])
AC_CHECK_LIB([avfilter], [av_buffersink_get_frame],
	[AC_DEFINE(HAVE_LIBAV_BUFFERSINK_GET_FRAME, 1, [Define to 1 if you have ffmpeg/libav with av_buffersink_get_frame])])
AC_CHECK_LIB([avfilter], [avfilter_graph_parse_ptr],
	[AC_DEFINE(HAVE_LIBAV_GRAPH_PARSE_PTR, 1, [Define to 1 if you have ffmpeg/libav with avfilter_graph_parse_ptr])])
AC_CHECK_LIB([avcodec], [av_packet_unref],
	[AC_DEFINE(HAVE_LIBAV_PACKET_UNREF, 1, [Define to 1 if you have ffmpeg/libav with av_packet_unref])])
AC_CHECK_LIB([avcodec], [av_packet_rescale_ts],
	[AC_DEFINE(HAVE_LIBAV_PACKET_RESCALE_TS, 1, [Define to 1 if you have ffmpeg/libav with av_packet_rescale_ts])])
AC_CHECK_LIB([avformat], [avformat_alloc_output_context2],
	[AC_DEFINE(HAVE_LIBAV_ALLOC_OUTPUT_CONTEXT2, 1, [Define to 1 if you have ffmpeg/libav with avformat_alloc_output_context2])])
AC_CHECK_LIB([avutil], [av_frame_alloc],
	[AC_DEFINE(HAVE_LIBAV_FRAME_ALLOC, 1, [Define to 1 if you have ffmpeg/libav with av_frame_alloc])])
AC_CHECK_LIB([avutil], [av_frame_get_best_effort_timestamp],
	[AC_DEFINE(HAVE_LIBAV_BEST_EFFORT_TIMESTAMP, 1, [Define to 1 if you have ffmpeg/libav with av_frame_get_best_effort_timestamp])])
AC_CHECK_LIB([avutil], [av_image_fill_arrays],
	[AC_DEFINE(HAVE_LIBAV_IMAGE_FILL_ARRAYS, 1, [Define to 1 if you have ffmpeg/libav with av_image_fill_arrays])])
AC_CHECK_LIB([avutil], [av_image_get_buffer_size],
	[AC_DEFINE(HAVE_LIBAV_IMAGE_GET_BUFFER_SIZE, 1, [Define to 1 if you have ffmpeg/libav with av_image_get_buffer_size])])
LIBS=$save_LIBS

save_CPPFLAGS=$CPPFLAGS
CPPFLAGS="$LIBAV_CFLAGS $CPPFLAGS"
AC_CHECK_HEADERS([libavutil/channel_layout.h libavutil/mathematics.h])
CPPFLAGS=$save_CPPFLAGS

PKG_CHECK_MODULES(MINIXML, [ mxml ])

PKG_CHECK_MODULES(LIBEVENT, [ libevent >= 2 ])
PKG_CHECK_EXISTS([ libevent >= 2.1.4 ], [],
	[AC_DEFINE(HAVE_LIBEVENT2_OLD, 1, [Define to 1 if you have libevent 2 (<2.1.4)])])

AC_CHECK_HEADER([antlr3.h], [], [AC_MSG_ERROR([antlr3.h not found])])
AC_CHECK_LIB([antlr3c], [antlr3BaseRecognizerNew], [ANTLR3C_LIBS="-lantlr3c"], AC_MSG_ERROR([ANTLR3 C runtime (libantlr3c) not found]))
AC_CHECK_LIB([antlr3c], [antlr3NewAsciiStringInPlaceStream],
	[AC_DEFINE([ANTLR3C_NEW_INPUT], 0, [define if antlr3 C runtime uses new input routines])],
	[AC_DEFINE([ANTLR3C_NEW_INPUT], 1, [define if antlr3 C runtime uses new input routines])])
AC_SUBST([ANTLR3C_LIBS])

AM_PATH_LIBGCRYPT([1:1.2.0], [], [AC_MSG_ERROR([libgcrypt not found])])
AM_PATH_GPG_ERROR([1.6], [], [AC_MSG_ERROR([libgpg-error not found])])

AC_CHECK_HEADER([sys/eventfd.h], [AC_CHECK_FUNC([eventfd_write],
	[AC_DEFINE(HAVE_EVENTFD, 1, [Define to 1 if you have eventfd])])])

AC_CHECK_HEADER([sys/signalfd.h], [AC_CHECK_FUNC([signalfd],
	[AC_DEFINE(HAVE_SIGNALFD, 1, [Define to 1 if you have signalfd])])])

AC_CHECK_HEADER([sys/timerfd.h], [AC_CHECK_FUNC([timerfd_create],
	[AC_DEFINE(HAVE_TIMERFD, 1, [Define to 1 if you have timerfd])])])

AC_SEARCH_LIBS([inotify_add_watch], [inotify],
	[AC_DEFINE([HAVE_INOTIFY], [1], [Whether kernel has inotify support])],
	[AC_MSG_ERROR([inotify not found])])

AC_CHECK_SIZEOF(void *)

dnl --- Begin configuring the options ---
dnl ALSA
AC_ARG_WITH([alsa], [AS_HELP_STRING([--with-alsa], [with ALSA support (default=check)])], [], [with_alsa=check])
AS_IF([test "x$with_alsa" != "xno"], 
	[PKG_CHECK_MODULES([ALSA], [alsa],
		[with_alsa=yes
		AC_DEFINE([ALSA], 1, [Define to 1 to build with ALSA support])],
		[AS_IF([test "x$with_alsa" != "xcheck"],
			[AC_MSG_FAILURE([--with-alsa was given, but test for ALSA failed])])
		with_alsa=no])
])
AM_CONDITIONAL([COND_ALSA], [test "x$with_alsa" = "xyes"])

dnl PULSEAUDIO
AC_ARG_WITH([pulseaudio], [AS_HELP_STRING([--with-pulseaudio], [with Pulseaudio support (default=check)])], [], [with_pulseaudio=check])
AS_IF([test "x$with_pulseaudio" != "xno"], 
	[PKG_CHECK_MODULES([LIBPULSE], [libpulse],
		[with_pulseaudio=yes
		AC_DEFINE([PULSEAUDIO], 1, [Define to 1 to build with Pulseaudio support])
		AC_SEARCH_LIBS([pa_threaded_mainloop_set_name], [pulse],
			[AC_DEFINE(HAVE_PULSE_MAINLOOP_SET_NAME, 1, [Define to 1 if you have Pulseaudio with pa_threaded_mainloop_set_name])])],
		[AS_IF([test "x$with_pulseaudio" != "xcheck"],
			[AC_MSG_FAILURE([--with-pulseaudio was given, but test for Pulseaudio failed])])
		with_pulseaudio=no])
])
AM_CONDITIONAL([COND_PULSEAUDIO], [test "x$with_pulseaudio" = "xyes"])

dnl Build with libcurl
AC_ARG_WITH([libcurl], [AS_HELP_STRING([--with-libcurl], [with libcurl (default=check)])], [], [with_libcurl=check])
AS_IF([test "x$with_libcurl" != "xno"], 
	[PKG_CHECK_MODULES([LIBCURL], [libcurl],
		[with_libcurl=yes
		AC_DEFINE([HAVE_LIBCURL], 1, [Define to 1 to build with libcurl])],
		[AS_IF([test "x$with_libcurl" != "xcheck"],
			[AC_MSG_FAILURE([--with-libcurl was given, but test for libcurl failed])])
		with_libcurl=no])
])

dnl Build with json-c
AC_ARG_WITH([json], [AS_HELP_STRING([--with-json], [with json-c (default=check)])], [], [with_json=check])
AS_IF([test "x$with_json" != "xno"], 
	[with_json_c=no
	PKG_CHECK_EXISTS([ json-c >= 0.11 ],
		[PKG_CHECK_MODULES([JSON_C], [json-c], [with_json_c=yes], [:])],
		[PKG_CHECK_MODULES([JSON_C], [json],
			[with_json_c=yes
			AC_DEFINE(HAVE_JSON_C_OLD, 1, [Define 1 to if you have json-c < 0.11])], [:])])
	AS_IF([test "$with_json_c" = "yes"],
		[with_json=yes
		AC_DEFINE([HAVE_JSON], 1, [Define to 1 to build with json-c])],
		[AS_IF([test "x$with_json" != "xcheck"],
			[AC_MSG_FAILURE([--with-json was given, but test for json-c failed])])
		with_json=no])
])

dnl iTunes playlists with libplist
AC_ARG_ENABLE(itunes, AS_HELP_STRING([--enable-itunes], [enable iTunes Music Library XML support (default=no)]))
AS_IF([test "x$enable_itunes" = "xyes"], [
	AC_DEFINE(ITUNES, 1, [Define to 1 to enable iTunes XML support])
	PKG_CHECK_MODULES(LIBPLIST, [ libplist >= 0.16 ])
])
AM_CONDITIONAL(COND_ITUNES, [test "x$enable_itunes" = "xyes"])

dnl Spotify with dynamic linking to libspotify
AC_ARG_ENABLE(spotify, AS_HELP_STRING([--enable-spotify], [enable Spotify support (default=no)]))
AS_IF([test "x$enable_spotify" = "xyes"], [
	AC_DEFINE(SPOTIFY, 1, [Define to 1 to enable Spotify support])
	AS_IF([test "x$with_json" = "xno"], AC_MSG_ERROR([Spotify support requires json-c]))
	AC_CHECK_HEADER([libspotify/api.h], [],
		[AC_MSG_ERROR([libspotify/api.h not found])])
	AC_DEFINE(HAVE_SPOTIFY_H, 1, [Define to 1 if you have the <libspotify/api.h> header file.])

	dnl Don't link to libspotify, but instead enable dynamic linking
	SPOTIFY_CFLAGS="-rdynamic"
	SPOTIFY_LIBS="-ldl"
	AC_SUBST(SPOTIFY_CFLAGS)
	AC_SUBST(SPOTIFY_LIBS)
])
AM_CONDITIONAL(COND_SPOTIFY, [test "x$enable_spotify" = "xyes"])

dnl LastFM support with libcurl
AC_ARG_ENABLE(lastfm, AS_HELP_STRING([--enable-lastfm], [enable LastFM support (default=no)]))
AS_IF([test "x$enable_lastfm" = "xyes"], [
	AC_DEFINE(LASTFM, 1, [Define to 1 to enable LastFM support])
	AS_IF([test "x$with_libcurl" = "xno"], [AC_MSG_ERROR([LastFM support requires libcurl])])
	AC_CHECK_LIB([mxml], [mxmlGetOpaque], [AC_DEFINE(HAVE_MXML_GETOPAQUE, 1, [Define to 1 if your mxml has mxmlGetOpaque.])], [], [$MINIXML_LIBS])
])
AM_CONDITIONAL(COND_LASTFM, [test "x$enable_lastfm" = "xyes"])

dnl ChromeCast support with libprotobuf-c
AC_ARG_ENABLE([chromecast], [AS_HELP_STRING([--enable-chromecast], [enable ChromeCast support (default=no)])])
AS_IF([test "x$enable_chromecast" = "xyes"],
	[AC_DEFINE(CHROMECAST, 1, [Define to 1 to enable Chromecast support])
	AS_IF([test "x$with_json" = xno], [AC_MSG_ERROR([Chromecast support requires json-c])])
	PKG_CHECK_MODULES(LIBPROTOBUF_C, [ libprotobuf-c >= 1.0.0 ], [], [
		AC_MSG_CHECKING([for v0 libprotobuf-c])
		AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <google/protobuf-c/protobuf-c.h>
#if PROTOBUF_C_MAJOR > 0
#error "protobuf version not < 1.0.0"
#endif
]])],
			[AC_MSG_RESULT([yes])
			AC_DEFINE(HAVE_PROTOBUF_OLD, 1, [Define to 1 if you have libprotobuf < 1.0.0])
			protobuf_old=yes],
			[AC_MSG_RESULT([no])
			AC_MSG_ERROR([Chromecast support requires protobuf-c])])
		AC_CHECK_LIB([protobuf-c], [protobuf_c_message_pack], [], [AC_MSG_ERROR([Chromecast support requires protobuf-c])])
	])
	PKG_CHECK_MODULES(GNUTLS, [gnutls])
])
AM_CONDITIONAL(COND_CHROMECAST, [test "x$enable_chromecast" = "xyes"])
AM_CONDITIONAL(COND_PROTOBUF_OLD, [test "x$protobuf_old" = "xyes"])

dnl MPD support
AC_ARG_ENABLE(mpd, AS_HELP_STRING([--disable-mpd], [disable MPD client protocol support (default=no)]))
AS_IF([test "x$enable_mpd" != "xno"], [
	AC_DEFINE(MPD, 1, [Define to 1 to enable MPD support])
])
AM_CONDITIONAL(COND_MPD, [test "x$enable_mpd" != "xno"])

#
# Defining users and groups
#
AC_ARG_WITH([daapd_user],
	[AS_HELP_STRING([--with-daapd-user=USER],
		[User for running forked-daapd (default=daapd)])],
	[test x"$withval" = xyes && withval=], [withval=])
DAAPD_USER=${withval:-daapd}
AC_SUBST(DAAPD_USER)

AC_ARG_WITH([daapd_group],
	[AS_HELP_STRING([--with-daapd-group=GROUP],
		[Group for daapd user (default=USER)])],
	[test x"$withval" = xyes && withval=], [withval=])
DAAPD_GROUP=${withval:-$DAAPD_USER}
AC_SUBST(DAAPD_GROUP)
dnl --- End options ---

AC_CONFIG_FILES([
	src/Makefile
	sqlext/Makefile
	Makefile
	forked-daapd.spec
])
AC_OUTPUT
