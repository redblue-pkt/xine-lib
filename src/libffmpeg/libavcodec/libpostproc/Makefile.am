AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libpostprocess.la

EXTRA_DIST = postprocess_template.c

libpostprocess_la_SOURCES = postprocess.c

noinst_HEADERS = mangle.h postprocess.h postprocess_internal.h

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBFFMPEG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
