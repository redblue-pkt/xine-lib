include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_audio_filter_upmix.la

xineplug_post_audio_filter_upmix_la_SOURCES = \
        upmix.c
xineplug_post_audio_filter_upmix_la_LIBADD = $(XINE_LIB)
xineplug_post_audio_filter_upmix_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@ -lm

