AC_INIT([rt-app], [1.0], [juri.lelli@arm.com, vincent.guittot@linaro.org])
AC_COPYRIGHT([Copyright (C) 2009 Giacomo Bagnoli <g.bagnoli@asidev.com>
Copyright (C) 2016 Juri Lelli      <juri.lelli@arm.com>
Copyright (C) 2016 Vincent Guittot <vincent.guittot@linaro.org>])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-bzip2])
m4_pattern_allow([AM_PROG_AR])
AM_PROG_AR
AC_PROG_CC
AC_PROG_LIBTOOL
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/rt-app.c])
AC_HEADER_STDC

AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([m], [round])
AC_CHECK_LIB([rt], [clock_gettime])
AC_CHECK_LIB([json-c], [json_object_from_file], [], [AC_MSG_ERROR([json-c libraries required])])
AC_CHECK_FUNCS(sched_setattr, [], [])

AC_ARG_WITH([deadline],
	[AS_HELP_STRING([--with-deadline], 
		[Add support for SCHED_DEADLINE])],
	[AC_DEFINE([DLSCHED], [1], [Define if you have SCHED_DEADLINE support])], 
	[with_deadline=no])

AM_CONDITIONAL([SET_DLSCHED], [test x$with_deadline != xno && test !HAVE_SCHED_SETATTR])

AC_ARG_VAR([LOGLVL], [verbosity level, from 0 to 100. 100 is very verbose])

if test -z "${LOGLVL}";then
	AC_DEFINE([LOG_LEVEL], [50], [Define log message verbosity])
else
	AC_DEFINE_UNQUOTED([LOG_LEVEL], [${LOGLVL}])
fi

AC_DEFINE([BUILD_DATE], ["m4_esyscmd_s([date +"%Y-%m-%d %H:%M:%S %Z"])"], [Build date.])

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile src/Makefile libdl/Makefile README COPYING])
AC_OUTPUT
