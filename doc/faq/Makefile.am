include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml
docs_DATA = faq.html faq.txt

docsdir = $(prefix)/share/doc/xine/faq

docs: $(docs_DATA)

dist-hook:
	@touch $(EXTRA_DIST)
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

faq.html: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch faq.html; \
	  fi; \
	fi

faq.txt: faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b txt faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch faq.txt; \
	  fi; \
	fi
