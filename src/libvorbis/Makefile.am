##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @GLOBAL_CFLAGS@ @VORBIS_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_VORBIS
vorbis_module = xineplug_decode_vorbis.la
endif

lib_LTLIBRARIES = $(vorbis_module)

xineplug_decode_vorbis_la_SOURCES = xine_decoder.c
xineplug_decode_vorbis_la_LIBADD = @VORBIS_LIBS@
xineplug_decode_vorbis_la_LDFLAGS = -avoid-version -module

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) @OGG_CFLAGS@ @VORBIS_CFLAGS@"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
