LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_spu.la
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la

if HAVE_DVDNAV

xineplug_decode_spu_la_SOURCES = \
	spu.c \
	xine_decoder.c
AM_CPPFLAGS = $(DVDNAV_CFLAGS)
xineplug_decode_spu_la_LIBADD = $(XINE_LIB) $(DVDNAV_LIBS)

else

xineplug_decode_spu_la_SOURCES = \
	nav_read.c \
	spu.c \
	xine_decoder.c
AM_CPPFLAGS = -I$(top_srcdir)/src/input/libdvdnav
xineplug_decode_spu_la_LIBADD = $(XINE_LIB)

endif

xineplug_decode_spu_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = spu.h
include_HEADERS = spu_decoder_api.h

$(XINE_LIB):
	@cd $(top_builddir)/src/xine-engine && $(MAKE)

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
