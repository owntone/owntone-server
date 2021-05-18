AC_INIT([librespot-c], [0.1])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

AC_SEARCH_LIBS([pthread_exit], [pthread], [],	[AC_MSG_ERROR([[pthreads library is required]])])

PKG_CHECK_MODULES([LIBEVENT], [libevent])
PKG_CHECK_MODULES([JSON_C], [json-c])
PKG_CHECK_MODULES([LIBGCRYPT], [libgcrypt])
PKG_CHECK_MODULES([LIBCURL], [libcurl])
PKG_CHECK_MODULES([LIBPROTOBUF_C], [libprotobuf-c])

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
