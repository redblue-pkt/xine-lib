include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = fooviz.c

AM_CFLAGS = @ANSI_FLAGS@

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_visualizations.la

xineplug_post_visualizations_la_SOURCES = \
        visualizations.c fftscope.c oscope.c
xineplug_post_visualizations_la_LIBADD = $(XINE_LIB)
xineplug_post_visualizations_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@ -lm
