CFLAGS = @BUILD_LIB_STATIC@ @GLOBAL_CFLAGS@

noinst_LTLIBRARIES = libdemux.la

#libdemux_la_SOURCES = demux_avi.c demux_mpeg_block.c demux_mpeg.c \
#	demux_mpgaudio.c demux_elem.c
libdemux_la_SOURCES = demux_avi.c demux_mpeg_block.c demux_mpeg.c
#libdemux_la_DEPENDENCIES = libsdeps
#libdemux_la_LIBADD = $(top_builddir)/libmpg123/libmpg123.la

include_HEADERS = demux.h

#libsdeps:
#	@cd $(top_builddir)/libmpg123 && $(MAKE) libmpg123.la

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
	$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in

