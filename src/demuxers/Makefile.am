CFLAGS = @CFLAGS@ $(OGG_CFLAGS)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)


if HAVE_VORBIS
ogg_module = xineplug_dmx_ogg.la
endif

if HAVE_ZLIB
qt_modules = xineplug_dmx_qt.la
endif

if BUILD_ASF
asf_module = xineplug_dmx_asf.la 
endif

##
# IMPORTANT:
# ---------
# All of xine demuxer plugins should be named like the scheme "xineplug_dmx_"
#
lib_LTLIBRARIES = $(ogg_module) $(qt_modules) $(asf_module) xineplug_dmx_avi.la \
	xineplug_dmx_mpeg_block.la xineplug_dmx_mpeg.la \
	xineplug_dmx_mpeg_elem.la xineplug_dmx_mpeg_audio.la \
	xineplug_dmx_mpeg_pes.la xineplug_dmx_mpeg_ts.la \
	xineplug_dmx_cda.la

xineplug_dmx_ogg_la_SOURCES = demux_ogg.c
xineplug_dmx_ogg_la_LIBADD = $(OGG_LIBS) \
	$(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_ogg_la_LDFLAGS = -avoid-version -module

xineplug_dmx_avi_la_SOURCES = demux_avi.c
xineplug_dmx_avi_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_avi_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_block_la_SOURCES = demux_mpeg_block.c
xineplug_dmx_mpeg_block_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_block_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_la_SOURCES = demux_mpeg.c
xineplug_dmx_mpeg_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_audio_la_SOURCES = demux_mpgaudio.c
xineplug_dmx_mpeg_audio_la_LIBADD = -lm $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_audio_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_elem_la_SOURCES = demux_elem.c
xineplug_dmx_mpeg_elem_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_elem_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_pes_la_SOURCES = demux_pes.c
xineplug_dmx_mpeg_pes_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_pes_la_LDFLAGS = -avoid-version -module

xineplug_dmx_mpeg_ts_la_SOURCES = demux_ts.c
xineplug_dmx_mpeg_ts_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_mpeg_ts_la_LDFLAGS = -avoid-version -module

xineplug_dmx_qt_la_SOURCES = demux_qt.c
xineplug_dmx_qt_la_LDFLAGS = -avoid-version -module
xineplug_dmx_qt_la_LIBADD = -lz $(top_builddir)/src/xine-engine/libxine.la

xineplug_dmx_asf_la_SOURCES = demux_asf.c
xineplug_dmx_asf_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_asf_la_LDFLAGS = -avoid-version -module

xineplug_dmx_cda_la_SOURCES = demux_cda.c
xineplug_dmx_cda_la_LIBADD = $(top_builddir)/src/xine-engine/libxine.la
xineplug_dmx_cda_la_LDFLAGS = -avoid-version -module

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

