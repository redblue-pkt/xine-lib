include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_a52.la

xineplug_decode_a52_la_SOURCES = \
	bitstream.c \
	bit_allocate.c \
	crc.c \
	downmix.c \
	imdct.c \
	parse.c \
	xine_decoder.c

xineplug_decode_a52_la_LIBADD = $(XINE_LIB)
xineplug_decode_a52_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = \
	a52.h \
	a52_internal.h \
	bitstream.h \
	tables.h
