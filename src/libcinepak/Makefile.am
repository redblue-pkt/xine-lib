CFLAGS = @CFLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_cinepak.la

xineplug_decode_cinepak_la_SOURCES = xine_decoder.c
xineplug_decode_cinepak_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_cinepak_la_LDFLAGS = -avoid-version -module

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
