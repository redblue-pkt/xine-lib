include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = \
             video_window.h \
             XineOpenGLView.m \
             XineOpenGLView.h \
             XineVideoWindow.m \
             XineVideoWindow.h

if HAVE_MACOSX_VIDEO

lib_LTLIBRARIES = libxine_macosx_video.la

libxine_macosx_video_la_SOURCES = XineOpenGLView.m XineVideoWindow.m
# The "-Wl,-framework -Wl,..." is needed for libtool versions before
# 1.5.x (1.257): the default version that ships with Mac OS X is 1.5 (1.1220)
libxine_macosx_video_la_LDFLAGS = -version-info \
	$(LT_CURRENT):$(LT_REVISION):$(LT_AGE) \
	-Wl,-framework -Wl,Cocoa -framework Cocoa \
	-Wl,-framework -Wl,OpenGL -framework OpenGL
libxine_macosx_video_la_OBJCFLAGS = $(OBJCFLAGS)

include_HEADERS = video_window.h XineOpenGLView.h XineVideoWindow.h

endif

