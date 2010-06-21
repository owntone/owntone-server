# libunistring.m4 serial 4
dnl Copyright (C) 2009-2010 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl gl_LIBUNISTRING
dnl Searches for an installed libunistring.
dnl If found, it sets and AC_SUBSTs HAVE_LIBUNISTRING=yes and the LIBUNISTRING
dnl and LTLIBUNISTRING variables and augments the CPPFLAGS variable, and
dnl #defines HAVE_LIBUNISTRING to 1. Otherwise, it sets and AC_SUBSTs
dnl HAVE_LIBUNISTRING=no and LIBUNISTRING and LTLIBUNISTRING to empty.

AC_DEFUN([gl_LIBUNISTRING],
[
  dnl First, try to link without -liconv. libunistring often depends on
  dnl libiconv, but we don't know (and often don't need to know) where
  dnl libiconv is installed.
  AC_LIB_HAVE_LINKFLAGS([unistring], [],
    [#include <uniconv.h>], [u8_strconv_from_locale((char*)0);],
    [no, consider installing GNU libunistring])
  if test "$ac_cv_libunistring" != yes; then
    dnl Second try, with -liconv.
    AC_REQUIRE([AM_ICONV])
    if test -n "$LIBICONV"; then
      dnl We have to erase the cached result of the first AC_LIB_HAVE_LINKFLAGS
      dnl invocation, otherwise the second one will not be run.
      unset ac_cv_libunistring
      glus_save_LIBS="$LIBS"
      LIBS="$LIBS $LIBICONV"
      AC_LIB_HAVE_LINKFLAGS([unistring], [],
        [#include <uniconv.h>], [u8_strconv_from_locale((char*)0);],
        [no, consider installing GNU libunistring])
      if test -n "$LIBUNISTRING"; then
        LIBUNISTRING="$LIBUNISTRING $LIBICONV"
        LTLIBUNISTRING="$LTLIBUNISTRING $LTLIBICONV"
      fi
      LIBS="$glus_save_LIBS"
    fi
  fi
])
