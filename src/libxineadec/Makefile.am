include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = fooaudio.c

libdir = $(XINE_PLUGINDIR)

AM_CFLAGS = -DNSF_PLAYER

SUBDIRS = gsm610 nosefart

lib_LTLIBRARIES = \
	xineplug_decode_adpcm.la \
	xineplug_decode_logpcm.la \
	xineplug_decode_roqaudio.la \
	xineplug_decode_gsm610.la \
	xineplug_decode_interplayaudio.la \
	xineplug_decode_nsf.la \
	xineplug_decode_28k8.la

xineplug_decode_roqaudio_la_SOURCES = roqaudio.c
xineplug_decode_roqaudio_la_LIBADD = $(XINE_LIB)
xineplug_decode_roqaudio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_adpcm_la_SOURCES = adpcm.c
xineplug_decode_adpcm_la_LIBADD = $(XINE_LIB)
xineplug_decode_adpcm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_logpcm_la_SOURCES = logpcm.c
xineplug_decode_logpcm_la_LIBADD = $(XINE_LIB)
xineplug_decode_logpcm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_gsm610_la_SOURCES = gsm610.c
xineplug_decode_gsm610_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_gsm610_la_LIBADD = \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/gsm610/libgsm610.la

xineplug_decode_interplayaudio_la_SOURCES = interplayaudio.c
xineplug_decode_interplayaudio_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_interplayaudio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_nsf_la_SOURCES = nsf.c
#xineplug_decode_nsf_la_CFLAGS = -DNSF_PLAYER
xineplug_decode_nsf_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_nsf_la_LIBADD = \
	$(XINE_LIB) \
	$(top_builddir)/src/libxineadec/nosefart/libnosefart.la

xineplug_decode_28k8_la_SOURCES = 28k8.c
xineplug_decode_28k8_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_28k8_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

# noinst_HEADERS = 
