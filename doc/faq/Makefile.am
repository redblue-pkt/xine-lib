include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml
docs_DATA = faq.html faq.txt

docsdir = $(prefix)/share/doc/xine/faq

docs: docs-prepare $(docs_DATA)

docs-prepare: $(srcdir)/faq.sgml
	test -f faq.sgml || cp $^ .

dist-hook:
	@touch $(addprefix $(srcdir)/, $(EXTRA_DIST))
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

faq.html: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch $@; \
	  fi; \
	fi

faq.txt: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b txt faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch $@; \
	  fi; \
	fi
