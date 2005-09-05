include $(top_srcdir)/misc/Makefile.common

SUBDIRS = include

EXTRA_DIST = \
	scripts/move_xine_fonts.bat \
	scripts/ffmpeg_win32.patch \
	config.h \
	README \
	libdvdnav.def \
	libxine.def \
	libxinesupport.def \
	xine_plugin.def \
	libxine.dsw \
	xine.dsw


dist-hook:
	cp $(srcdir)/*.dsp $(distdir)
