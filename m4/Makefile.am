##
## Process this file with automake to produce Makefile.in
##
if INSTALL_M4
m4datadir = @ACLOCAL_DIR@
else
m4datadir = $(datadir)/aclocal
endif

m4data_DATA = xine.m4

EXTRA_DIST = \
	aa.m4 \
	alsa.m4 \
	arts.m4 \
	as.m4 \
	codeset.m4 \
	dvdnav.m4 \
	esd.m4 \
	gettext.m4 \
	glibc21.m4 \
	iconv.m4 \
	irixal.m4 \
	isc-posix.m4 \
	lcmessage.m4 \
	libfame.m4 \
	ogg.m4 \
	progtest.m4 \
	sdl.m4 \
	vorbis.m4 \
	xine.m4 \
	_xine.m4 \
	xvid.m4

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
