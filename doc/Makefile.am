
SUBDIRS = man hackersguide

EXTRA_DIST = $(docs_DOCS) README dataflow.dia

docdir = $(prefix)/share/doc/xine
docs_DOCS = 

install-data-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(DESTDIR)$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	$(mkinstalldirs) $$dir; \
	$(INSTALL_DATA) $$doc $$dir/$$name; \
	  echo "installing $$doc as $$dir/$$name"; \
	done

uninstall-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(DESTDIR)$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	rm -f $$dir/$$name; \
	  echo "removing $$dir/$$name" ; \
	done

debug:
install-debug:

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
