CFLAGS = @GLOBAL_CFLAGS@ 

SUBDIRS = wine

if HAVE_W32DLL
w32dll_codec = xineplug_decode_w32dll.la
endif

# LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine codecs should be named like the scheme "xineplug_decode_"
#
lib_LTLIBRARIES = $(w32dll_codec)

xineplug_decode_w32dll_la_SOURCES = w32codec.c
xineplug_decode_w32dll_la_LDFLAGS = -avoid-version -module
xineplug_decode_w32dll_la_LIBADD = wine/libwine.la

noinst_HEADERS = w32codec.h

debug:
	$(MAKE) CFLAGS="$(CFLAGS) -g -DSTATIC_WIN32_PATH"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
