##
## Process this file with automake to produce Makefile.in
##

EXTRA_DIST = xine.h.tmpl.in

include_HEADERS  = xine.h

xine.h: xine.h.tmpl
	@echo "creating xine.h"; \
	rm -f xine.h && \
	echo '/* !! DO NO EDIT THIS FILE, it is automatically generated */' \
	  > xine.h && \
	cat xine.h.tmpl >> xine.h


debug:


install-debug: install


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in xine.h
