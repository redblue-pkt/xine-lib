REAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -I../librtsp

AM_CFLAGS = $(REAL_CFLAGS) @ANSI_FLAGS@

noinst_LTLIBRARIES = libreal.la

libreal_la_SOURCES = \
  real.c \
  asmrp.c \
  rmff.c \
  sdpplin.c

libreal_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
  real.h \
  asmrp.h \
  rmff.h \
  sdpplin.h
  
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
