include $(top_srcdir)/misc/Makefile.common

SUBDIRS = contrib include

EXTRA_DIST = \
	scripts/move_xine_fonts.bat \
	README \
	libdvdnav.def \
	libdvdread.def \
	libxine.def \
	libxinesupport.def \
	xineplug_decode_spu.def \
	xine_plugin.def \
	libxine.dsw \
	xine.dsw


dist-hook:
	cp $(srcdir)/*.dsp $(distdir)
