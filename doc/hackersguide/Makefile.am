include $(top_srcdir)/misc/Makefile.common

hackersguide_sgml = hackersguide.sgml \
		    intro.sgml \
		    library.sgml \
		    overview.sgml \
		    internals.sgml \
		    stream.sgml \
		    output.sgml

EXTRA_DIST = README $(hackersguide_sgml) architecture.fig library.fig overlays.fig 
docs_DOCS = hackersguide.html architecture.png library.png overlays.png

docdir = $(prefix)/share/doc/xine/hackersguide

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

hackersguide.html: $(hackersguide_sgml)
	sgmltools -b onehtml hackersguide.sgml

%.png: %.fig
	fig2dev -L png -S 4 $< $@
