###
## Process this file with automake to produce Makefile.in
##

AUTOMAKE_OPTIONS = 1.11 no-dist-gzip dist-xz

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = doc m4 po include lib src contrib misc

DEBFILES = debian/README.Debian debian/changelog debian/control \
	debian/copyright debian/rules debian/compat \
	debian/shlibdeps.sh debian/libxine2-dev.install debian/libxine1.install

EXTRA_DIST = \
	autogen.sh \
	config.rpath \
	configure \
	config.guess \
	config.sub \
	depcomp \
	INSTALL \
	install-sh \
	libtool \
	ltmain.sh \
	missing \
	\
	win32/scripts/post_install.bat \
	win32/scripts/ffmpeg_win32.patch \
	win32/include/msvc/sys/time.h \
	win32/include/msvc/config.h \
	win32/include/msvc/inttypes.h \
	win32/include/msvc/stdint.h \
	win32/include/msvc/unistd.h \
	win32/include/dlfcn.h \
	win32/config.h \
	win32/README \
	win32/libdvdnav.def \
	win32/libxine.def \
	win32/libxinesupport.def \
	win32/xine_plugin.def \
	win32/xine.dsw

CLEANFILES = a.out
CONFIG_CLEAN_FILES = libtool po/Makevars.extra
MAINTAINERCLEANFILES = \
	Makefile.in configure acinclude.m4 aclocal.m4 \
	stamp-h.in ltconfig ltmain.sh \
	config.guess config.sub install-sh missing mkinstalldirs \
	depcomp config.log

dist_doc_DATA = COPYING NEWS README TODO CREDITS ChangeLog

docs clean-docs:
	@cd doc && $(MAKE) $@

distcheck-lax:
	@$(MAKE) distcheck_lax=true distcheck

pass1:
	@$(MAKE) MULTIPASS_CFLAGS='$(PASS1_CFLAGS)'

pass2:
	@$(MAKE) MULTIPASS_CFLAGS='$(PASS2_CFLAGS)'


prune-cache:
	-rm -rf config.cache autom4te.cache


release-check:
	@./config.status misc/relchk.sh
	@mv -f .cvsversion .cvsversion.tmp
	@./autogen.sh noconfig && $(SHELL) misc/relchk.sh
	@mv -f .cvsversion.tmp .cvsversion


slackbuild:
	@(cd misc && $(MAKE) SlackBuild) && \
	 PREFIX="/usr" misc/SlackBuild


install-exec-hook:
	find $(top_builddir) -name \*.so | \
	  xargs $(STRINGS) -a | \
	  $(EGREP) '^([-a-z]+/[-+.a-z0-9]+:[^:]+:[^;]+;[ \t]*)+' | \
	  sed -e 's/:[^;]\+;/\n/g' | \
	  sort -u | \
	  sed -e '1 d' >$(DESTDIR)$(XINE_PLUGINDIR)/mime.types

uninstall-hook:
	rm -f $(DESTDIR)$(XINE_PLUGINDIR)/mime.types


install-data-hook:
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*.la
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*/*.la
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*.dll.a
	@rm -f $(DESTDIR)$(XINE_PLUGINDIR)/*/*.dll.a
	@if test -x "$(top_srcdir)/post-install.sh" ; then \
	  $(top_srcdir)/post-install.sh ; \
	fi

dist-hook:
	cp -p $(srcdir)/version.sh $(distdir)
	cp -p $(srcdir)/win32/*.dsp $(distdir)/win32/


MOSTLYCLEANFILES = \
	$(PACKAGE)_$(VERSION).tar.gz \
	$(PACKAGE)_$(VERSION).tar.bz2 \
	$(PACKAGE)-$(VERSION).tar.xz \
	$(distdir).tar.gz \
	$(PACKAGE).tgz \
	package_descriptions

mostlyclean-local:
	-rm -rf $(distdir)

maintainer-clean-generic-hook:
	rm -f config.status

$(top_srcdir)/configure:  $(am__configure_deps)
	cd $(srcdir) && $(AUTOCONF) && $(SED) -i -e '/gnu_ld/,/;;/ s/--rpath \$${wl}/--rpath,/' $@
