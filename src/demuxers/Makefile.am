AM_CFLAGS = $(OGG_CFLAGS) @ANSI_FLAGS@

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

libdir = $(XINE_PLUGINDIR)

# Sensing of OGG/VORBIS, ZLIB and ASF is broken in cvscompile.sh.

if HAVE_VORBIS
ogg_module = xineplug_dmx_ogg.la
endif

if BUILD_ASF
asf_module = xineplug_dmx_asf.la 
endif

if HAVE_LIBMNG
mng_module = xineplug_dmx_mng.la
endif

##
# IMPORTANT:
# ---------
# All of xine demuxer plugins should be named like the scheme "xineplug_dmx_"

XINELIB = $(top_builddir)/src/xine-engine/libxine.la

lib_LTLIBRARIES = $(ogg_module) $(asf_module) $(mng_module) \
	xineplug_dmx_games.la \
	xineplug_dmx_audio.la \
	xineplug_dmx_mpeg_ts.la \
	xineplug_dmx_avi.la\
	xineplug_dmx_mpeg_block.la \
	xineplug_dmx_mpeg.la \
	xineplug_dmx_mpeg_elem.la \
	xineplug_dmx_qt.la \
	xineplug_dmx_fli.la \
	xineplug_dmx_yuv4mpeg2.la \
	xineplug_dmx_real.la \
	xineplug_dmx_rawdv.la \
	xineplug_dmx_pva.la \
	xineplug_dmx_yuv_frames.la

xineplug_dmx_ogg_la_SOURCES = demux_ogg.c
xineplug_dmx_ogg_la_LIBADD = $(OGG_LIBS) $(VORBIS_LIBS) $(XINELIB)
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

xineplug_dmx_fli_la_SOURCES = demux_fli.c
xineplug_dmx_fli_la_LIBADD = $(XINELIB)
xineplug_dmx_fli_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_yuv4mpeg2_la_SOURCES = demux_yuv4mpeg2.c
xineplug_dmx_yuv4mpeg2_la_LIBADD = $(XINELIB)
xineplug_dmx_yuv4mpeg2_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_real_la_SOURCES = demux_real.c
xineplug_dmx_real_la_LIBADD = $(XINELIB)
xineplug_dmx_real_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_rawdv_la_SOURCES = demux_rawdv.c
xineplug_dmx_rawdv_la_LIBADD = $(XINELIB)
xineplug_dmx_rawdv_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_mng_la_SOURCES = demux_mng.c
xineplug_dmx_mng_la_LIBADD = $(XINELIB) $(ZLIB_LIBS) $(MNG_LIBS)
xineplug_dmx_mng_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_pva_la_SOURCES = demux_pva.c
xineplug_dmx_pva_la_LIBADD = $(XINELIB)
xineplug_dmx_pva_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_games_la_SOURCES = group_games.c demux_eawve.c \
				demux_idcin.c demux_ipmovie.c demux_roq.c \
				demux_vqa.c demux_wc3movie.c demux_str.c \
				demux_film.c demux_smjpeg.c
xineplug_dmx_games_la_LIBADD = $(XINELIB)
xineplug_dmx_games_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_audio_la_SOURCES = group_audio.c demux_aud.c demux_aiff.c \
				demux_cdda.c demux_mpgaudio.c demux_nsf.c \
				demux_realaudio.c demux_snd.c demux_voc.c \
				demux_vox.c demux_wav.c
xineplug_dmx_audio_la_LIBADD = $(XINELIB)
xineplug_dmx_audio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_dmx_yuv_frames_la_SOURCES = demux_yuv_frames.c
xineplug_dmx_yuv_frames_la_LIBADD = $(XINELIB)
xineplug_dmx_yuv_frames_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@


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

