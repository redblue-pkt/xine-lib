##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = libavcodec 

CFLAGS = @GLOBAL_CFLAGS@ 

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

xineplug_decode_ff_la_SOURCES = xine_decoder.c

xineplug_decode_ff_la_LDFLAGS = \
		$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
		-avoid-version -module

noinst_HEADERS = config.h

debug:
	$(MAKE) CFLAGS="-Wall -DDEBUG -g -DXINE_COMPILE"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
