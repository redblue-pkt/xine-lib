##
## Process this file with automake to produce Makefile.in
##

SUBDIRS = armv4l i386 mlib alpha

EXTRA_DIST = fdctref.c imgresample.c motion_est_template.c wmv2.c

AM_CFLAGS = $(LIBFFMPEG_CFLAGS)
ASFLAGS =

LIBTOOL = $(SHELL) $(top_builddir)/libtool-nofpic

noinst_LTLIBRARIES = libavcodec.la

libavcodec_la_SOURCES = \
	common.c \
	dsputil.c \
	dv.c \
	error_resilience.c \
	eval.c \
	fft.c \
	h263.c \
	h263dec.c \
	huffyuv.c \
	imgconvert.c \
	jfdctfst.c \
	jfdctint.c \
	jrevdct.c \
	mdct.c \
	mace.c \
	mem.c \
	mjpeg.c \
	motion_est.c \
	mpeg12.c \
	mpegaudiodec.c \
	mpegvideo.c \
	msmpeg4.c \
	ratecontrol.c \
	rv10.c \
	simple_idct.c \
	svq1.c \
	utils.c \
	wmadec.c 
#imgresample.c

libavcodec_la_LDFLAGS = \
	$(top_builddir)/src/libffmpeg/libavcodec/armv4l/libavcodec_armv4l.la \
	$(top_builddir)/src/libffmpeg/libavcodec/i386/libavcodec_mmx.la \
	$(top_builddir)/src/libffmpeg/libavcodec/mlib/libavcodec_mlib.la \
	-avoid-version -module


noinst_HEADERS = \
	avcodec.h \
	common.h \
	dsputil.h \
	dvdata.h \
	h263data.h \
	mangle.h \
	mpeg4data.h \
	mpeg12data.h \
	mpegaudio.h \
	mpegaudiodectab.h \
	mpegaudiotab.h \
	mpegvideo.h \
	msmpeg4data.h \
	simple_idct.h \
	svq1_cb.h \
	wmadata.h

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
