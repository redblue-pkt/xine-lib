include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBMPEG2_CFLAGS)

EXTRA_DIST = idct_mlib.c idct_mlib.h motion_comp_mlib.c

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mpeg2.la

xineplug_decode_mpeg2_la_SOURCES = \
	xine_decoder.c

xineplug_decode_mpeg2_la_LIBADD = $(XINE_LIB) /usr/local/lib/libmpeg2.la 
xineplug_decode_mpeg2_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

