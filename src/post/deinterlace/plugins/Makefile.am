include $(top_srcdir)/misc/Makefile.common

AM_CPPFLAGS = -I$(top_srcdir)/src/post/deinterlace

EXTRA_DIST = 

libdir = $(XINE_PLUGINDIR)/post

noinst_LTLIBRARIES = libdeinterlaceplugins.la

libdeinterlaceplugins_la_SOURCES = \
	double.c \
	greedy.c \
	linear.c \
	linearblend.c \
	vfir.c \
	weave.c \
	greedy2frame.c
libdeinterlaceplugins_la_LIBADD = $(XINE_LIB)
libdeinterlaceplugins_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = plugins.h
