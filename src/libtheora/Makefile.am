##
## Process this file with automake to produce Makefile.in
##

AM_CFLAGS = $(THEORA_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_THEORA
theora_module = xineplug_decode_theora.la
endif

lib_LTLIBRARIES = $(theora_module)

xineplug_decode_theora_la_SOURCES = xine_decoder.c
xineplug_decode_theora_la_LIBADD = $(OGG_LIBS) $(THEORA_LIBS)
xineplug_decode_theora_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

debug:
#	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(OGG_CFLAGS) $(THEORA_CFLAGS)"
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
