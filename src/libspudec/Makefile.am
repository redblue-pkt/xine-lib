include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_spu.la

if HAVE_DVDNAV

xineplug_decode_spu_la_SOURCES = \
	spu.c \
	xine_decoder.c
AM_CPPFLAGS = $(DVDNAV_CFLAGS)
xineplug_decode_spu_la_LIBADD = $(XINE_LIB) $(DVDNAV_LIBS)

else

xineplug_decode_spu_la_SOURCES = \
	nav_read.c \
	spu.c \
	xine_decoder.c
AM_CPPFLAGS = -I$(top_srcdir)/src/input/libdvdnav
xineplug_decode_spu_la_LIBADD = $(XINE_LIB)

endif

xineplug_decode_spu_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = spu.h
