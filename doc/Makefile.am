include $(top_srcdir)/misc/Makefile.common

SUBDIRS = man hackersguide faq

EXTRA_DIST = $(docs_DOCS) 
docdir = $(prefix)/share/doc/xine
docs_DOCS = README README.plugins dataflow.dia README.dvb \
	    README.config_en README.dxr3 README.dxr3_pl README.freebsd \
	    README.irix README.mrl README.opengl README.solaris README.syncfb \
	    README.tvmode README.network_dvd 


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
