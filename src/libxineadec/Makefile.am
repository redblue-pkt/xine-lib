include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = fooaudio.c

SUBDIRS = gsm610 nosefart

xineplug_LTLIBRARIES = \
	xineplug_decode_gsm610.la \
	xineplug_decode_nsf.la

xineplug_decode_gsm610_la_SOURCES = gsm610.c
xineplug_decode_gsm610_la_CFLAGS = $(VISIBILITY_FLAG)
xineplug_decode_gsm610_la_LDFLAGS = $(xineplug_ldflags)
xineplug_decode_gsm610_la_LIBADD = \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/gsm610/libgsm610.la

xineplug_decode_nsf_la_SOURCES = nsf.c
xineplug_decode_nsf_la_CFLAGS = $(VISIBILITY_FLAG) -DNSF_PLAYER -fno-strict-aliasing
xineplug_decode_nsf_la_LDFLAGS = $(xineplug_ldflags)
xineplug_decode_nsf_la_LIBADD = -lm \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/nosefart/libnosefart.la

# noinst_HEADERS = 
