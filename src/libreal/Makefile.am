##
## Process this file with automake to produce Makefile.in
##

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_real.la xineplug_decode_real_audio.la

xineplug_decode_real_la_SOURCES = xine_decoder.c
xineplug_decode_real_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_real_audio_la_SOURCES = audio_decoder.c
xineplug_decode_real_audio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

debug:
#	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(OGG_CFLAGS) $(VORBIS_CFLAGS)"
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
