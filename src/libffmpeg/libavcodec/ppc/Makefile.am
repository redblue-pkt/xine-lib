include $(top_srcdir)/misc/Makefile.common

# we must not use CFLAGS here, gcc optimizations produce
# bad code if we do so.
AM_CFLAGS = $(LIBFFMPEG_CFLAGS)

ASFLAGS =

noinst_LTLIBRARIES = libavcodec_ppc.la

libavcodec_ppc_src =  dsputil_altivec.c \
		      dsputil_ppc.c \
		      fft_altivec.c \
		      idct_altivec.c \
		      gmc_altivec.c \
		      mpegvideo_altivec.c \
		      mpegvideo_ppc.c
libavcodec_ppc_dummy = libavcodec_ppc_dummy.c

EXTRA_DIST =  $(libavcodec_ppc_src) $(libavcodec_ppc_dummy)

if ARCH_POWERPC
ppc_modules = $(libavcodec_ppc_src)
endif


libavcodec_ppc_la_SOURCES = $(ppc_modules) $(libavcodec_ppc_dummy)

noinst_HEADERS = dsputil_altivec.h dsputil_ppc.h

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<
