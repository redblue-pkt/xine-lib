SUBDIRS = fonts

EXTRA_DIST = autogen.sh \
	build_rpms.sh \
	SlackBuild.in \
	SlackBuild \
	vga.xinefont.gz \
	xine-fontconv.c \
	xine-lib.spec.in \
	xine-lib.spec 

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
	$(CC) -g -o xine-fontconv xine-fontconv.c -lfreetype -lz -I/usr/include/freetype2
