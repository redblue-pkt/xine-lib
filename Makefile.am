###
## Process this file with automake to produce Makefile.in
##

AUTOMAKE_OPTIONS = 1.11 no-dist-gzip dist-xz

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = doc po lib src contrib misc

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
	include/xine/version.h.in
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

if GENERATED_INTTYPES_H
inttypes_h = include/inttypes.h
endif
noinst_HEADERS = include/config.h
nodist_noinst_HEADERS = include/configure.h
nodist_include_HEADERS = $(inttypes_h)
include_HEADERS = include/xine.h
xineincludedir = $(includedir)/xine
xineinclude_HEADERS = \
	include/xine/alphablend.h	\
	include/xine/array.h		\
	include/xine/attributes.h	\
	include/xine/audio_decoder.h	\
	include/xine/audio_out.h	\
	include/xine/broadcaster.h	\
	include/xine/buffer.h		\
	include/xine/compat.h		\
	include/xine/configfile.h	\
	include/xine/demux.h		\
	include/xine/info_helper.h	\
	include/xine/input_plugin.h	\
	include/xine/io_helper.h	\
	include/xine/list.h		\
	include/xine/metronom.h		\
	include/xine/mfrag.h		\
	include/xine/os_types.h		\
	include/xine/osd.h		\
	include/xine/plugin_catalog.h	\
	include/xine/pool.h		\
	include/xine/post.h		\
	include/xine/refcounter.h	\
	include/xine/resample.h		\
	include/xine/ring_buffer.h	\
	include/xine/scratch.h		\
	include/xine/sorted_array.h	\
	include/xine/spu.h		\
	include/xine/spu_decoder.h	\
	include/xine/stree.h		\
	include/xine/tickets.h		\
	include/xine/vdr.h		\
	include/xine/version.h		\
	include/xine/video_decoder.h	\
	include/xine/video_out.h	\
	include/xine/video_overlay.h	\
	include/xine/vo_scale.h		\
	include/xine/xine_buffer.h	\
	include/xine/xine_internal.h	\
	include/xine/xine_module.h	\
	include/xine/xine_plugin.h	\
	include/xine/xineintl.h		\
	include/xine/xineutils.h	\
	include/xine/xmllexer.h		\
	include/xine/xmlparser.h


CLEANFILES = a.out
DISTCLEANFILES = include/xine/version.h
CONFIG_CLEAN_FILES = \
	libtool \
	po/Makevars.extra \
	$(inttypes_h)
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
	mkdir $(distdir)/win32/
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
