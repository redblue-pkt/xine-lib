include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = foovideo.c

if HAVE_LIBPNG
image_module = xineplug_decode_image.la
endif

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = $(image_module) \
	xineplug_decode_cinepak.la \
	xineplug_decode_cyuv.la \
	xineplug_decode_fli.la \
	xineplug_decode_idcinvideo.la \
	xineplug_decode_msrle.la \
	xineplug_decode_msvc.la \
	xineplug_decode_qtsmc.la \
	xineplug_decode_roqvideo.la \
	xineplug_decode_rgb.la \
	xineplug_decode_svq1.la \
	xineplug_decode_yuv.la \
	xineplug_decode_qtrpza.la \
	xineplug_decode_wc3video.la \
	xineplug_decode_qtrle.la \
	xineplug_decode_interplayvideo.la \
	xineplug_decode_yuv_frames.la

xineplug_decode_cinepak_la_SOURCES = cinepak.c
xineplug_decode_cinepak_la_LIBADD = $(XINE_LIB)
xineplug_decode_cinepak_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_cyuv_la_SOURCES = cyuv.c
xineplug_decode_cyuv_la_LIBADD = $(XINE_LIB)
xineplug_decode_cyuv_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_msvc_la_SOURCES = msvc.c
xineplug_decode_msvc_la_LIBADD = $(XINE_LIB)
xineplug_decode_msvc_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_roqvideo_la_SOURCES = roqvideo.c
xineplug_decode_roqvideo_la_LIBADD = $(XINE_LIB)
xineplug_decode_roqvideo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_svq1_la_SOURCES = svq1.c svq1_codebooks.h
xineplug_decode_svq1_la_LIBADD = $(XINE_LIB)
xineplug_decode_svq1_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_fli_la_SOURCES = fli.c
xineplug_decode_fli_la_LIBADD = $(XINE_LIB)
xineplug_decode_fli_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_rgb_la_SOURCES = rgb.c
xineplug_decode_rgb_la_LIBADD = $(XINE_LIB)
xineplug_decode_rgb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_yuv_la_SOURCES = yuv.c
xineplug_decode_yuv_la_LIBADD = $(XINE_LIB)
xineplug_decode_yuv_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_msrle_la_SOURCES = msrle.c
xineplug_decode_msrle_la_LIBADD = $(XINE_LIB)
xineplug_decode_msrle_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_idcinvideo_la_SOURCES = idcinvideo.c
xineplug_decode_idcinvideo_la_LIBADD = $(XINE_LIB)
xineplug_decode_idcinvideo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_qtsmc_la_SOURCES = qtsmc.c
xineplug_decode_qtsmc_la_LIBADD = $(XINE_LIB)
xineplug_decode_qtsmc_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_qtrpza_la_SOURCES = qtrpza.c
xineplug_decode_qtrpza_la_LIBADD = $(XINE_LIB)
xineplug_decode_qtrpza_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_wc3video_la_SOURCES = wc3video.c
xineplug_decode_wc3video_la_LIBADD = $(XINE_LIB)
xineplug_decode_wc3video_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_qtrle_la_SOURCES = qtrle.c
xineplug_decode_qtrle_la_LIBADD = $(XINE_LIB)
xineplug_decode_qtrle_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_interplayvideo_la_SOURCES = interplayvideo.c
xineplug_decode_interplayvideo_la_LIBADD = $(XINE_LIB)
xineplug_decode_interplayvideo_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_yuv_frames_la_SOURCES = yuv_frames.c
xineplug_decode_yuv_frames_la_LIBADD = $(XINE_LIB)
xineplug_decode_yuv_frames_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_image_la_SOURCES = image.c
xineplug_decode_image_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS) $(PNG_LIBS)
xineplug_decode_image_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = svq1_codebooks.h
