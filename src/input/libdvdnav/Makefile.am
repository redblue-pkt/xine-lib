DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE

AM_CPPFLAGS = -DDVDNAV_COMPILE
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
	vmcmd.c \
	ifo_print.c \
	ifo_read.c \
	md5.c \
	nav_print.c \
	nav_read.c \
	dvd_reader.c \
	dvd_input.c \
        dvd_udf.c

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
	dvd_types.h \
	ifo_print.h \
	ifo_read.h \
	ifo_types.h \
	md5.h \
	nav_print.h \
	nav_read.h \
	nav_types.h \
	dvd_reader.h \
	dvd_input.h \
	dvd_udf.h \
	bswap.h 

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
