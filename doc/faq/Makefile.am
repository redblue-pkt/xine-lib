include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml

docs_DATA = faq.html faq.txt

docsdir = $(prefix)/share/doc/xine/faq

docs: $(docs_DATA)

distclean-generic: clean-docs

clean-docs:
	rm -f $(docs_DATA)
	test "x$(srcdir)" = x. || rm -f faq.sgml

dist-hook:
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

faq.sgml: $(srcdir)/faq.sgml
	cp $^ .

if HAVE_SGMLTOOLS
faq.html: faq.sgml
	  $(SGMLTOOLS) -b onehtml faq.sgml;
else
faq.html: faq.sgml
	  if test x"$(fail_if_missing)" = x"yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else \
	    touch $@; \
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
	    touch $^; \
	  fi
endif
