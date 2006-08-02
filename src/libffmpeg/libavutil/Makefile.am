include $(top_srcdir)/misc/Makefile.common

AM_CPPFLAGS = $(LIBFFMPEG_CPPFLAGS)
AM_CFLAGS = -fno-strict-aliasing
ASFLAGS =

noinst_LTLIBRARIES = libavutil.la

libavutil_la_SOURCES = \
	adler32.c \
	crc.c \
	integer.c \
	lls.c \
	log.c \
	mathematics.c \
	md5.c \
	rational.c

libavutil_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	adler32.h \
	avutil.h \
	bswap.h \
	common.h \
	crc.h \
	integer.h \
	intfloat_readwrite.h \
	lls.h \
	log.h \
	mathematics.c \
	md5.h \
	rational.h
