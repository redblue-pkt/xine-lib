include $(top_srcdir)/misc/Makefile.common

SUBDIRS = libavcodec 

# I don't think this should include internal docs that are not interesting
# for end users at all... -siggi
EXTRA_DIST =

INTERNAL_DOCS = diff_to_ffmpeg_cvs.txt

libdir = $(XINE_PLUGINDIR)

lib_LTLIBRARIES = xineplug_decode_ff.la

xineplug_decode_ff_la_SOURCES = xine_decoder.c
xineplug_decode_ff_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
xineplug_decode_ff_la_LIBADD = $(XINE_LIB) \
	$(top_builddir)/src/libffmpeg/libavcodec/libavcodec.la \
	$(top_builddir)/src/libffmpeg/libavcodec/libpostproc/libpostprocess.la
