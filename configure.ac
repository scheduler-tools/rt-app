AC_INIT([rt-app], [0.1], [g.bagnoli@asidev.com])
AC_COPYRIGHT([Copyright (C) 2009 Giacomo Bagnoli <g.bagnoli@asidev.com])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-bzip2])
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/rt-app.c])
AC_HEADER_STDC
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([m], [round])
AC_CHECK_LIB([rt], [clock_gettime])
AC_ARG_WITH([lockmem], AS_HELP_STRING([--enable-lockmem],[lock all memory to avoid it being swapped out]))
AS_IF([test "x$enable_lockmem" = "xyes"], [
	AC_DEFINE([LOCKMEM], [], [lock all memory to avoid it being swapout])
])

AC_ARG_WITH([aquosa],
	[AS_HELP_STRING([--enable-aquosa], 
         [Add support for the AQuoSA framework])],
	[],
	[with_aquosa=check])
LIBQRESLIB=
AS_IF([test "x$with_aquosa" != xno],
	[AC_CHECK_LIB([qreslib], [qres_init],
		[AC_SUBST([LIBQRESLIB], ["-lqreslib"])
		 AC_DEFINE([AQUOSA],[1], [Define if you have qreslib])
		],
		[if test "x$with_aquosa" != xcheck; then
			AC_MSG_FAILURE([AQuoSA library not found])
		 fi
		], -lqreslib)])
AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile src/Makefile README:README.in INSTALL:INSTALL.in])
AC_OUTPUT
