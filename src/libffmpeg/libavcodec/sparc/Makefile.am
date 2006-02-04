include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
AM_CPPFLAGS = -I$(top_srcdir)/src/libffmpeg/libavutil
ASFLAGS =

noinst_LTLIBRARIES = libavcodec_sparc.la

libavcodec_sparc_src = dsputil_vis.c
libavcodec_sparc_dummy = libavcodec_sparc_dummy.c

EXTRA_DIST = $(libavcodec_sparc_src) $(libavcodec_sparc_dummy) vis.h

sparc_modules = $(libavcodec_sparc_src)

libavcodec_sparc_la_SOURCES = $(sparc_modules) $(libavcodec_sparc_dummy)
