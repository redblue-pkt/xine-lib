#
# the Makefile for flac plugins
#

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la

lib_LTLIBRARIES = xineplug_flac.la

xineplug_flac_la_SOURCES = demux_flac.c decoder_flac.c
xineplug_flac_la_LDFLAGS = -avoid-version -module -lFLAC @XINE_PLUGIN_MIN_SYMS@


# noinst_HEADERS = 

$(XINE_LIB):
	@cd $(top_builddir)/src/xine-engine && $(MAKE)

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
