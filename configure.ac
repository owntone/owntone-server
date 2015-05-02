dnl Process this file with autoconf to produce a configure script.

AC_INIT([forked-daapd], [23.0])
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

AC_ARG_ENABLE(flac, AS_HELP_STRING([--enable-flac], [extract FLAC metadata with libflac (default=no)]),
                      use_flac=true;
                      CPPFLAGS="${CPPFLAGS} -DFLAC")

AC_ARG_ENABLE(musepack, AS_HELP_STRING([--enable-musepack], [extract Musepack metadata with taglib (default=no)]),
                      use_musepack=true;
                      CPPFLAGS="${CPPFLAGS} -DMUSEPACK")

AC_ARG_ENABLE(itunes, AS_HELP_STRING([--enable-itunes], [enable iTunes library support (default=no)]),
                      use_itunes=true;
                      CPPFLAGS="${CPPFLAGS} -DITUNES")

AC_ARG_ENABLE(spotify, AS_HELP_STRING([--enable-spotify], [enable Spotify library support (default=no)]),
                      use_spotify=true;
                      CPPFLAGS="${CPPFLAGS} -DSPOTIFY")

AC_ARG_ENABLE(lastfm, AS_HELP_STRING([--enable-lastfm], [enable LastFM support (default=no)]),
                      use_lastfm=true;
                      CPPFLAGS="${CPPFLAGS} -DLASTFM")

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

AC_ARG_WITH(oss4, AS_HELP_STRING([--with-oss4], [use OSS4 (default Linux=no, FreeBSD=yes)]),
                  AS_IF([test "x$with_oss4" = "xyes"],[use_oss4=true],[use_oss4=false]);
                  )

AC_ARG_WITH(alsa, AS_HELP_STRING([--with-alsa], [use ALSA (default Linux=yes, FreeBSD=no)]),
                  AS_IF([test "x$with_alsa" = "xyes"],[use_alsa=true],[use_alsa=false]);
                  )

AC_ARG_ENABLE(mpd, AS_HELP_STRING([--enable-mpd], [enable MPD client protocol support (default=no)]),
                      use_mpd=true;
                      CPPFLAGS="${CPPFLAGS} -DMPD")

AM_CONDITIONAL(COND_FLAC, test x$use_flac = xtrue)
AM_CONDITIONAL(COND_MUSEPACK, test x$use_musepack = xtrue)
AM_CONDITIONAL(COND_ITUNES, test x$use_itunes = xtrue)
AM_CONDITIONAL(COND_SPOTIFY, test x$use_spotify = xtrue)
AM_CONDITIONAL(COND_LASTFM, test x$use_lastfm = xtrue)
AM_CONDITIONAL(COND_OSS4, test x$use_oss4 = xtrue)
AM_CONDITIONAL(COND_ALSA, test x$use_alsa = xtrue)
AM_CONDITIONAL(COND_MPD, test x$use_mpd = xtrue)

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

PKG_CHECK_EXISTS([ libavcodec >= 54.35 ],
	[PKG_CHECK_EXISTS([ libswresample ],
		libxxresample=libswresample;
	,
		libxxresample=libavresample;
	)]
)

if test x$libxxresample = xlibswresample; then
	AC_DEFINE(HAVE_LIBSWRESAMPLE, 1, [Define to 1 if you have ffmpeg's libswresample])
elif test x$libxxresample = xlibavresample; then
	AC_DEFINE(HAVE_LIBAVRESAMPLE, 1, [Define to 1 if you have libav's libavresample])
fi

PKG_CHECK_MODULES(LIBAV, [ libavformat libavcodec libswscale libavutil $libxxresample ])

save_LIBS="$LIBS"
AC_CHECK_LIB([avcodec], [avcodec_find_best_pix_fmt_of_list],
	AC_DEFINE(HAVE_FFMPEG, 1, [Define to 1 if you have ffmpeg (and not libav).]))
LIBS="$save_LIBS"

PKG_CHECK_MODULES(MINIXML, [ mxml ])

PKG_CHECK_MODULES(LIBEVENT, [ libevent >= 2 ],
        AC_DEFINE(HAVE_LIBEVENT2, 1, [Define to 1 if you have libevent 2])
)

AM_CONDITIONAL(COND_LIBEVENT20, false)
AM_CONDITIONAL(COND_LIBEVENT21, false)

PKG_CHECK_EXISTS([ libevent >= 2.1 ],
	AM_CONDITIONAL(COND_LIBEVENT21, true),
	AM_CONDITIONAL(COND_LIBEVENT20, true)
)
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

if test x$use_flac = xtrue; then
	PKG_CHECK_MODULES(FLAC, [ flac ])
fi

if test x$use_musepack = xtrue; then
	PKG_CHECK_MODULES(TAGLIB, [ taglib_c ])
fi

if test x$use_itunes = xtrue; then
	PKG_CHECK_MODULES(LIBPLIST, [ libplist >= 0.16 ])
fi

if test x$use_spotify = xtrue; then
	AC_CHECK_HEADER(libspotify/api.h, , AC_MSG_ERROR([libspotify/api.h not found]))
	AC_DEFINE(HAVE_SPOTIFY_H, 1, [Define to 1 if you have the <libspotify/api.h> header file.])
	dnl Don't link to libspotify, but instead enable dynamic linking
	SPOTIFY_CFLAGS="-rdynamic"
	SPOTIFY_LIBS="-ldl"
	AC_SUBST(SPOTIFY_CFLAGS)
	AC_SUBST(SPOTIFY_LIBS)
fi

if test x$use_lastfm = xtrue; then
	PKG_CHECK_MODULES(LIBCURL, [ libcurl ])
	AC_CHECK_LIB([mxml], [mxmlGetOpaque],
		AC_DEFINE(HAVE_MXML_GETOPAQUE, 1, [Define to 1 if your mxml has mxmlGetOpaque.]))
fi

if test x$use_alsa = xtrue; then
	PKG_CHECK_MODULES(ALSA, [ alsa ])
	AC_DEFINE(LAUDIO_USE_ALSA, 1, [define if local audio output uses ALSA])
	CPPFLAGS="${CPPFLAGS} -DALSA"
fi

if test x$use_oss4 = xtrue; then
	AC_CHECK_HEADER(sys/soundcard.h, , AC_MSG_ERROR([sys/soundcard.h not found]))
	CPPFLAGS="${CPPFLAGS} -DOSS4"
fi

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

dnl Checks for header files.
AC_HEADER_STDC
AC_HEADER_SYS_WAIT

AC_OUTPUT(src/Makefile sqlext/Makefile Makefile)
