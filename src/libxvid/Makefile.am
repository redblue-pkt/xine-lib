AM_CFLAGS = @XVID_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)


if HAVE_XVID
xvid_modules = xineplug_decode_xvid.la
endif

lib_LTLIBRARIES = $(xvid_modules)

xineplug_decode_xvid_la_SOURCES = xine_decoder.c
xineplug_decode_xvid_la_LIBADD = @XVID_LIBS@
xineplug_decode_xvid_la_LDFLAGS = -avoid-version -module

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
