DVD_CFLAGS = -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE 

AM_CFLAGS = $(DVD_CFLAGS) @ANSI_FLAGS@

noinst_LTLIBRARIES = libdvdread.la

libdvdread_la_SOURCES = \
	dvd_input.c \
	dvd_reader.c \
	dvd_udf.c \
	ifo_read.c \
        ifo_print.c \
	md5.c \
	nav_read.c \
	nav_print.c

libdvdread_la_LDFLAGS = $(THREAD_LIBS) -avoid-version -module

noinst_HEADERS = \
	bswap.h \
	dvd_udf.h \
	dvd_input.h \
	dvdread_internal.h \
	ifo_read.h \
        ifo_print.h \
	md5.h \
	nav_read.h \
	nav_print.h

include_HEADERS = \
	dvd_reader.h \
	ifo_types.h \
	nav_types.h

##
## Install header files (default=$includedir/xine)
##
install-includeHEADERS: $(include_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/xine
	@list='$(include_HEADERS)'; for p in $$list; do \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p; \
	done


##
## Remove them
##
uninstall-includeHEADERS:
	@$(NORMAL_UNINSTALL)
	list='$(include_HEADERS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(includedir)/xine/$$p; \
	done

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
