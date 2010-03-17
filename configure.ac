AC_INIT([rt-app], [0.2_alpha1], [g.bagnoli@asidev.com])
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
          AS_IF([test "x$with_aquosa" != xno],
            [AC_CHECK_LIB([qreslib], [qres_init],
              [AC_SUBST([QRESLIB], ["-lqreslib"])
               AC_DEFINE([AQUOSA], [1],
                         [Define if you have libqreslib])
              ],
              [AC_MSG_FAILURE(
                 [qreslib test failed (--without-aquosa to disable)])],
              [-lqreslib])])

AC_ARG_WITH([deadline],
	[AS_HELP_STRING([--with-deadline], 
         [Add support for SCHED_DEADLINE])],
	[],
	[with_deadline=no])

AS_IF([test "x$with_deadline" != xno],
	[AC_DEFINE([DLSCHED], [1], [Define if you have SCHED_DEADLINE support])
     ])

AC_ARG_WITH([json],
	    [AS_HELP_STRING([--with-json],
			    [Add support for reading json config file]
			   )
	    ], [], [with_json=no]
	   )
LIBJSON=
AS_IF([test "x$with_json" != "xno"],
      [AC_CHECK_LIB([json], [json_object_from_file],
	      	    [AC_SUBST([LIBJSON], ["-ljson"])
        	     AC_DEFINE([JSON], [1], [Define if you have libjson])
       	            ],
       	            [AC_MSG_FAILURE([libyaml test failed (use --without-json to disable)])],
                    [-ljson]
		   )
      ]
     ) 

AC_CONFIG_HEADERS([src/config.h])
AC_CONFIG_FILES([Makefile src/Makefile libdl/Makefile README:README.in INSTALL:INSTALL.in])
AC_OUTPUT
