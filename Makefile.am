##
## Process this file with automake to produce Makefile.in
##

AUTOMAKE_OPTIONS = 1.3

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = doc m4 intl po misc include src

DEBFILES = debian/README.Debian debian/changelog debian/control \
	debian/copyright debian/rules debian/compat \
	debian/shlibdeps.sh debian/libxine-dev.install debian/libxine1.install

EXTRA_DIST = autogen.sh \
	     automake.diff \
	     $(DEBFILES) \
	     ChangeLog \
	     configure \
	     config.guess \
	     config.sub \
	     COPYING \
	     INSTALL \
	     install-sh \
	     libtool \
	     ltmain.sh \
	     missing \
	     mkinstalldirs \
	     NEWS \
	     README \
	     TODO \
	     depcomp \
	     CREDITS \
	     @DEPCOMP@

noinst_HEADERS = config.h

CONFIG_CLEAN_FILES = libtool-nofpic

docs:
	@cd doc && $(MAKE) $@


debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;


install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit; \
	done;
	$(MAKE) $(AM_MAKEFLAGS) install-data-hook


prune-cache:
	-rm -f config.cache


release-check: misc/relchk.sh
	@mv -f .cvsversion .cvsversion.tmp
	@./autogen.sh && $(SHELL) misc/relchk.sh
	@mv -f .cvsversion.tmp .cvsversion


slackbuild:
	@(cd misc && $(MAKE) SlackBuild) && \
	 PREFIX="/usr" misc/SlackBuild


install-data-hook:
	@if [ -f "$(top_srcdir)/post-install.sh" ]; then $(top_srcdir)/post-install.sh; fi


mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#* 
	-rm -f $(PACKAGE)_$(VERSION).tar.gz
	-rm -f $(distdir).tar.gz $(PACKAGE).tgz package_descriptions
	-rm -rf $(distdir)


maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in configure acinclude.m4 aclocal.m4
	-rm -f config.h.in stamp-h.in ltconfig ltmain.sh
	-rm -f config.guess config.sub install-sh missing mkinstalldirs
	-rm -f libtool-nofpic depcomp config.log

maintainer-clean-generic-hook: 
	rm -f config.status
