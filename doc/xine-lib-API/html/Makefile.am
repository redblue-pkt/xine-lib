docs_DOCS =  group__video__ratio.html group__visual__types.html \
	annotated.html group__xine__api.html doxygen.css \
	group__xine__init.html doxygen.gif group__xine__management.html \
	files.html group__xine__version.html functions.html index.html \
	globals.html modules.html group__autoplay__group.html \
	structao__functions__t.html group__browse__group.html \
	structcfg__data__t.html group__build__info.html \
	structconfig__values__s.html group__config__group.html \
	structconfig__values__t.html group__demux__strategy.html \
	structmrl__t.html group__event__group.html structvo__driver__s.html \
	group__loadplugins__group.html structvo__driver__t.html \
	group__mrl__types.html structvo__frame__t.html \
	group__status__group.html structvo__overlay__t.html \
	group__ui__callbacks.html structxine__t.html \
	group__version__group.html xine_8h-source.html group__video__cap.html \
	xine_8h.html group__video__group.html xine_logo.png \
	group__video__prop.html

EXTRA_DIST = $(docs_DOCS)

docdir = $(prefix)/share/doc/xine/xine-lib-API/html

install-data-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
	name=`echo $$doc` \
	dir=$$destdir; \
	$(mkinstalldirs) $$dir; \
	$(INSTALL_DATA) $$doc $$dir/$$name; \
	  echo "installing $$doc as $$dir/$$name"; \
	done

uninstall-local:
	@documentations='$(docs_DOCS)'; \
	for doc in $$documentations; do \
	destdir=$(docdir); \
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
