DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE 

CFLAGS = @CFLAGS@ $(DVD_CFLAGS) 

noinst_LTLIBRARIES = libdvdread.la

libdvdread_la_SOURCES = dvd_input.c dvd_reader.c dvd_udf.c ifo_read.c \
        ifo_print.c nav_read.c nav_print.c bswap.h dvd_udf.h dvd_input.h
libdvdread_la_LDFLAGS = $(THREAD_LIBS) \
	-avoid-version -module

noinst_HEADERS = dvd_reader.h ifo_types.h ifo_read.h \
        ifo_print.h nav_types.h nav_read.h nav_print.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(OGG_CFLAGS) $(VORBIS_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
