
SUBDIRS = man

EXTRA_DIST = $(docs_DOCS) MRL.txt dataflow.dia

docdir = $(prefix)/share/doc/xine
docs_DOCS = README README.syncfb README.freebsd FAQ bug_report_form


install-data-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	$(mkinstalldirs) $$dir; \
	$(INSTALL_DATA) $$doc $$dir/$$name; \
	  echo "installing $$doc as $$dir/$$name"; \
	done


uninstall-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	rm -f $$dir/$$name; \
	  echo "removing $$dir/$$name" ; \
	done


debug:

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
