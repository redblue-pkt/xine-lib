##
## Process this file with automake to produce Makefile.in
##

#CFLAGS = -D_FILE_OFFSET_BITS=64 @GLOBAL_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H

CFLAGS = @GLOBAL_CFLAGS@ @LIBFFMPEG_CFLAGS@ -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec_mlib.la

EXTRA_DIST = dsputil_mlib.c

if HAVE_MLIB
mlib_modules = $(EXTRA_DIST)
endif

libavcodec_mlib_la_SOURCES = $(mlib_modules)

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
