SUBDIRS = libcdio libvcd

libdir = $(XINE_PLUGINDIR)

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic
XINE_LIB = $(top_builddir)/src/xine-engine/libxine.la

vcdx_SRCS = xineplug_inp_vcd.c vcdplayer.c vcdio.c xine-extra.c

EXTRA_DIST = $(vcdx_SRCS)

if ENABLE_VCDX
lib_LTLIBRARIES = xineplug_inp_vcdx.la

AM_CFLAGS = $(LIBCDIO_CFLAGS) $(LIBVCD_CFLAGS)

xineplug_inp_vcdx_la_SOURCES = $(vcdx_SRCS)
xineplug_inp_vcdx_la_LDFLAGS = -avoid-version -module
if HAVE_VCDNAV
xineplug_inp_vcdx_la_LIBADD = $(XINE_LIB) $(LIBVCDINFO_LIBS)
else
xineplug_inp_vcdx_la_LIBADD = $(XINE_LIB) $(LIBVCD_LIBS) $(LIBVCDINFO_LIBS)
endif

endif

noinst_HEADERS = vcdio.h vcdplayer.h xine-extra.h

debug:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBCDIO_CFLAGS) $(LIBVCD_CFLAGS)"

install-debug: debug
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && $(MAKE) $@) || exit;\
	done;
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
