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

docs: docs-prepare $(docs_DATA)

docs-prepare: $(addprefix $(srcdir)/, $(hackersguide_sgml))
	for file in $(hackersguide_sgml); do \
	  test -f $$file || cp $(srcdir)/$$file .; \
	done

dist-hook:
	@touch $(addprefix $(srcdir)/, $(EXTRA_DIST))
	@make fail_if_missing=yes docs
	cp $(docs_DATA) $(distdir)

$(hackersguide_sgml): $(addprefix $(srcdir)/,$(hackersguide_sgml))
	cp $^ .

hackersguide.html: $(hackersguide_sgml)
	@if test "$(SGMLTOOLS)" != "no"; then \
	  $(SGMLTOOLS) -b onehtml hackersguide.sgml; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install sgmltools-lite."; \
	    exit 1; \
	  else touch $@; \
	  fi; \
	fi

%.png: $(srcdir)/%.fig
	@if test "$(FIG2DEV)" != "no"; then \
	  $(FIG2DEV) -L png -S 4 $< $@; \
	else if test "$(fail_if_missing)" = "yes"; then \
	    echo "Please install fig2dev."; \
	    exit 1; \
	  else touch $@; \
	  fi; \
	fi
