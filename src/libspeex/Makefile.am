include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(SPEEX_CFLAGS)

libdir = $(XINE_PLUGINDIR)

if HAVE_SPEEX
speex_module = xineplug_decode_speex.la
endif

lib_LTLIBRARIES = $(speex_module)

xineplug_decode_speex_la_SOURCES = xine_decoder.c
xineplug_decode_speex_la_LIBADD = $(SPEEX_LIBS) $(XINE_LIB)
xineplug_decode_speex_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
