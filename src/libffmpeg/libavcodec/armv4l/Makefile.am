##
## Process this file with automake to produce Makefile.in
##

#CFLAGS = @GLOBAL_CFLAGS@ @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H

CFLAGS = -O2 @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec_armv4l.la

EXTRA_DIST =  jrevdct_arm.S dsputil_arm.c

#if HAVE_ARMV4L
#armv4l_modules = $(EXTRA_DIST)
#endif
armv4l_modules =

libavcodec_armv4l_la_SOURCES = $(armv4l_modules)

noinst_HEADERS = 

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<

debug:
	@$(MAKE) CFLAGS="@DEBUG_CFLAGS@ @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
