AM_CFLAGS = -I$(srcdir)/wine

#DEBUG_CFLAGS = @DEBUG_CFLAGS@ -I$(srcdir)/wine

SUBDIRS = wine DirectShow dmo qtx

if HAVE_W32DLL
w32dll_codec = xineplug_decode_w32dll.la
qt_codec = xineplug_decode_qt.la
endif

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la
libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine codecs should be named like the scheme "xineplug_decode_"
#
lib_LTLIBRARIES = $(w32dll_codec) $(qt_codec)

xineplug_decode_w32dll_la_SOURCES = w32codec.c
xineplug_decode_w32dll_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_w32dll_la_LIBADD = \
	$(top_builddir)/src/libw32dll/wine/libwine.la \
	$(XINE_LIB) \
	$(top_builddir)/src/libw32dll/DirectShow/libds_filter.la \
	$(top_builddir)/src/libw32dll/dmo/libdmo_filter.la \
	@KSTAT_LIBS@

xineplug_decode_qt_la_SOURCES = qt_decoder.c
xineplug_decode_qt_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_qt_la_LIBADD = \
	$(top_builddir)/src/libw32dll/wine/libwine.la \
	$(XINE_LIB) \
	@KSTAT_LIBS@

noinst_HEADERS = libwin32.h w32codec.h

$(XINE_LIB):
	@cd $(top_builddir)/src/xine-engine && $(MAKE)

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
