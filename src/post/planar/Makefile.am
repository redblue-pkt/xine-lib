include $(top_srcdir)/misc/Makefile.common

POSTPROC_INT_LIB = $(top_builddir)/src/libffmpeg/libavcodec/libpostproc/libpostprocess.la

if HAVE_FFMPEG
postproc_lib = $(FFMPEG_POSTPROC_LIBS)
ff_cppflags = $(FFMPEG_CPPFLAGS)
else
ff_cppflags = -I$(top_srcdir)/src/libffmpeg/libavcodec/libpostproc
postproc_lib = $(POSTPROC_INT_LIB)
postproc_dep = $(postproc_lib)
endif

# -fomit-frame-pointer is always needed. it might cause debug to not
# work, but at least it compiles.
AM_CPPFLAGS = $(ff_cppflags) -fomit-frame-pointer

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_planar.la

xineplug_post_planar_la_SOURCES = planar.c invert.c expand.c fill.c boxblur.c \
                                  denoise3d.c eq.c eq2.c unsharp.c pp.c
xineplug_post_planar_la_DEPENDENCIES = $(postproc_dep)
xineplug_post_planar_la_LIBADD = $(XINE_LIB) $(postproc_lib) -lm $(THREAD_LIBS)
xineplug_post_planar_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

$(POSTPROC_INT_LIB):
	cd $(top_builddir)/src/libffmpeg/libavcodec/libpostproc && $(MAKE) libpostprocess.la
