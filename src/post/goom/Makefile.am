include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = zoom_filter_mmx.c ppc_zoom_ultimate.s

## -fomit-frame-pointer segfaults here
CFLAGS = `echo @CFLAGS@ | sed -e 's/-fomit-frame-pointer//g'`

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_goom.la

if PPC_ARCH
## DON T WORK  extra_files = ppc_zoom_ultimate.s
else
## only compile if MMX is supported.
if HAVE_FFMMX
  extra_files = zoom_filter_mmx.c
endif
endif

xineplug_post_goom_la_SOURCES = xine_goom.c \
			        goom_core.c goom_core.h \
			        goom_tools.h goom_config.h \
			        filters.c filters.h \
			        lines.c lines.h \
			        ifs_display.c ifs.c ifs.h \
			        graphic.c graphic.h \
			        drawmethods.h $(extra_files)

xineplug_post_goom_la_LIBADD = $(XINE_LIB)
xineplug_post_goom_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@
