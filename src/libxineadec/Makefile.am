#
# the Makefile for the native xine audio decoders
#


libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = \
	xineplug_decode_adpcm.la \
	xineplug_decode_logpcm.la \
	xineplug_decode_roqaudio.la 

xineplug_decode_roqaudio_la_SOURCES = roqaudio.c
xineplug_decode_roqaudio_la_LDFLAGS = -avoid-version -module

xineplug_decode_adpcm_la_SOURCES = adpcm.c
xineplug_decode_adpcm_la_LDFLAGS = -avoid-version -module

xineplug_decode_logpcm_la_SOURCES = logpcm.c
xineplug_decode_logpcm_la_LDFLAGS = -avoid-version -module

# noinst_HEADERS = 

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
