##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @GLOBAL_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

if HAVE_DIVXDECORE
divx4_codec = xineplug_decode_divx4.la
endif

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = $(divx4_codec)

xineplug_decode_divx4_la_SOURCES = xine_decoder.c

xineplug_decode_divx4_la_LDFLAGS = -ldivxdecore -avoid-version -module

#noinst_HEADERS = config.h

debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) CFLAGS="@DEBUG_CFLAGS@"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
