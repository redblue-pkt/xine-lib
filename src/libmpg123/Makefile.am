include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mpg123.la

xineplug_decode_mpg123_la_SOURCES = \
	common.c \
	dct64_i386.c \
	decode_i386.c \
	interface.c \
	layer1.c \
	layer2.c \
	layer3.c \
	tabinit.c \
	xine_decoder.c

xineplug_decode_mpg123_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = huffman.h mpg123.h mpglib.h l2tables.h
