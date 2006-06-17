include $(top_srcdir)/misc/Makefile.common

AM_CPPFLAGS = $(LIBFFMPEG_CPPFLAGS)
AM_CFLAGS = -fno-strict-aliasing
ASFLAGS =

noinst_LTLIBRARIES = libavutil.la

libavutil_la_SOURCES = \
	integer.c \
	mathematics.c \
	rational.c

libavutil_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	avutil.h \
	bswap.h \
	common.h \
	integer.h \
	intfloat_readwrite.h \
	mathematics.h \
	rational.h
