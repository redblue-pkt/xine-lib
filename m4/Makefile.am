##
## Process this file with automake to produce Makefile.in
##
if INSTALL_M4
m4datadir = @ACLOCAL_DIR@
m4data_DATA = xine.m4
endif

EXTRA_DIST = _xine.m4 alsa.m4 esd.m4 xine.m4

debug:

install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
