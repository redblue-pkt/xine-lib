include $(top_srcdir)/misc/Makefile.common

AM_CFLAGS = -O2 $(LIBFFMPEG_CFLAGS)
ASFLAGS =

noinst_LTLIBRARIES = libavcodec_armv4l.la

libavcodec_armv4l_src = dsputil_arm.c jrevdct_arm.S 
libavcodec_armv4l_dummy =  libavcodec_armv4l_dummy.c
EXTRA_DIST =  $(libavcodec_armv4l_src) $(libavcodec_armv4l_dummy)

#if HAVE_ARMV4L
#armv4l_modules = $(libavcodec_armv4l_src)
#endif
armv4l_modules = 

libavcodec_armv4l_la_SOURCES = $(armv4l_modules) $(libavcodec_armv4l_dummy)

noinst_HEADERS = 

.s.lo:
	$(ASCOMPILE) -o $@ `test -f $< || echo '$(srcdir)/'`$<
