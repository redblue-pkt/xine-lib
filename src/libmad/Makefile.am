CFLAGS = @CFLAGS@ -DOPT_SPEED

EXTRA_DIST = imdct_l_arm.S

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_mad.la

xineplug_decode_mad_la_SOURCES = \
	bit.c \
	fixed.c \
	frame.c \
	huffman.c \
	layer12.c \
	layer3.c \
	stream.c \
	synth.c \
	timer.c \
	version.c \
	xine_decoder.c

xineplug_decode_mad_la_LDFLAGS = -avoid-version -module

noinst_HEADERS = \
	D.dat \
	imdct_s.dat \
	qc_table.dat \
	rq_table.dat \
	sf_table.dat \
	bit.h \
	fixed.h \
	frame.h \
	global.h \
	huffman.h \
	layer12.h \
	layer3.h \
	stream.h \
	synth.h \
	timer.h \
	version.h


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
