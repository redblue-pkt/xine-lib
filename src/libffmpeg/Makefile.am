include $(top_srcdir)/misc/Makefile.common

SUBDIRS = libavcodec 

# I don't think this should include internal docs that are not interesting
# for end users at all... -siggi
EXTRA_DIST =

INTERNAL_DOCS = diff_to_ffmpeg_cvs.txt

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

if HAVE_DXR3
AM_CPPFLAGS = -I$(top_srcdir)/src/dxr3
xineplug_decode_ff_la_SOURCES = xine_decoder.c xine_encoder.c
# The dxr3 uses ffmpegs MPEG encoder by dlopen()ing the ffmpeg plugin and
# dlsym()ing the necessary function. Therefore we must allow more exported
# symbols and cannot use @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module
else
xineplug_decode_ff_la_SOURCES = xine_decoder.c
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
endif

xineplug_decode_ff_la_LIBADD = $(XINE_LIB) \
	$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
	$(top_builddir)/src/libffmpeg/libavcodec/libpostproc/libpostprocess.la
