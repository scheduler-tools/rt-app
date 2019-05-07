ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(srcdir)/../libdl/
bin_PROGRAMS = rt-app
rt_app_SOURCES= rt-app_types.h rt-app_args.h rt-app_utils.h rt-app_utils.c rt-app_args.c rt-app.h  rt-app.c 
rt_app_SOURCES += rt-app_parse_config.h rt-app_parse_config.c rt-app_taskgroups.h rt-app_taskgroups.c
rt_app_LDADD = $(QRESLIB)
if SET_DLSCHED
rt_app_LDADD += ../libdl/libdl.a
endif
dist_bin_SCRIPTS = $(srcdir)/../doc/workgen
