RTSP_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -I$(srcdir)/../libreal

AM_CFLAGS = $(RTSP_CFLAGS) @ANSI_FLAGS@

noinst_LTLIBRARIES = librtsp.la

librtsp_la_SOURCES = \
  rtsp.c \
  rtsp_session.c

librtsp_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
  rtsp.h \
  rtsp_session.h
  
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
