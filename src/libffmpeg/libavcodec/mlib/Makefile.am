include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
ASFLAGS =

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
