AC_INIT([libairptp], [0.4])
AC_CONFIG_AUX_DIR([.])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

dnl Defines _GNU_SOURCE globally when needed
AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

PKG_PROG_PKG_CONFIG()

AM_CPPFLAGS="-Wall"
AC_SUBST([AM_CPPFLAGS])

AC_CHECK_HEADERS([endian.h sys/endian.h libkern/OSByteOrder.h], [found_endian_headers=yes; break;])
AS_IF([test "x$found_endian_headers" != "xyes"], [AC_MSG_ERROR([[Missing functions to swap byte order]])])

AC_SEARCH_LIBS([pthread_exit], [pthread], [], [AC_MSG_ERROR([[pthreads library is required]])])

PKG_CHECK_MODULES([LIBEVENT], [libevent libevent_pthreads])

AC_ARG_ENABLE([daemon], [AS_HELP_STRING([--enable-daemon], [build airptpd daemon (default: no)])])
AM_CONDITIONAL([BUILD_DAEMON], [test "x$enable_daemon" = "xyes"])
AM_COND_IF([BUILD_DAEMON],
  [AC_CHECK_HEADER([sys/signalfd.h], [AC_CHECK_FUNCS([signalfd])])]
  [AC_CHECK_HEADER([sys/event.h], [AC_CHECK_FUNCS([kqueue])])])

AC_ARG_ENABLE([tests], [AS_HELP_STRING([--enable-tests], [configure tests for make check (default: no)])])
AM_CONDITIONAL([BUILD_TESTS], [test "x$enable_tests" = "xyes"])

AC_ARG_ENABLE([install_systemd], [AS_HELP_STRING([--enable-install-systemd], [install systemd service file (default: no)])])
AM_CONDITIONAL([INSTALL_SYSTEMD], [test "x$enable_install_systemd" = "xyes"])

AC_ARG_WITH([systemddir],
	[AS_HELP_STRING([--with-systemddir=DIR], [directory for systemd service files (default=LIBDIR/systemd/system)])],
	[[test x"$withval" = xyes && withval=]], [[withval=]])
SYSTEMD_DIR=${withval:-$libdir/systemd/system}
AC_SUBST([SYSTEMD_DIR])

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile daemon/Makefile daemon/airptpd.service])

AC_OUTPUT
