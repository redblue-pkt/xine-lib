
SUBDIRS = man xine-lib-API

EXTRA_DIST = $(docs_DOCS) FAQ FAQ_es MRL.txt README.solaris dataflow.dia\
			  xine-lib-API.cfg README.xinerc README.dxr3

docdir = $(prefix)/share/doc/xine
docs_DOCS =

docs:
	@if test x"$$DOXYGEN" != "xno"; then \
	  echo -n "  + Creating docs: "; \
	  if test ! -d xine-lib-API/html; \
	     then mkdir -p xine-lib-API/html; \
	  fi; \
	  cp ../misc/xine_logo.png ./xine-lib-API/html; \
	  echo -e "<HTML>\n<HEAD>\n<TITLE>\$$projectname</TITLE>\n<IMG SRC=\"xine_logo.png\" alt=\"xine logo\" ALIGN=CENTER WIDTH=100% CELLPADDING=0 BORDER=0>\n</HEAD>\n<BODY BGCOLOR=#000000 TEXT=#ccccFF LINK=#a0a0FF VLINK=#40ddff ALINK=#ffffff leftmargin=\"0\" topmargin=\"0\" marginwidth=\"0\" marginheight=\"0\">\n" > ./xine-lib-API/xine_header.html; \
	  $(DOXYGEN) xine-lib-API.cfg; \
	  echo "done.";\
	fi


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
