include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_planar.la

xineplug_post_planar_la_SOURCES = planar.c invert.c expand.c boxblur.c \
                                  denoise3d.c eq.c eq2.c unsharp.c 
xineplug_post_planar_la_LIBADD = $(XINE_LIB) -lm
xineplug_post_planar_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

