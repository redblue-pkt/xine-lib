DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE \
  -I$(top_srcdir)/src/input/libdvdread/

AM_CFLAGS = $(DVD_CFLAGS) @ANSI_FLAGS@

noinst_LTLIBRARIES = libdvdnav.la

libdvdnav_la_SOURCES = \
	decoder.c \
	dvdnav.c \
	highlight.c \
	navigation.c \
	read_cache.c \
	remap.c \
	searching.c \
	settings.c \
	vm.c \
	vmcmd.c 

libdvdnav_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
	decoder.h \
	dvdnav.h \
	dvdnav_events.h \
	dvdnav_internal.h \
	remap.h \
	vm.h \
	vmcmd.h \
	read_cache.h \
	dvd_types.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(DVD_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
