include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(VORBIS_CFLAGS)

libdir = $(XINE_PLUGINDIR)

if HAVE_VORBIS
vorbis_module = xineplug_decode_vorbis.la
endif

lib_LTLIBRARIES = $(vorbis_module)

xineplug_decode_vorbis_la_SOURCES = xine_decoder.c
xineplug_decode_vorbis_la_LIBADD = $(VORBIS_LIBS)
xineplug_decode_vorbis_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
