include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = @XVID_CFLAGS@

libdir = $(XINE_PLUGINDIR)


if HAVE_XVID
xvid_modules = xineplug_decode_xvid.la
endif

lib_LTLIBRARIES = $(xvid_modules)

xineplug_decode_xvid_la_SOURCES = xine_decoder.c
xineplug_decode_xvid_la_LIBADD = @XVID_LIBS@
xineplug_decode_xvid_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
