##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = libavcodec 

CFLAGS = @CFLAGS@ -D_FILE_OFFSET_BITS=64 -Wall -DXINE_COMPILE 

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

xineplug_decode_ff_la_SOURCES = xine_decoder.c

xineplug_decode_ff_la_LDFLAGS = \
		$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
		-avoid-version -module

noinst_HEADERS = config.h bswap.h

debug:
	@$(MAKE) CFLAGS="-D_FILE_OFFSET_BITS=64 -Wall -DDEBUG -O -finline-functions -g -DXINE_COMPILE -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
