include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = foovideo.c

if HAVE_LIBPNG
image_module = xineplug_decode_image.la
endif

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = $(image_module) \
	xineplug_decode_bitplane.la \
	xineplug_decode_rgb.la \
	xineplug_decode_yuv.la

xineplug_decode_bitplane_la_SOURCES = bitplane.c
xineplug_decode_bitplane_la_LIBADD = $(XINE_LIB)
xineplug_decode_bitplane_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_rgb_la_SOURCES = rgb.c
xineplug_decode_rgb_la_LIBADD = $(XINE_LIB)
xineplug_decode_rgb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_yuv_la_SOURCES = yuv.c
xineplug_decode_yuv_la_LIBADD = $(XINE_LIB)
xineplug_decode_yuv_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_decode_image_la_SOURCES = image.c
xineplug_decode_image_la_LIBADD = $(XINE_LIB) $(DYNAMIC_LD_LIBS) $(PNG_LIBS)
xineplug_decode_image_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
