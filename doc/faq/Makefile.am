
EXTRA_DIST = $(docs_DOCS) README faq.sgml
docdir = $(prefix)/share/doc/xine/faq
docs_DOCS = faq.html faq.txt


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
install-debug: install

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
