ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(srcdir)/../libdl/
bin_PROGRAMS = rt-app
rt_app_SOURCES= rt-app_types.h rt-app_args.h rt-app_utils.h rt-app_utils.c rt-app_args.c rt-app.h  rt-app.c 
if AMJSON
rt_app_SOURCES += rt-app_parse_config.h rt-app_parse_config.c
endif
rt_app_LDADD = $(QRESLIB) ../libdl/libdl.a $(LIBJSON)

