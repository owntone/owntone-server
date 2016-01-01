dnl Process this file with autoconf to produce a configure script.

AC_INIT([forked-daapd], [23.4])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign -Wno-portability subdir-objects])

AC_USE_SYSTEM_EXTENSIONS

dnl Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT([disable-static])

AC_CHECK_PROG(GPERF, [gperf], [gperf])
if test "x$GPERF" = x; then
	AC_MSG_ERROR([GNU gperf not found, please install it])
fi
AC_SUBST(GPERF)

AC_CHECK_PROG(ANTLR, [antlr3], [antlr3])
if test "x$ANTLR" = x; then
	if test -d $srcdir/src/pregen; then
		for f in $srcdir/src/pregen/*; do
			bf=`basename $f`
			ln -sf pregen/$bf $srcdir/src/$bf
		done
		AC_MSG_NOTICE([antlr3 wrapper not found, using pre-generated files])
	else
		AC_MSG_ERROR([antlr3 wrapper not found and pre-generated files not available])
	fi
fi
AC_SUBST(ANTLR)
AM_CONDITIONAL(COND_ANTLR, test "x$ANTLR" != x)

CFLAGS="$CFLAGS -Wall -D_LARGEFILE_SOURCE"

AC_CHECK_HEADERS([sys/wait.h])
AC_CHECK_HEADERS([sys/param.h])
AC_CHECK_HEADERS([sys/select.h])
AC_CHECK_HEADERS([dirent.h])
AC_CHECK_HEADERS([regex.h])
AC_CHECK_FUNCS(posix_fadvise)
AC_CHECK_FUNCS(strptime)
AC_CHECK_FUNCS(strtok_r)
AC_CHECK_FUNCS(timegm)
AC_CHECK_FUNCS(euidaccess)

dnl Large File Support (LFS)
AC_SYS_LARGEFILE
AC_TYPE_OFF_T

dnl Checks for libraries.
gl_LIBUNISTRING

if test x$HAVE_LIBUNISTRING != xyes; then
   AC_MSG_ERROR([GNU libunistring is required])
fi

PKG_CHECK_MODULES(ZLIB, [ zlib ])
PKG_CHECK_MODULES(CONFUSE, [ libconfuse ])
PKG_CHECK_MODULES(AVAHI, [ avahi-client >= 0.6.24 ])
PKG_CHECK_MODULES(SQLITE3, [ sqlite3 >= 3.5.0 ])

save_LIBS="$LIBS"
LIBS="$SQLITE3_LIBS"
dnl Check that SQLite3 has the unlock notify API built-in
AC_CHECK_LIB([sqlite3], [sqlite3_unlock_notify], [], AC_MSG_ERROR([SQLite3 was built without unlock notify support]))
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
LIBS="$save_LIBS"

PKG_CHECK_MODULES(LIBAV, [ libavformat libavcodec libswscale libavutil libavfilter ])

dnl Checks for misc libav and ffmpeg API differences
save_LIBS="$LIBS"
AC_CHECK_LIB([avcodec], [avcodec_find_best_pix_fmt_of_list],
	AC_DEFINE(HAVE_FFMPEG, 1, [Define to 1 if you have ffmpeg/libav with avcodec_find_best_pix_fmt_of_list]),,[-lavutil])

AC_CHECK_LIB([avfilter], [av_buffersrc_add_frame_flags],
	AC_DEFINE(HAVE_LIBAV_BUFFERSRC_ADD_FRAME_FLAGS, 1, [Define to 1 if you have ffmpeg/libav with av_buffersrc_add_frame_flags]))
AC_CHECK_LIB([avfilter], [av_buffersink_get_frame],
	AC_DEFINE(HAVE_LIBAV_BUFFERSINK_GET_FRAME, 1, [Define to 1 if you have ffmpeg/libav with av_buffersink_get_frame]))
AC_CHECK_LIB([avfilter], [avfilter_graph_parse_ptr],
	AC_DEFINE(HAVE_LIBAV_GRAPH_PARSE_PTR, 1, [Define to 1 if you have ffmpeg/libav with avfilter_graph_parse_ptr]))
AC_CHECK_LIB([avcodec], [av_packet_rescale_ts],
	AC_DEFINE(HAVE_LIBAV_PACKET_RESCALE_TS, 1, [Define to 1 if you have ffmpeg/libav with av_packet_rescale_ts]),,[-lavutil])
AC_CHECK_LIB([avformat], [avformat_alloc_output_context2],
	AC_DEFINE(HAVE_LIBAV_ALLOC_OUTPUT_CONTEXT2, 1, [Define to 1 if you have ffmpeg/libav with avformat_alloc_output_context2]))
AC_CHECK_LIB([avutil], [av_frame_alloc],
	AC_DEFINE(HAVE_LIBAV_FRAME_ALLOC, 1, [Define to 1 if you have ffmpeg/libav with av_frame_alloc]))
AC_CHECK_LIB([avutil], [av_frame_get_best_effort_timestamp],
	AC_DEFINE(HAVE_LIBAV_BEST_EFFORT_TIMESTAMP, 1, [Define to 1 if you have ffmpeg/libav with av_frame_get_best_effort_timestamp]))

AC_CHECK_HEADERS([libavutil/channel_layout.h])
AC_CHECK_HEADERS([libavutil/mathematics.h])
LIBS="$save_LIBS"

PKG_CHECK_MODULES(MINIXML, [ mxml ])

PKG_CHECK_MODULES(LIBEVENT, [ libevent >= 2 ])
PKG_CHECK_EXISTS([ libevent >= 2.1.4 ], ,
	AC_DEFINE(HAVE_LIBEVENT2_OLD, 1, [Define to 1 if you have libevent 2 (<2.1.4)])
)

AC_CHECK_HEADER(antlr3.h, , AC_MSG_ERROR([antlr3.h not found]))
AC_CHECK_LIB([antlr3c], [antlr3BaseRecognizerNew], [ANTLR3C_LIBS="-lantlr3c"], AC_MSG_ERROR([ANTLR3 C runtime (libantlr3c) not found]))
AC_CHECK_LIB([antlr3c], [antlr3NewAsciiStringInPlaceStream],
	AC_DEFINE(ANTLR3C_NEW_INPUT, 0, [define if antlr3 C runtime uses new input routines]),
	AC_DEFINE(ANTLR3C_NEW_INPUT, 1, [define if antlr3 C runtime uses new input routines]))
AC_SUBST(ANTLR3C_LIBS)

AM_PATH_LIBGCRYPT([1:1.2.0], , AC_MSG_ERROR([libgcrypt not found]))
AM_PATH_GPG_ERROR([1.6], , AC_MSG_ERROR([libgpg-error not found]))

case "$host" in
  *-*-linux-*)
	AC_CHECK_HEADERS([sys/eventfd.h])
	AC_CHECK_FUNC(eventfd_write, AC_DEFINE(HAVE_EVENTFD, 1, [Define to 1 if you have eventfd]))

	AC_CHECK_HEADER(sys/signalfd.h, , AC_MSG_ERROR([signalfd required; glibc 2.9+ recommended]))

	AC_CHECK_HEADER(sys/timerfd.h, , AC_MSG_ERROR([timerfd required; glibc 2.8+ recommended]))
	AC_CHECK_FUNC(timerfd_create, , AC_MSG_ERROR([timerfd required; glibc 2.8+ recommended]))
	;;
esac

AC_CHECK_SIZEOF(void *)

AC_CHECK_HEADERS(getopt.h,,)
AC_CHECK_HEADERS(stdint.h,,)

dnl --- Begin configuring the options ---
dnl iTunes playlists with libplist
AC_ARG_ENABLE(itunes, AS_HELP_STRING([--enable-itunes], [enable iTunes library support (default=no)]))
AS_IF([test "x$enable_itunes" = "xyes"], [
	CPPFLAGS="${CPPFLAGS} -DITUNES"
	PKG_CHECK_MODULES(LIBPLIST, [ libplist >= 0.16 ])
])
AM_CONDITIONAL(COND_ITUNES, [test "x$enable_itunes" = "xyes"])

dnl Spotify with dynamic linking to libspotify
AC_ARG_ENABLE(spotify, AS_HELP_STRING([--enable-spotify], [enable Spotify library support (default=no)]))
AS_IF([test "x$enable_spotify" = "xyes"], [
	CPPFLAGS="${CPPFLAGS} -DSPOTIFY"
	AC_CHECK_HEADER(libspotify/api.h, , AC_MSG_ERROR([libspotify/api.h not found]))
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
	CPPFLAGS="${CPPFLAGS} -DLASTFM"
	PKG_CHECK_MODULES(LIBCURL, [ libcurl ])
	AC_CHECK_LIB([mxml], [mxmlGetOpaque], AC_DEFINE(HAVE_MXML_GETOPAQUE, 1, [Define to 1 if your mxml has mxmlGetOpaque.]))
])
AM_CONDITIONAL(COND_LASTFM, [test "x$enable_lastfm" = "xyes"])

dnl MPD support
AC_ARG_ENABLE(mpd, AS_HELP_STRING([--disable-mpd], [disable MPD client protocol support (default=no)]))
AS_IF([test "x$enable_mpd" != "xno"], [
	CPPFLAGS="${CPPFLAGS} -DMPD"
])
AM_CONDITIONAL(COND_MPD, [test "x$enable_mpd" != "xno"])
dnl --- End options ---

dnl Selection of local audio sound system
case "$host" in
	*-*-linux-*)
		use_alsa=true
		use_oss4=false
		;;
	*-*-kfreebsd*-*|*-*-freebsd*)
		use_alsa=false
		use_oss4=true
		;;
esac

AC_ARG_WITH(alsa, AS_HELP_STRING([--with-alsa], [use ALSA (default Linux=yes, FreeBSD=no)]), [
	AS_IF([test "x$with_alsa" = "xyes"], [use_alsa=true], [use_alsa=false])
])
if test x$use_alsa = xtrue; then
	CPPFLAGS="${CPPFLAGS} -DALSA"
	PKG_CHECK_MODULES(ALSA, [ alsa ])
fi
AM_CONDITIONAL(COND_ALSA, test x$use_alsa = xtrue)

AC_ARG_WITH(oss4, AS_HELP_STRING([--with-oss4], [use OSS4 (default Linux=no, FreeBSD=yes)]), [
	AS_IF([test "x$with_oss4" = "xyes"], [use_oss4=true], [use_oss4=false])
])
if test x$use_oss4 = xtrue; then
	CPPFLAGS="${CPPFLAGS} -DOSS4"
	AC_CHECK_HEADER(sys/soundcard.h, , AC_MSG_ERROR([sys/soundcard.h not found]))
fi
AM_CONDITIONAL(COND_OSS4, test x$use_oss4 = xtrue)

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT

AC_OUTPUT(src/Makefile sqlext/Makefile Makefile)
