include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml
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

dist-hook:
	@make fail_if_missing=yes docs
	cp $(docs_DOCS) $(distdir)

faq.html: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  fi; \
	fi

faq.txt: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b txt faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  fi; \
	fi
