include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = zoom_filter_mmx.c zoom_filter_xmmx.c ppc_zoom_ultimate.s

## -fomit-frame-pointer segfaults here
CFLAGS = `echo @CFLAGS@ | sed -e 's/-fomit-frame-pointer//g'`

libdir = $(XINE_PLUGINDIR)/post

lib_LTLIBRARIES = xineplug_post_goom.la

if PPC_ARCH
## DON T WORK  extra_files = ppc_zoom_ultimate.s
else
## only compile if MMX is supported.
if HAVE_FFMMX
  extra_files = zoom_filter_mmx.c zoom_filter_xmmx.c
endif
endif

xineplug_post_goom_la_SOURCES = xine_goom.c \
			        goom_core.c \
			        filters.c \
			        lines.c \
			        ifs_display.c ifs.c \
			        graphic.c \
			        gfontlib.c \
				gfontrle.c \
				surf3d.c surface.c \
				tentacle3d.c \
				$(extra_files)

xineplug_post_goom_la_LIBADD = $(XINE_LIB)
xineplug_post_goom_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = zoom_filter_mmx.h
