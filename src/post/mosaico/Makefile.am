##
## Process this file with automake to produce Makefile.in
##

AM_CFLAGS = @ANSI_FLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@ @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_mosaico.la xineplug_post_switch.la

xineplug_post_mosaico_la_SOURCES = mosaico.c
xineplug_post_mosaico_la_LIBADD = $(XINE_LIB)
xineplug_post_mosaico_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_post_switch_la_SOURCES = switch.c
xineplug_post_switch_la_LIBADD = $(XINE_LIB)
xineplug_post_switch_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

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
