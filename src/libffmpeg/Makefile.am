include $(top_srcdir)/misc/Makefile.common

if HAVE_FFMPEG
FF_CPPFLAGS = $(FFMPEG_CPPFLAGS)
link_ffmpeg = $(FFMPEG_LIBS)
else
link_ffmpeg = \
  $(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
  $(top_builddir)/src/libffmpeg/libavcodec/libpostproc/libpostprocess.la
SUBDIRS = libavcodec
endif

# this must always be included, even if the current machine has no DXR3...
EXTRA_DIST = xine_encoder.c diff_to_ffmpeg_cvs.txt

INTERNAL_DOCS = diff_to_ffmpeg_cvs.txt

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la xineplug_decode_dvaudio.la

if HAVE_DXR3
AM_CPPFLAGS = -I$(top_srcdir)/src/dxr3 $(X_CFLAGS) $(FF_CPPFLAGS)
xineplug_decode_ff_la_SOURCES = xine_decoder.c audio_decoder.c video_decoder.c \
                                xine_encoder.c mpeg_parser.c
# The dxr3 uses ffmpegs MPEG encoder by dlopen()ing the ffmpeg plugin and
# dlsym()ing the necessary function. Therefore we must allow more exported
# symbols and cannot use @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module
else
AM_CPPFLAGS = $(FF_CPPFLAGS)
xineplug_decode_ff_la_SOURCES = xine_decoder.c audio_decoder.c video_decoder.c
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
endif

xineplug_decode_ff_la_LIBADD = $(MLIB_LIBS) $(XINE_LIB) -lm $(ZLIB_LIBS) \
	$(link_ffmpeg)

xineplug_decode_dvaudio_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_dvaudio_la_SOURCES = dvaudio_decoder.c
xineplug_decode_dvaudio_la_LIBADD  = $(XINE_LIB)

noinst_HEADERS = xine_decoder.h mpeg_parser.h
