include $(top_srcdir)/misc/Makefile.common

SUBDIRS = libdha vidix

EXTRA_DIST = video_out_directfb.c video_out_opengl.c

VIDIX_CFLAGS = -I$(top_builddir)/src/video_out/vidix \
	-I$(top_srcdir)/src/video_out/vidix

AM_CFLAGS = $(X_CFLAGS) \
	-DXINE_COMPILE $(SDL_CFLAGS) $(VIDIX_CFLAGS) \
	$(AALIB_CFLAGS) $(MLIB_CFLAGS)

libdir = $(XINE_PLUGINDIR)

if HAVE_X11
xshm_module = xineplug_vo_out_xshm.la
syncfb_module = xineplug_vo_out_syncfb.la
if HAVE_XV
xv_module = xineplug_vo_out_xv.la
endif
#if HAVE_OPENGL
#opengl_module = xineplug_vo_out_opengl.la
#endif
endif

if HAVE_VIDIX
vidix_module = xineplug_vo_out_vidix.la
endif

if HAVE_SUNFB
pgx64_module = xineplug_vo_out_pgx64.la
endif

if HAVE_AA
aa_module = xineplug_vo_out_aa.la
endif

if HAVE_FB
fb_module = xineplug_vo_out_fb.la
endif

#if HAVE_DIRECTFB
#directfb_module = xineplug_vo_out_directfb.la
#endif

if HAVE_SDL
sdl_module = xineplug_vo_out_sdl.la
endif



# important:
# 
# all xine video out plugins should be named  
# "xineplug_vo_out_*"

#lib_LTLIBRARIES = $(xshm_module) $(xv_module) $(directfb_module)  \
#                  $(syncfb_module) $(fb_module) $(opengl_module) $(sdl_module) $(vidix_module)
lib_LTLIBRARIES = $(xshm_module) $(xv_module) $(aa_module) $(pgx64_module) \
	$(vidix_module) $(fb_module) $(sdl_module) $(syncfb_module) \
	xineplug_vo_out_none.la

xineplug_vo_out_xv_la_SOURCES = deinterlace.c alphablend.c video_out_xv.c
xineplug_vo_out_xv_la_LIBADD = \
	$(XV_LIB) \
	$(X_LIBS) \
	-lXext \
	$(XINE_LIB)

xineplug_vo_out_xv_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_xshm_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_xshm.c
xineplug_vo_out_xshm_la_LIBADD =  $(X_LIBS) -lXext \
	$(XINE_LIB)
xineplug_vo_out_xshm_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_vo_out_opengl_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
#	alphablend.c video_out_opengl.c
#xineplug_vo_out_opengl_la_LIBADD =  $(OPENGL_LIBS) $(GLUT_LIBS) $(GLU_LIBS) $(X_LIBS) \
#	$(XINE_LIB)
#xineplug_vo_out_opengl_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_syncfb_la_SOURCES = alphablend.c video_out_syncfb.c 
xineplug_vo_out_syncfb_la_LIBADD = $(X_LIBS) $(XINE_LIB)
xineplug_vo_out_syncfb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_aa_la_SOURCES =  video_out_aa.c
xineplug_vo_out_aa_la_LIBADD = $(AALIB_LIBS)
xineplug_vo_out_aa_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_fb_la_SOURCES = yuv2rgb.c yuv2rgb_mmx.c yuv2rgb_mlib.c \
	alphablend.c video_out_fb.c
xineplug_vo_out_fb_la_LIBADD = $(XINE_LIB)
xineplug_vo_out_fb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

#xineplug_vo_out_directfb_la_SOURCES = alphablend.c video_out_directfb.c
#xineplug_vo_out_directfb_la_LIBADD = $(DIRECTFB_LIBS)
#xineplug_vo_out_directfb_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_sdl_la_SOURCES = alphablend.c video_out_sdl.c
xineplug_vo_out_sdl_la_LIBADD = $(SDL_LIBS) $(XINE_LIB)
xineplug_vo_out_sdl_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_vidix_la_SOURCES = alphablend.c video_out_vidix.c
xineplug_vo_out_vidix_la_LIBADD =  $(X_LIBS) \
	$(top_builddir)/src/video_out/vidix/libvidix.la \
	$(top_builddir)/src/video_out/libdha/libdha.la
xineplug_vo_out_vidix_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_pgx64_la_SOURCES = alphablend.c video_out_pgx64.c
xineplug_vo_out_pgx64_la_LIBADD = $(X_LIBS)
xineplug_vo_out_pgx64_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

xineplug_vo_out_none_la_SOURCES =  video_out_none.c
xineplug_vo_out_none_la_LDFLAGS = -avoid-version -module @XINE_PLUGIN_MIN_SYMS@

noinst_HEADERS = alphablend.h deinterlace.h video_out_syncfb.h yuv2rgb.h 
