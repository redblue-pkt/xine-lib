SUBDIRS = fonts

EXTRA_DIST = autogen.sh upload.pl SlackBuild.in SlackBuild build_rpms.sh \
	xine-lib.spec.in xine-lib.spec \
	xine_logo.png xine_logo.zyuy2 xine-fontconv.c xine-logoconv.c

bin_SCRIPTS = xine-config

datadir = $(XINE_SKINDIR)
data_DATA = xine_logo.png xine_logo.zyuy2

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
