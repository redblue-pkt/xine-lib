AM_CFLAGS = $(OGG_CFLAGS) @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

# Sensing of OGG/VORBIS, ZLIB and ASF is broken in cvscompile.sh.

if HAVE_VORBIS
ogg_module = xineplug_dmx_ogg.la
endif

#if HAVE_ZLIB
#qt_modules = xineplug_dmx_qt.la
#endif

if BUILD_ASF
asf_module = xineplug_dmx_asf.la 
endif

##
# IMPORTANT:
# ---------
# All of xine demuxer plugins should be named like the scheme "xineplug_dmx_"

#lib_LTLIBRARIES = $(ogg_module) $(asf_module) $(qt_modules) xineplug_dmx_avi.la\
#	xineplug_dmx_mpeg_block.la xineplug_dmx_mpeg.la \
#	xineplug_dmx_mpeg_elem.la xineplug_dmx_mpeg_audio.la \
#	xineplug_dmx_cda.la xineplug_dmx_film.la \
#	xineplug_dmx_roq.la xineplug_dmx_fli.la \
#	xineplug_dmx_smjpeg.la xineplug_dmx_wav.la \
#	xineplug_dmx_idcin.la xineplug_dmx_wc3movie.la \
#	xineplug_dmx_vqa.la xineplug_dmx_voc.la \
#	xineplug_dmx_aiff.la xineplug_dmx_snd.la \
#	xineplug_dmx_yuv4mpeg2.la xineplug_dmx_real.la
#	xineplug_dmx_mpeg_pes.la xineplug_dmx_mpeg_ts.la

XINELIB = $(top_builddir)/src/xine-engine/libxine.la

lib_LTLIBRARIES = $(ogg_module) $(asf_module) xineplug_dmx_avi.la\
	xineplug_dmx_mpeg_block.la \
	xineplug_dmx_mpeg.la \
	xineplug_dmx_mpeg_audio.la \
	xineplug_dmx_mpeg_elem.la \
	xineplug_dmx_qt.la \
	xineplug_dmx_cda.la \
	xineplug_dmx_film.la \
	xineplug_dmx_roq.la \
	xineplug_dmx_fli.la \
	xineplug_dmx_smjpeg.la \
	xineplug_dmx_wav.la \
	xineplug_dmx_idcin.la \
	xineplug_dmx_wc3movie.la \
	xineplug_dmx_vqa.la \
	xineplug_dmx_voc.la \
	xineplug_dmx_aiff.la \
	xineplug_dmx_snd.la \
	xineplug_dmx_yuv4mpeg2.la \
	xineplug_dmx_real.la \
	xineplug_dmx_realaudio.la \
	xineplug_dmx_eawve.la \
	xineplug_dmx_mpeg_ts.la

xineplug_dmx_ogg_la_SOURCES = demux_ogg.c
xineplug_dmx_ogg_la_LIBADD = $(OGG_LIBS) $(VORBIS_LIBS)\
	$(XINELIB)
xineplug_dmx_ogg_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_avi_la_SOURCES = demux_avi.c
xineplug_dmx_avi_la_LIBADD = $(XINELIB)
xineplug_dmx_avi_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mpeg_block_la_SOURCES = demux_mpeg_block.c
xineplug_dmx_mpeg_block_la_LIBADD = $(XINELIB)
xineplug_dmx_mpeg_block_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mpeg_la_SOURCES = demux_mpeg.c
xineplug_dmx_mpeg_la_LIBADD = $(XINELIB)
xineplug_dmx_mpeg_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mpeg_audio_la_SOURCES = demux_mpgaudio.c
xineplug_dmx_mpeg_audio_la_LIBADD = -lm $(XINELIB)
xineplug_dmx_mpeg_audio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mpeg_elem_la_SOURCES = demux_elem.c
xineplug_dmx_mpeg_elem_la_LIBADD = $(XINELIB)
xineplug_dmx_mpeg_elem_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_dmx_mpeg_pes_la_SOURCES = demux_pes.c
#xineplug_dmx_mpeg_pes_la_LIBADD = $(XINELIB)
#xineplug_dmx_mpeg_pes_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mpeg_ts_la_SOURCES = demux_ts.c
xineplug_dmx_mpeg_ts_la_LIBADD = $(XINELIB)
xineplug_dmx_mpeg_ts_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_qt_la_SOURCES = demux_qt.c
xineplug_dmx_qt_la_LIBADD = $(XINELIB)
xineplug_dmx_qt_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_asf_la_SOURCES = demux_asf.c
xineplug_dmx_asf_la_LIBADD = $(XINELIB)
xineplug_dmx_asf_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_cda_la_SOURCES = demux_cda.c
xineplug_dmx_cda_la_LIBADD = $(XINELIB)
xineplug_dmx_cda_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_film_la_SOURCES = demux_film.c
xineplug_dmx_film_la_LIBADD = $(XINELIB)
xineplug_dmx_film_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_roq_la_SOURCES = demux_roq.c
xineplug_dmx_roq_la_LIBADD = $(XINELIB)
xineplug_dmx_roq_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_fli_la_SOURCES = demux_fli.c
xineplug_dmx_fli_la_LIBADD = $(XINELIB)
xineplug_dmx_fli_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_smjpeg_la_SOURCES = demux_smjpeg.c
xineplug_dmx_smjpeg_la_LIBADD = $(XINELIB)
xineplug_dmx_smjpeg_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_wav_la_SOURCES = demux_wav.c
xineplug_dmx_wav_la_LIBADD = $(XINELIB)
xineplug_dmx_wav_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_idcin_la_SOURCES = demux_idcin.c
xineplug_dmx_idcin_la_LIBADD = $(XINELIB)
xineplug_dmx_idcin_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_wc3movie_la_SOURCES = demux_wc3movie.c
xineplug_dmx_wc3movie_la_LIBADD = $(XINELIB)
xineplug_dmx_wc3movie_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_vqa_la_SOURCES = demux_vqa.c
xineplug_dmx_vqa_la_LIBADD = $(XINELIB)
xineplug_dmx_vqa_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_voc_la_SOURCES = demux_voc.c
xineplug_dmx_voc_la_LIBADD = $(XINELIB)
xineplug_dmx_voc_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_aiff_la_SOURCES = demux_aiff.c
xineplug_dmx_aiff_la_LIBADD = $(XINELIB)
xineplug_dmx_aiff_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_snd_la_SOURCES = demux_snd.c
xineplug_dmx_snd_la_LIBADD = $(XINELIB)
xineplug_dmx_snd_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_yuv4mpeg2_la_SOURCES = demux_yuv4mpeg2.c
xineplug_dmx_yuv4mpeg2_la_LIBADD = $(XINELIB)
xineplug_dmx_yuv4mpeg2_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_real_la_SOURCES = demux_real.c
xineplug_dmx_real_la_LIBADD = $(XINELIB)
xineplug_dmx_real_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_realaudio_la_SOURCES = demux_realaudio.c
xineplug_dmx_realaudio_la_LIBADD = $(XINELIB)
xineplug_dmx_realaudio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_eawve_la_SOURCES = demux_eawve.c
xineplug_dmx_eawve_la_LIBADD = $(XINELIB)
xineplug_dmx_eawve_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

include_HEADERS = demux.h
noinst_HEADERS = asfheader.h qtpalette.h

$(XINELIB):
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

