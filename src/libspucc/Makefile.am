include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_spucc.la

xineplug_decode_spucc_la_SOURCES =  cc_decoder.c xine_decoder.c
xineplug_decode_spucc_la_LIBADD = $(XINE_LIB)
xineplug_decode_spucc_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = cc_decoder.h
