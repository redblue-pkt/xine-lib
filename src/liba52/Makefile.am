AM_CFLAGS = $(LIBA52_CFLAGS)
DEBUG_CFLAGS = @DEBUG_CFLAGS@ $(LIBA52_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_a52.la

xineplug_decode_a52_la_SOURCES = \
	bitstream.c \
	bit_allocate.c \
	downmix.c \
	imdct.c \
	parse.c \
	xine_decoder.c

xineplug_decode_a52_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_decode_a52_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	a52.h \
	a52_internal.h \
	bitstream.h \
	tables.h

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
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
