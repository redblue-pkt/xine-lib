##
## Process this file with automake to produce Makefile.in
##

#CFLAGS = -D_FILE_OFFSET_BITS=64 @CFLAGS@

CFLAGS = @CFLAGS@ $(LIBFFMPEG_CFLAGS)
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec_mlib.la

libavcodec_mlib_src = dsputil_mlib.c
libavcodec_mlib_dummy = libavcodec_mlib_dummy.c

EXTRA_DIST = $(libavcodec_mlib_src) $(libavcodec_mlib_dummy)

if HAVE_MLIB
mlib_modules = $(libavcodec_mlib_src)
endif

libavcodec_mlib_la_SOURCES = $(mlib_modules) $(libavcodec_mlib_dummy)

noinst_HEADERS = 

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBFFMPEG_CFLAGS) -DCONFIG_DECODERS -DHAVE_AV_CONFIG_H"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
