##
## Process this file with automake to produce Makefile.in
##

AM_CFLAGS = @ANSI_FLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@ @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_goom.la

xineplug_post_goom_la_SOURCES = \
        xine_goom.c \
	goom_core.c goom_core.h \
	goom_tools.h goom_config.h \
	filters.c filters.h \
	lines.c lines.h \
	ifs_display.c ifs.c ifs.h \
	graphic.c graphic.h

xineplug_post_goom_la_LIBADD = $(XINE_LIB)
xineplug_post_goom_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

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

