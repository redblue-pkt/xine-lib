EXTRA_DIST = autogen.sh upload.pl SlackBuild.in SlackBuild build_rpms.sh \
	xine-lib.spec.in xine-lib.spec guenter.spec.in guenter.spec\
	xine_logo.png vga.xinefont.gz

bin_SCRIPTS = xine-config

datadir = $(XINE_SKINDIR)
#this is just a hack. osd fonts should be installed in another dir
data_DATA = xine_logo.png vga.xinefont.gz

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
