EXTRA_DIST = xine.h.in

include_HEADERS  = xine.h

CONFIG_CLEAN_FILES = xine.h

debug:
       @$(MAKE) CFLAGS="$(DEBUG_CFLAGS)"

install-debug: debug
       @$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
       -rm -f *~ \#* .*~ .\#* 

maintainer-clean-generic:
       -@echo "This command is intended for maintainers to use;"
       -@echo "it deletes files that may require special tools to rebuild."
       -rm -f Makefile.in
