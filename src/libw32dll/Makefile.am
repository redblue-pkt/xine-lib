CFLAGS = @GLOBAL_CFLAGS@ 

SUBDIRS = wine DirectShow
# SUBDIRS = wine dshow_cpp 

if HAVE_W32DLL
w32dll_codec = xineplug_decode_w32dll.la
endif

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine codecs should be named like the scheme "xineplug_decode_"
#
lib_LTLIBRARIES = $(w32dll_codec)

xineplug_decode_w32dll_la_SOURCES = w32codec.c
xineplug_decode_w32dll_la_LDFLAGS = -avoid-version -module
xineplug_decode_w32dll_la_LIBADD = $(top_builddir)/src/libw32dll/wine/libwine.la \
	$(top_builddir)/src/libw32dll/DirectShow/libds_filter.la @KSTAT_LIBS@
#	$(top_builddir)/src/libw32dll/dshow_cpp/libds_filter.la @KSTAT_LIBS@

noinst_HEADERS = w32codec.h libwin32.h

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
