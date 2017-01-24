# fork_checks.m4 serial 2
dnl Copyright (c) Scott Shambarger <devel@shambarger.net>
dnl
dnl Copying and distribution of this file, with or without modification, are
dnl permitted in any medium without royalty provided the copyright notice
dnl and this notice are preserved. This file is offered as-is, without any
dnl warranty.

dnl _FORK_FUNC_MERGE
dnl ----------------
dnl Internal only.  Defines function used by FORK_VAR_PREPEND
AC_DEFUN([_FORK_FUNC_MERGE], [[
# fork_fn_merge(before, after)
# create wordlist removing duplicates
fork_fn_merge() {
  fork_fn_var_result=$][1
  for element in $][2; do
    fork_fn_var_haveit=
    for x in $fork_fn_var_result; do
      if test "X$x" = "X$element"; then
        fork_fn_var_haveit=1
	break
      fi
    done
    if test -z "$fork_fn_var_haveit"; then
      fork_fn_var_result="${fork_fn_var_result}${fork_fn_var_result:+ }$element"
    fi
  done
  echo "$fork_fn_var_result"
  unset fork_fn_var_haveit
  unset fork_fn_var_result
}]])

dnl FORK_VAR_PREPEND(VARNAME, BEFORE)
dnl ---------------------------------
dnl Prepends words in BEFORE to the contents of VARNAME, skipping any
dnl duplicate words.
AC_DEFUN([FORK_VAR_PREPEND],
[AC_REQUIRE([_FORK_FUNC_MERGE])dnl
[ $1=$(fork_fn_merge "$2" "$$1")]])

dnl FORK_VARS_PREPEND(TARGET, LIBS_ENV, CFLAGS_ENV)
dnl -----------------------------------------------
dnl Prepend LIBS_ENV to LIBS and TARGET_LIBS
dnl Append CFLAGS_ENV to CPPFLAGS and TARGET_CPPFLAGS.
AC_DEFUN([FORK_VARS_PREPEND],
[[
  LIBS="$$2 $LIBS"
  $1_LIBS="$$2 $$1_LIBS"]
 FORK_VAR_PREPEND([CPPFLAGS], [$$3])
 FORK_VAR_PREPEND([$1_CPPFLAGS], [$$3])
])

dnl _FORK_VARS_ADD_PREFIX(TARGET)
dnl -----------------------------
dnl Internal use only. Add libdir prefix to {TARGET_}LIBS and
dnl includedir prefix to {TARGET_}CPPFLAGS as fallback search paths
dnl expanding all variables.
AC_DEFUN([_FORK_VARS_ADD_PREFIX],
[AC_REQUIRE([AC_LIB_PREPARE_PREFIX])
 AC_LIB_WITH_FINAL_PREFIX([[
  eval LIBS=\"-L$libdir $LIBS\"
  eval $1_LIBS=\"-L$libdir $$1_LIBS\"
  eval fork_tmp_cppflags=\"-I$includedir\"]
 FORK_VAR_PREPEND([CPPFLAGS], [$fork_tmp_cppflags])
 FORK_VAR_PREPEND([$1_CPPFLAGS], [$fork_tmp_cppflags])
 ])
])

dnl FORK_CHECK_DECLS(SYMBOLS, INCLUDE, [ACTION-IF-FOUND],
dnl   [ACTION-IF-NOT-FOUND])
dnl -----------------------------------------------------
dnl Expands AC_CHECK_DECLS with SYMBOLS and INCLUDE appended to
dnl AC_INCLUDES_DEFAULT.
dnl NOTE: Remember that AC_CHECK_DECLS defines HAVE_* to 1 or 0
dnl (not 1 or undefined!)
AC_DEFUN([FORK_CHECK_DECLS],
[AC_CHECK_DECLS([$1], [$3], [$4], [AC_INCLUDES_DEFAULT
[@%:@include <$2>]])
])

dnl FORK_FUNC_REQUIRE(TARGET, DESCRIPTION, ENV, LIBRARY, FUNCTION, [HEADER],
dnl   [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl ------------------------------------------------------------------------
dnl Check for software which lacks pkg-config support, setting TARGET_CPPFLAGS
dnl and TARGET_LIBS with working values if FUNCTION found, or failing if
dnl it's not.  If ENV_CFLAGS and ENV_LIBS overrides are set (ENV is prefix),
dnl tries to link FUNCTION/include HEADER with them.  Without overrides,
dnl expands like AC_SEARCH_LIBS on FUNCTION (trying without and with LIBRARY),
dnl adding $prefix paths if necessary.  If FUNCTION found, verifies optional
dnl HEADER can be included (or fails with error), and expands optional
dnl ACTION-IF-FOUND with working CPPFLAGS/LIBS for additional checks.
dnl DESCRIPTION used as friendly name in error messages to help user
dnl identify software to install.  If FUNCTION not found, either displays
dnl error suggested use of ENV_* overrides, or if ENV_* were not set
dnl expands optional ACTION-IF-NOT-FOUND in place of error.
dnl Restores original CPPFLAGS and LIBS when done.
AC_DEFUN([FORK_FUNC_REQUIRE],
[AS_VAR_PUSHDEF([FORK_MSG], [fork_msg_$3])
 AC_ARG_VAR([$3_CFLAGS], [C compiler flags for $2, overriding search])
 AC_ARG_VAR([$3_LIBS], [linker flags for $2, overriding search])
 [fork_save_$3_LIBS=$LIBS; fork_save_$3_CPPFLAGS=$CPPFLAGS
  fork_found_$3=yes]
 AS_IF([[test -n "$$3_CFLAGS" && test -n "$$3_LIBS"]],
	[dnl ENV variables provided, just verify they work
	 AS_VAR_SET([FORK_MSG], [["
Library specific environment variables $3_LIBS and
$3_CFLAGS were used, verify they are correct..."]])
	 FORK_VARS_PREPEND([$1], [$3_LIBS], [$3_CFLAGS])
	 AC_CHECK_FUNC([[$5]], [],
		[AC_MSG_FAILURE([[Unable to link function $5 with $2.$]FORK_MSG])])],
	[dnl Search w/o LIBRARY, w/ LIBRARY, and finally adding $prefix path
	 AS_VAR_SET([FORK_MSG], [["
Install $2 in the default include path, or alternatively set
library specific environment variables $3_CFLAGS
and $3_LIBS."]])
	 AC_MSG_CHECKING([[for library containing $5...]])
	 AC_TRY_LINK_FUNC([[$5]], [AC_MSG_RESULT([[none required]])],
		[[LIBS="-l$4 $LIBS"
		 $1_LIBS="-l$4 $$1_LIBS"]
		 AC_TRY_LINK_FUNC([[$5]], [AC_MSG_RESULT([[-l$4]])],
			 [_FORK_VARS_ADD_PREFIX([$1])
			  AC_TRY_LINK_FUNC([[$5]], [AC_MSG_RESULT([[-l$4]])],
				[AC_MSG_RESULT([[no]])
				 fork_found_$3=no])])
		])
	])
 AS_IF([[test "$fork_found_$3" != "no"]],
	[dnl check HEADER, then expand FOUND
	 m4_ifval([$6], [AC_CHECK_HEADER([[$6]], [],
		[AC_MSG_FAILURE([[Unable to find header $6 for $2.$]FORK_MSG])])])
	 $7])
 [LIBS=$fork_save_$3_LIBS; CPPFLAGS=$fork_save_$3_CPPFLAGS]
 dnl Expand NOT-FOUND after restoring saved flags to allow recursive expansion
 AS_IF([[test "$fork_found_$3" = "no"]],
	[m4_default_nblank([$8],
		[AC_MSG_FAILURE([[Function $5 in lib$4 not found.$]FORK_MSG])])])
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
dnl overriding default error.  Restores original CPPFLAGS and LIBS when done.
AC_DEFUN([FORK_MODULES_CHECK],
[PKG_CHECK_MODULES([$2], [[$3]],
	[[fork_save_$2_LIBS=$LIBS; fork_save_$2_CPPFLAGS=$CPPFLAGS]
	 FORK_VARS_PREPEND([$1], [$2_LIBS], [$2_CFLAGS])
	 m4_ifval([$4], [AC_CHECK_FUNC([[$4]], [],
		[AC_MSG_ERROR([[Unable to link function $4]])])])
	 m4_ifval([$5], [AC_CHECK_HEADER([[$5]], [],
		[AC_MSG_ERROR([[Unable to find header $5]])])])
	 $6
	 [LIBS=$fork_save_$2_LIBS; CPPFLAGS=$fork_save_$2_CPPFLAGS]],
	 m4_default_nblank_quoted([$7]))
])

dnl FORK_ARG_WITH_CHECK(TARGET, DESCRIPTION, OPTION, ENV, MODULES, [FUNCTION],
dnl   [HEADER], [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
dnl --------------------------------------------------------------------------
dnl Create an --with-OPTION with a default of "check" (include MODULES
dnl if they are available).  Expands FORK_MODULES_CHECK with remaining
dnl arguments.  Defines HAVE_ENV to 1 if package found.  DESCRIPTION is used
dnl in option help.  Shell variable with_OPTION set to yes before
dnl ACTION-IF-FOUND.  Default ACTION-IF-NOT-FOUND will fail
dnl if --with-OPTION given and MODULES not found, or sets shell var
dnl with_OPTION to no if option was check.  A non-empty ACTION-IF-NOT-FOUND
dnl overrides this behavior to allow alternate checks.
AC_DEFUN([FORK_ARG_WITH_CHECK],
[AC_ARG_WITH([[$3]], [AS_HELP_STRING([--with-$3],
	[with $2 (default=check)])], [],
	[[with_$3=check]])
 AS_IF([[test "x$with_$3" != "xno"]],
	[FORK_MODULES_CHECK([$1], [$4], [$5], [$6], [$7],
		[[with_$3=yes]
		 AC_DEFINE([HAVE_$4], 1, [Define to 1 to build with $2])
		 $8],
		[m4_default_nblank([$9],
			[AS_IF([[test "x$with_$3" != "xcheck"]],
				[AC_MSG_FAILURE([[--with-$3 was given, but test for $5 failed]])])
			 [with_$3=no]])
		])
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
