#
# the Makefile for flac plugins
#

AM_CFLAGS = $(LIBFLAC_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

if HAVE_FLAC
flac_module = xineplug_flac.la
endif

libdir = $(XINE_PLUGINDIR)
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la

lib_LTLIBRARIES = $(flac_module)

xineplug_flac_la_SOURCES = demux_flac.c decoder_flac.c
xineplug_flac_la_LIBADD = $(LIBFLAC_LIBS)
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
