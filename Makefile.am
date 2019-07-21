###
## Process this file with automake to produce Makefile.in
##

AUTOMAKE_OPTIONS = 1.11 no-dist-gzip dist-xz

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = doc po include lib src contrib misc

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
	m4/attributes.m4 \
	m4/audio_out.m4 \
	m4/decoders.m4 \
	m4/directx.m4 \
	m4/gas.m4 \
	m4/gettext.m4 \
	m4/getopt_long.m4 \
	m4/glibc21.m4 \
	m4/glibc2.m4 \
	m4/intmax.m4 \
	m4/codeset.m4 \
	m4/iconv.m4 \
	m4/input.m4 \
	m4/intldir.m4 \
	m4/intl.m4 \
	m4/intlmacosx.m4 \
	m4/inttypes_h.m4 \
	m4/inttypes-pri.m4 \
	m4/intdiv0.m4 \
	m4/lcmessage.m4 \
	m4/lib-ld.m4 \
	m4/lib-link.m4 \
	m4/lib-prefix.m4 \
	m4/libFLAC.m4 \
	m4/libtool.m4 \
	m4/lock.m4 \
	m4/longlong.m4 \
	m4/lt~obsolete.m4 \
	m4/ltoptions.m4 \
	m4/ltsugar.m4 \
	m4/ltversion.m4 \
	m4/macosx.m4 \
	m4/misc.m4 \
	m4/nls.m4 \
	m4/objc.m4 \
	m4/package.m4 \
	m4/pkg.m4 po.m4 \
	m4/pthreads.m4 \
	m4/printf-posix.m4 \
	m4/programs.m4 \
	m4/progtest.m4 \
	m4/size_max.m4 \
	m4/stdint_h.m4 \
	m4/summary.m4 \
	m4/symbol.m4 \
	m4/types.m4 \
	m4/uintmax_t.m4 \
	m4/video_out.m4 \
	m4/visibility.m4 \
	m4/wchar_t.m4 \
	m4/wint_t.m4 \
	m4/xine.m4 \
	m4/xsize.m4
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

if INSTALL_M4
m4datadir = $(ACLOCAL_DIR)
else
m4datadir = $(datadir)/aclocal
endif
m4data_DATA = m4/xine.m4


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
