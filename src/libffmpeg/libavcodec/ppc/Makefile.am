##
## Process this file with automake to produce Makefile.in
##

#CFLAGS = @CFLAGS@ @LIBFFMPEG_CFLAGS@

# we must not use CFLAGS here, gcc optimizations produce
# bad code if we do so.
CFLAGS = -O2 $(LIBFFMPEG_CFLAGS)

ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec_ppc.la

libavcodec_ppc_src =  dsputil_altivec.c dsputil_ppc.c
libavcodec_ppc_dummy = libavcodec_ppc_dummy.c

EXTRA_DIST =  $(libavcodec_ppc_src) $(libavcodec_ppc_dummy)

if ARCH_POWERPC
ppc_modules = $(libavcodec_ppc_src)
endif


libavcodec_ppc_la_SOURCES = $(ppc_modules) $(libavcodec_ppc_dummy)

noinst_HEADERS = dsputil_altivec.h

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<

debug:
	@$(MAKE) CFLAGS="$(DEBUG_CFLAGS) $(LIBFFMPEG_CFLAGS)"

install-debug: debug
	@$(MAKE) $(AM_MAKEFLAGS) install-exec-am install-data-am

mostlyclean-generic:
	-rm -f *~ \#* .*~ .\#*

maintainer-clean-generic:
	-@echo "This command is intended for maintainers to use;"
	-@echo "it deletes files that may require special tools to rebuild."
	-rm -f Makefile.in
