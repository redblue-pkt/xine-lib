##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = libavcodec 

# I don't think this should include internal docs that are not interesting
# for end users at all... -siggi
EXTRA_DIST =

INTERNAL_DOCS = diff_to_ffmpeg_cvs.txt

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la
libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

xineplug_decode_ff_la_SOURCES = xine_decoder.c
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_ff_la_LIBADD = $(XINE_LIB) \
	$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la

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

$(XINE_LIB):
	@cd $(top_builddir)/src/xine-engine && $(MAKE)
