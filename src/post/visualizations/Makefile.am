##
## Process this file with automake to produce Makefile.in
##

EXTRA_DIST = fooviz.c

AM_CFLAGS = @ANSI_FLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@ @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_visualizations.la

xineplug_post_visualizations_la_SOURCES = \
        visualizations.c fftscope.c oscope.c
xineplug_post_visualizations_la_LIBADD = $(XINE_LIB)
xineplug_post_visualizations_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@ -lm

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

