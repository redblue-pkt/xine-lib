EXTRA_DIST = autogen.sh upload.pl SlackBuild.in SlackBuild build_rpms.sh \
	xine-lib.spec.in xine-lib.spec \
	debian/control debian/copyright debian/menu \
	debian/rules debian/changelog debian/xine.docs\
	xine_logo.png

bin_SCRIPTS = xine-config

datadir = $(XINE_SKINDIR)
data_DATA = xine_logo.png

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
