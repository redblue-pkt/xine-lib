##
## Process this file with automake to produce Makefile.in
##

CFLAGS = @CFLAGS@ $(THREAD_CFLAGS) -DXINE_LOGO_FILE=\"file://@XINE_SKINPATH@/xine_logo.mpg\"


lib_LTLIBRARIES = libxine.la

libxine_la_SOURCES = xine.c metronom.c configfile.c buffer.c \
	load_plugins.c video_decoder.c buffer_types.c \
	audio_decoder.c video_out.c audio_out.c resample.c events.c lrb.c \
	video_overlay.c osd.c scratch.c locale.c
libxine_la_DEPENDENCIES = @INTLLIBS@
libxine_la_LIBADD = $(THREAD_LIBS) $(DYNAMIC_LD_LIBS) @INTLLIBS@ $(ZLIB_LIBS) -lm

libxine_la_LDFLAGS =  \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-release $(LT_RELEASE) 

include_HEADERS  = buffer.h metronom.h configfile.h \
        audio_out.h resample.h video_out.h xine_internal.h spu_decoder.h \
	events.h lrb.h video_overlay.h osd.h scratch.h xineintl.h

noinst_HEADERS = bswap.h

@INCLUDED_INTL_TRUE@@INTLLIBS@:
@INCLUDED_INTL_TRUE@	@cd $(top_builddir)/intl && $(MAKE) libintl.la

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) -DXINE_LOGO_FILE=\"file://@XINE_SKINPATH@/xine_logo.mpg\""

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am



###
# Install header files (default=$includedir/xine)
#
install-includeHEADERS: $(include_HEADERS)
	@$(NORMAL_INSTALL)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/xine
	@list='$(include_HEADERS)'; for p in $$list; do \
	  if test -f "$$p"; then d= ; else d="$(srcdir)/"; fi; \
	  echo " $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p"; \
	  $(INSTALL_DATA) $$d$$p $(DESTDIR)$(includedir)/xine/$$p; \
	done


###
# Remove them
#
uninstall-includeHEADERS:
	@$(NORMAL_UNINSTALL)
	list='$(include_HEADERS)'; for p in $$list; do \
	  rm -f $(DESTDIR)$(includedir)/xine/$$p; \
	done


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
