CFLAGS = @GLOBAL_CFLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

##
# IMPORTANT:
# ---------
# All of xine demuxer plugins should be named like the scheme "xineplug_dmx_"
#
lib_LTLIBRARIES = xineplug_dmx_avi.la xineplug_dmx_mpeg_block.la \
	xineplug_dmx_mpeg.la xineplug_dmx_mpeg_elem.la \
	xineplug_dmx_mpeg_audio.la xineplug_dmx_mpeg_pes.la \
	xineplug_dmx_mpeg_ts.la xineplug_dmx_qt.la 

xineplug_dmx_avi_la_SOURCES = demux_avi.c
xineplug_dmx_avi_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_block_la_SOURCES = demux_mpeg_block.c
xineplug_dmx_mpeg_block_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_la_SOURCES = demux_mpeg.c
xineplug_dmx_mpeg_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_audio_la_SOURCES = demux_mpgaudio.c
xineplug_dmx_mpeg_audio_la_LIBADD = -lm
xineplug_dmx_mpeg_audio_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_elem_la_SOURCES = demux_elem.c
xineplug_dmx_mpeg_elem_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_pes_la_SOURCES = demux_pes.c
xineplug_dmx_mpeg_pes_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_ts_la_SOURCES = demux_ts.c
xineplug_dmx_mpeg_ts_la_LDFLAGS = -avoid-version -module

xineplug_dmx_qt_la_SOURCES = demux_qt.c
xineplug_dmx_qt_la_LDFLAGS = -avoid-version -module
xineplug_dmx_qt_la_LIBADD = -lz

include_HEADERS = demux.h

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

