CFLAGS = @BUILD_LIB_STATIC@ @GLOBAL_CFLAGS@

EXTRA_DIST = 

noinst_LTLIBRARIES = libspudec.la

libspudec_la_SOURCES = spudec.c spudec.h

noinst_HEADERS = spudec.h

debug:
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
