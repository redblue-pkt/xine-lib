SUBDIRS = fonts

EXTRA_DIST = build_rpms.sh \
	cdda_server.c \
	SlackBuild.in \
	SlackBuild \
	vga.xinefont.gz \
	xine-fontconv.c \
	xine-lib.spec.in \
	xine-lib.spec \
	libxine.pc.in \
	libdvdcss-1.2.6-network.patch

bin_SCRIPTS = xine-config

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA=libxine.pc

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in

xine-fontconv:	xine-fontconv.c
	$(CC) -W -Wall -g -o xine-fontconv xine-fontconv.c -lz `freetype-config --cflags` `freetype-config --libs`

cdda_server: cdda_server.c
	$(CC) -W -Wall -g -o cdda_server cdda_server.c -ldl
