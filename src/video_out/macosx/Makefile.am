include $(top_srcdir)/misc/Makefile.common

if HAVE_MACOSX_VIDEO

AM_CFLAGS = $(X_CFLAGS) -DXINE_COMPILE \
        $(SUNDGA_CFLAGS) $(SDL_CFLAGS) $(VIDIX_CFLAGS) \
	$(AALIB_CFLAGS) $(MLIB_CFLAGS) $(LIBSTK_CFLAGS) \
	$(DIRECTFB_CFLAGS) $(DIRECTX_CFLAGS) $(CACA_CFLAGS)

macosx_module = xineplug_vo_out_macosx.la

lib_LTLIBRARIES = libxine_macosx_video.la

libxine_macosx_video_la_SOURCES = video_window.m
libxine_macosx_video_la_LIBADD = 
libxine_macosx_video_la_LDFLAGS = -version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)
libxine_macosx_video_la_OBJCFLAGS = $(OBJCFLAGS)

include_HEADERS = video_window.h

endif
