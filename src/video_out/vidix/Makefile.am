EXTRA_DIST = README vidix.txt

SUBDIRS = drivers

CFLAGS = @CFLAGS@ @STATIC@

if HAVE_VIDIX
vidix_lib = libvidix.la
endif

noinst_LTLIBRARIES = $(vidix_lib)

libvidix_la_SOURCES = vidixlib.c

noinst_HEADERS = fourcc.h vidix.h vidixlib.h

AM_CPPFLAGS = -I$(top_srcdir)/src/video_out/vidix \
	-I$(top_srcdir)/src/video_out/libdha

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) @STATIC@ -DXINE_COMPILE"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
