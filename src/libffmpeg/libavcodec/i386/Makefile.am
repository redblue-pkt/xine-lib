include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
# CFLAGS is here to filter out -funroll-loops because it causes bad
# behavior of libavcodec
CFLAGS = `echo @CFLAGS@ | sed -e 's/-funroll-loops//g'`

ASFLAGS =

noinst_LTLIBRARIES = libavcodec_mmx.la

libavcodec_mmx_src = \
	cputest.c \
	dsputil_mmx.c \
	fdct_mmx.c \
	fft_sse.c \
	idct_mmx.c \
	motion_est_mmx.c \
	mpegvideo_mmx.c \
	simple_idct_mmx.c

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
