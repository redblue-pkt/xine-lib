include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = -DOPT_SPEED

EXTRA_DIST = imdct_l_arm.S

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mad.la

xineplug_decode_mad_la_SOURCES = \
	bit.c \
	fixed.c \
	frame.c \
	huffman.c \
	layer12.c \
	layer3.c \
	stream.c \
	synth.c \
	timer.c \
	version.c \
	xine_decoder.c

xineplug_decode_mad_la_LIBADD = $(XINE_LIB)
xineplug_decode_mad_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = \
	D.dat \
	imdct_s.dat \
	qc_table.dat \
	rq_table.dat \
	sf_table.dat \
	bit.h \
	fixed.h \
	frame.h \
	global.h \
	huffman.h \
	layer12.h \
	layer3.h \
	stream.h \
	synth.h \
	timer.h \
	version.h
