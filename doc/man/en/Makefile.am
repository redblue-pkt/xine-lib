SUBDIRS = man3

man_MANS = xine-lib.3

EXTRA_DIST = $(man_MANS) xine-lib.3.in

##xine-lib-man:
##	cat xine-lib.3.in > xine-lib.3 && \
##	(for f in `ls man3/*.3`; do \
##	  (echo -e ".B $$f\n" | sed -e 's/man3\///g' -e 's/\.3//g'); \
##	done) >> xine-lib.3

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
