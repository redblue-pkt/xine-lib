include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = video_window.m video_window.h

if HAVE_MACOSX_VIDEO

AM_CFLAGS = $(X_CFLAGS) -DXINE_COMPILE \
       $(SUNDGA_CFLAGS) $(SDL_CFLAGS) $(VIDIX_CFLAGS) \
       $(AALIB_CFLAGS) $(MLIB_CFLAGS) $(LIBSTK_CFLAGS) \
       $(DIRECTFB_CFLAGS) $(DIRECTX_CFLAGS) $(CACA_CFLAGS)

macosx_module = xineplug_vo_out_macosx.la

lib_LTLIBRARIES = libxine_macosx_video.la

libxine_macosx_video_la_SOURCES = video_window.m
libxine_macosx_video_la_LIBADD =
# The -Wl,-framework -Wl,Cocoa is needed for libtool versions before
# 1.5.x (1.257): the default version that ships with Mac OS X is 1.5 (1.1220)
libxine_macosx_video_la_LDFLAGS = -version-info \
	$(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-Wl,-framework -Wl,Cocoa -framework Cocoa \
	-Wl,-framework -Wl,OpenGL -framework OpenGL \
libxine_macosx_video_la_OBJCFLAGS = $(OBJCFLAGS)

include_HEADERS = video_window.h
endif
