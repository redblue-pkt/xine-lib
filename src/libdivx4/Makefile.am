##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @GLOBAL_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_divx4.la

xineplug_decode_divx4_la_SOURCES = xine_decoder.c
xineplug_decode_divx4_la_LDFLAGS = -avoid-version -module
xineplug_decode_divx4_la_LIBADD = $(top_builddir)/src/xine-utils/libxineutils.la

noinst_HEADERS = decore-if.h

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
