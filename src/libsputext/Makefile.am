LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

if HAVE_GICONV
sputext_decoder = xineplug_decode_sputext.la xineplug_dmx_sputext.la
endif

lib_LTLIBRARIES = $(sputext_decoder)

xineplug_dmx_sputext_la_SOURCES = demux_sputext.c
## libgiconv is only needed with FreeBSD
xineplug_dmx_sputext_la_LIBADD  = $(XINELIB) $(GICONV_BSD_LIBS)
xineplug_dmx_sputext_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_sputext_la_SOURCES = xine_decoder.c 
xineplug_decode_sputext_la_LIBADD  = $(XINELIB)
xineplug_decode_sputext_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

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
