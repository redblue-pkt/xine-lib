##
## Process this file with automake to produce Makefile.in
##

AM_CFLAGS = $(SPEEX_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_SPEEX
speex_module = xineplug_decode_speex.la
endif

lib_LTLIBRARIES = $(speex_module)

xineplug_decode_speex_la_SOURCES = xine_decoder.c
xineplug_decode_speex_la_LIBADD = $(SPEEX_LIBS)
xineplug_decode_speex_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

debug:
#	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(OGG_CFLAGS) $(SPEEX_CFLAGS)"
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
