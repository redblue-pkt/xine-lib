include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml

docs_DATA = faq.html faq.txt

docsdir = $(prefix)/share/doc/xine/faq

docs: docs-prepare $(docs_DATA)

distclean-generic: clean-docs

docs-prepare: $(srcdir)/faq.sgml
	test -f ./faq.sgml || cp $^ .

clean-docs:
	rm -f $(docs_DATA)
	test "x$(srcdir)" = x. || rm -f faq.sgml

dist-hook:
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

if HAVE_SGMLTOOLS
faq.html: docs-prepare faq.sgml
	$(SGMLTOOLS) -b onehtml faq.sgml;
else
faq.html: docs-prepare faq.sgml
	if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install sgmltools-lite."; \
	  exit 1; \
	else \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif

if HAVE_SGMLTOOLS
faq.txt: faq.sgml
	$(SGMLTOOLS) -b txt faq.sgml;
else
faq.txt: faq.sgml
	if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install sgmltools-lite."; \
	  exit 1; \
	else \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif
