include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = $(docs_DOCS) README faq.sgml
docs_DOCS = faq.html faq.txt

docdir = $(prefix)/share/doc/xine/faq

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

docs: $(docs_DOCS)

faq.html: faq.sgml
	sgmltools -b onehtml faq.sgml

faq.txt: faq.sgml
	sgmltools -b txt faq.sgml
