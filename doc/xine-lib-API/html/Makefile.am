docs_DOCS = \
	group__demux__strategy.html  modules.html                    group__event__group.html \
	structao__driver__t.html     group__loadplugins__group.html  structao__functions__t.html \
	group__mrl__types.html	     structcfg__data__t.html \
	annotated.html		     group__status__group.html	     structconfig__values__s.html \
	doxygen.css		     group__ui__callbacks.html	     structconfig__values__t.html \
	doxygen.gif		     group__version__group.html      structmrl__t.html \
	files.html		     group__video__cap.html	     structvo__driver__s.html \
	functions.html		     group__video__group.html	     structvo__driver__t.html \
	globals.html		     group__video__prop.html	     structvo__frame__t.html \
	group__audio__cap.html	     group__video__ratio.html	     structvo__overlay__t.html \
	group__audio__group.html     group__visual__types.html	     structxine__t.html \
	group__audio__prop.html      group__xine__api.html	     xine_8h-source.html \
	group__autoplay__group.html  group__xine__init.html	     xine_8h.html \
	group__browse__group.html    group__xine__management.html    xine_logo.png \
	group__build__info.html      group__xine__version.html \
	group__config__group.html    index.html

EXTRA_DIST = $(docs_DOCS)

docdir = $(prefix)/share/doc/xine/xine-lib-API/html

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

debug:
install-debug:

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
