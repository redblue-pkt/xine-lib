##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = libavcodec 

EXTRA_DIST = diff_to_ffmpeg_cvs.txt

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

xineplug_decode_ff_la_SOURCES = xine_decoder.c
xineplug_decode_ff_la_LDFLAGS = \
		$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
		-avoid-version -module

debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
