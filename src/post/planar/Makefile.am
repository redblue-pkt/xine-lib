include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = @ANSI_FLAGS@

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_invert.la xineplug_post_expand.la

xineplug_post_invert_la_SOURCES = invert.c
xineplug_post_invert_la_LIBADD = $(XINE_LIB)
xineplug_post_invert_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_post_expand_la_SOURCES = expand.c
xineplug_post_expand_la_LIBADD = $(XINE_LIB)
xineplug_post_expand_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

