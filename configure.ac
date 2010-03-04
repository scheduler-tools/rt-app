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

AC_ARG_WITH([aquosa],
            [AS_HELP_STRING([--with-aquosa],
              [Add support for the AQuoSA framework])],
            [],
            [with_aquosa=no])
          
          QRESLIB=
          AS_IF([test "x$with_readline" != xno],
            [AC_CHECK_LIB([qreslib], [qres_init],
              [AC_SUBST([QRESLIB], ["-lqreslib"])
               AC_DEFINE([AQUOSA], [1],
                         [Define if you have libqreslib])
              ],
              [AC_MSG_FAILURE(
                 [qreslib test failed (--without-aquosa to disable)])],
              [-lqreslib])])

AC_ARG_WITH([deadline],
	[AS_HELP_STRING([--enable-deadline], 
         [Add support for SCHED_DEADLINE])],
	[],
	[with_deadline=no])
LIBQRESLIB=
AS_IF([test "x$with_deadline" != xno],
	[AC_DEFINE([DEADLINE], [1], [Define if you have SCHED_DEADLINE support])
     ])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile src/Makefile README:README.in INSTALL:INSTALL.in])
AC_OUTPUT
