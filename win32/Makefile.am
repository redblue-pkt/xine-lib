include $(top_srcdir)/misc/Makefile.common

SUBDIRS = contrib

EXTRA_DIST = \
	include/msvc/config.h \
	include/msvc/inttypes.h \
	include/msvc/stdint.h \
	include/netinet/in.h \
	include/sys/time.h \
	include/sys/socket.h \
	include/dlfcn.h \
	include/netdb.h \
	include/pwd.h \
	include/unistd.h \
	include/xinesuppt.h \
	scripts/move_xine_fonts.bat \
	README \
	libdvdnav.def \
	libdvdread.def \
	libxine.def \
	libxinesupport.def \
	xineplug_decode_spu.def \
	xine_plugin.def \
	libxine.dsw \
	xine.dsw \
	config.h


dist-hook:
	cp $(srcdir)/*.dsp $(distdir)
