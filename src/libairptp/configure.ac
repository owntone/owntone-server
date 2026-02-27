AC_INIT([libairptp], [0.1])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

dnl Defines _GNU_SOURCE globally when needed
AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

AM_CPPFLAGS="-Wall"
AC_SUBST([AM_CPPFLAGS])

AC_CHECK_HEADERS([endian.h sys/endian.h libkern/OSByteOrder.h], [found_endian_headers=yes; break;])
AS_IF([test "x$found_endian_headers" != "xyes"], [AC_MSG_ERROR([[Missing functions to swap byte order]])])

AC_SEARCH_LIBS([pthread_exit], [pthread], [], [AC_MSG_ERROR([[pthreads library is required]])])

PKG_CHECK_MODULES([LIBEVENT], [libevent libevent_pthreads])

AC_CONFIG_FILES([Makefile src/Makefile])

AC_ARG_ENABLE([daemon],
  [AS_HELP_STRING([--enable-daemon], [build airptpd daemon (default: no)])],
  [enable_daemon=$enableval],
  [enable_daemon=no])
AM_CONDITIONAL([BUILD_DAEMON], [test "x$enable_daemon" = "xyes"])
AM_COND_IF([BUILD_DAEMON],
  [AC_CHECK_HEADER([sys/signalfd.h], [AC_CHECK_FUNCS([signalfd])])]
  [AC_CHECK_HEADER([sys/event.h], [AC_CHECK_FUNCS([kqueue])])]
  [AC_CONFIG_FILES([daemon/Makefile])])

AC_ARG_ENABLE([tests],
  [AS_HELP_STRING([--enable-tests], [configure tests for make check (default: no)])],
  [enable_tests=$enableval],
  [enable_tests=no])
AM_CONDITIONAL([BUILD_TESTS], [test "x$enable_tests" = "xyes"])
AM_COND_IF([BUILD_TESTS], [AC_CONFIG_FILES([tests/Makefile])])

AC_OUTPUT
