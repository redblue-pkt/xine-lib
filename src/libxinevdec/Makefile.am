CFLAGS = @CFLAGS@
DEBUG_CFLAGS = @DEBUG_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_cinepak.la xineplug_decode_cyuv.la \
	xineplug_decode_msvc.la xineplug_decode_roqvideo.la

xineplug_decode_cinepak_la_SOURCES = cinepak.c
xineplug_decode_cinepak_la_LDFLAGS = -avoid-version -module

xineplug_decode_cyuv_la_SOURCES = cyuv.c
xineplug_decode_cyuv_la_LDFLAGS = -avoid-version -module

xineplug_decode_msvc_la_SOURCES = msvc.c
xineplug_decode_msvc_la_LDFLAGS = -avoid-version -module

xineplug_decode_roqvideo_la_SOURCES = roqvideo.c
xineplug_decode_roqvideo_la_LDFLAGS = -avoid-version -module

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
