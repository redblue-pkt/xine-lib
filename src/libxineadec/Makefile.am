include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = fooaudio.c

SUBDIRS = gsm610 nosefart

AM_LDFLAGS = $(xineplug_ldflags)

xineplug_LTLIBRARIES = \
	xineplug_decode_gsm610.la \
	xineplug_decode_nsf.la \
	xineplug_decode_lpcm.la

xineplug_decode_gsm610_la_SOURCES = gsm610.c
xineplug_decode_gsm610_la_CFLAGS = $(VISIBILITY_FLAG)
xineplug_decode_gsm610_la_LIBADD = $(XINE_LIB) gsm610/libgsm610.la

xineplug_decode_nsf_la_SOURCES = nsf.c
xineplug_decode_nsf_la_CFLAGS = $(VISIBILITY_FLAG) -DNSF_PLAYER -fno-strict-aliasing
xineplug_decode_nsf_la_LIBADD = $(XINE_LIB) -lm nosefart/libnosefart.la

xineplug_decode_lpcm_la_SOURCES = xine_lpcm_decoder.c
xineplug_decode_lpcm_la_CFLAGS = $(VISIBILITY_FLAG)
xineplug_decode_lpcm_la_LIBADD = $(XINE_LIB)
