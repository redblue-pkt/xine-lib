include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBFLAC_CFLAGS)

if HAVE_FLAC
flac_module = xineplug_flac.la
endif

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = $(flac_module)

xineplug_flac_la_SOURCES = demux_flac.c decoder_flac.c
xineplug_flac_la_LIBADD = $(LIBFLAC_LIBS)
xineplug_flac_la_LDFLAGS = -avoid-version -module -lFLAC @XINE_PLUGIN_MIN_SYMS@
