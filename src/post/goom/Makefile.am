include $(top_srcdir)/misc/Makefile.common

libdir = $(XINE_PLUGINDIR)/post

EXTRA_DIST = mmx.c xmmx.c ppc_drawings.s ppc_zoom_ultimate.s

## -fomit-frame-pointer segfaults here
CFLAGS = `echo @CFLAGS@ | sed -e 's/-fomit-frame-pointer//g;s/-Os//g'`

lib_LTLIBRARIES = xineplug_post_goom.la

## doesn't work
#if PPC_ARCH
#extra_files = ppc_drawings.s ppc_zoom_ultimate.s
#AM_CPPFLAGS = -DCPU_POWERPC
#endif

if HAVE_FFMMX
extra_files = mmx.c xmmx.c
AM_CPPFLAGS = -DHAVE_MMX
endif

xineplug_post_goom_la_SOURCES = $(extra_files) xine_goom.c \
	config_param.c convolve_fx.c cpu_info.c drawmethods.c filters.c flying_stars_fx.c \
	gfontlib.c gfontrle.c goom_core.c goom_hash.c goom_script.c goom_script_lex.c \
	goom_script_yacc.c goom_tools.c graphic.c ifs.c lines.c mathtools.c \
	plugin_info.c sound_tester.c surf3d.c tentacle3d.c v3d.c
xineplug_post_goom_la_LIBADD = $(XINE_LIB)
xineplug_post_goom_la_LDFLAGS = -avoid-version -module $(XINE_PLUGIN_MIN_SYMS)

noinst_HEADERS = cpu_info.h default_scripts.h drawmethods.h gfontlib.h gfontrle.h goom.h \
	goom_config.h goom_config_param.h goom_filters.h goom_fx.h goom_graphic.h goom_hash.h \
	goom_plugin_info.h goom_script.h goom_script_scanner.h goom_script_yacc.h \
	goom_tools.h goom_typedefs.h goom_visual_fx.h ifs.h lines.h mathtools.h mmx.h \
	ppc_drawings.h ppc_zoom_ultimate.h sound_tester.h surf3d.h tentacle3d.h v3d.h
