include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = fooaudio.c

libdir = $(XINE_PLUGINDIR)

AM_CFLAGS = -DNSF_PLAYER

SUBDIRS = gsm610 nosefart

lib_LTLIBRARIES = \
	xineplug_decode_gsm610.la \
	xineplug_decode_nsf.la

xineplug_decode_gsm610_la_SOURCES = gsm610.c
xineplug_decode_gsm610_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_gsm610_la_LIBADD = \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/gsm610/libgsm610.la

xineplug_decode_nsf_la_SOURCES = nsf.c
xineplug_decode_nsf_la_CFLAGS = -DNSF_PLAYER -fno-strict-aliasing
xineplug_decode_nsf_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_nsf_la_LIBADD = \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/nosefart/libnosefart.la

# noinst_HEADERS = 
