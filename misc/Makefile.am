SUBDIRS = fonts

EXTRA_DIST = autogen.sh SlackBuild.in SlackBuild build_rpms.sh \
	xine-lib.spec.in xine-lib.spec xine-fontconv.c \
	xine_logo.mpv vga.xinefont.gz

datadir = $(XINE_SKINDIR)
data_DATA = xine_logo.png xine_logo.mpv

bin_SCRIPTS = xine-config

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in

xine-fontconv:	xine-fontconv.c
	gcc -g -o xine-fontconv xine-fontconv.c -lfreetype -lz -I/usr/include/freetype2
