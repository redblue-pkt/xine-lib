##
## Process this file with automake to produce Makefile.in
##

EXTRA_DIST = xine.h.in

include_HEADERS  = xine.h

CONFIG_CLEAN_FILES = xine.h

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: install


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
