include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_divx4.la

xineplug_decode_divx4_la_SOURCES = xine_decoder.c
xineplug_decode_divx4_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = decore-if.h
