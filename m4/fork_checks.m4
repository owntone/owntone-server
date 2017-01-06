# fork_checks.m4 serial 1
dnl Copyright (c) Scott Shambarger <devel@shambarger.net>
dnl
dnl Copying and distribution of this file, with or without modification, are
dnl permitted in any medium without royalty provided the copyright notice
dnl and this notice are preserved. This file is offered as-is, without any
dnl warranty.

dnl _FORK_VARS_SET(TARGET, VAR)
dnl --------------------------
dnl Convenience function to set CPPFLAGS/LIBS and TARGET_{CPPFLAGS/LIBS}
dnl from VAR_{CFLAGS/LIBS}
m4_define([_FORK_VARS_SET],
[[
  LIBS="$][$2_LIBS $LIBS"
  CPPFLAGS="$][$2_CFLAGS $CPPFLAGS"
  ][$1_LIBS="$][$2_LIBS $][$1_LIBS"]
 AC_LIB_APPENDTOVAR([$1_CPPFLAGS], [$][$2_CFLAGS])
])

dnl FORK_LIB_REQUIRE(TARGET, DESCRIPTION, ENV, LIBRARY, [FUNCTION], [HEADER],
dnl   [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl -------------------------------------------------------------------------
dnl Check for software which lacks pkg-config support, failing if not found.
dnl When ENV_CFLAGS and ENV_LIBS are set (ENV is prefix), tries to link
dnl FUNCTION/include HEADER with them, and adds them to TARGET_CPPFLAGS and
dnl TARGET_LIBS.  With unset environment, expands like AC_SEARCH_LIBS on
dnl FUNCTION/LIBRARY and checks HEADER with default CPPFLAGS/LIBS, and suggests
dnl providing ENV variables on failure.  Expands optional ACTION-IF-FOUND
dnl with working CPPFLAGS/LIBS for additional checks.  DESCRIPTION used as
dnl friendly name in error messages to help user identify software.
dnl Restores original CPPFLAGS and LIBS when done. Expands
dnl ACTION-IF-NOT-FOUND if ENV_* not set, and FUNCTION in LIBRARY not
dnl found overriding default error. (NOTE: default must be empty to get error)
AC_DEFUN([FORK_LIB_REQUIRE],
[AS_VAR_PUSHDEF([FORK_MSG], [fork_msg_$3])
 AC_ARG_VAR([$3_CFLAGS], [C compiler flags for $2, overriding search])
 AC_ARG_VAR([$3_LIBS], [linker flags for $2, overriding search])
 [save_$3_LIBS=$LIBS; save_$3_CPPFLAGS=$CPPFLAGS]
 AS_IF([[test -n "$][$3_CFLAGS" && test -n "$][$3_LIBS"]],
	[AS_VAR_SET([FORK_MSG], [["
Library specific environment variables $3_LIBS and
$3_CFLAGS were used, verify they are correct..."]])
	 _FORK_VARS_SET([$1], [$3])
	 m4_ifval([$5], [AC_CHECK_FUNC([[$5]], [],
		[AC_MSG_FAILURE([[Unable to link function $5 with $2.$]FORK_MSG])])])],
	[AS_VAR_SET([FORK_MSG], [["
Install $2 in the default include path, or alternatively set
library specific environment variables $3_CFLAGS
and $3_LIBS."]])
	 m4_ifval([$5],
		[AC_MSG_CHECKING([[for library containing $5...]])
		 AC_TRY_LINK_FUNC([[$5]], [AC_MSG_RESULT([[none required]])],
			[[LIBS="-l$4 $LIBS"
			 $1_LIBS="-l$4 $][$1_LIBS"]
			 AC_TRY_LINK_FUNC([[$5]], [AC_MSG_RESULT([[-l$4]])],
				[AC_MSG_RESULT([[no]])
				 m4_default([$8], [AC_MSG_FAILURE([[Function $5 in lib$4 not found.$]FORK_MSG])])])
			])
		])
	])
 m4_ifval([$6], [AC_CHECK_HEADER([[$6]], [],
	[AC_MSG_FAILURE([[Unable to find header $6 for $2.$]FORK_MSG])])])
 $7
 [LIBS=$save_$3_LIBS; CPPFLAGS=$save_$3_CPPFLAGS]
 AS_VAR_POPDEF([FORK_MSG])
])

dnl FORK_MODULES_CHECK(TARGET, ENV, MODULES, [FUNCTION], [HEADER],
dnl   [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl --------------------------------------------------------------
dnl Expands PKG_CHECK_MODULES, but when found also attempt to link
dnl FUNCTION and include HEADER.  Appends working package values to
dnl TARGET_CPPFLAGS and TARGET_LIBS. Expands optional ACTION-IF-FOUND with
dnl working CPPFLAGS/LIBS for additional checks.  Expands
dnl ACTION-IF-NOT-FOUND only if package not found (not link/include failures)
dnl overriding default error. (NOTE: default must be empty to get error!)
dnl Restores original CPPFLAGS and LIBS when done.
AC_DEFUN([FORK_MODULES_CHECK],
[PKG_CHECK_MODULES([$2], [[$3]],
	[[save_$2_LIBS=$LIBS; save_$2_CPPFLAGS=$CPPFLAGS]
	 _FORK_VARS_SET([$1], [$2])
	 m4_ifval([$4], [AC_CHECK_FUNC([[$4]], [],
		[AC_MSG_ERROR([[Unable to link function $4]])])])
	 m4_ifval([$5], [AC_CHECK_HEADER([[$5]], [],
		[AC_MSG_ERROR([[Unable to find header $5]])])])
	 $6
	 [LIBS=$save_$2_LIBS; CPPFLAGS=$save_$2_CPPFLAGS]], [$7])
])

dnl FORK_ARG_WITH_CHECK(TARGET, DESCRIPTION, OPTION, ENV, MODULES, [FUNCTION],
dnl   [HEADER], [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl --------------------------------------------------------------------------
dnl Create an --with-OPTION with a default of "check" (include MODULES
dnl if they are available).  Expands FORK_MODULES_CHECK with remaining
dnl arguments.  Defines HAVE_ENV to 1 if package found.  DESCRIPTION is used
dnl in option help.  Shell variable with_OPTION set to yes before
dnl ACTION-IF-FOUND.  Default ACTION-IF-NOT-FOUND will fail
dnl if --with-OPTION given, and MODULES not found, or sets shell var
dnl with_OPTION to no (setting NOT-FOUND overrides this behavior to allow
dnl alternate checks).
AC_DEFUN([FORK_ARG_WITH_CHECK],
[AC_ARG_WITH([[$3]], [AS_HELP_STRING([--with-$3],
	[with $2 (default=check)])], [],
	[[with_$3=check]])
 AS_IF([[test "x$with_$3" != "xno"]],
	[FORK_MODULES_CHECK([$1], [$4], [$5], [$6], [$7],
		[[with_$3=yes]
		 AC_DEFINE([HAVE_$4], 1,
			[Define to 1 to build with $2])
		 $8],
		[m4_default([$9], [AS_IF([[test "x$with_$3" != "xcheck"]],
			[AC_MSG_FAILURE([[--with-$3 was given, but test for $5 failed]])])
			 [with_$3=no]])])dnl keep default empty!
	])
])

dnl FORK_ARG_ENABLE(DESCRIPTION, OPTION, DEFINE, [ACTION-IF-ENABLE])
dnl ----------------------------------------------------------------
dnl Create an --enable-OPTION, setting shell variable enable_OPTION
dnl to no by default.  If feature is enabled, defines DEFINE to 1
dnl and expand ACTION-IF_ENABLE.  DESCRIPTION is used in option help.
AC_DEFUN([FORK_ARG_ENABLE],
[AC_ARG_ENABLE([[$2]], [AS_HELP_STRING([--enable-$2],
	[enable $1 (default=no)])])
 AS_IF([[test "x$enable_$2" = "xyes"]],
	[AC_DEFINE([$3], 1, [Define to 1 to enable $1])
	 $4],
	[[enable_$2=no]])
])

dnl FORK_ARG_DISABLE(DESCRIPTION, OPTION, DEFINE, [ACTION-IF-ENABLE])
dnl ----------------------------------------------------------------
dnl Create an --disable-OPTION, setting shell variable enable_OPTION
dnl to yes by default.  If feature is enabled, defines DEFINE to 1
dnl and expand ACTION-IF_ENABLE.  DESCRIPTION is used in option help.
AC_DEFUN([FORK_ARG_DISABLE],
[AC_ARG_ENABLE([[$2]], [AS_HELP_STRING([--disable-$2],
	[disable $1 (default=no)])])
 AS_IF([[test "x$enable_$2" = "x" || test "x$enable_$2" = "xyes"]],
	[AC_DEFINE([$3], 1, [Define to 1 to enable $1])
	 [enable_$2=yes]
	 $4],
	[[enable_$2=no]])
])
