include $(top_srcdir)/misc/Makefile.common

hackersguide_sgml = hackersguide.sgml \
		    intro.sgml \
		    library.sgml \
		    overview.sgml \
		    internals.sgml \
		    stream.sgml \
		    output.sgml

docs_DATA = hackersguide.html \
            architecture.png library.png overlays.png post_frame.png

EXTRA_DIST = README $(hackersguide_sgml) \
	architecture.fig library.fig overlays.fig post_frame.fig

docsdir = $(prefix)/share/doc/xine/hackersguide

docs: $(docs_DATA)

distclean-generic: clean-docs

docs-prepare: $(addprefix $(srcdir)/, $(hackersguide_sgml))
	for file in $(hackersguide_sgml); do \
	  test -f ./$$file || cp $(srcdir)/$$file .; \
	done

clean-docs:
	rm -f $(docs_DATA)
	test "x$(srcdir)" = x. || rm -f $(hackersguide_sgml)

dist-hook:
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

if HAVE_SGMLTOOLS
hackersguide.html: docs-prepare $(hackersguide_sgml)
	$(SGMLTOOLS) -b onehtml hackersguide.sgml;
else
hackersguide.html: docs-prepare $(hackersguide_sgml)
	if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install sgmltools-lite."; \
	  exit 1; \
	else \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif

if HAVE_FIG2DEV
%.png: $(srcdir)/%.fig
	$(FIG2DEV) -L png -S 4 $< $@;
else
%.png: $(srcdir)/%.fig
	if test x"$(fail_if_missing)" = x"yes"; then \
	  echo "Please install fig2dev."; \
	  exit 1; \
	else \
	  touch $@; \
	  sleep 1; \
	  touch $^; \
	fi
endif
