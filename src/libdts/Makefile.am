include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_dts.la

xineplug_decode_dts_la_SOURCES = decoder.c print_info.c xine_decoder.c
xineplug_decode_dts_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = decoder.h decoder_internal.h dts_debug.h huffman_tables.h \
	print_info.h
