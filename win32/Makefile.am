include $(top_srcdir)/misc/Makefile.common

SUBDIRS = contrib include

EXTRA_DIST = \
	scripts/move_xine_fonts.bat \
	README \
	libdvdnav.def \
	libxine.def \
	libxinesupport.def \
	xine_plugin.def \
	libxine.dsw \
	xine.dsw


dist-hook:
	cp $(srcdir)/*.dsp $(distdir)
