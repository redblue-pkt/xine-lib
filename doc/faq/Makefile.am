include $(top_srcdir)/misc/Makefile.common

EXTRA_DIST = README faq.sgml
docs_DATA = $(srcdir)/faq.html $(srcdir)/faq.txt

docsdir = $(prefix)/share/doc/xine/faq

docs: $(docs_DATA)

dist-hook:
	@touch $(addprefix $(srcdir)/, $(EXTRA_DIST))
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

faq.html: $(srcdir)/faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml $(srcdir)/faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch faq.html; \
	  fi; \
	fi

faq.txt: $(srcdir)/faq.sgml
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b txt $(srcdir)/faq.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch faq.txt; \
	  fi; \
	fi
