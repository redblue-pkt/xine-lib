include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = -I$(top_srcdir)/src/libffmpeg/libavcodec/libpostproc
POSTPROC_LIB  = $(top_builddir)/src/libffmpeg/libavcodec/libpostproc/libpostprocess.la

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_planar.la

xineplug_post_planar_la_SOURCES = planar.c invert.c expand.c fill.c boxblur.c \
                                  denoise3d.c eq.c eq2.c unsharp.c pp.c
xineplug_post_planar_la_DEPENDENCIES = $(POSTPROC_LIB)
xineplug_post_planar_la_LIBADD = $(XINE_LIB) $(POSTPROC_LIB) -lm
xineplug_post_planar_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

$(POSTPROC_LIB):
	cd $(top_builddir)/src/libffmpeg/libavcodec/libpostproc && $(MAKE) libpostprocess.la
