##
## Process this file with automake to produce Makefile.in
##

# we must not use CFLAGS here, gcc optimizations produce
# bad code if we do so.
AM_CFLAGS = -O2 $(LIBFFMPEG_CFLAGS)

ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec_mmx.la

libavcodec_mmx_src = \
	dsputil_mmx.c \
	fdct_mmx.c \
	idct_mmx.c \
	motion_est_mmx.c \
	mpegvideo_mmx.c \
	simple_idct_mmx.c
#	fft_sse.c - needs new header from gcc 3.1

libavcodec_mmx_dummy = libavcodec_mmx_dummy.c

EXTRA_DIST = \
	$(libavcodec_mmx_dummy) \
	$(libavcodec_mmx_src) \
	mpegvideo_mmx_template.c

if HAVE_FFMMX
mmx_modules = $(libavcodec_mmx_src)
endif


libavcodec_mmx_la_SOURCES = $(mmx_modules) $(libavcodec_mmx_dummy)

noinst_HEADERS = dsputil_mmx_avg.h dsputil_mmx_rnd.h mmx.h

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
